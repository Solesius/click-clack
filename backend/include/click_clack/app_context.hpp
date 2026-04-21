#pragma once
// ──────────────────────────────────────────────────────────────
// click-clack / app_context.hpp
// DI container — owns all services, wires dependencies
// Application wiring: composes WAL, materializer, and transports.
// ──────────────────────────────────────────────────────────────

#include "core/types.hpp"
#include "core/wal.hpp"
#include "views/materializer.hpp"
#include "mcp/mcp_server.hpp"
#include "mcp/dispatcher.hpp"
#include "mcp/http_transport.hpp"
#include "transport/ws_controller.hpp"
#include "transport/http_controller.hpp"

#include <memory>

namespace cc {

struct AppContext {
    HubConfig     config;
    Wal           wal;
    Materializer  materializer;
    std::unique_ptr<McpServer>      mcp;
    std::unique_ptr<mcp::Dispatcher> mcp_dispatcher;

    AppContext(const AppContext&) = delete;
    AppContext& operator=(const AppContext&) = delete;
    AppContext(AppContext&&) = delete;
    AppContext& operator=(AppContext&&) = delete;

    [[nodiscard]] static auto create(HubConfig cfg) -> celer::Result<std::unique_ptr<AppContext>> {
        auto ctx = std::unique_ptr<AppContext>(new AppContext{std::move(cfg)});

        // 1. Open WAL
        auto wal_res = ctx->wal.open(ctx->config);
        if (!wal_res) return celer::Result<std::unique_ptr<AppContext>>{
            std::unexpected(wal_res.error())};

        // 2. Recover epoch counter
        auto recovered = ctx->wal.recover();
        if (!recovered) return celer::Result<std::unique_ptr<AppContext>>{
            std::unexpected(recovered.error())};

        // 3. Open materializer views
        auto mat_res = ctx->materializer.open(ctx->config);
        if (!mat_res) return celer::Result<std::unique_ptr<AppContext>>{
            std::unexpected(mat_res.error())};

        // 4. Wire WAL → Materializer → WebSocket broadcast
        ctx->wal.on_clack([&mat = ctx->materializer](const Clack& clack) {
            mat.on_clack(clack);
            HubWebSocketController::broadcast(clack);
        });

        // 5. Create MCP server
        ctx->mcp = std::make_unique<McpServer>(ctx->wal, ctx->materializer);
        ctx->mcp->register_tools();

        // 5b. Native MCP JSON-RPC dispatcher (MCP 2025-06-18)
        ctx->mcp_dispatcher = std::make_unique<mcp::Dispatcher>(
            *ctx->mcp, ctx->wal, ctx->materializer);

        // 6. Inject into Drogon controllers
        HubWebSocketController::inject(&ctx->wal, &ctx->materializer);
        HubHttpController::inject(&ctx->wal, ctx->mcp.get());
        mcp::McpHttpController::inject(ctx->mcp_dispatcher.get());

        return ctx;
    }

private:
    explicit AppContext(HubConfig cfg) : config(std::move(cfg)) {}
};

} // namespace cc
