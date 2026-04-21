#pragma once
// ──────────────────────────────────────────────────────────────
// click-clack / transport / http_controller.hpp
// REST endpoints — health, metrics, MCP tool dispatch
// Drogon HTTP controller — REST surface for the hub.
// ──────────────────────────────────────────────────────────────

#include "../core/types.hpp"
#include "../core/wal.hpp"
#include "../mcp/mcp_server.hpp"

#include <drogon/HttpController.h>
#include <nlohmann/json.hpp>

namespace cc {

using json = nlohmann::json;

namespace detail {
inline drogon::HttpResponsePtr json_response(const json& j) {
    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setContentTypeCode(drogon::CT_APPLICATION_JSON);
    resp->setBody(j.dump());
    return resp;
}
} // namespace detail

class HubHttpController : public drogon::HttpController<HubHttpController> {
public:
    HubHttpController() = default;

    static void inject(Wal* wal, McpServer* mcp) {
        s_wal_ = wal;
        s_mcp_ = mcp;
    }

    METHOD_LIST_BEGIN
    ADD_METHOD_TO(HubHttpController::health,    "/api/v1/health",  drogon::Get);
    ADD_METHOD_TO(HubHttpController::metrics,   "/api/v1/metrics", drogon::Get);
    ADD_METHOD_TO(HubHttpController::mcp_tools, "/api/v1/mcp/tools", drogon::Get);
    ADD_METHOD_TO(HubHttpController::mcp_call,  "/api/v1/mcp/call",  drogon::Post);
    METHOD_LIST_END

    void health(const drogon::HttpRequestPtr&,
                std::function<void(const drogon::HttpResponsePtr&)>&& callback)
    {
        json body{
            {"status",  "ok"},
            {"version", "0.1.0"},
            {"wal_open", s_wal_ ? s_wal_->is_open() : false},
            {"epoch",    s_wal_ ? s_wal_->current_epoch() : 0},
        };
        auto resp = detail::json_response(body);
        callback(resp);
    }

    void metrics(const drogon::HttpRequestPtr&,
                 std::function<void(const drogon::HttpResponsePtr&)>&& callback)
    {
        json body{
            {"current_epoch", s_wal_ ? s_wal_->current_epoch() : 0},
        };
        auto resp = detail::json_response(body);
        callback(resp);
    }

    void mcp_tools(const drogon::HttpRequestPtr&,
                   std::function<void(const drogon::HttpResponsePtr&)>&& callback)
    {
        if (!s_mcp_) {
            auto resp = detail::json_response(
                json{{"error", "mcp_not_ready"}});
            resp->setStatusCode(drogon::k503ServiceUnavailable);
            callback(resp);
            return;
        }
        auto resp = detail::json_response(s_mcp_->list_tools());
        callback(resp);
    }

    void mcp_call(const drogon::HttpRequestPtr& req,
                  std::function<void(const drogon::HttpResponsePtr&)>&& callback)
    {
        if (!s_mcp_) {
            auto resp = detail::json_response(
                json{{"error", "mcp_not_ready"}});
            resp->setStatusCode(drogon::k503ServiceUnavailable);
            callback(resp);
            return;
        }

        auto body = json::parse(req->body(), nullptr, false);
        if (body.is_discarded()) {
            auto resp = detail::json_response(
                json{{"error", "invalid_json"}});
            resp->setStatusCode(drogon::k400BadRequest);
            callback(resp);
            return;
        }

        auto tool     = body.value("tool", "");
        auto args     = body.value("args", json{});
        auto agent_id = body.value("agent_id", "anonymous");

        auto result = s_mcp_->dispatch(tool, args, agent_id);
        auto resp = detail::json_response(result);
        callback(resp);
    }

private:
    static inline Wal*       s_wal_{nullptr};
    static inline McpServer* s_mcp_{nullptr};
};

} // namespace cc
