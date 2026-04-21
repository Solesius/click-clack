#pragma once
// ──────────────────────────────────────────────────────────────
// click-clack / mcp / http_transport.hpp
// MCP Streamable HTTP transport (stateless POST mode).
//
// Single endpoint POST /mcp — accepts a JSON-RPC Request or Notification,
// returns a single application/json JSON-RPC Response (202 for notifications).
// Full SSE streaming mode can be added later; stateless POST is sufficient
// for VS Code copilot when paired with stdio bridging.
// ──────────────────────────────────────────────────────────────

#include "protocol.hpp"
#include "dispatcher.hpp"

#include <drogon/HttpController.h>

namespace cc::mcp {

class McpHttpController : public drogon::HttpController<McpHttpController> {
public:
    McpHttpController() = default;

    static void inject(Dispatcher* d) noexcept { s_dispatcher_ = d; }

    METHOD_LIST_BEGIN
    ADD_METHOD_TO(McpHttpController::handle_post, "/mcp", drogon::Post);
    ADD_METHOD_TO(McpHttpController::handle_get,  "/mcp", drogon::Get);
    METHOD_LIST_END

    void handle_post(const drogon::HttpRequestPtr& req,
                     std::function<void(const drogon::HttpResponsePtr&)>&& cb)
    {
        if (!s_dispatcher_) return send_error(cb, 503, "mcp not ready");

        auto body = json::parse(req->body(), nullptr, false);
        if (body.is_discarded()) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k400BadRequest);
            resp->setContentTypeCode(drogon::CT_APPLICATION_JSON);
            resp->setBody(make_error(RpcId{}, ec::ParseError, "invalid json").dump());
            cb(resp);
            return;
        }

        // Stateless: one logical session per request.
        Session sess;
        sess.initialized.store(true, std::memory_order_relaxed);

        const auto msg = classify(body);
        const auto out = s_dispatcher_->dispatch(msg, sess);

        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setContentTypeCode(drogon::CT_APPLICATION_JSON);
        resp->addHeader("Mcp-Session-Id", "stateless");

        if (!out) {
            // Notification — per spec: 202 Accepted, empty body.
            resp->setStatusCode(drogon::k202Accepted);
            resp->setBody("");
        } else {
            resp->setStatusCode(drogon::k200OK);
            resp->setBody(out->dump());
        }
        cb(resp);
    }

    void handle_get(const drogon::HttpRequestPtr&,
                    std::function<void(const drogon::HttpResponsePtr&)>&& cb)
    {
        // Stateless mode: we do not offer a persistent SSE channel.
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k405MethodNotAllowed);
        resp->setContentTypeCode(drogon::CT_APPLICATION_JSON);
        resp->setBody(R"({"error":"server operates in stateless POST mode"})");
        cb(resp);
    }

private:
    static void send_error(const std::function<void(const drogon::HttpResponsePtr&)>& cb,
                           int code, std::string_view msg)
    {
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(static_cast<drogon::HttpStatusCode>(code));
        resp->setContentTypeCode(drogon::CT_APPLICATION_JSON);
        resp->setBody(json{{"error", std::string{msg}}}.dump());
        cb(resp);
    }

    static inline Dispatcher* s_dispatcher_{nullptr};
};

} // namespace cc::mcp
