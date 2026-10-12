#include "tooling/registry/server.hpp"

#include "tooling/registry/http.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

namespace ahfl::registry {

namespace {

constexpr std::size_t kMaxHeaderSize = 64U * 1024U;
constexpr std::size_t kMaxBodySize = 64U * 1024U * 1024U;
constexpr int kReadTimeoutMs = 30000;
constexpr int kPollIntervalMs = 200;
constexpr int kListenBacklog = 16;

[[nodiscard]] bool write_all(int fd, std::string_view data) {
    while (!data.empty()) {
        const auto written = ::send(fd, data.data(), data.size(), MSG_NOSIGNAL);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        data.remove_prefix(static_cast<std::size_t>(written));
    }
    return true;
}

void send_response(int fd, const HttpResponse &response) {
    (void)write_all(fd, serialize_http_response(response));
}

} // namespace

RegistryServer::RegistryServer(ServerConfig config, std::shared_ptr<RequestHandler> handler)
    : config_(std::move(config)), handler_(std::move(handler)) {}

RegistryServer::~RegistryServer() {
    if (listen_fd_ >= 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
    }
}

void RegistryServer::start() {
#if defined(__linux__)
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
#else
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ >= 0 && ::fcntl(listen_fd_, F_SETFD, FD_CLOEXEC) < 0) {
        const int saved = errno;
        ::close(listen_fd_);
        listen_fd_ = -1;
        errno = saved;
    }
#endif
    if (listen_fd_ < 0) {
        throw std::runtime_error(std::string("registry server socket: ") + std::strerror(errno));
    }

    int reuse = 1;
    if (::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        const int saved = errno;
        ::close(listen_fd_);
        listen_fd_ = -1;
        throw std::runtime_error(std::string("registry server setsockopt: ") +
                                 std::strerror(saved));
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(config_.port);
    if (::inet_pton(AF_INET, config_.host.c_str(), &address.sin_addr) != 1) {
        ::close(listen_fd_);
        listen_fd_ = -1;
        throw std::runtime_error("registry server host is not a valid IPv4 address: " +
                                 config_.host);
    }

    if (::bind(listen_fd_, reinterpret_cast<sockaddr *>(&address), sizeof(address)) < 0) {
        const int saved = errno;
        ::close(listen_fd_);
        listen_fd_ = -1;
        throw std::runtime_error(std::string("registry server bind: ") + std::strerror(saved));
    }
    if (::listen(listen_fd_, kListenBacklog) < 0) {
        const int saved = errno;
        ::close(listen_fd_);
        listen_fd_ = -1;
        throw std::runtime_error(std::string("registry server listen: ") + std::strerror(saved));
    }

    sockaddr_in actual{};
    socklen_t actual_len = sizeof(actual);
    if (::getsockname(listen_fd_, reinterpret_cast<sockaddr *>(&actual), &actual_len) < 0) {
        const int saved = errno;
        ::close(listen_fd_);
        listen_fd_ = -1;
        throw std::runtime_error(std::string("registry server getsockname: ") +
                                 std::strerror(saved));
    }
    actual_port_ = ntohs(actual.sin_port);
}

void RegistryServer::handle_connection(int client_fd) {
    std::string buffer;
    std::size_t header_end = std::string::npos;
    while (header_end == std::string::npos) {
        if (buffer.size() > kMaxHeaderSize) {
            send_response(client_fd,
                          HttpResponse::text(400, "text/plain", "request headers too large"));
            return;
        }
        pollfd pfd{client_fd, POLLIN, 0};
        const int ready = ::poll(&pfd, 1, kReadTimeoutMs);
        if (ready <= 0) {
            return; // timeout, error, or interruption — close silently
        }
        char chunk[4096];
        const auto received = ::recv(client_fd, chunk, sizeof(chunk), 0);
        if (received <= 0) {
            return;
        }
        buffer.append(chunk, static_cast<std::size_t>(received));
        const auto found = buffer.find("\r\n\r\n");
        if (found != std::string::npos) {
            header_end = found;
        }
    }

    const std::string raw_head = buffer.substr(0, header_end + 4);
    std::string body = buffer.substr(header_end + 4);

    auto head = parse_http_head(raw_head);
    if (!head.has_value()) {
        send_response(client_fd, HttpResponse::text(400, "text/plain", "malformed request"));
        return;
    }
    if (head->content_length > kMaxBodySize) {
        send_response(client_fd,
                      HttpResponse::text(413, "text/plain", "request body too large"));
        return;
    }

    while (body.size() < head->content_length) {
        pollfd pfd{client_fd, POLLIN, 0};
        const int ready = ::poll(&pfd, 1, kReadTimeoutMs);
        if (ready <= 0) {
            return;
        }
        char chunk[16384];
        const auto wanted = std::min<std::size_t>(
            sizeof(chunk), head->content_length - body.size());
        const auto received = ::recv(client_fd, chunk, wanted, 0);
        if (received <= 0) {
            return;
        }
        body.append(chunk, static_cast<std::size_t>(received));
    }

    auto request = build_http_request(std::move(*head), std::move(body));
    if (!request.has_value()) {
        send_response(client_fd,
                      HttpResponse::text(400, "text/plain", "content-length mismatch"));
        return;
    }

    const auto response = handler_->handle(*request);
    send_response(client_fd, response);
}

void RegistryServer::serve() {
    while (!stop_requested_.load(std::memory_order_relaxed)) {
        pollfd pfd{listen_fd_, POLLIN, 0};
        const int ready = ::poll(&pfd, 1, kPollIntervalMs);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (ready == 0) {
            continue;
        }
        if ((pfd.revents & POLLIN) == 0) {
            break; // listening socket is in an error state
        }

        sockaddr_in peer{};
        socklen_t peer_len = sizeof(peer);
#if defined(__linux__)
        const int client_fd =
            ::accept4(listen_fd_, reinterpret_cast<sockaddr *>(&peer), &peer_len, SOCK_CLOEXEC);
#else
        const int client_fd =
            ::accept(listen_fd_, reinterpret_cast<sockaddr *>(&peer), &peer_len);
#endif
        if (client_fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (stop_requested_.load(std::memory_order_relaxed)) {
                break;
            }
            continue; // transient accept failure — keep serving
        }
#if !defined(__linux__)
        if (::fcntl(client_fd, F_SETFD, FD_CLOEXEC) < 0) {
            ::close(client_fd);
            continue;
        }
#endif
        handle_connection(client_fd);
        ::close(client_fd);
    }
}

void RegistryServer::stop() noexcept {
    stop_requested_.store(true, std::memory_order_relaxed);
}

std::uint16_t RegistryServer::port() const noexcept {
    return actual_port_ != 0 ? actual_port_ : config_.port;
}

} // namespace ahfl::registry
