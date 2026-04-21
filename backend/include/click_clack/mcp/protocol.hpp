#pragma once
// ──────────────────────────────────────────────────────────────
// click-clack / mcp / protocol.hpp
// Model Context Protocol — JSON-RPC envelopes & MCP types
// Ported from https://github.com/modelcontextprotocol/csharp-sdk
// Protocol version: 2025-06-18
// ──────────────────────────────────────────────────────────────

#include <nlohmann/json.hpp>
#include <celer/core/result.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace cc::mcp {

using json = nlohmann::json;
using celer::Error;
using celer::Result;
using celer::VoidResult;

// ── Protocol constants ──────────────────────────────────────────
inline constexpr std::string_view kProtocolVersion = "2025-06-18";
inline constexpr std::string_view kServerName      = "click-clack-hub";
inline constexpr std::string_view kServerVersion   = "0.1.0";

// ── JSON-RPC error codes (from McpErrorCode) ────────────────────
namespace ec {
inline constexpr int ParseError              = -32700;
inline constexpr int InvalidRequest          = -32600;
inline constexpr int MethodNotFound          = -32601;
inline constexpr int InvalidParams           = -32602;
inline constexpr int InternalError           = -32603;
inline constexpr int ResourceNotFound        = -32002;
inline constexpr int TransportError          = -32000;
} // namespace ec

// ── JSON-RPC envelope ───────────────────────────────────────────
struct RpcId {
    std::variant<std::monostate, std::int64_t, std::string> v;

    [[nodiscard]] bool is_null() const noexcept { return v.index() == 0; }

    [[nodiscard]] json to_json() const {
        switch (v.index()) {
            case 1: return std::get<std::int64_t>(v);
            case 2: return std::get<std::string>(v);
            default: return nullptr;
        }
    }

    [[nodiscard]] static RpcId from_json(const json& j) {
        RpcId id;
        if (j.is_number_integer())    id.v = j.get<std::int64_t>();
        else if (j.is_string())       id.v = j.get<std::string>();
        return id;
    }
};

enum class MessageKind : std::uint8_t {
    Request,
    Notification,
    Response,
    ResponseError,
    Invalid,
};

struct Message {
    MessageKind kind{MessageKind::Invalid};
    RpcId id;
    std::string method;
    json params;     // request/notification payload
    json result;     // success response
    json error;      // {code,message,data?} on error response
};

// Classify an incoming envelope per the csharp-sdk converter order:
//   1. error present & id present  → ResponseError
//   2. result present & id present → Response
//   3. method & id present         → Request
//   4. method present (no id)      → Notification
//   5. else                        → Invalid
[[nodiscard]] inline Message classify(const json& j) {
    Message m;
    if (!j.is_object() || j.value("jsonrpc", "") != "2.0") return m;

    const bool has_id     = j.contains("id") && !j["id"].is_null();
    const bool has_method = j.contains("method") && j["method"].is_string();
    const bool has_result = j.contains("result");
    const bool has_error  = j.contains("error") && !j["error"].is_null();

    if (has_id) m.id = RpcId::from_json(j["id"]);

    if (has_error && has_id) {
        m.kind  = MessageKind::ResponseError;
        m.error = j["error"];
        return m;
    }
    if (has_result && has_id) {
        m.kind   = MessageKind::Response;
        m.result = j["result"];
        return m;
    }
    if (has_method) {
        m.method = j["method"].get<std::string>();
        m.params = j.value("params", json::object());
        m.kind   = has_id ? MessageKind::Request : MessageKind::Notification;
    }
    return m;
}

// ── Envelope builders ───────────────────────────────────────────
[[nodiscard]] inline json make_response(const RpcId& id, json result) {
    return json{{"jsonrpc", "2.0"}, {"id", id.to_json()}, {"result", std::move(result)}};
}

[[nodiscard]] inline json make_error(const RpcId& id, int code, std::string_view message,
                                     std::optional<json> data = std::nullopt) {
    json err{{"code", code}, {"message", std::string{message}}};
    if (data) err["data"] = std::move(*data);
    return json{{"jsonrpc", "2.0"}, {"id", id.to_json()}, {"error", std::move(err)}};
}

[[nodiscard]] inline json make_notification(std::string_view method, json params = json::object()) {
    return json{{"jsonrpc", "2.0"}, {"method", std::string{method}}, {"params", std::move(params)}};
}

// ── MCP content primitives ──────────────────────────────────────
[[nodiscard]] inline json text_content(std::string text) {
    return json{{"type", "text"}, {"text", std::move(text)}};
}

[[nodiscard]] inline json resource_content(std::string uri, std::string mime, std::string text) {
    return json{
        {"type", "resource"},
        {"resource", json{
            {"uri", std::move(uri)},
            {"mimeType", std::move(mime)},
            {"text", std::move(text)},
        }},
    };
}

[[nodiscard]] inline json call_tool_result(json content, bool is_error = false,
                                           std::optional<json> structured = std::nullopt) {
    json out{{"content", std::move(content)}, {"isError", is_error}};
    if (structured) out["structuredContent"] = std::move(*structured);
    return out;
}

[[nodiscard]] inline json call_tool_text(std::string text, bool is_error = false) {
    return call_tool_result(json::array({text_content(std::move(text))}), is_error);
}

[[nodiscard]] inline json call_tool_structured(const json& value) {
    return call_tool_result(json::array({text_content(value.dump(2))}),
                             /*is_error=*/false, value);
}

} // namespace cc::mcp
