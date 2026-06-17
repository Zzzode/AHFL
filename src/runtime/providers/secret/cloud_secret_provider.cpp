#include "runtime/providers/secret/cloud_secret_provider.hpp"

#include "base/json/json_value.hpp"
#include "base/support/http.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace ahfl::secret {
namespace {

[[nodiscard]] bool is_unreserved_uri_byte(unsigned char value) {
    return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z') ||
           (value >= '0' && value <= '9') || value == '-' || value == '_' || value == '.' ||
           value == '~';
}

[[nodiscard]] std::string percent_encode(std::string_view value) {
    constexpr char digits[] = "0123456789ABCDEF";
    std::string encoded;
    encoded.reserve(value.size());
    for (const auto ch : value) {
        const auto byte = static_cast<unsigned char>(ch);
        if (is_unreserved_uri_byte(byte)) {
            encoded.push_back(static_cast<char>(byte));
            continue;
        }
        encoded.push_back('%');
        encoded.push_back(digits[(byte >> 4U) & 0x0F]);
        encoded.push_back(digits[byte & 0x0F]);
    }
    return encoded;
}

[[nodiscard]] std::string trim_trailing_slash(std::string value) {
    while (!value.empty() && value.back() == '/') {
        value.pop_back();
    }
    return value;
}

[[nodiscard]] std::string cloud_secret_url(const CloudSecretManagerConfig &config,
                                           std::string_view key) {
    auto base = trim_trailing_slash(config.address);
    if (config.project.empty()) {
        return base + "/v1/secrets/" + percent_encode(key);
    }
    return base + "/v1/projects/" + percent_encode(config.project) + "/secrets/" +
           percent_encode(key) + "/versions/" + percent_encode(config.version) + ":access";
}

[[nodiscard]] std::optional<std::string> json_string_field(const ahfl::json::JsonValue &object,
                                                           std::string_view key) {
    const auto *field = object.get(key);
    if (field == nullptr) {
        return std::nullopt;
    }
    const auto value = field->as_string();
    if (!value.has_value()) {
        return std::nullopt;
    }
    return std::string(*value);
}

[[nodiscard]] int base64_value(char ch) {
    if (ch >= 'A' && ch <= 'Z') {
        return ch - 'A';
    }
    if (ch >= 'a' && ch <= 'z') {
        return ch - 'a' + 26;
    }
    if (ch >= '0' && ch <= '9') {
        return ch - '0' + 52;
    }
    if (ch == '+') {
        return 62;
    }
    if (ch == '/') {
        return 63;
    }
    return -1;
}

[[nodiscard]] std::optional<std::string> decode_base64(std::string_view input) {
    std::string output;
    int value = 0;
    int bits = -8;
    for (const char ch : input) {
        if (ch == '=') {
            break;
        }
        if (ch == '\r' || ch == '\n' || ch == ' ' || ch == '\t') {
            continue;
        }
        const int decoded = base64_value(ch);
        if (decoded < 0) {
            return std::nullopt;
        }
        value = (value << 6) + decoded;
        bits += 6;
        if (bits >= 0) {
            output.push_back(static_cast<char>((value >> bits) & 0xFF));
            bits -= 8;
        }
    }
    return output;
}

[[nodiscard]] std::optional<std::string> cloud_secret_value(const std::string &body,
                                                            std::string_view key) {
    auto parsed = ahfl::json::parse_json(body);
    if (!parsed.has_value() || !*parsed || !(*parsed)->is_object()) {
        return std::nullopt;
    }

    if (auto value = json_string_field(**parsed, "value"); value.has_value()) {
        return value;
    }
    if (auto value = json_string_field(**parsed, "secret_value"); value.has_value()) {
        return value;
    }
    if (auto value = json_string_field(**parsed, "SecretString"); value.has_value()) {
        return value;
    }

    const auto *data = (*parsed)->get("data");
    if (data != nullptr && data->is_object()) {
        if (auto value = json_string_field(*data, "value"); value.has_value()) {
            return value;
        }
        if (auto value = json_string_field(*data, key); value.has_value()) {
            return value;
        }
    }

    const auto *payload = (*parsed)->get("payload");
    if (payload != nullptr && payload->is_object()) {
        if (auto encoded = json_string_field(*payload, "data"); encoded.has_value()) {
            return decode_base64(*encoded);
        }
    }

    return std::nullopt;
}

} // namespace

CloudSecretManagerProvider::CloudSecretManagerProvider(CloudSecretManagerConfig config)
    : config_(std::move(config)) {}

std::optional<std::string> CloudSecretManagerProvider::resolve(std::string_view key) {
    if (config_.address.empty() || config_.token.empty()) {
        return std::nullopt;
    }

    const std::string key_str(key);
    if (auto it = cache_.find(key_str); it != cache_.end()) {
        return it->second;
    }

    ahfl::support::HttpRequest request;
    request.method = "GET";
    request.url = cloud_secret_url(config_, key_str);
    request.timeout_seconds = static_cast<int>(config_.timeout.count());
    request.headers.emplace_back("Accept", "application/json");
    request.headers.emplace_back("Authorization", "Bearer " + config_.token);

    const auto response = ahfl::support::execute_http(request);
    if (!response.is_success()) {
        return std::nullopt;
    }

    auto value = cloud_secret_value(response.body, key_str);
    if (!value.has_value()) {
        return std::nullopt;
    }
    cache_.emplace(key_str, *value);
    return value;
}

void CloudSecretManagerProvider::refresh(std::string_view key) {
    const std::string key_str(key);
    cache_.erase(key_str);
    static_cast<void>(resolve(key));
}

const CloudSecretManagerConfig &CloudSecretManagerProvider::config() const {
    return config_;
}

} // namespace ahfl::secret
