#pragma once
// ──────────────────────────────────────────────────────────────
// click-clack / core / types.hpp
// Domain types transpiled from sml/click_clack.sml entities
// ──────────────────────────────────────────────────────────────

#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cc {

// ── Verb (8-bit wire enum) ──────────────────────────────────

enum class Verb : std::uint8_t {
    Announce   = 0x01,
    Claim      = 0x02,
    Yield      = 0x03,
    Progress   = 0x04,
    Artifact   = 0x05,
    Complete   = 0x06,
    Error      = 0x07,
    Query      = 0x08,
    Respond    = 0x09,
    Observe    = 0x0A,
    Direct     = 0x0B,
    Approve    = 0x0C,
    Reject     = 0x0D,
    Halt       = 0x0E,
    Resume     = 0x0F,
    Heartbeat  = 0x10,
    Ask        = 0x11,
    Answer     = 0x12,
    Ack        = 0x13,
    Reserve    = 0x14,
    Release    = 0x15,
};

[[nodiscard]] constexpr auto verb_to_str(Verb v) noexcept -> std::string_view {
    switch (v) {
        case Verb::Announce:  return "ANNOUNCE";
        case Verb::Claim:     return "CLAIM";
        case Verb::Yield:     return "YIELD";
        case Verb::Progress:  return "PROGRESS";
        case Verb::Artifact:  return "ARTIFACT";
        case Verb::Complete:  return "COMPLETE";
        case Verb::Error:     return "ERROR";
        case Verb::Query:     return "QUERY";
        case Verb::Respond:   return "RESPOND";
        case Verb::Observe:   return "OBSERVE";
        case Verb::Direct:    return "DIRECT";
        case Verb::Approve:   return "APPROVE";
        case Verb::Reject:    return "REJECT";
        case Verb::Halt:      return "HALT";
        case Verb::Resume:    return "RESUME";
        case Verb::Heartbeat: return "HEARTBEAT";
        case Verb::Ask:       return "ASK";
        case Verb::Answer:    return "ANSWER";
        case Verb::Ack:       return "ACK";
        case Verb::Reserve:   return "RESERVE";
        case Verb::Release:   return "RELEASE";
    }
    return "UNKNOWN";
}

[[nodiscard]] constexpr auto str_to_verb(std::string_view s) noexcept -> Verb {
    if (s == "ANNOUNCE")  return Verb::Announce;
    if (s == "CLAIM")     return Verb::Claim;
    if (s == "YIELD")     return Verb::Yield;
    if (s == "PROGRESS")  return Verb::Progress;
    if (s == "ARTIFACT")  return Verb::Artifact;
    if (s == "COMPLETE")  return Verb::Complete;
    if (s == "ERROR")     return Verb::Error;
    if (s == "QUERY")     return Verb::Query;
    if (s == "RESPOND")   return Verb::Respond;
    if (s == "OBSERVE")   return Verb::Observe;
    if (s == "DIRECT")    return Verb::Direct;
    if (s == "APPROVE")   return Verb::Approve;
    if (s == "REJECT")    return Verb::Reject;
    if (s == "HALT")      return Verb::Halt;
    if (s == "RESUME")    return Verb::Resume;
    if (s == "HEARTBEAT") return Verb::Heartbeat;
    if (s == "ASK")       return Verb::Ask;
    if (s == "ANSWER")    return Verb::Answer;
    if (s == "ACK")       return Verb::Ack;
    if (s == "RESERVE")   return Verb::Reserve;
    if (s == "RELEASE")   return Verb::Release;
    return Verb::Announce; // fallback
}

// ── Flags (8-bit bitmask) ───────────────────────────────────

struct Flags {
    std::uint8_t raw{0};

    [[nodiscard]] constexpr auto urgent()    const noexcept -> bool { return (raw & 0x01) != 0; }
    [[nodiscard]] constexpr auto blocking()  const noexcept -> bool { return (raw & 0x02) != 0; }
    [[nodiscard]] constexpr auto hitl_req()  const noexcept -> bool { return (raw & 0x04) != 0; }
    [[nodiscard]] constexpr auto ephemeral() const noexcept -> bool { return (raw & 0x08) != 0; }

    constexpr void set_urgent(bool v)    noexcept { v ? (raw |= 0x01) : (raw &= ~0x01); }
    constexpr void set_blocking(bool v)  noexcept { v ? (raw |= 0x02) : (raw &= ~0x02); }
    constexpr void set_hitl_req(bool v)  noexcept { v ? (raw |= 0x04) : (raw &= ~0x04); }
    constexpr void set_ephemeral(bool v) noexcept { v ? (raw |= 0x08) : (raw &= ~0x08); }
};

// ── ClackHeader (64 bytes, packed, little-endian) ───────────

