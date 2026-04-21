#pragma once
// ──────────────────────────────────────────────────────────────
// click-clack / core / epoch.hpp
// Atomic epoch counter — lock-free, monotonic, crash-recoverable
// Monotonic epoch allocator — lock-free, process-local.
// ──────────────────────────────────────────────────────────────

#include <atomic>
#include <cstdint>

namespace cc {

class EpochCounter {
public:
    EpochCounter() = default;
    ~EpochCounter() = default;

    EpochCounter(const EpochCounter&) = delete;
    EpochCounter& operator=(const EpochCounter&) = delete;
    EpochCounter(EpochCounter&&) = delete;
    EpochCounter& operator=(EpochCounter&&) = delete;

    [[nodiscard]] auto next() noexcept -> std::uint64_t {
        return counter_.fetch_add(1, std::memory_order_relaxed) + 1;
    }

    [[nodiscard]] auto current() const noexcept -> std::uint64_t {
        return counter_.load(std::memory_order_acquire);
    }

    void recover(std::uint64_t max_epoch) noexcept {
        counter_.store(max_epoch, std::memory_order_release);
    }

private:
    std::atomic<std::uint64_t> counter_{0};
};

} // namespace cc
