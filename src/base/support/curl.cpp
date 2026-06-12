#include "base/support/curl.hpp"

#include "base/support/process.hpp"

#include <chrono>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>

namespace ahfl::support {

namespace {

class TempFile {
  public:
    explicit TempFile(std::string_view prefix) : prefix_(prefix) {}

    [[nodiscard]] bool create() {
        path_ = make_path();
        const int fd = open(path_.c_str(), O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC,
                            S_IRUSR | S_IWUSR);
        if (fd < 0) {
            error_ = std::strerror(errno);
            return false;
        }
        close(fd);
        return true;
    }

    [[nodiscard]] bool write(std::string_view content) {
        path_ = make_path();
        const int fd = open(path_.c_str(), O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC,
                            S_IRUSR | S_IWUSR);
        if (fd < 0) {
            error_ = std::strerror(errno);
            return false;
        }
        while (!content.empty()) {
            const auto written = ::write(fd, content.data(), content.size());
            if (written < 0) {
                if (errno == EINTR) {
                    continue;
                }
                error_ = std::strerror(errno);
                close(fd);
                return false;
            }
            content.remove_prefix(static_cast<std::size_t>(written));
        }
        close(fd);
        return true;
    }

    [[nodiscard]] const std::filesystem::path &path() const { return path_; }
    [[nodiscard]] const std::string &error() const { return error_; }

    ~TempFile() {
        if (!path_.empty()) {
            std::error_code ec;
            std::filesystem::remove(path_, ec);
        }
    }

    TempFile(const TempFile &) = delete;
    TempFile &operator=(const TempFile &) = delete;
    TempFile(TempFile &&) = default;
    TempFile &operator=(TempFile &&) = default;

  private:
    [[nodiscard]] std::filesystem::path make_path() const {
        const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        std::ostringstream name;
        name << prefix_ << getpid() << "-" << now << ".tmp";
        return std::filesystem::temp_directory_path() / name.str();
    }

