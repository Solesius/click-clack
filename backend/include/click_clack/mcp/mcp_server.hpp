#pragma once
// ──────────────────────────────────────────────────────────────
// click-clack / mcp / mcp_server.hpp
// MCP tool + resource dispatch
// MCP server: tool dispatch over JSON-RPC 2.0.
// ──────────────────────────────────────────────────────────────

#include "../core/types.hpp"
#include "../core/wal.hpp"
#include "../views/materializer.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <functional>
#include <mutex>
#include <random>
#include <sstream>
#include <iomanip>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace cc {

using json = nlohmann::json;

// MCP tool handler: takes JSON args, returns JSON result
using McpToolHandler = std::function<json(const json& args, std::string_view caller_agent_id)>;

namespace detail {
// Lightweight RFC-4122 v4 UUID (lowercase hex, dashes), no external deps.
// Thread-safe via thread_local PRNG.
inline std::string make_uuid_v4() {
    thread_local std::mt19937_64 rng{std::random_device{}()};
    const std::uint64_t a = rng();
    const std::uint64_t b = rng();
    std::uint8_t bytes[16];
    for (int i = 0; i < 8; ++i) bytes[i]     = static_cast<std::uint8_t>(a >> (i * 8));
    for (int i = 0; i < 8; ++i) bytes[i + 8] = static_cast<std::uint8_t>(b >> (i * 8));
    bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0F) | 0x40); // version 4
    bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3F) | 0x80); // variant RFC 4122
    std::ostringstream os;
    os << std::hex << std::setfill('0');
    for (int i = 0; i < 16; ++i) {
        os << std::setw(2) << static_cast<int>(bytes[i]);
        if (i == 3 || i == 5 || i == 7 || i == 9) os << '-';
    }
    return os.str();
}
} // namespace detail

class McpServer {
public:
    McpServer(Wal& wal, Materializer& views)
        : wal_(wal), views_(views) {}

    ~McpServer() = default;

    McpServer(const McpServer&) = delete;
    McpServer& operator=(const McpServer&) = delete;

    // ── Initialization ──────────────────────────────────────

