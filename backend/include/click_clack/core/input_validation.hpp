#pragma once
// ──────────────────────────────────────────────────────────────
// click-clack / core / input_validation.hpp
// Ingress validation for untrusted Click fields.
//
// Central, auditable point where every untrusted string entering
// the WAL is bounded. Called by cc.post_clack, the WS `post`
// handler, and any other ingress. Prevents:
//
//   F-01  header-length truncation via uint8 narrow-cast
//   F-12  log injection / control-character smuggling in agent_id
//
// Length limits track the wire format in codec.hpp:
//   agent_id/task_id/subject  = uint8_t (≤ 255 bytes)
//   payload                   = uint32_t (≤ 2^32-1, clamped to 4 MiB)
//
// Charset: ASCII printable without CR/LF/NUL for short identifiers;
// payload is opaque bytes but capped in size.
// ──────────────────────────────────────────────────────────────

#include "types.hpp"

#include <celer/celer.hpp>

#include <cstdint>
#include <string_view>

namespace cc {

struct FieldLimits {
    static constexpr std::size_t kMaxAgentId = 64;       // tighter than wire cap
    static constexpr std::size_t kMaxTaskId  = 128;
    static constexpr std::size_t kMaxSubject = 255;
    static constexpr std::size_t kMaxPayload = 4 * 1024 * 1024;  // 4 MiB
};

[[nodiscard]] inline auto is_safe_id_char(char c) noexcept -> bool {
    // Allow [A-Za-z0-9._:@/\-], reject control chars and everything exotic
    // (including newlines, tabs, and bytes ≥ 0x7F). Intentionally strict.
    return (c >= 'A' && c <= 'Z')
        || (c >= 'a' && c <= 'z')
        || (c >= '0' && c <= '9')
        || c == '.' || c == '_' || c == '-'
        || c == ':' || c == '@' || c == '/';
}

[[nodiscard]] inline auto validate_id_field(std::string_view v,
                                            std::size_t max_len,
                                            std::string_view name)
    -> celer::VoidResult
{
    if (v.size() > max_len) {
        return celer::VoidResult{std::unexpected(celer::Error{
            "validation",
            std::string{name} + " exceeds "
                + std::to_string(max_len) + " byte limit"})};
    }
    for (char c : v) {
        if (!is_safe_id_char(c)) {
            return celer::VoidResult{std::unexpected(celer::Error{
                "validation",
                std::string{name} + " contains disallowed character"})};
        }
    }
    return {};
}

// Allow control characters and non-ASCII in subject (it's a human-readable
// short message), but still bar CR/LF/NUL to stop log injection.
[[nodiscard]] inline auto validate_subject(std::string_view v)
    -> celer::VoidResult
{
    if (v.size() > FieldLimits::kMaxSubject) {
        return celer::VoidResult{std::unexpected(celer::Error{
            "validation",
            "subject exceeds " + std::to_string(FieldLimits::kMaxSubject)
                + " byte limit"})};
    }
    for (char c : v) {
        if (c == '\0' || c == '\n' || c == '\r') {
            return celer::VoidResult{std::unexpected(celer::Error{
                "validation", "subject contains forbidden control char"})};
        }
    }
    return {};
}

[[nodiscard]] inline auto validate_click(const Click& click)
    -> celer::VoidResult
{
    if (auto r = validate_id_field(click.agent_id,
                                   FieldLimits::kMaxAgentId,
                                   "agent_id"); !r) return r;
    if (auto r = validate_id_field(click.task_id,
                                   FieldLimits::kMaxTaskId,
                                   "task_id");  !r) return r;
    if (auto r = validate_subject(click.subject); !r) return r;

    if (click.payload.size() > FieldLimits::kMaxPayload) {
        return celer::VoidResult{std::unexpected(celer::Error{
            "validation",
            "payload exceeds " + std::to_string(FieldLimits::kMaxPayload)
                + " byte limit"})};
    }
    return {};
}

} // namespace cc
