#pragma once
// ──────────────────────────────────────────────────────────────
// click-clack / mcp / dispatcher.hpp
// Native MCP JSON-RPC dispatcher — transport-agnostic.
// Converts an inbound Message → outbound json response (or std::nullopt
// for notifications). Uses celer Result<T>/StreamHandle where data flows.
// ──────────────────────────────────────────────────────────────

#include "protocol.hpp"
#include "tool_catalog.hpp"
#include "mcp_server.hpp"   // existing tool registry (reused)
#include "../views/materializer.hpp"
#include "../core/wal.hpp"

#include <celer/core/result.hpp>
#include <celer/core/stream.hpp>

#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace cc::mcp {

using celer::Chunk;
using celer::StreamHandle;

// ── Session state (per connection) ─────────────────────────────
struct Session {
    std::string id;
    std::atomic<bool> initialized{false};
    std::string client_name;
    std::string client_version;
    std::string protocol_version{std::string{kProtocolVersion}};
    std::string caller_agent_id{"vscode-copilot"};
};

// ── Dispatcher ─────────────────────────────────────────────────
class Dispatcher {
public:
    Dispatcher(McpServer& tools, Wal& wal, Materializer& views)
        : tools_(tools), wal_(wal), views_(views) {
        tool_index_ = click_clack_tools();
        resource_index_ = click_clack_resources();
    }

    Dispatcher(const Dispatcher&)            = delete;
    Dispatcher& operator=(const Dispatcher&) = delete;

    // Dispatch one message. Returns the outbound JSON envelope
    // (response or error) or std::nullopt for notifications.
    [[nodiscard]] auto dispatch(const Message& msg, Session& sess) const
        -> std::optional<json>
    {
        switch (msg.kind) {
            case MessageKind::Request:      return handle_request(msg, sess);
            case MessageKind::Notification: handle_notification(msg, sess); return std::nullopt;
            case MessageKind::Invalid:
                return make_error(msg.id, ec::InvalidRequest, "invalid JSON-RPC envelope");
            default:
                return std::nullopt;  // responses from client are out-of-scope
        }
    }

private:
    // ── Request dispatch table ─────────────────────────────────
    [[nodiscard]] json handle_request(const Message& m, Session& s) const {
        const auto& method = m.method;
        if (method == "initialize")              return on_initialize(m, s);
        if (method == "ping")                    return make_response(m.id, json::object());
        if (method == "tools/list")              return on_tools_list(m);
        if (method == "tools/call")              return on_tools_call(m, s);
        if (method == "resources/list")          return on_resources_list(m);
        if (method == "resources/templates/list")return make_response(m.id, json{{"resourceTemplates", json::array()}});
        if (method == "resources/read")          return on_resources_read(m);
        if (method == "resources/subscribe")     return make_response(m.id, json::object());
        if (method == "resources/unsubscribe")   return make_response(m.id, json::object());
        if (method == "prompts/list")            return make_response(m.id, json{{"prompts", json::array()}});
        if (method == "logging/setLevel")        return make_response(m.id, json::object());
        if (method == "completion/complete")     return make_response(m.id,
                                                          json{{"completion", json{{"values", json::array()}, {"hasMore", false}}}});
        return make_error(m.id, ec::MethodNotFound, std::string{"unknown method: "} + method);
    }

    void handle_notification(const Message& m, Session& s) const {
        if (m.method == "notifications/initialized") {
            s.initialized.store(true, std::memory_order_release);
        }
        // notifications/cancelled, notifications/progress → ignore (no long-running ops yet)
    }

    // ── initialize ─────────────────────────────────────────────
    [[nodiscard]] json on_initialize(const Message& m, Session& s) const {
        s.client_name      = m.params.value("/clientInfo/name"_json_pointer, std::string{});
        s.client_version   = m.params.value("/clientInfo/version"_json_pointer, std::string{});
        s.protocol_version = m.params.value("protocolVersion", std::string{kProtocolVersion});

        if (!s.client_name.empty()) s.caller_agent_id = "mcp:" + s.client_name;

        json result{
            {"protocolVersion", std::string{kProtocolVersion}},
            {"capabilities", json{
                {"tools",     json{{"listChanged", false}}},
                {"resources", json{{"listChanged", false}, {"subscribe", false}}},
                {"logging",   json::object()},
            }},
            {"serverInfo", json{
                {"name",    std::string{kServerName}},
                {"version", std::string{kServerVersion}},
            }},
            {"instructions",
                "click-clack coordination hub. Use cc.post_clack, cc.claim_task, "
                "cc.report_progress, cc.complete_task to drive workflows. "
                "Read cc.query_timeline / cc.query_tasks for state. "
                "Resources under cc:// expose live snapshots."},
        };
        return make_response(m.id, std::move(result));
    }