    void register_tools() {
        // Resolve the effective agent id: explicit `as_agent` argument wins
        // over the transport-level caller id. This makes per-request identity
        // override a first-class primitive (fixes the "agent-c and vscode-copilot
        // looked like the same process" papercut).
        auto who = [](const json& args, std::string_view caller) -> std::string {
            if (args.contains("as_agent") && args["as_agent"].is_string()) {
                auto s = args["as_agent"].get<std::string>();
                if (!s.empty()) return s;
            }
            return std::string{caller};
        };

        // Task-id auto-assignment: if the caller omits `task_id` (or sends ""),
        // mint a fresh UUIDv4 via drogon's utility (already a transitive dep).
        // Returned JSON always includes `task_id` so callers can thread it
        // through follow-up calls. Also returns `task_id_generated: true`
        // when auto-assigned, so clients can distinguish "new task" from
        // "existing task" without a separate roundtrip.
        struct TaskIdResolution {
            std::string id;
            bool        generated;
        };
        auto ensure_task = [](const json& args) -> TaskIdResolution {
            auto supplied = args.value("task_id", std::string{});
            if (!supplied.empty()) return {supplied, false};
            // lowercase v4 UUID, e.g. "3b1e0d7a-…"; prefix for readability at a glance
            return {"t-" + detail::make_uuid_v4(), true};
        };

        // ── cc.whoami ───────────────────────────────────────
        tools_["cc.whoami"] = [who](const json& args, std::string_view caller) -> json {
            return json{
                {"agent_id",   who(args, caller)},
                {"transport_caller", std::string{caller}},
                {"overridden", args.contains("as_agent")},
            };
        };

        // Core tool
        tools_["cc.post_clack"] = [this, who, ensure_task](const json& args, std::string_view caller) -> json {
            auto tid = ensure_task(args);
            Click click{};
            click.verb     = args.value("verb", "");
            click.agent_id = who(args, caller);
            click.task_id  = tid.id;
            click.subject  = args.value("subject", "");
            click.flags    = static_cast<std::uint8_t>(args.value("flags", 0));
            click.parent   = args.value("parent", std::uint64_t{0});
            click.payload  = args.contains("payload") ? args["payload"].dump() : "{}";

            auto result = wal_.append(click);
            if (!result) return json{{"error", result.error().message}};
            json out{
                {"epoch",        result->header.epoch},
                {"timestamp_us", result->header.timestamp_us},
                {"task_id",      tid.id},
            };
            if (tid.generated) out["task_id_generated"] = true;
            return out;
        };

        // Query tools
        tools_["cc.query_timeline"] = [this](const json& args, std::string_view) -> json {
            auto since = args.value("since_epoch", std::uint64_t{0});
            auto limit = args.value("limit", 100);

            // Single-verb legacy filter
            std::optional<std::string> verb;
            if (args.contains("verb_filter") && args["verb_filter"].is_string()) {
                verb = args["verb_filter"].get<std::string>();
            }
            // Combined filters (new): verbs[], task_id, agent_id
            std::unordered_set<std::string> verbs;
            if (args.contains("verbs") && args["verbs"].is_array()) {
                for (const auto& v : args["verbs"]) {
                    if (v.is_string()) verbs.insert(v.get<std::string>());
                }
            }
            std::string task_filter  = args.value("task_id", "");
            std::string agent_filter = args.value("agent_id", "");

            // Pull a generous window so we can filter server-side and still honour `limit`.
            auto window = std::max(limit * 8, 256);
            auto result = wal_.read_range(since, window);
            if (!result) return json{{"error", result.error().message}};

            json clacks = json::array();
            for (const auto& c : *result) {
                auto vstr = std::string{verb_to_str(c.header.verb)};
                if (verb && vstr != *verb) continue;
                if (!verbs.empty() && !verbs.contains(vstr)) continue;
                if (!task_filter.empty()  && c.task_id  != task_filter)  continue;
                if (!agent_filter.empty() && c.agent_id != agent_filter) continue;
                clacks.push_back(clack_to_json(c));
                if (static_cast<int>(clacks.size()) >= limit) break;
            }
            return clacks;
        };

        tools_["cc.query_agent_log"] = [this](const json& args, std::string_view) -> json {
            auto agent_id = args.value("agent_id", "");
            auto since    = args.value("since_epoch", std::uint64_t{0});
            auto limit    = args.value("limit", 100);

            auto result = views_.query_agent_log(agent_id, since, limit);
            if (!result) return json{{"error", result.error().message}};

            json out = json::array();
            for (const auto& c : *result) out.push_back(clack_to_json(c));
            return out;
        };

        tools_["cc.query_task"] = [this](const json& args, std::string_view) -> json {
            auto task_id = args.value("task_id", "");
            auto result  = views_.query_task(task_id);
            if (!result) return json{{"error", result.error().message}};
            if (!*result) return json{{"error", "not_found"}};
            return task_state_to_json(**result);
        };

        tools_["cc.query_tasks"] = [this](const json& args, std::string_view) -> json {
            auto status = args.contains("status_filter")
                ? std::optional<std::string_view>{args["status_filter"].get<std::string>()}
                : std::nullopt;
            auto limit = args.value("limit", 100);

            auto result = views_.query_tasks(status, limit);
            if (!result) return json{{"error", result.error().message}};

            json out = json::array();
            for (const auto& t : *result) out.push_back(task_state_to_json(t));
            return out;
        };

        tools_["cc.query_thread"] = [this](const json& args, std::string_view) -> json {
            auto root = args.value("root_epoch", std::uint64_t{0});
            auto result = views_.query_thread(root);
            if (!result) return json{{"error", result.error().message}};

            json out = json::array();
            for (const auto& n : *result) {
                out.push_back(json{
                    {"root_epoch", n.root_epoch},
                    {"depth",      n.depth},
                    {"clack",      clack_to_json(n.clack)},
                });
            }
            return out;
        };

        tools_["cc.query_hitl_queue"] = [this](const json&, std::string_view) -> json {
            auto result = views_.query_hitl_queue();
            if (!result) return json{{"error", result.error().message}};

            json out = json::array();
            for (const auto& item : *result) {
                out.push_back(json{
                    {"epoch",    item.epoch},
                    {"clack",    clack_to_json(item.clack)},
                    {"resolved", item.resolved},
                });
            }
            return out;
        };

        tools_["cc.query_presence"] = [this](const json&, std::string_view) -> json {
            auto result = views_.query_presence();
            if (!result) return json{{"error", result.error().message}};

            json out = json::array();
            for (const auto& a : *result) out.push_back(presence_to_json(a));
            return out;
        };

        // ── Pinning: peer-vote + operator override ─────────
        //
        // Governance model: any agent can cast one vote per clack. When the
        // vote count crosses `pin_threshold_` (default 2) the clack is
        // considered pinned. An operator (human) may force pin/unpin via
        // cc.pin_override regardless of vote count.
        auto compute_pinned = [this](const PinState& p) -> bool {
            if (p.manual_override.has_value()) return *p.manual_override;
            return p.voters.size() >= pin_threshold_;
        };

        auto now_us = []() -> std::uint64_t {
            return static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count());
        };

