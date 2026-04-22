// ──────────────────────────────────────────────────────────────
// click-clack / mcp-bridge / main.cpp
// Stdio ↔ HTTP MCP bridge (celer style: fast, smart, safe).
//
// VS Code's built-in MCP client spawns this process and talks
// line-delimited JSON-RPC 2.0 over stdin/stdout. Each inbound line
// is POSTed to the hub's /mcp endpoint and the response is emitted
// back verbatim. Notifications (no `id`) get no reply.
//
// Transport: raw TCP HTTP/1.1 (no curl dependency).
// Logging: stderr only — stdout is reserved for the protocol.
// ──────────────────────────────────────────────────────────────

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>

namespace {

// F-14: RAII guard for POSIX fds so every early-return path in dial()
// and post_json() closes its socket exactly once.
class FdHandle {
public:
    FdHandle() = default;
    explicit FdHandle(int fd) noexcept : fd_{fd} {}
    ~FdHandle() { reset(); }

    FdHandle(const FdHandle&) = delete;
    FdHandle& operator=(const FdHandle&) = delete;

    FdHandle(FdHandle&& other) noexcept : fd_{other.fd_} { other.fd_ = -1; }
    FdHandle& operator=(FdHandle&& other) noexcept {
        if (this != &other) {
            reset();
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }

    [[nodiscard]] int get()  const noexcept { return fd_; }
    [[nodiscard]] bool valid() const noexcept { return fd_ >= 0; }
    int release() noexcept { int f = fd_; fd_ = -1; return f; }
    void reset(int fd = -1) noexcept {
        if (fd_ >= 0) ::close(fd_);
        fd_ = fd;
    }

private:
    int fd_{-1};
};

struct Endpoint {
    std::string host{"127.0.0.1"};
    std::uint16_t port{33514};
    std::string path{"/mcp"};
};

[[nodiscard]] Endpoint resolve_endpoint() {
    Endpoint ep;
    if (const char* p = std::getenv("CC_HUB_HOST")) ep.host = p;
    if (const char* p = std::getenv("CC_HUB_PORT")) ep.port = static_cast<std::uint16_t>(std::atoi(p));
    if (const char* p = std::getenv("CC_HUB_PATH")) ep.path = p;
    return ep;
}

[[nodiscard]] int dial(const Endpoint& ep) {
    FdHandle sock{::socket(AF_INET, SOCK_STREAM, 0)};
    if (!sock.valid()) return -1;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(ep.port);
    if (::inet_pton(AF_INET, ep.host.c_str(), &addr.sin_addr) != 1) {
        hostent* he = ::gethostbyname(ep.host.c_str());
        if (!he || !he->h_addr_list[0]) return -1;
        std::memcpy(&addr.sin_addr, he->h_addr_list[0], sizeof(in_addr));
    }
    if (::connect(sock.get(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        return -1;
    }
    return sock.release();
}

bool send_all(int fd, const char* buf, std::size_t n) {
    while (n) {
        auto w = ::send(fd, buf, n, 0);
        if (w <= 0) return false;
        buf += w; n -= static_cast<std::size_t>(w);
    }
    return true;
}

// Read until we've consumed Content-Length bytes of body (HTTP/1.0 Connection: close).
[[nodiscard]] std::string post_json(const Endpoint& ep, const std::string& body) {
    FdHandle sock{dial(ep)};
    if (!sock.valid()) return {};

    std::string req;
    req.reserve(body.size() + 256);
    req += "POST "; req += ep.path; req += " HTTP/1.1\r\n";
    req += "Host: "; req += ep.host; req += ":"; req += std::to_string(ep.port); req += "\r\n";
    req += "Content-Type: application/json\r\n";
    req += "Accept: application/json, text/event-stream\r\n";
    req += "MCP-Protocol-Version: 2025-06-18\r\n";
    req += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    req += "Connection: close\r\n\r\n";
    req += body;

    if (!send_all(sock.get(), req.data(), req.size())) return {};

    std::string raw;
    char buf[4096];
    for (;;) {
        auto r = ::recv(sock.get(), buf, sizeof(buf), 0);
        if (r <= 0) break;
        raw.append(buf, static_cast<std::size_t>(r));
    }
    // sock closes via FdHandle dtor

    const auto split = raw.find("\r\n\r\n");
    if (split == std::string::npos) return {};
    return raw.substr(split + 4);
}

} // namespace

int main() {
    const auto ep = resolve_endpoint();
    std::cerr << "[cc-mcp-bridge] → http://" << ep.host << ":" << ep.port << ep.path << "\n";

    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;

        const auto resp = post_json(ep, line);
        // If the inbound line had no `id`, it's a notification — no reply.
        // Cheap heuristic: scan for `"id"` followed by : (robust enough here).
        const bool is_notification = (line.find("\"id\"") == std::string::npos);
        if (is_notification) continue;

        if (resp.empty()) {
            std::cout << R"({"jsonrpc":"2.0","id":null,"error":{"code":-32000,"message":"hub unreachable"}})"
                      << "\n" << std::flush;
        } else {
            std::cout << resp << "\n" << std::flush;
        }
    }
    return 0;
}
