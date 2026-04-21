#pragma once
// ──────────────────────────────────────────────────────────────
// click-clack / core / wal.hpp
// Append-only Write-Ahead Log backed by a composite-tree store.
// ──────────────────────────────────────────────────────────────

#include "codec.hpp"
#include "epoch.hpp"
#include "types.hpp"

#include <celer/celer.hpp>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <vector>

namespace cc {

using ClackCallback = std::function<void(const Clack&)>;

class Wal {
public:
    Wal() = default;
    ~Wal() { close(); }

    Wal(const Wal&) = delete;
    Wal& operator=(const Wal&) = delete;
    Wal(Wal&&) = delete;
    Wal& operator=(Wal&&) = delete;

    // ── Lifecycle ───────────────────────────────────────────

    [[nodiscard]] auto open(const HubConfig& config) -> celer::VoidResult {
        auto factory = celer::backends::rocksdb::factory({.path = config.wal_path});
        std::vector<celer::TableDescriptor> schema{{"wal", "clacks"}};
        auto res = celer::open(factory, schema);
        if (!res) return res;

        auto db = celer::db("wal");
        if (!db) return celer::VoidResult{std::unexpected(db.error())};

        auto tbl = db->table("clacks");
        if (!tbl) return celer::VoidResult{std::unexpected(tbl.error())};

        clacks_.emplace(std::move(*tbl));
        open_ = true;
        return {};
    }

    [[nodiscard]] auto recover() -> celer::Result<std::uint64_t> {
        if (!open_) {
            return celer::Result<std::uint64_t>{
                std::unexpected(celer::Error{"wal", "wal_not_open"})};
        }

        auto pairs = clacks_->handle()->prefix_scan("");
        if (!pairs) return celer::Result<std::uint64_t>{std::unexpected(pairs.error())};

        std::uint64_t max_epoch = 0;
        for (const auto& kv : *pairs) {
            auto epoch = key_to_epoch(kv.key);
            if (epoch > max_epoch) max_epoch = epoch;
        }

        epoch_.recover(max_epoch);
        latest_epoch_.store(max_epoch, std::memory_order_release);
        return max_epoch;
    }

    void close() {
        if (open_) {
            clacks_.reset();
            open_ = false;
            (void)celer::close();
        }
    }

    // ── Write path ──────────────────────────────────────────

    [[nodiscard]] auto append(const Click& click) -> celer::Result<Clack> {
        if (!open_) {
            return celer::Result<Clack>{
                std::unexpected(celer::Error{"wal", "wal_not_open"})};
        }

        const auto ep = epoch_.next();
        const auto ts = now_us();
        auto clack = click_to_clack(click, ep, ts);

        auto serialized = serialize_clack(clack);

        // Read back the CRC that serialize_clack computed
        std::uint32_t crc{};
        std::memcpy(&crc, serialized.data() + offsetof(ClackHeader, checksum), sizeof(crc));
        clack.header.checksum = crc;

        auto key = epoch_key(ep);
        auto res = clacks_->put_raw(key, serialized);
        if (!res) return celer::Result<Clack>{std::unexpected(res.error())};

        // Notify listeners
        {
            std::lock_guard lock(listeners_mu_);
            for (auto& cb : listeners_) {
                cb(clack);
            }
        }

        // Wake any waiters blocked in wait_for_next()
        {
            std::lock_guard lock(notify_mu_);
            latest_epoch_.store(clack.header.epoch, std::memory_order_release);
        }
        notify_cv_.notify_all();

        return clack;
    }

    // ── Read path ───────────────────────────────────────────

    [[nodiscard]] auto read_range(std::uint64_t since_epoch, int limit)
        -> celer::Result<std::vector<Clack>>
    {
        if (!open_) {
            return celer::Result<std::vector<Clack>>{
                std::unexpected(celer::Error{"wal", "wal_not_open"})};
        }

        auto since_key = epoch_key(since_epoch);
        auto pairs = clacks_->handle()->prefix_scan("");
        if (!pairs) return celer::Result<std::vector<Clack>>{std::unexpected(pairs.error())};

        std::vector<Clack> result;
        result.reserve(static_cast<std::size_t>(limit));

        for (const auto& kv : *pairs) {
            if (kv.key < since_key) continue;
            if (static_cast<int>(result.size()) >= limit) break;
            auto clack = deserialize_clack(kv.value);
            if (clack) result.push_back(std::move(*clack));
        }

        return result;
    }

    [[nodiscard]] auto read_one(std::uint64_t epoch)
        -> celer::Result<std::optional<Clack>>
    {
        if (!open_) {
            return celer::Result<std::optional<Clack>>{
                std::unexpected(celer::Error{"wal", "wal_not_open"})};
        }

        auto key = epoch_key(epoch);
        auto raw = clacks_->get_raw(key);
        if (!raw) return celer::Result<std::optional<Clack>>{std::unexpected(raw.error())};
        if (!*raw) return std::optional<Clack>{std::nullopt};

        auto clack = deserialize_clack(**raw);
        if (!clack) return celer::Result<std::optional<Clack>>{std::unexpected(clack.error())};
        return std::optional<Clack>{std::move(*clack)};
    }

    // ── Listener registration ───────────────────────────────

    void on_clack(ClackCallback cb) {
        std::lock_guard lock(listeners_mu_);
        listeners_.push_back(std::move(cb));
    }

    // ── Long-poll: block until an epoch > since is appended,
    //    or timeout elapses. Returns the current (post-wait) epoch.
    //    Safe for concurrent callers.
    [[nodiscard]] auto wait_for_next(std::uint64_t since_epoch,
                                     std::chrono::milliseconds timeout) noexcept
        -> std::uint64_t
    {
        std::unique_lock lock(notify_mu_);
        notify_cv_.wait_for(lock, timeout, [&] {
            return latest_epoch_.load(std::memory_order_acquire) > since_epoch;
        });
        return latest_epoch_.load(std::memory_order_acquire);
    }

    // ── Queries ─────────────────────────────────────────────

    [[nodiscard]] auto is_open() const noexcept -> bool { return open_; }
    [[nodiscard]] auto current_epoch() const noexcept -> std::uint64_t { return epoch_.current(); }

private:
    bool                            open_{false};
    EpochCounter                    epoch_;
    std::optional<celer::TableRef>  clacks_;
    std::mutex                      listeners_mu_;
    std::vector<ClackCallback>      listeners_;

    // Long-poll notifier state
    std::mutex                      notify_mu_;
    std::condition_variable         notify_cv_;
    std::atomic<std::uint64_t>      latest_epoch_{0};
};

} // namespace cc