        tools_["cc.vote_pin"] = [this, who, compute_pinned, now_us]
            (const json& args, std::string_view caller) -> json
        {
            auto agent = who(args, caller);
            auto epoch = args.value("epoch", std::uint64_t{0});
            if (epoch == 0) return json{{"error", "epoch required"}};
            bool unvote = args.value("unvote", false);

            PinState snapshot;
            {
                std::lock_guard lock(pin_mu_);
                auto& st = pins_[epoch];
                st.epoch = epoch;
                if (unvote) st.voters.erase(agent);
                else        st.voters.insert(agent);
                st.updated_us = now_us();
                snapshot = st;
            }

            // Audit trail in WAL (non-fatal if append fails).
            json payload{
                {"kind",   "pin_vote"},
                {"epoch",  epoch},
                {"unvote", unvote},
                {"votes",  snapshot.voters.size()},
            };
            Click click{
                unvote ? "OBSERVE" : "OBSERVE",
                agent, std::string{},
                std::string{"pin:"} + std::to_string(epoch),
                0, epoch, payload.dump(),
            };
            auto r = wal_.append(click);
            std::uint64_t audit_epoch = r ? r->header.epoch : 0;

            return json{
                {"epoch",          epoch},
                {"pinned",         compute_pinned(snapshot)},
                {"votes",          snapshot.voters.size()},
                {"voters",         json(snapshot.voters)},
                {"threshold",      pin_threshold_},
                {"manual_override", snapshot.manual_override.has_value()
                                     ? json(*snapshot.manual_override)
                                     : json(nullptr)},
                {"audit_epoch",    audit_epoch},
            };
        };

        tools_["cc.pin_override"] = [this, who, compute_pinned, now_us]
            (const json& args, std::string_view caller) -> json
        {
            auto agent = who(args, caller);
            auto epoch = args.value("epoch", std::uint64_t{0});
            if (epoch == 0) return json{{"error", "epoch required"}};

            // `pinned` may be: true (force pin), false (force unpin), or null/absent (clear override)
            std::optional<bool> override_val;
            if (args.contains("pinned") && !args["pinned"].is_null()) {
                override_val = args["pinned"].get<bool>();
            }

            PinState snapshot;
            {
                std::lock_guard lock(pin_mu_);
                auto& st = pins_[epoch];
                st.epoch = epoch;
                st.manual_override = override_val;
                st.override_by     = override_val ? agent : std::string{};
                st.updated_us      = now_us();
                snapshot = st;
            }

            json payload{
                {"kind",   "pin_override"},
                {"epoch",  epoch},
                {"pinned", override_val ? json(*override_val) : json(nullptr)},
                {"by",     agent},
            };
            Click click{
                "DIRECT", agent, std::string{},
                std::string{"pin_override:"} + std::to_string(epoch),
                0, epoch, payload.dump(),
            };
            auto r = wal_.append(click);
            std::uint64_t audit_epoch = r ? r->header.epoch : 0;

            return json{
                {"epoch",           epoch},
                {"pinned",          compute_pinned(snapshot)},
                {"manual_override", override_val ? json(*override_val) : json(nullptr)},
                {"override_by",     snapshot.override_by},
                {"votes",           snapshot.voters.size()},
                {"threshold",       pin_threshold_},
                {"audit_epoch",     audit_epoch},
            };
        };