    // ── tools/list ─────────────────────────────────────────────
    [[nodiscard]] json on_tools_list(const Message& m) const {
        json arr = json::array();
        for (const auto& t : tool_index_) arr.push_back(t.to_json());
        return make_response(m.id, json{{"tools", std::move(arr)}});
    }

    // ── tools/call ─────────────────────────────────────────────
    [[nodiscard]] json on_tools_call(const Message& m, const Session& s) const {
        const auto name = m.params.value("name", std::string{});
        const auto args = m.params.value("arguments", json::object());

        if (name.empty()) {
            return make_error(m.id, ec::InvalidParams, "missing 'name'");
        }

        // Resolve against the existing tool registry. dispatch() returns
        // either a domain JSON or {"error": ...}. We convert to MCP shape.
        const auto raw = tools_.dispatch(name, args, s.caller_agent_id);

        const bool unknown = raw.is_object() && raw.value("error", "") == "unknown_tool";
        if (unknown) {
            return make_error(m.id, ec::InvalidParams,
                              std::string{"unknown tool: "} + name);
        }

        const bool tool_err = raw.is_object() && raw.contains("error");
        json result = call_tool_result(
            json::array({text_content(raw.dump(2))}),
            tool_err,
            /*structured=*/raw);
        return make_response(m.id, std::move(result));
    }

    // ── resources/list ─────────────────────────────────────────
    [[nodiscard]] json on_resources_list(const Message& m) const {
        json arr = json::array();
        for (const auto& r : resource_index_) arr.push_back(r.to_json());
        return make_response(m.id, json{{"resources", std::move(arr)}});
    }

    // ── resources/read (uses celer streams for large views) ────
    [[nodiscard]] json on_resources_read(const Message& m) const {
        const auto uri = m.params.value("uri", std::string{});
        if (uri.empty()) return make_error(m.id, ec::InvalidParams, "missing 'uri'");

        auto body = read_resource(uri);
        if (!body) {
            return make_error(m.id, ec::ResourceNotFound, body.error().message);
        }

        json contents = json::array({
            json{
                {"uri",      uri},
                {"mimeType", "application/json"},
                {"text",     body->dump()},
            }
        });
        return make_response(m.id, json{{"contents", std::move(contents)}});
    }

    // Streams the payload through a celer StreamHandle<char> for back-pressure
    // discipline, then folds into a JSON value. Celer-style error plumbing.
    [[nodiscard]] Result<json> read_resource(std::string_view uri) const {
        if (uri == "cc://timeline") {
            auto clacks = wal_.read_range(0, 1000);
            if (!clacks) return std::unexpected(clacks.error());
            json out = json::array();
            for (const auto& c : *clacks) out.push_back(clack_to_json(c));
            return fold_to_json(std::move(out));
        }
        if (uri == "cc://tasks") {
            auto tasks = views_.query_tasks(std::nullopt, 1000);
            if (!tasks) return std::unexpected(tasks.error());
            json out = json::array();
            for (const auto& t : *tasks) out.push_back(task_state_to_json(t));
            return fold_to_json(std::move(out));
        }
        if (uri == "cc://presence") {
            auto p = views_.query_presence();
            if (!p) return std::unexpected(p.error());
            json out = json::array();
            for (const auto& a : *p) out.push_back(presence_to_json(a));
            return fold_to_json(std::move(out));
        }
        if (uri == "cc://hitl") {
            auto q = views_.query_hitl_queue();
            if (!q) return std::unexpected(q.error());
            json out = json::array();
            for (const auto& item : *q) {
                out.push_back(json{
                    {"epoch",    item.epoch},
                    {"clack",    clack_to_json(item.clack)},
                    {"resolved", item.resolved},
                });
            }
            return fold_to_json(std::move(out));
        }
        return std::unexpected(Error{"ResourceNotFound", std::string{uri}});
    }

    // Route a JSON payload through a celer byte-stream so we honour the
    // library's backpressure contract even for in-memory reads. This will
    // naturally extend to true streaming once WAL exposes StreamHandle<Clack>.
    [[nodiscard]] static Result<json> fold_to_json(json value) {
        auto dumped = value.dump();
        auto stream = celer::stream::from_string(std::move(dumped));
        auto collected = celer::stream::collect_string(stream);
        if (!collected) return std::unexpected(collected.error());
        auto parsed = json::parse(*collected, nullptr, false);
        if (parsed.is_discarded()) {
            return std::unexpected(Error{"ResourceDecode", "json parse failure"});
        }
        return parsed;
    }

    McpServer&                        tools_;
    Wal&                              wal_;
    Materializer&                     views_;
    std::vector<ToolDescriptor>       tool_index_;
    std::vector<ResourceDescriptor>   resource_index_;
};

} // namespace cc::mcp
