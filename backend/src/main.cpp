// ──────────────────────────────────────────────────────────────
// click-clack / main.cpp
// Hub entry point — bootstraps the app context and Drogon loop.
// ──────────────────────────────────────────────────────────────

#include <click_clack/app_context.hpp>
#include <click_clack/core/path_sandbox.hpp>

#include <drogon/drogon.h>
#include <iostream>
#include <csignal>
#include <cstdlib>
#include <filesystem>

static std::unique_ptr<cc::AppContext> g_app;

static void handle_signal(int sig) {
    std::cout << "\n[click-clack] shutting down (signal " << sig << ")...\n";
    drogon::app().quit();
}

// Resolve the default persistent data root:
//   $CC_DATA_DIR                       (explicit override)
//   $XDG_DATA_HOME/click-clack         (Linux XDG)
//   $HOME/.local/share/click-clack     (fallback)
[[nodiscard]] static auto default_data_root() -> std::filesystem::path {
    namespace fs = std::filesystem;
    if (const auto* p = std::getenv("CC_DATA_DIR"); p && *p) return fs::path{p};
    if (const auto* p = std::getenv("XDG_DATA_HOME"); p && *p) return fs::path{p} / "click-clack";
    if (const auto* p = std::getenv("HOME"); p && *p) return fs::path{p} / ".local/share/click-clack";
    return fs::path{"./data"};  // last-resort fallback
}

auto main([[maybe_unused]] int argc, [[maybe_unused]] char** argv) -> int {
    // ── Parse config from CLI / env ─────────────────────────
    cc::HubConfig config{};

    // Resolve persistent, absolute paths by default so the WAL survives
    // across sessions regardless of the process's working directory.
    const auto root = default_data_root();
    config.wal_path   = (root / "wal").string();
    config.views_path = (root / "views").string();

    // F-02: confine any CC_WAL_PATH/CC_VIEWS_PATH override to the data root.
    auto apply_sandboxed = [&](const char* env_name, std::string& target) {
        const auto* p = std::getenv(env_name);
        if (!p || !*p) return;
        auto r = cc::resolve_under_root(root, p);
        if (!r) {
            std::cerr << "[click-clack] rejecting " << env_name
                      << " (" << r.error().message << "), keeping default\n";
            return;
        }
        target = *r;
    };
    apply_sandboxed("CC_WAL_PATH",   config.wal_path);
    apply_sandboxed("CC_VIEWS_PATH", config.views_path);
    if (const auto* p = std::getenv("CC_HTTP_PORT"))  config.http_port  = std::stoi(p);

    // Ensure data dirs exist
    std::filesystem::create_directories(config.wal_path);
    std::filesystem::create_directories(config.views_path);

    // ── Bootstrap (walks the hub_startup DAG) ───────────────
    auto ctx = cc::AppContext::create(std::move(config));
    if (!ctx) {
        std::cerr << "[click-clack] FATAL: " << ctx.error().message << "\n";
        return 1;
    }
    g_app = std::move(*ctx);

    std::cout << "[click-clack] Hub started — epoch recovered to "
              << g_app->wal.current_epoch() << "\n";
    std::cout << "[click-clack] Data root: " << root << "\n";
    std::cout << "[click-clack]   wal:   " << g_app->config.wal_path << "\n";
    std::cout << "[click-clack]   views: " << g_app->config.views_path << "\n";
    std::cout << "[click-clack] Listening on :" << g_app->config.http_port << "\n";
    std::cout << "[click-clack] WebSocket at /ws/v1/hub\n";
    std::cout << "[click-clack] MCP tools at /api/v1/mcp/call\n";
    std::cout << "[click-clack] Native MCP endpoint at POST /mcp (JSON-RPC 2.0, 2025-06-18)\n";

    // ── Signal handling ─────────────────────────────────────
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    // ── Drogon event loop ───────────────────────────────────
    drogon::app()
        .setLogLevel(trantor::Logger::kInfo)
        .addListener("0.0.0.0", static_cast<uint16_t>(g_app->config.http_port))
        .setThreadNum(4)
        // F-09: bound per-request body size. 1 MiB is generous for JSON-RPC
        // MCP tool calls and rejects accidental large-body DoS.
        .setClientMaxBodySize(1024 * 1024)
        .setClientMaxMemoryBodySize(256 * 1024)
        .run();

    // ── Cleanup (RAII handles the rest) ─────────────────────
    g_app.reset();
    std::cout << "[click-clack] Clean shutdown.\n";
    return 0;
}