        tools_["cc.query_pins"] = [this, compute_pinned]
            (const json&, std::string_view) -> json
        {
            // Snapshot under lock then read the WAL outside the lock to keep
            // vote paths unblocked.
            std::vector<PinState> snapshot;
            {
                std::lock_guard lock(pin_mu_);
                snapshot.reserve(pins_.size());
                for (const auto& [_, st] : pins_) snapshot.push_back(st);
            }

            // Build epoch → clack map by one bulk read (cheap relative to WAL tail).
            std::unordered_map<std::uint64_t, Clack> index;
            if (auto r = wal_.read_range(0, 100000); r) {
                for (auto& c : *r) index.emplace(c.header.epoch, std::move(c));
            }

            json out = json::array();
            for (const auto& st : snapshot) {
                bool pinned = compute_pinned(st);
                // Omit fully-idle entries (no votes, no override, not pinned)
                // to keep the board clean.
                if (!pinned && st.voters.empty() && !st.manual_override) continue;

                json item{
                    {"epoch",           st.epoch},
                    {"pinned",          pinned},
                    {"votes",           st.voters.size()},
                    {"voters",          json(st.voters)},
                    {"threshold",       pin_threshold_},
                    {"manual_override", st.manual_override.has_value()
                                          ? json(*st.manual_override)
                                          : json(nullptr)},
                    {"override_by",     st.override_by},
                    {"updated_us",      st.updated_us},
                };
                if (auto it = index.find(st.epoch); it != index.end()) {
                    item["clack"] = clack_to_json(it->second);
                }
                out.push_back(item);
            }
            return out;
        };

        // Shorthand tools
        tools_["cc.claim_task"] = [this, who, ensure_task](const json& args, std::string_view caller) -> json {
            auto agent = who(args, caller);
            auto tid     = ensure_task(args);
            auto slice   = args.value("slice", std::string{});   // optional sub-claim slice
            auto reason  = args.value("reason", std::string{});

            json payload{{"reason", reason}};
            if (!slice.empty()) payload["slice"] = slice;

            // De-clobber: if the task is already claimed by ANOTHER agent on the
            // SAME slice, surface that as an advisory warning. (Non-blocking;
            // callers that want a slice can still force-claim by ignoring the warn.)
            json warn = json::value_t::null;
            auto existing = views_.query_task(tid.id);
            if (existing && *existing) {
                const auto& ts = **existing;
                if (ts.owner_agent && *ts.owner_agent != agent &&
                    (ts.status == TaskStatus::Claimed || ts.status == TaskStatus::InProgress))
                {
                    warn = json{
                        {"code", "already_claimed"},
                        {"by",   *ts.owner_agent},
                        {"status", std::string{task_status_to_str(ts.status)}},
                        {"suggest", "pass slice to sub-claim, or call cc.query_task first"},
                    };
                }
            }

            Click click{"CLAIM", agent, tid.id, "Claiming task", 0, 0, payload.dump()};
            auto result = wal_.append(click);
            if (!result) return json{{"error", result.error().message}};
            json out{{"epoch", result->header.epoch}, {"task_id", tid.id}};
            if (tid.generated)   out["task_id_generated"] = true;
            if (!warn.is_null()) out["warning"] = warn;
            if (!slice.empty())  out["slice"]   = slice;
            return out;
        };

