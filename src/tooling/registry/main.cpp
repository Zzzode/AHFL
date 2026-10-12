#include "tooling/registry/registry_handler.hpp"
#include "tooling/registry/server.hpp"
#include "tooling/registry/store.hpp"

#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

namespace {

volatile std::sig_atomic_t g_shutdown_requested = 0;

void handle_signal(int /*signal*/) {
    g_shutdown_requested = 1;
}

struct Options {
    ahfl::registry::ServerConfig server;
    std::filesystem::path storage{"./registry-storage"};
};

void print_usage(const char *program) {
    std::cerr << "Usage: " << program << " [--port <port>] [--host <ipv4>] [--storage <path>]\n"
              << "\n"
              << "  --port <port>      TCP port to listen on (default: 8080, 0 = ephemeral)\n"
              << "  --host <ipv4>      IPv4 address to bind (default: 127.0.0.1)\n"
              << "  --storage <path>   Storage root directory (default: ./registry-storage)\n"
              << "  --help             Show this help text\n";
}

[[nodiscard]] bool parse_u16(std::string_view text, std::uint16_t &result) {
    if (text.empty()) {
        return false;
    }
    unsigned value = 0;
    for (const char digit : text) {
        if (digit < '0' || digit > '9') {
            return false;
        }
        value = value * 10 + static_cast<unsigned>(digit - '0');
        if (value > 65535U) {
            return false;
        }
    }
    result = static_cast<std::uint16_t>(value);
    return true;
}

[[nodiscard]] bool parse_args(int argc, char **argv, Options &options) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help") {
            print_usage(argv[0]);
            std::exit(0);
        }
        if (arg == "--port" || arg == "--host" || arg == "--storage") {
            if (i + 1 >= argc) {
                std::cerr << "missing value for " << arg << "\n";
                return false;
            }
            const std::string value = argv[++i];
            if (arg == "--port") {
                if (!parse_u16(value, options.server.port)) {
                    std::cerr << "invalid port: " << value << "\n";
                    return false;
                }
            } else if (arg == "--host") {
                options.server.host = value;
            } else {
                options.storage = value;
            }
            continue;
        }
        std::cerr << "unknown argument: " << arg << "\n";
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char **argv) {
    Options options;
    if (!parse_args(argc, argv, options)) {
        print_usage(argv[0]);
        return 2;
    }

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    try {
        ahfl::registry::RegistryStore store(options.storage);
        auto handler = std::make_shared<ahfl::registry::RegistryRequestHandler>(std::move(store));
        ahfl::registry::RegistryServer server(options.server, std::move(handler));
        server.start();

        std::cerr << "ahfl-registry listening on http://" << options.server.host << ":"
                  << server.port() << " (storage: " << options.storage << ")\n";

        std::thread runner([&server] { server.serve(); });
        while (g_shutdown_requested == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        server.stop();
        runner.join();
        std::cerr << "ahfl-registry stopped\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "ahfl-registry: " << error.what() << "\n";
        return 1;
    }
}
