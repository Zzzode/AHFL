#include "ahfl/runtime/http_transport.hpp"

#include <array>
#include <cstdio>
#include <sstream>

namespace ahfl::runtime {

std::string HttpTransport::shell_quote(std::string_view value) {
    std::string quoted = "'";
    for (const auto character : value) {
        if (character == '\'') {
            quoted += "'\\''";
        } else {
            quoted.push_back(character);
        }
    }
    quoted += "'";
    return quoted;
}

std::string HttpTransport::build_curl_command(const HttpRequest &request) {
    std::ostringstream cmd;
    cmd << "curl -s -w '\\n%{http_code}' -X " << request.method << " "
        << shell_quote(request.url) << " ";

    for (const auto &[key, value] : request.headers) {
        cmd << "-H " << shell_quote(key + ": " + value) << " ";
    }

    if (!request.body.empty()) {
        cmd << "-d " << shell_quote(request.body) << " ";
    }

    cmd << "--max-time " << request.timeout_seconds;
    return cmd.str();
}

HttpResponse HttpTransport::execute(const HttpRequest &request) {
    const auto command = build_curl_command(request) + " 2>&1";

    FILE *pipe = popen(command.c_str(), "r");
    if (pipe == nullptr) {
        return HttpResponse{
            .status_code = 0,
            .body = {},
            .error = "failed to start curl process",
        };
    }

    std::string output;
    std::array<char, 4096> buffer{};
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        output += buffer.data();
    }

    const int exit_code = pclose(pipe);

    // curl exit code 28 = timeout
    if (exit_code != 0 && output.empty()) {
        if (exit_code == 28 || (WIFEXITED(exit_code) && WEXITSTATUS(exit_code) == 28)) {
            return HttpResponse{
                .status_code = 0,
                .body = {},
                .error = "timeout",
            };
        }
        return HttpResponse{
            .status_code = 0,
            .body = {},
            .error = "curl command failed with exit code " + std::to_string(exit_code),
        };
    }

    // Extract HTTP status code from last line (written by curl -w)
    int status_code = 0;
    std::string body = output;

    const auto last_newline = output.rfind('\n');
    if (last_newline != std::string::npos && last_newline > 0) {
        const auto status_str = output.substr(last_newline + 1);
        body = output.substr(0, last_newline);

        // Remove trailing newline from body if present
        if (!body.empty() && body.back() == '\n') {
            body.pop_back();
        }

        try {
            status_code = std::stoi(status_str);
        } catch (...) {
            // Could not parse status code
            status_code = 0;
            body = output;
        }
    } else if (!output.empty()) {
        // Entire output might be just the status code (empty body)
        try {
            status_code = std::stoi(output);
            body.clear();
        } catch (...) {
            status_code = 0;
            body = output;
        }
    }

    if (status_code == 0 && exit_code != 0) {
        // Check for timeout via exit code
        const int real_exit = WIFEXITED(exit_code) ? WEXITSTATUS(exit_code) : exit_code;
        if (real_exit == 28) {
            return HttpResponse{
                .status_code = 0,
                .body = body,
                .error = "timeout",
            };
        }
        return HttpResponse{
            .status_code = 0,
            .body = body,
            .error = "curl failed with exit code " + std::to_string(real_exit),
        };
    }

    return HttpResponse{
        .status_code = status_code,
        .body = body,
        .error = {},
    };
}

} // namespace ahfl::runtime