        tools_["cc.report_progress"] = [this, who, ensure_task](const json& args, std::string_view caller) -> json {
            auto tid = ensure_task(args);
            json payload{{"pct", args.value("pct", 0)}, {"summary", args.value("summary", "")}};
            Click click{"PROGRESS", who(args, caller), tid.id,
                         args.value("summary", ""), 0, 0, payload.dump()};
            auto result = wal_.append(click);
            if (!result) return json{{"error", result.error().message}};
            json out{{"epoch", result->header.epoch}, {"task_id", tid.id}};
            if (tid.generated) out["task_id_generated"] = true;
            return out;
        };

        tools_["cc.complete_task"] = [this, who, ensure_task](const json& args, std::string_view caller) -> json {
            auto tid = ensure_task(args);
            json payload{{"summary", args.value("summary", "")}};
            if (args.contains("artifact_epochs")) payload["artifacts"] = args["artifact_epochs"];
            Click click{"COMPLETE", who(args, caller), tid.id,
                         args.value("summary", ""), 0, 0, payload.dump()};
            auto result = wal_.append(click);
            if (!result) return json{{"error", result.error().message}};
            json out{{"epoch", result->header.epoch}, {"task_id", tid.id}};
            if (tid.generated) out["task_id_generated"] = true;
            return out;
        };

        tools_["cc.post_artifact"] = [this, who, ensure_task](const json& args, std::string_view caller) -> json {
            auto tid = ensure_task(args);
            json payload{
                {"kind", args.value("kind", "file")},
                {"path", args.value("path", "")},
                {"sha256", args.value("sha256", "")},
            };
            if (args.contains("content")) payload["content"] = args["content"];
            Click click{"ARTIFACT", who(args, caller), tid.id,
                         args.value("path", ""), 0, 0, payload.dump()};
            auto result = wal_.append(click);
            if (!result) return json{{"error", result.error().message}};
            json out{{"epoch", result->header.epoch}, {"task_id", tid.id}};
            if (tid.generated) out["task_id_generated"] = true;
            return out;
        };

        tools_["cc.heartbeat"] = [this, who](const json& args, std::string_view caller) -> json {
            json payload{{"load", args.value("load", 0.0)}};
            Click click{"HEARTBEAT", who(args, caller), "", "heartbeat", 0, 0, payload.dump()};
            auto result = wal_.append(click);
            if (!result) return json{{"error", result.error().message}};
            return json{{"epoch", result->header.epoch}};
        };

        // ── cc.ack — acknowledge a specific clack ───────────
        tools_["cc.ack"] = [this, who](const json& args, std::string_view caller) -> json {
            auto epoch = args.value("epoch", std::uint64_t{0});
            if (epoch == 0) return json{{"error", "epoch required"}};
            auto note  = args.value("note", std::string{});

            // Echo task_id from the acked clack for downstream filtering.
            std::string task_id;
            auto origin = wal_.read_one(epoch);
            if (origin && *origin) task_id = (*origin)->task_id;

            json payload{{"ack_epoch", epoch}};
            if (!note.empty()) payload["note"] = note;

            Click click{"ACK", who(args, caller), task_id, "ack", 0, epoch, payload.dump()};
            auto result = wal_.append(click);
            if (!result) return json{{"error", result.error().message}};
            return json{{"epoch", result->header.epoch}, {"acked", epoch}};
        };

        // ── cc.ask / cc.answer — dialogue primitive ─────────
        tools_["cc.ask"] = [this, who, ensure_task](const json& args, std::string_view caller) -> json {
            auto subject = args.value("subject", std::string{});
            auto tid     = ensure_task(args);
            auto to      = args.value("to", std::string{});
            auto body    = args.contains("body") ? args["body"] : json::object();

            json payload{{"body", body}};
            if (!to.empty()) payload["to"] = to;

            Click click{"ASK", who(args, caller), tid.id, subject, 0, 0, payload.dump()};
            auto result = wal_.append(click);
            if (!result) return json{{"error", result.error().message}};
            json out{{"epoch", result->header.epoch}, {"task_id", tid.id}};
            if (tid.generated) out["task_id_generated"] = true;
            return out;
        };