struct alignas(8) ClackHeader {
    std::uint64_t epoch{0};
    std::uint64_t timestamp_us{0};
    Verb          verb{Verb::Announce};
    Flags         flags{};
    std::uint8_t  agent_id_len{0};
    std::uint8_t  task_id_len{0};
    std::uint8_t  subject_len{0};
    std::uint8_t  pad0_{0};           // align payload_len to 4
    std::uint32_t payload_len{0};
    std::uint32_t checksum{0};
    std::uint64_t parent_epoch{0};
    std::uint8_t  reserved_[24]{};    // pad to 64 bytes

    static constexpr std::size_t kSize = 64;
};

static_assert(sizeof(ClackHeader) == ClackHeader::kSize,
              "ClackHeader must be exactly 64 bytes");

// ── Clack (header + variable region) ────────────────────────

struct Clack {
    ClackHeader header{};
    std::string agent_id;
    std::string task_id;
    std::string subject;
    std::string payload;   // UTF-8 JSON
};

// ── Click (inbound wire type — what agents send) ────────────

struct Click {
    std::string verb;
    std::string agent_id;
    std::string task_id;    // may be empty
    std::string subject;
    std::uint8_t flags{0};
    std::uint64_t parent{0};
    std::string payload;    // JSON string
};

// ── View-model entities (CQRS projections) ──────────────────

enum class TaskStatus : std::uint8_t {
    Unclaimed, Claimed, InProgress, Completed, Errored, Halted,
};

[[nodiscard]] constexpr auto task_status_to_str(TaskStatus s) noexcept -> std::string_view {
    switch (s) {
        case TaskStatus::Unclaimed:  return "unclaimed";
        case TaskStatus::Claimed:    return "claimed";
        case TaskStatus::InProgress: return "in_progress";
        case TaskStatus::Completed:  return "completed";
        case TaskStatus::Errored:    return "errored";
        case TaskStatus::Halted:     return "halted";
    }
    return "unclaimed";
}

[[nodiscard]] constexpr auto str_to_task_status(std::string_view s) noexcept -> TaskStatus {
    if (s == "claimed")     return TaskStatus::Claimed;
    if (s == "in_progress") return TaskStatus::InProgress;
    if (s == "completed")   return TaskStatus::Completed;
    if (s == "errored")     return TaskStatus::Errored;
    if (s == "halted")      return TaskStatus::Halted;
    return TaskStatus::Unclaimed;
}

struct TaskState {
    std::string              task_id;
    TaskStatus               status{TaskStatus::Unclaimed};
    std::optional<std::string> owner_agent;
    std::uint64_t            last_epoch{0};
    std::string              last_verb;
    int                      pct{0};
    std::string              summary;
    int                      artifact_count{0};
    std::uint64_t            created_epoch{0};
    std::uint64_t            updated_us{0};
};

enum class AgentStatus : std::uint8_t {
    Online, Idle, Busy, Offline,
};

[[nodiscard]] constexpr auto agent_status_to_str(AgentStatus s) noexcept -> std::string_view {
    switch (s) {
        case AgentStatus::Online:  return "online";
        case AgentStatus::Idle:    return "idle";
        case AgentStatus::Busy:    return "busy";
        case AgentStatus::Offline: return "offline";
    }
    return "offline";
}

struct AgentPresenceState {
    std::string              agent_id;
    AgentStatus              status{AgentStatus::Offline};
    std::uint64_t            last_epoch{0};
    std::uint64_t            last_seen_us{0};
    std::vector<std::string> capabilities;
    std::string              model;
    float                    load{0.0f};
};

struct ThreadNode {
    std::uint64_t root_epoch{0};
    int           depth{0};
    Clack         clack;
};

struct ArtifactEntry {
    std::string              task_id;
    std::uint64_t            epoch{0};
    std::string              kind;     // "file" | "diff" | "url"
    std::string              path;
    std::optional<std::string> content;
    std::string              sha256;
    std::string              review_status{"pending"}; // "pending"|"approved"|"rejected"
    std::optional<std::uint64_t> review_epoch;
};

struct HITLItem {
    std::uint64_t            epoch{0};
    Clack                    clack;
    bool                     resolved{false};
    std::optional<std::uint64_t> resolution_epoch;
};

// ── Config ──────────────────────────────────────────────────

struct HubConfig {
    std::string wal_path{"./data/wal"};
    std::string views_path{"./data/views"};
    int         http_port{33514};
    int         mcp_port{3001};
    std::uint64_t agent_timeout_us{30'000'000};  // 30s
    std::uint64_t compaction_threshold{1'000'000};
    std::uint64_t retention_window_us{86'400'000'000}; // 24h
};

} // namespace cc
