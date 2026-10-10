#pragma once

#include "tooling/registry/request_handler.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

namespace ahfl::registry {

struct ServerConfig {
    /// IPv4 address to bind. "0.0.0.0" exposes the server on all interfaces.
    std::string host{"127.0.0.1"};
    /// TCP port. 0 lets the OS pick a free port; port() reports the actual one.
    std::uint16_t port{8080};
};

/// Single-threaded HTTP/1.1 server on raw POSIX sockets (no external runtime
/// dependency). One request per connection (Connection: close): the registry
/// protocol is short-lived request/response and the client opens a fresh
/// connection per request. Endpoint logic lives behind RequestHandler, so the
/// transport can be replaced without touching route code.
class RegistryServer {
  public:
    RegistryServer(ServerConfig config, std::shared_ptr<RequestHandler> handler);
    ~RegistryServer();

    RegistryServer(const RegistryServer &) = delete;
    RegistryServer &operator=(const RegistryServer &) = delete;

    /// Creates the listening socket, binds and listens. Throws
    /// std::runtime_error on failure. A port of 0 selects an ephemeral port.
    void start();

    /// Accepts and serves connections until stop() is called (from any
    /// thread) or a fatal socket error occurs.
    void serve();

    /// Requests the accept loop to exit. Returns immediately; serve() observes
    /// the request within its poll interval.
    void stop() noexcept;

    /// The actual listening port (meaningful after start()).
    [[nodiscard]] std::uint16_t port() const noexcept;

  private:
    void handle_connection(int client_fd);

    ServerConfig config_;
    std::shared_ptr<RequestHandler> handler_;
    int listen_fd_{-1};
    std::uint16_t actual_port_{0};
    std::atomic<bool> stop_requested_{false};
};

} // namespace ahfl::registry