        tools_["cc.answer"] = [this, who](const json& args, std::string_view caller) -> json {
            auto ask_epoch = args.value("ask_epoch", std::uint64_t{0});
            if (ask_epoch == 0) return json{{"error", "ask_epoch required"}};
            auto body = args.contains("body") ? args["body"] : json::object();

            // Inherit task_id & subject from the ASK so threading is clean.
            std::string task_id;
            std::string subject = "answer";
            auto origin = wal_.read_one(ask_epoch);
            if (origin && *origin) {
                task_id = (*origin)->task_id;
                if (!(*origin)->subject.empty()) subject = "re: " + (*origin)->subject;
            }

            json payload{{"body", body}, {"ask_epoch", ask_epoch}};
            Click click{"ANSWER", who(args, caller), task_id, subject, 0, ask_epoch, payload.dump()};
            auto result = wal_.append(click);
            if (!result) return json{{"error", result.error().message}};
            return json{{"epoch", result->header.epoch}};
        };

        // ── cc.reserve_path / release_path / query_reservations ─
        // Advisory soft-lock: announce intent to touch a path. Not enforced.
        // Stored as RESERVE/RELEASE clacks + in-memory index for fast lookup.
        tools_["cc.reserve_path"] = [this, who, ensure_task](const json& args, std::string_view caller) -> json {
            auto path    = args.value("path", std::string{});
            if (path.empty()) return json{{"error", "path required"}};
            auto tid     = ensure_task(args);
            auto ttl_ms  = args.value("ttl_ms", std::uint64_t{300000}); // 5 min default
            auto agent   = who(args, caller);

            std::lock_guard lock(reserve_mu_);
            auto now_us = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count());
            auto expires_us = now_us + ttl_ms * 1000;

            // Clobber check
            json conflict = json::value_t::null;
            if (auto it = reservations_.find(path); it != reservations_.end()) {
                if (it->second.agent != agent && it->second.expires_us > now_us) {
                    conflict = json{
                        {"code", "reserved"},
                        {"by",   it->second.agent},
                        {"task_id", it->second.task_id},
                        {"expires_us", it->second.expires_us},
                    };
                }
            }

            json payload{{"path", path}, {"ttl_ms", ttl_ms}, {"expires_us", expires_us}};
            if (!conflict.is_null()) payload["conflict"] = conflict;

            Click click{"RESERVE", agent, tid.id, path, 0, 0, payload.dump()};
            auto result = wal_.append(click);
            if (!result) return json{{"error", result.error().message}};

            reservations_[path] = Reservation{agent, tid.id, expires_us, result->header.epoch};

