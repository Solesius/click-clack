#pragma once
// ──────────────────────────────────────────────────────────────
// click-clack / mcp / tool_catalog.hpp
// MCP tool descriptors — name, description, JSON Schema
// Celer-style: constexpr data + free functions; no exceptions
// ──────────────────────────────────────────────────────────────

#include "protocol.hpp"

#include <array>
#include <string_view>

namespace cc::mcp {

// A tool descriptor matches the MCP Tool schema:
//   { name, description, inputSchema, annotations? }
struct ToolDescriptor {
    std::string_view name;
    std::string_view description;
    json             input_schema;
    json             annotations = json::object();

    [[nodiscard]] json to_json() const {
        return json{
            {"name",         std::string{name}},
            {"description",  std::string{description}},
            {"inputSchema",  input_schema},
            {"annotations",  annotations},
        };
    }
};

// Annotation presets (hints, not security).
[[nodiscard]] inline json read_only() {
    return json{{"readOnlyHint", true}, {"idempotentHint", true}};
}
[[nodiscard]] inline json mutating() {
    return json{{"readOnlyHint", false}, {"idempotentHint", false}};
}

// ── Schema helpers (compact JSON Schema literals) ──────────────
[[nodiscard]] inline json schema_object(std::initializer_list<std::pair<const std::string, json>> props,
                                        std::vector<std::string> required = {}) {
    json p = json::object();
    for (const auto& [k, v] : props) p[k] = v;
    json s{{"type", "object"}, {"properties", std::move(p)}};
    if (!required.empty()) s["required"] = std::move(required);
    return s;
}

[[nodiscard]] inline json prop_string(std::string_view desc = "") {
    return json{{"type", "string"}, {"description", std::string{desc}}};
}
[[nodiscard]] inline json prop_integer(std::string_view desc = "") {
    return json{{"type", "integer"}, {"description", std::string{desc}}};
}
[[nodiscard]] inline json prop_number(std::string_view desc = "") {
    return json{{"type", "number"}, {"description", std::string{desc}}};
}
[[nodiscard]] inline json prop_object(std::string_view desc = "") {
    return json{{"type", "object"}, {"description", std::string{desc}}, {"additionalProperties", true}};
}
[[nodiscard]] inline json prop_array_of(json item, std::string_view desc = "") {
    return json{{"type", "array"}, {"items", std::move(item)}, {"description", std::string{desc}}};
}
[[nodiscard]] inline json prop_boolean(std::string_view desc = "") {
    return json{{"type", "boolean"}, {"description", std::string{desc}}};
}

// ── click-clack tool catalog ───────────────────────────────────
[[nodiscard]] inline std::vector<ToolDescriptor> click_clack_tools() {
    std::vector<ToolDescriptor> tools;
    tools.reserve(24);

    // The universal optional identity override — callers may pass `as_agent`
    // on ANY mutating tool to pin writes to a specific logical agent id,
    // overriding the transport-level caller. Useful when one OS process hosts
    // multiple logical agents.
    const json as_agent_prop = prop_string("Optional agent id override (logical identity for this call)");

    // task_id is ALWAYS optional on mutating tools. If omitted, the server
    // mints a fresh UUIDv4-based id (prefixed `t-`) and returns it in the
    // response under `task_id` with `task_id_generated: true`. The caller
    // should thread this id through follow-up calls to keep the same task.

    tools.push_back({
        "cc.post_clack",
        "Append a raw Click to the WAL. Returns {epoch, timestamp_us, task_id}. If task_id is omitted, a fresh UUID is generated server-side and returned (with task_id_generated: true).",
        schema_object({
            {"verb",     prop_string("Verb, e.g. ANNOUNCE | CLAIM | PROGRESS | COMPLETE | ERROR | ASK | ANSWER | ACK | RESERVE | RELEASE")},
            {"task_id",  prop_string("Optional task correlation id — auto-generated if omitted")},
            {"subject",  prop_string("Short human-readable subject")},
            {"flags",    prop_integer("Bitfield: urgent|blocking|hitl_req|ephemeral")},
            {"parent",   prop_integer("Parent epoch for threading (0 = root)")},
            {"payload",  prop_object("Free-form JSON payload")},
            {"as_agent", as_agent_prop},
        }, {"verb"}),
        mutating(),
    });

    tools.push_back({
        "cc.query_timeline",
        "Read a range of clacks from the WAL. Supports combined filters: single verb, verb list, task_id, agent_id.",
        schema_object({
            {"since_epoch", prop_integer("Start epoch (0 = from beginning)")},
            {"limit",       prop_integer("Max clacks (default 100)")},
            {"verb_filter", prop_string("Optional single verb name")},
            {"verbs",       prop_array_of(prop_string(), "Optional verb whitelist (any-of)")},
            {"task_id",     prop_string("Optional task correlation filter")},
            {"agent_id",    prop_string("Optional agent id filter")},
        }),
        read_only(),
    });

    tools.push_back({
        "cc.query_agent_log",
        "Read every clack emitted by a specific agent.",
        schema_object({
            {"agent_id",    prop_string("Agent identifier")},
            {"since_epoch", prop_integer("Start epoch (0 = from beginning)")},
            {"limit",       prop_integer("Max clacks (default 100)")},
        }, {"agent_id"}),
        read_only(),
    });

    tools.push_back({
        "cc.query_epoch",
        "Fetch exactly one clack by epoch from the WAL.",
        schema_object({{"epoch", prop_integer("Epoch of the clack")}}, {"epoch"}),
        read_only(),
    });

    tools.push_back({
        "cc.query_task",
        "Fetch the materialized state of a single task.",
        schema_object({{"task_id", prop_string()}}, {"task_id"}),
        read_only(),
    });

    tools.push_back({
        "cc.query_tasks",
        "List tasks with optional status filter.",
        schema_object({
            {"status_filter", prop_string("unclaimed | claimed | in_progress | blocked | completed | error")},
            {"limit",         prop_integer("Default 100")},
        }),
        read_only(),
    });

    tools.push_back({
        "cc.query_thread",
        "Walk a thread rooted at the given epoch.",
        schema_object({{"root_epoch", prop_integer()}}, {"root_epoch"}),
        read_only(),
    });

    tools.push_back({
        "cc.query_hitl_queue",
        "Return the pending human-in-the-loop approval queue.",
        schema_object({}),
        read_only(),
    });

    tools.push_back({
        "cc.query_presence",
        "Return all currently known agents and their presence.",
        schema_object({}),
        read_only(),
    });

    tools.push_back({
        "cc.vote_pin",
        "Cast a peer vote to pin a clack to the top of the feed. When votes reach the threshold (default 2) the clack becomes pinned. Pass unvote=true to withdraw your vote.",
        schema_object({
            {"epoch",    prop_integer("Epoch of the clack to vote on")},
            {"unvote",   prop_boolean("Set true to withdraw your vote")},
            {"as_agent", as_agent_prop},
        }, {"epoch"}),
        mutating(),
    });

    tools.push_back({
        "cc.pin_override",
        "Operator override: force-pin or force-unpin a clack regardless of votes. Pass pinned=null (or omit) to clear the override and fall back to the vote count.",
        schema_object({
            {"epoch",    prop_integer("Epoch of the clack")},
            {"pinned",   prop_boolean("true = force pin, false = force unpin, null = clear override")},
            {"as_agent", as_agent_prop},
        }, {"epoch"}),
        mutating(),
    });

    tools.push_back({
        "cc.query_pins",
        "List all currently pinned clacks with vote counts and override state.",
        schema_object({}),
        read_only(),
    });

    tools.push_back({
        "cc.claim_task",
        "Claim a task for the calling agent. Writes a CLAIM clack. Returns {epoch, task_id}. Pass `slice` for sub-claims (allows multiple agents to co-own a task across disjoint slices). If task_id is omitted, a fresh id is minted (useful for `announce + claim` in one shot).",
        schema_object({
            {"task_id", prop_string("Optional — auto-generated if omitted")},
            {"reason",  prop_string("Optional justification")},
            {"slice",   prop_string("Optional slice id for sub-claim (e.g. 'backend', 'tests')")},
            {"as_agent", as_agent_prop},
        }),
        mutating(),
    });

    tools.push_back({
        "cc.report_progress",
        "Report progress on a task. Writes a PROGRESS clack. Returns {epoch, task_id}.",
        schema_object({
            {"task_id", prop_string("Optional — auto-generated if omitted")},
            {"pct",     prop_integer("0-100")},
            {"summary", prop_string()},
            {"as_agent", as_agent_prop},
        }),
        mutating(),
    });

    tools.push_back({
        "cc.complete_task",
        "Mark a task complete. Writes a COMPLETE clack. Returns {epoch, task_id}.",
        schema_object({
            {"task_id", prop_string("Optional — auto-generated if omitted")},
            {"summary", prop_string()},
            {"artifact_epochs", prop_array_of(prop_integer(), "Epochs of ARTIFACT clacks for this task")},
            {"as_agent", as_agent_prop},
        }),
        mutating(),
    });

    tools.push_back({
        "cc.post_artifact",
        "Attach an artifact to a task. Writes an ARTIFACT clack. Returns {epoch, task_id}.",
        schema_object({
            {"task_id", prop_string("Optional — auto-generated if omitted")},
            {"kind",    prop_string("file | patch | report | other")},
            {"path",    prop_string()},
            {"sha256",  prop_string()},
            {"content", prop_string("Optional inline text")},
            {"as_agent", as_agent_prop},
        }, {"kind"}),
        mutating(),
    });

    tools.push_back({
        "cc.heartbeat",
        "Emit a HEARTBEAT clack (maintains agent presence).",
        schema_object({
            {"load", prop_number("Current load factor 0.0-1.0")},
            {"as_agent", as_agent_prop},
        }),
        mutating(),
    });

    // ── New in 2025.8: identity, dialogue, long-poll, reservations ──

    tools.push_back({
        "cc.whoami",
        "Return the effective identity for this call. Useful for debugging multi-agent plumbing.",
        schema_object({{"as_agent", as_agent_prop}}),
        read_only(),
    });

    tools.push_back({
        "cc.wait",
        "Blocking long-poll: return clacks newer than since_epoch matching filters, waiting up to timeout_ms for a match. Eliminates the polling loop for reactive agents.",
        schema_object({
            {"since_epoch",  prop_integer("Anchor epoch; results must be strictly greater")},
            {"timeout_ms",   prop_integer("Max wait time in milliseconds (default 30000, cap 120000)")},
            {"limit",        prop_integer("Max clacks returned (default 64)")},
            {"task_id",      prop_string("Optional task correlation filter")},
            {"agent_id",     prop_string("Optional agent id filter")},
            {"parent_epoch", prop_integer("Optional parent epoch filter (great for cc.ask follow-ups)")},
            {"verbs",        prop_array_of(prop_string(), "Optional verb whitelist")},
        }, {"since_epoch"}),
        read_only(),
    });

    tools.push_back({
        "cc.ack",
        "Acknowledge a specific clack by epoch. Writes an ACK clack with the acked epoch as parent.",
        schema_object({
            {"epoch", prop_integer("Epoch of the clack being acknowledged")},
            {"note",  prop_string("Optional note")},
            {"as_agent", as_agent_prop},
        }, {"epoch"}),
        mutating(),
    });

    tools.push_back({
        "cc.ask",
        "Pose a question to another agent (or broadcast). Writes an ASK clack; pair with cc.wait(parent_epoch=<returned epoch>) to await answers. Returns {epoch, task_id}.",
        schema_object({
            {"subject", prop_string("Short question summary")},
            {"body",    prop_object("Structured question body")},
            {"task_id", prop_string("Optional — auto-generated if omitted")},
            {"to",      prop_string("Optional target agent id (omit to broadcast)")},
            {"as_agent", as_agent_prop},
        }, {"subject"}),
        mutating(),
    });

    tools.push_back({
        "cc.answer",
        "Answer a prior cc.ask by its epoch. Writes an ANSWER clack with parent=ask_epoch so threading holds.",
        schema_object({
            {"ask_epoch", prop_integer("Epoch of the ASK being answered")},
            {"body",      prop_object("Structured answer body")},
            {"as_agent",  as_agent_prop},
        }, {"ask_epoch"}),
        mutating(),
    });

    tools.push_back({
        "cc.reserve_path",
        "Announce an advisory soft-lock on a file path. Not enforced — but broadcast so other agents can coordinate. Returns {epoch, path, expires_us, task_id} (+ conflict info if another agent already holds it).",
        schema_object({
            {"path",    prop_string("Path to reserve")},
            {"task_id", prop_string("Optional — auto-generated if omitted")},
            {"ttl_ms",  prop_integer("Reservation lifetime in ms (default 300000 = 5 min)")},
            {"as_agent", as_agent_prop},
        }, {"path"}),
        mutating(),
    });

    tools.push_back({
        "cc.release_path",
        "Release an advisory reservation on a path.",
        schema_object({
            {"path",    prop_string()},
            {"task_id", prop_string()},
            {"as_agent", as_agent_prop},
        }, {"path"}),
        mutating(),
    });

    tools.push_back({
        "cc.query_reservations",
        "List active advisory path reservations (optionally filtered by prefix).",
        schema_object({
            {"path_prefix", prop_string("Optional path prefix filter")},
        }),
        read_only(),
    });

    tools.push_back({
        "cc.get_task",
        "Enriched task snapshot: state + recent clacks + artifacts + active claimants (per slice) + reservations. One round-trip.",
        schema_object({{"task_id", prop_string()}}, {"task_id"}),
        read_only(),
    });

    return tools;
}

// ── Resource catalog ───────────────────────────────────────────
// Celer-backed resources. URIs are cc://<namespace>/<selector>.
struct ResourceDescriptor {
    std::string uri;
    std::string name;
    std::string description;
    std::string mime_type;

    [[nodiscard]] json to_json() const {
        return json{
            {"uri",         uri},
            {"name",        name},
            {"description", description},
            {"mimeType",    mime_type},
        };
    }
};

[[nodiscard]] inline std::vector<ResourceDescriptor> click_clack_resources() {
    return {
        {"cc://timeline",   "timeline",    "Full WAL timeline as JSON array",             "application/json"},
        {"cc://tasks",      "tasks",       "All materialized tasks as JSON array",        "application/json"},
        {"cc://presence",   "presence",    "Agent presence snapshot as JSON array",       "application/json"},
        {"cc://hitl",       "hitl-queue",  "Pending human-in-the-loop queue as JSON",     "application/json"},
    };
}

} // namespace cc::mcp
