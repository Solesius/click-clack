#pragma once
// ──────────────────────────────────────────────────────────────
// click-clack / core / codec.hpp
// celer::Codec<Clack> — binary (de)serialization for WAL records
// Binary codec: fixed-width header + varint payload, CRC32C trailer.
// ──────────────────────────────────────────────────────────────

#include "types.hpp"
#include <celer/celer.hpp>

#include <chrono>
#include <cstring>

#ifdef __SSE4_2__
#include <nmmintrin.h>
#endif

namespace cc {

// ── CRC-32C (Castagnoli) ────────────────────────────────────

[[nodiscard]] inline auto crc32c(const void* data, std::size_t len) noexcept -> std::uint32_t {
    auto* p = static_cast<const std::uint8_t*>(data);
    std::uint32_t crc = 0xFFFFFFFF;

#ifdef __SSE4_2__
    // Hardware CRC intrinsic path
    while (len >= 4) {
        std::uint32_t word{};
        std::memcpy(&word, p, 4);
        crc = _mm_crc32_u32(crc, word);
        p += 4; len -= 4;
    }
    while (len-- > 0) {
        crc = _mm_crc32_u8(crc, *p++);
    }
#else
    // Software fallback — Sarwate table-less
    for (std::size_t i = 0; i < len; ++i) {
        crc ^= p[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0x82F63B78 & -(crc & 1));
        }
    }
#endif
    return crc ^ 0xFFFFFFFF;
}

// ── Wall-clock microseconds ─────────────────────────────────

[[nodiscard]] inline auto now_us() noexcept -> std::uint64_t {
    using namespace std::chrono;
    return static_cast<std::uint64_t>(
        duration_cast<microseconds>(system_clock::now().time_since_epoch()).count());
}

// ── Big-endian epoch key (for RocksDB sort order) ───────────

[[nodiscard]] inline auto epoch_key(std::uint64_t epoch) -> std::string {
    std::string key(8, '\0');
    for (int i = 7; i >= 0; --i) {
        key[static_cast<std::size_t>(7 - i)] = static_cast<char>(epoch >> (i * 8));
    }
    return key;
}

[[nodiscard]] inline auto key_to_epoch(std::string_view key) noexcept -> std::uint64_t {
    if (key.size() < 8) return 0;
    std::uint64_t epoch = 0;
    for (int i = 0; i < 8; ++i) {
        epoch = (epoch << 8) | static_cast<std::uint8_t>(key[static_cast<std::size_t>(i)]);
    }
    return epoch;
}

// ── Clack serialization ─────────────────────────────────────

[[nodiscard]] inline auto serialize_clack(const Clack& clack) -> std::string {
    const auto var_len = clack.agent_id.size() + clack.task_id.size()
                       + clack.subject.size() + clack.payload.size();
    std::string buf(ClackHeader::kSize + var_len, '\0');

    // Copy header (with checksum zeroed for CRC computation)
    ClackHeader hdr = clack.header;
    hdr.checksum = 0;
    std::memcpy(buf.data(), &hdr, ClackHeader::kSize);

    // Copy variable region
    char* p = buf.data() + ClackHeader::kSize;
    std::memcpy(p, clack.agent_id.data(), clack.agent_id.size()); p += clack.agent_id.size();
    std::memcpy(p, clack.task_id.data(),  clack.task_id.size());  p += clack.task_id.size();
    std::memcpy(p, clack.subject.data(),  clack.subject.size());  p += clack.subject.size();
    std::memcpy(p, clack.payload.data(),  clack.payload.size());

    // Compute CRC over header[0..31] + variable region
    const auto crc = crc32c(buf.data(), buf.size());

    // Patch checksum into the buffer at offset 28
    std::memcpy(buf.data() + offsetof(ClackHeader, checksum), &crc, sizeof(crc));

    return buf;
}

[[nodiscard]] inline auto deserialize_clack(std::string_view buf)
    -> celer::Result<Clack>
{
    if (buf.size() < ClackHeader::kSize) {
        return celer::Result<Clack>{std::unexpected(
            celer::Error{"codec", "buffer too small for ClackHeader"})};
    }

    Clack clack{};
    std::memcpy(static_cast<void*>(&clack.header), buf.data(), ClackHeader::kSize);

    const std::size_t expected_var = clack.header.agent_id_len
                                   + clack.header.task_id_len
                                   + clack.header.subject_len
                                   + clack.header.payload_len;

    if (buf.size() < ClackHeader::kSize + expected_var) {
        return celer::Result<Clack>{std::unexpected(
            celer::Error{"codec", "buffer too small for variable region"})};
    }

    const char* p = buf.data() + ClackHeader::kSize;
    clack.agent_id.assign(p, clack.header.agent_id_len);  p += clack.header.agent_id_len;
    clack.task_id.assign(p, clack.header.task_id_len);     p += clack.header.task_id_len;
    clack.subject.assign(p, clack.header.subject_len);     p += clack.header.subject_len;
    clack.payload.assign(p, clack.header.payload_len);

    // Verify CRC
    const auto stored_crc = clack.header.checksum;
    std::string check_buf(buf.data(), buf.size());
    std::uint32_t zero = 0;
    std::memcpy(check_buf.data() + offsetof(ClackHeader, checksum), &zero, sizeof(zero));
    const auto computed_crc = crc32c(check_buf.data(), check_buf.size());

    if (stored_crc != computed_crc) {
        return celer::Result<Clack>{std::unexpected(
            celer::Error{"codec", "CRC-32C mismatch"})};
    }

    return clack;
}

// ── Build Clack from Click ──────────────────────────────────

[[nodiscard]] inline auto click_to_clack(const Click& click,
                                         std::uint64_t epoch,
                                         std::uint64_t ts) -> Clack
{
    Clack clack{};
    clack.header.epoch        = epoch;
    clack.header.timestamp_us = ts;
    clack.header.verb         = str_to_verb(click.verb);
    clack.header.flags.raw    = click.flags;
    clack.header.agent_id_len = static_cast<std::uint8_t>(click.agent_id.size());
    clack.header.task_id_len  = static_cast<std::uint8_t>(click.task_id.size());
    clack.header.subject_len  = static_cast<std::uint8_t>(click.subject.size());
    clack.header.payload_len  = static_cast<std::uint32_t>(click.payload.size());
    clack.header.parent_epoch = click.parent;
    clack.agent_id = click.agent_id;
    clack.task_id  = click.task_id;
    clack.subject  = click.subject;
    clack.payload  = click.payload;
    return clack;
}

} // namespace cc

// ── celer::Codec specialization ─────────────────────────────

namespace celer {

template <>
struct Codec<cc::Clack, void> {
    [[nodiscard]] static auto encode(const cc::Clack& clack) -> Result<std::string> {
        return cc::serialize_clack(clack);
    }

    [[nodiscard]] static auto decode(std::string_view bytes) -> Result<cc::Clack> {
        return cc::deserialize_clack(bytes);
    }
};

} // namespace celer