            json out{{"epoch", result->header.epoch}, {"path", path},
                     {"expires_us", expires_us}, {"task_id", tid.id}};
            if (tid.generated)   out["task_id_generated"] = true;
            if (!conflict.is_null()) out["conflict"] = conflict;
            return out;
        };

        tools_["cc.release_path"] = [this, who](const json& args, std::string_view caller) -> json {
            auto path = args.value("path", std::string{});
            if (path.empty()) return json{{"error", "path required"}};
            auto agent = who(args, caller);

            {
                std::lock_guard lock(reserve_mu_);
                reservations_.erase(path);
            }

            json payload{{"path", path}};
            Click click{"RELEASE", agent, args.value("task_id", ""), path, 0, 0, payload.dump()};
            auto result = wal_.append(click);
            if (!result) return json{{"error", result.error().message}};
            return json{{"epoch", result->header.epoch}, {"released", path}};
        };

        tools_["cc.query_reservations"] = [this](const json& args, std::string_view) -> json {
            auto prefix = args.value("path_prefix", std::string{});
            auto now_us = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count());

            std::lock_guard lock(reserve_mu_);
            json out = json::array();
            for (const auto& [p, r] : reservations_) {
                if (r.expires_us <= now_us) continue;
                if (!prefix.empty() && p.rfind(prefix, 0) != 0) continue;
                out.push_back(json{
                    {"path", p},
                    {"agent", r.agent},
                    {"task_id", r.task_id},
                    {"expires_us", r.expires_us},
                    {"epoch", r.epoch},
                });
            }
            return out;
        };

        // ── cc.get_task — enriched single-task snapshot ─────
        // Combines task_state + recent progress + artifacts + active claimants +
        // any reservations. One round-trip instead of 3-4.
        tools_["cc.get_task"] = [this](const json& args, std::string_view) -> json {
            auto task_id = args.value("task_id", std::string{});
            if (task_id.empty()) return json{{"error", "task_id required"}};

            json out{{"task_id", task_id}};

            // Base state
            if (auto ts = views_.query_task(task_id); ts && *ts) {
                out["state"] = task_state_to_json(**ts);
            } else {
                out["state"] = json::value_t::null;
            }

            // Artifacts
            json arts = json::array();
            if (auto a = views_.query_artifacts(task_id); a) {
                for (const auto& ae : *a) {
                    arts.push_back(json{
                        {"epoch", ae.epoch},
                        {"kind",  ae.kind},
                        {"path",  ae.path},
                        {"sha256", ae.sha256},
                        {"review_status", ae.review_status},
                    });
                }
            }
            out["artifacts"] = arts;

            // Recent clacks for this task (progress, claims, asks, etc.)
            json recent = json::array();
            std::unordered_map<std::string, std::string> claimant_slices;
            if (auto r = wal_.read_range(0, 2048); r) {
                for (const auto& c : *r) {
                    if (c.task_id != task_id) continue;
                    recent.push_back(clack_to_json(c));
                    // Track active claimants by slice
                    if (c.header.verb == Verb::Claim) {
                        auto p = nlohmann::json::parse(c.payload, nullptr, false);
                        std::string slice = (p.is_object() && p.contains("slice") && p["slice"].is_string())
                            ? p["slice"].get<std::string>() : std::string{""};
                        claimant_slices[c.agent_id + "/" + slice] =
                            std::to_string(c.header.epoch);
                    } else if (c.header.verb == Verb::Complete || c.header.verb == Verb::Yield) {
                        // Non-slice coarse clear
                        for (auto it = claimant_slices.begin(); it != claimant_slices.end(); ) {
                            if (it->first.rfind(c.agent_id + "/", 0) == 0) it = claimant_slices.erase(it);
                            else ++it;
                        }
                    }
                }
                if (recent.size() > 64) {
                    recent.erase(recent.begin(), recent.begin() + (recent.size() - 64));
                }
            }
            out["recent"] = recent;

            json claimants = json::array();
            for (const auto& [k, epoch_str] : claimant_slices) {
                auto slash = k.find('/');
                claimants.push_back(json{
                    {"agent", k.substr(0, slash)},
                    {"slice", k.substr(slash + 1)},
                    {"epoch", std::stoull(epoch_str)},
                });
            }
            out["active_claimants"] = claimants;

            // Reservations touching this task
            json reserves = json::array();
            {
                std::lock_guard lock(reserve_mu_);
                auto now_us = static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count());
                for (const auto& [p, r] : reservations_) {
                    if (r.task_id != task_id) continue;
                    if (r.expires_us <= now_us) continue;
                    reserves.push_back(json{
                        {"path", p}, {"agent", r.agent},
                        {"expires_us", r.expires_us}, {"epoch", r.epoch},
                    });
                }
            }
            out["reservations"] = reserves;

            return out;
        };

        // ── cc.wait — blocking long-poll ────────────────────
        // The headline upgrade. Blocks server-side until a clack matching the
        // supplied filters arrives (or timeout). Eliminates the polling loop.
        tools_["cc.wait"] = [this](const json& args, std::string_view) -> json {
            auto since      = args.value("since_epoch", std::uint64_t{0});
            auto timeout_ms = args.value("timeout_ms", 30000);
            auto limit      = args.value("limit", 64);
            timeout_ms = std::clamp(timeout_ms, 0, 120000); // hard cap 2 min

            std::string task_filter  = args.value("task_id",  std::string{});
            std::string agent_filter = args.value("agent_id", std::string{});
            std::uint64_t parent_filter = args.value("parent_epoch", std::uint64_t{0});
            std::unordered_set<std::string> verbs;
            if (args.contains("verbs") && args["verbs"].is_array()) {
                for (const auto& v : args["verbs"]) {
                    if (v.is_string()) verbs.insert(v.get<std::string>());
                }
            }

            auto matches = [&](const Clack& c) -> bool {
                if (!task_filter.empty()  && c.task_id  != task_filter)  return false;
                if (!agent_filter.empty() && c.agent_id != agent_filter) return false;
                if (parent_filter != 0    && c.header.parent_epoch != parent_filter) return false;
                if (!verbs.empty()) {
                    auto v = std::string{verb_to_str(c.header.verb)};
                    if (!verbs.contains(v)) return false;
                }
                return true;
            };

            auto collect = [&](std::uint64_t from) -> json {
                json out = json::array();
                auto r = wal_.read_range(from, std::max(limit * 4, 64));
                if (!r) return out;
                for (const auto& c : *r) {
                    if (!matches(c)) continue;
                    out.push_back(clack_to_json(c));
                    if (static_cast<int>(out.size()) >= limit) break;
                }
                return out;
            };

            // Fast path: existing match already present.
            auto initial = collect(since);
            if (!initial.empty()) {
                return json{{"clacks", initial}, {"timed_out", false}};
            }

            // Block until new epoch appears (or timeout) and retry, potentially multiple
            // times, until we either find a match or our total timeout budget is spent.
            using clk = std::chrono::steady_clock;
            auto deadline = clk::now() + std::chrono::milliseconds(timeout_ms);
            while (clk::now() < deadline) {
                auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                    deadline - clk::now());
                auto new_epoch = wal_.wait_for_next(since, remaining);
                if (new_epoch <= since) break;  // timeout hit
                auto batch = collect(since);
                if (!batch.empty()) {
                    return json{{"clacks", batch}, {"timed_out", false}};
                }
                since = new_epoch; // advance and keep waiting if no match on new epoch
            }

            return json{{"clacks", json::array()}, {"timed_out", true}};
        };
    }

    // ── Tool dispatch ───────────────────────────────────────

    [[nodiscard]] auto dispatch(std::string_view tool_name,
                                const json& args,
                                std::string_view agent_id) -> json
    {
        auto it = tools_.find(std::string{tool_name});
        if (it == tools_.end()) {
            return json{{"error", "unknown_tool"}, {"tool", tool_name}};
        }
        return it->second(args, agent_id);
    }

    // ── Tool listing (for MCP discovery) ────────────────────

    [[nodiscard]] auto list_tools() const -> json {
        json tools = json::array();
        for (const auto& [name, _] : tools_) {
            tools.push_back(json{{"name", name}});
        }
        return tools;
    }

