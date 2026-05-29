#pragma once

#include <string>
#include <string_view>
#include <unordered_map>

namespace ahfl::runtime {

struct HttpRequest {
    std::string url;
    std::string method{"POST"};
    std::unordered_map<std::string, std::string> headers;
    std::string body;
    int timeout_seconds{30};
};

struct HttpResponse {
    int status_code{0};
    std::string body;
    std::string error;

    [[nodiscard]] bool is_success() const { return status_code >= 200 && status_code < 300; }
    [[nodiscard]] bool is_timeout() const { return status_code == 0 && error.find("timeout") != std::string::npos; }
};

class HttpTransport {
  public:
    [[nodiscard]] HttpResponse execute(const HttpRequest &request);

    /// Build the curl command string (exposed for testing).
    [[nodiscard]] static std::string build_curl_command(const HttpRequest &request);

  private:
    [[nodiscard]] static std::string shell_quote(std::string_view value);
};

} // namespace ahfl::runtime