    std::string prefix_;
    std::filesystem::path path_;
    std::string error_;
};

using TempBodyFile = TempFile;
using TempHeaderFile = TempFile;

[[nodiscard]] std::string quote_config_value(std::string_view value) {
    std::string quoted = "\"";
    for (const char character : value) {
        switch (character) {
        case '\\': quoted += "\\\\"; break;
        case '"': quoted += "\\\""; break;
        case '\n': quoted += "\\n"; break;
        case '\r': quoted += "\\r"; break;
        case '\t': quoted += "\\t"; break;
        default: quoted.push_back(character); break;
        }
    }
    quoted += '"';
    return quoted;
}

[[nodiscard]] std::string build_curl_config(const CurlRequest &request,
                                            const std::optional<std::filesystem::path> &body_path,
                                            const std::optional<std::filesystem::path> &header_path) {
    std::ostringstream config;
    config << "silent\n";
    config << "show-error\n";
    config << "write-out = " << quote_config_value("\n%{http_code}") << "\n";
    config << "request = " << quote_config_value(request.method) << "\n";
    config << "url = " << quote_config_value(request.url) << "\n";
    config << "max-time = " << quote_config_value(std::to_string(request.timeout_seconds)) << "\n";

    switch (request.http_version) {
    case CurlHttpVersion::Default: break;
    case CurlHttpVersion::Http2: config << "http2\n"; break;
    case CurlHttpVersion::Http2PriorKnowledge: config << "http2-prior-knowledge\n"; break;
    }

    for (const auto &[key, value] : request.headers) {
        config << "header = " << quote_config_value(key + ": " + value) << "\n";
    }
    if (body_path.has_value()) {
        config << "data-binary = " << quote_config_value("@" + body_path->string()) << "\n";
    }
    if (header_path.has_value()) {
        config << "dump-header = " << quote_config_value(header_path->string()) << "\n";
    }
    return config.str();
}

[[nodiscard]] std::vector<std::pair<std::string, std::string>>
parse_header_file(const std::filesystem::path &path) {
    std::vector<std::pair<std::string, std::string>> headers;
    std::ifstream file(path);
    if (!file.is_open()) {
        return headers;
    }
    std::string line;
    while (std::getline(file, line)) {
        // Remove trailing \r if present (HTTP headers use \r\n)
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        // Skip empty lines and HTTP status lines (e.g. "HTTP/1.1 200 OK")
        if (line.empty() || line.starts_with("HTTP/")) {
            continue;
        }
        const auto colon_pos = line.find(':');
        if (colon_pos == std::string::npos) {
            continue;
        }
        std::string key = line.substr(0, colon_pos);
        std::string value = line.substr(colon_pos + 1);
        // Trim leading whitespace from value
        const auto value_start = value.find_first_not_of(" \t");
        if (value_start != std::string::npos) {
            value = value.substr(value_start);
        } else {
            value.clear();
        }
        headers.emplace_back(std::move(key), std::move(value));
    }
    return headers;
}

[[nodiscard]] CurlResponse parse_curl_output(const ProcessResult &process) {
    CurlResponse response;
    response.exit_code = process.exit_code;
    response.timed_out = process.timed_out;

    if (process.timed_out) {
        response.error = "timeout";
        return response;
    }

    std::string output = process.stdout_output;
    while (!output.empty() && (output.back() == '\n' || output.back() == '\r')) {
        output.pop_back();
    }

    const auto last_newline = output.rfind('\n');
    std::string status_text;
    if (last_newline == std::string::npos) {
        status_text = output;
    } else {
        response.body = output.substr(0, last_newline);
        status_text = output.substr(last_newline + 1);
    }

    try {
        response.status_code = std::stoi(status_text);
    } catch (...) {
        response.status_code = 0;
        response.body = process.stdout_output;
    }

    if (process.exit_code != 0) {
        response.error = process.stderr_output.empty()
                             ? "curl failed with exit code " + std::to_string(process.exit_code)
                             : process.stderr_output;
    }
    return response;
}

} // namespace

CurlResponse execute_curl(const CurlRequest &request) {
    std::optional<TempBodyFile> body_file;
    std::optional<std::filesystem::path> body_path;
    if (request.body.has_value()) {
        body_file.emplace("ahfl-curl-body-");
        if (!body_file->write(*request.body)) {
            return CurlResponse{.status_code = 0,
                                .exit_code = -1,
                                .body = {},
                                .error = "failed to write curl body file: " + body_file->error(),
                                .timed_out = false};
        }
        body_path = body_file->path();
    }

    std::optional<TempHeaderFile> header_file;
    std::optional<std::filesystem::path> header_path;
    if (request.capture_headers) {
        header_file.emplace("ahfl-curl-headers-");
        if (!header_file->create()) {
            return CurlResponse{.status_code = 0,
                                .exit_code = -1,
                                .body = {},
                                .error = "failed to create curl header file: " + header_file->error(),
                                .timed_out = false};
        }
        header_path = header_file->path();
    }

    const auto config = build_curl_config(request, body_path, header_path);
    ProcessConfig process_config;
    process_config.executable = "curl";
    process_config.arguments = {"--config", "-"};
    process_config.stdin_input = config;
    process_config.timeout = std::chrono::seconds{request.timeout_seconds + 5};

    auto response = parse_curl_output(launch_process(process_config));

    if (request.capture_headers && header_path.has_value()) {
        response.response_headers = parse_header_file(*header_path);
    }

    return response;
}

std::string describe_curl_command(const CurlRequest &request) {
    std::ostringstream command;
    command << "curl --config -";
    command << " # " << request.method << " " << request.url;
    command << " headers=" << request.headers.size();
    if (request.body.has_value()) {
        command << " body_bytes=" << request.body->size();
    }
    command << " --max-time " << request.timeout_seconds;
    return command.str();
}

} // namespace ahfl::support