private:
    Wal&          wal_;
    Materializer& views_;
    std::unordered_map<std::string, McpToolHandler> tools_;

    // Advisory path reservations (not persisted across restart; the WAL
    // carries the audit trail via RESERVE/RELEASE clacks).
    struct Reservation {
        std::string   agent;
        std::string   task_id;
        std::uint64_t expires_us{0};
        std::uint64_t epoch{0};
    };
    mutable std::mutex                           reserve_mu_;
    std::unordered_map<std::string, Reservation> reservations_;

    // Pin state — in-memory board of clacks elevated to the top of the feed.
    // A clack is pinned when EITHER:
    //   (a) manual_override == true  (operator force-pin), or
    //   (b) manual_override is unset AND voters.size() >= pin_threshold_.
    // manual_override == false explicitly unpins regardless of votes.
    // The WAL holds the audit trail via OBSERVE (vote) / DIRECT (override) clacks.
    struct PinState {
        std::uint64_t                   epoch{0};
        std::unordered_set<std::string> voters;
        std::optional<bool>             manual_override;
        std::string                     override_by;
        std::uint64_t                   updated_us{0};
    };
    mutable std::mutex                              pin_mu_;
    std::unordered_map<std::uint64_t, PinState>     pins_;
    std::size_t                                     pin_threshold_{2};
};

} // namespace cc
