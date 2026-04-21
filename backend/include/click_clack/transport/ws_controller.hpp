#pragma once
// ──────────────────────────────────────────────────────────────
// click-clack / transport / ws_controller.hpp
// WebSocket hub — real-time clack fan-out + operator interaction
// Drogon WebSocket controller — live view subscriptions.
// ──────────────────────────────────────────────────────────────

#include "../core/types.hpp"
#include "../core/wal.hpp"
#include "../views/materializer.hpp"

#include <drogon/WebSocketController.h>
#include <nlohmann/json.hpp>

#include <atomic>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>

namespace cc {

using json = nlohmann::json;

class HubWebSocketController
    : public drogon::WebSocketController<HubWebSocketController>
{
public:
    HubWebSocketController() = default;

    // Injected post-construction (Drogon creates controllers)
    static void inject(Wal* wal, Materializer* views) {
        s_wal_   = wal;
        s_views_ = views;
    }

    void handleNewMessage(const drogon::WebSocketConnectionPtr& ws_conn,
                          std::string&& message,
                          const drogon::WebSocketMessageType& type) override
    {
        if (type != drogon::WebSocketMessageType::Text) return;

        auto body = json::parse(message, nullptr, false);
        if (body.is_discarded()) {
            ws_conn->send(json{{"type", "error"}, {"message", "invalid_json"}}.dump());
            return;
        }

        auto action = body.value("action", "");

        if (action == "post") {
            handle_post(ws_conn, body);
        } else if (action == "subscribe") {
            handle_subscribe(ws_conn, body);
        } else if (action == "unsubscribe") {
            handle_unsubscribe(ws_conn, body);
        } else {
            ws_conn->send(json{{"type", "error"}, {"message", "unknown_action"}}.dump());
        }
    }

    void handleNewConnection(const drogon::HttpRequestPtr&,
                             const drogon::WebSocketConnectionPtr& ws_conn) override
    {
        std::lock_guard lock(mu_);
        clients_.insert(ws_conn);
    }

    void handleConnectionClosed(const drogon::WebSocketConnectionPtr& ws_conn) override
    {
        std::lock_guard lock(mu_);
        clients_.erase(ws_conn);
        subscriptions_.erase(ws_conn);
    }

    // ── Broadcast to all connected clients ──────────────────

    static void broadcast(const Clack& clack) {
        auto frame = json{{"type", "clack"}, {"data", clack_to_json(clack)}}.dump();
        std::lock_guard lock(mu_);
        for (const auto& conn : clients_) {
            if (conn->connected()) {
                conn->send(frame);
            }
        }
    }

    WS_PATH_LIST_BEGIN
    WS_PATH_ADD("/ws/v1/hub");
    WS_PATH_LIST_END

private:
    void handle_post(const drogon::WebSocketConnectionPtr& ws_conn, const json& body) {
        if (!s_wal_) {
            ws_conn->send(json{{"type", "error"}, {"message", "wal_not_ready"}}.dump());
            return;
        }

        auto click_json = body.value("click", json{});
        Click click{};
        click.verb     = click_json.value("verb", "");
        click.agent_id = click_json.value("agent_id", "operator");
        click.task_id  = click_json.value("task_id", "");
        click.subject  = click_json.value("subject", "");
        click.flags    = static_cast<std::uint8_t>(click_json.value("flags", 0));
        click.parent   = click_json.value("parent", std::uint64_t{0});
        click.payload  = click_json.contains("payload") ? click_json["payload"].dump() : "{}";

        auto result = s_wal_->append(click);
        if (!result) {
            ws_conn->send(json{{"type", "error"}, {"message", result.error().message}}.dump());
            return;
        }

        ws_conn->send(json{{"type", "ack"}, {"epoch", result->header.epoch}}.dump());
        // broadcast happens via WAL listener → HubWebSocketController::broadcast
    }

    void handle_subscribe(const drogon::WebSocketConnectionPtr& ws_conn, const json& body) {
        auto view = body.value("view", "");
        {
            std::lock_guard lock(mu_);
            subscriptions_[ws_conn].insert(view);
        }
        send_snapshot(ws_conn, view);
    }

    void handle_unsubscribe(const drogon::WebSocketConnectionPtr& ws_conn, const json& body) {
        auto view = body.value("view", "");
        std::lock_guard lock(mu_);
        if (auto it = subscriptions_.find(ws_conn); it != subscriptions_.end()) {
            it->second.erase(view);
        }
    }

    void send_snapshot(const drogon::WebSocketConnectionPtr& ws_conn, const std::string& view) {
        if (!s_views_) return;

        json data;
        if (view == "timeline") {
            auto res = s_wal_->read_range(0, 1000);
            if (res) {
                data = json::array();
                for (const auto& c : *res) data.push_back(clack_to_json(c));
            }
        } else if (view == "tasks") {
            auto res = s_views_->query_tasks(std::nullopt, 1000);
            if (res) {
                data = json::array();
                for (const auto& t : *res) data.push_back(task_state_to_json(t));
            }
        } else if (view == "agents") {
            auto res = s_views_->query_presence();
            if (res) {
                data = json::array();
                for (const auto& a : *res) data.push_back(presence_to_json(a));
            }
        } else if (view == "hitl") {
            auto res = s_views_->query_hitl_queue();
            if (res) {
                data = json::array();
                for (const auto& h : *res) {
                    data.push_back(json{
                        {"epoch", h.epoch},
                        {"clack", clack_to_json(h.clack)},
                        {"resolved", h.resolved},
                    });
                }
            }
        }

        ws_conn->send(json{{"type", "snapshot"}, {"view", view}, {"data", data}}.dump());
    }

    // Static state (Drogon manages controller lifetime)
    static inline Wal*          s_wal_{nullptr};
    static inline Materializer* s_views_{nullptr};
    static inline std::mutex    mu_;
    static inline std::set<drogon::WebSocketConnectionPtr> clients_;
    static inline std::unordered_map<drogon::WebSocketConnectionPtr,
                                      std::set<std::string>> subscriptions_;
};

} // namespace cc
