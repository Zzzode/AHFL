#include "tooling/dap/dap_server.hpp"

#include <cctype>
#include <cstddef>
#include <iostream>
#include <optional>
#include <string>

namespace {

std::string trim_ascii(std::string value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
        value.pop_back();
    }
    std::size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])) != 0) {
        ++start;
    }
    return value.substr(start);
}

std::optional<std::string> read_content_length_frame(std::istream &in) {
    std::string line;
    std::optional<std::size_t> content_length;
    bool saw_header = false;

    while (std::getline(in, line)) {
        saw_header = true;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            break;
        }

        constexpr std::string_view prefix = "Content-Length:";
        if (line.size() >= prefix.size() && line.substr(0, prefix.size()) == prefix) {
            auto value = trim_ascii(line.substr(prefix.size()));
            try {
                content_length = static_cast<std::size_t>(std::stoull(value));
            } catch (...) {
                return std::nullopt;
            }
        }
    }

    if (!saw_header) {
        return std::nullopt;
    }
    if (!content_length.has_value()) {
        return std::nullopt;
    }

    std::string body(*content_length, '\0');
    in.read(body.data(), static_cast<std::streamsize>(body.size()));
    if (in.gcount() != static_cast<std::streamsize>(body.size())) {
        return std::nullopt;
    }

    return "Content-Length: " + std::to_string(*content_length) + "\r\n\r\n" + body;
}

void print_usage(std::ostream &out) {
    out << "Usage: ahfl-dap [--help]\n\n"
        << "Starts the AHFL Debug Adapter Protocol server on stdin/stdout.\n";
}

} // namespace

int main(int argc, char **argv) {
    if (argc > 2) {
        print_usage(std::cerr);
        return 2;
    }
    if (argc == 2) {
        const std::string arg = argv[1];
        if (arg == "--help" || arg == "-h") {
            print_usage(std::cout);
            return 0;
        }
        std::cerr << "unknown option: " << arg << '\n';
        print_usage(std::cerr);
        return 2;
    }

    ahfl::dap::DapServer server;
    while (auto frame = read_content_length_frame(std::cin)) {
        auto request = server.decode_message(*frame);
        if (request.command.empty()) {
            continue;
        }

        auto response = server.handle_request(request);
        std::cout << server.encode_message(response) << std::flush;
        if (request.command == "disconnect") {
            break;
        }
    }

    return 0;
}
