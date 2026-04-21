// ──────────────────────────────────────────────────────────────
// click-clack / ffi / capi.cpp — C ABI implementation
// Thin wrapper around Wal + Materializer + McpServer.
// ──────────────────────────────────────────────────────────────
#include "click_clack/ffi/capi.h"

#include "click_clack/core/types.hpp"
#include "click_clack/core/wal.hpp"
#include "click_clack/views/materializer.hpp"
#include "click_clack/mcp/mcp_server.hpp"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <cstring>
#include <exception>
#include <memory>
#include <string>
#include <string_view>

namespace {

using nlohmann::json;

// Per-thread last error. Lifetime is bound to the thread; callers
// should read it before making another cc_* call.
thread_local std::string tls_last_error;

void set_error(std::string_view msg) noexcept {
    try { tls_last_error.assign(msg); }
    catch (...) { tls_last_error.clear(); }
}

// strdup replacement that goes through plain malloc so callers
// can free with cc_string_free (which calls std::free).
[[nodiscard]] char* dup_cstr(std::string_view s) noexcept {
    auto* p = static_cast<char*>(std::malloc(s.size() + 1));
    if (!p) {
        set_error("out of memory");
        return nullptr;
    }
    if (!s.empty()) std::memcpy(p, s.data(), s.size());
    p[s.size()] = '\0';
    return p;
}

} // namespace

// The opaque handle owns Wal → Materializer → McpServer in a
// defined shutdown order.
struct cc_hub {
    cc::Wal                          wal{};
    cc::Materializer                 views{};
    std::unique_ptr<cc::McpServer>   mcp{};
};

extern "C" {

cc_hub_t* cc_hub_open(const char* config_json) {
    try {
        if (!config_json) {
            set_error("config_json is null");
            return nullptr;
        }

        auto cfg_j = json::parse(config_json, nullptr, false);
        if (cfg_j.is_discarded() || !cfg_j.is_object()) {
            set_error("config_json is not a JSON object");
            return nullptr;
        }

        cc::HubConfig config{};
        config.wal_path   = cfg_j.value("wal_path",   std::string{"./cc-data/wal"});
        config.views_path = cfg_j.value("views_path", std::string{"./cc-data/views"});

        auto hub = std::make_unique<cc_hub_t>();

        if (auto r = hub->wal.open(config); !r) {
            set_error(std::string{"wal.open: "} + r.error().message);
            return nullptr;
        }
        if (auto r = hub->views.open(config); !r) {
            hub->wal.close();
            set_error(std::string{"views.open: "} + r.error().message);
            return nullptr;
        }

        hub->wal.on_clack([h = hub.get()](const cc::Clack& c) {
            h->views.on_clack(c);
        });

        hub->mcp = std::make_unique<cc::McpServer>(hub->wal, hub->views);
        hub->mcp->register_tools();

        set_error("");
        return hub.release();
    } catch (const std::exception& e) {
        set_error(e.what());
        return nullptr;
    } catch (...) {
        set_error("unknown exception");
        return nullptr;
    }
}

void cc_hub_close(cc_hub_t* hub) {
    if (!hub) return;
    try {
        hub->mcp.reset();
        hub->wal.close();
    } catch (...) { /* swallow: close must not throw across FFI */ }
    delete hub;
}

char* cc_hub_dispatch(cc_hub_t*   hub,
                      const char* tool_name,
                      const char* args_json,
                      const char* agent_id)
{
    try {
        if (!hub) { set_error("hub is null"); return nullptr; }
        if (!tool_name) { set_error("tool_name is null"); return nullptr; }

        json args = json::object();
        if (args_json && *args_json) {
            auto parsed = json::parse(args_json, nullptr, false);
            if (!parsed.is_discarded()) args = std::move(parsed);
        }

        auto result = hub->mcp->dispatch(tool_name, args, agent_id ? agent_id : "");
        set_error("");
        return dup_cstr(result.dump());
    } catch (const std::exception& e) {
        set_error(e.what());
        json err{{"error", "ffi_exception"}, {"message", e.what()}};
        return dup_cstr(err.dump());
    } catch (...) {
        set_error("unknown exception");
        json err{{"error", "ffi_exception"}};
        return dup_cstr(err.dump());
    }
}

char* cc_hub_list_tools(cc_hub_t* hub) {
    try {
        if (!hub) { set_error("hub is null"); return nullptr; }
        set_error("");
        return dup_cstr(hub->mcp->list_tools().dump());
    } catch (const std::exception& e) {
        set_error(e.what());
        return nullptr;
    } catch (...) {
        set_error("unknown exception");
        return nullptr;
    }
}

void cc_string_free(char* s) {
    if (s) std::free(s);
}

const char* cc_last_error(void) {
    return tls_last_error.c_str();
}

const char* cc_version(void) {
    return "0.1.0";
}

} // extern "C"
