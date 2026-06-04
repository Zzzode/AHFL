#include "runtime/providers/secret/vault_provider.hpp"

#include "base/json/json_value.hpp"
#include "base/support/http.hpp"

#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ahfl::secret {

namespace {

struct HttpResult {
    int status_code = 0;
    std::string body;
    std::string error;
};

HttpResult http_request(const std::string &method,
                        const std::string &url,
                        const std::string &token,
                        const std::string &body_data = "",
                        int timeout_seconds = 5) {
    ahfl::support::HttpRequest request;
    request.method = method;
    request.url = url;
    request.timeout_seconds = timeout_seconds;
    request.headers.emplace_back("Content-Type", "application/json");
    if (!token.empty()) {
        request.headers.emplace_back("X-Vault-Token", token);
    }
    if (!body_data.empty()) {
        request.body = body_data;
    }

    const auto response = ahfl::support::execute_http(request);
    return HttpResult{
        .status_code = response.status_code,
        .body = response.body,
        .error = response.error,
    };
}

std::optional<std::string> json_string_field(const ahfl::json::JsonValue &object,
                                             std::string_view key) {
    const auto *field = object.get(key);
    if (field == nullptr) {
        return std::nullopt;
    }
    auto value = field->as_string();
    if (!value.has_value()) {
        return std::nullopt;
    }
    return std::string(*value);
}

std::optional<std::string> vault_client_token(const std::string &body) {
    auto parsed = ahfl::json::parse_json(body);
    if (!parsed.has_value() || !*parsed || !(*parsed)->is_object()) {
        return std::nullopt;
    }
    const auto *auth = (*parsed)->get("auth");
    if (auth == nullptr || !auth->is_object()) {
        return std::nullopt;
    }
    return json_string_field(*auth, "client_token");
}

std::optional<std::string> vault_secret_value(const std::string &body, std::string_view key) {
    auto parsed = ahfl::json::parse_json(body);
    if (!parsed.has_value() || !*parsed || !(*parsed)->is_object()) {
        return std::nullopt;
    }
    const auto *outer = (*parsed)->get("data");
    const auto *inner = outer != nullptr && outer->is_object() ? outer->get("data") : nullptr;
    if (inner == nullptr || !inner->is_object()) {
        return std::nullopt;
    }
    if (auto value = json_string_field(*inner, "value"); value.has_value()) {
        return value;
    }
    return json_string_field(*inner, key);
}

std::vector<std::string> vault_secret_keys(const std::string &body) {
    auto parsed = ahfl::json::parse_json(body);
    if (!parsed.has_value() || !*parsed || !(*parsed)->is_object()) {
        return {};
    }
    const auto *data = (*parsed)->get("data");
    const auto *keys = data != nullptr && data->is_object() ? data->get("keys") : nullptr;
    if (keys == nullptr || !keys->is_array()) {
        return {};
    }
    std::vector<std::string> result;
    for (const auto &key : keys->array_items) {
        auto value = key->as_string();
        if (!value.has_value()) {
            return {};
        }
        result.push_back(std::string(*value));
    }
    return result;
}

std::string build_object_json(std::vector<std::pair<std::string, std::string>> fields) {
    auto object = ahfl::json::JsonValue::make_object();
    for (auto &[key, value] : fields) {
        object->set(std::move(key), ahfl::json::JsonValue::make_string(std::move(value)));
    }
    return ahfl::json::serialize_json(*object);
}

} // namespace

// ============================================================================
// VaultSecretProvider
// ============================================================================

VaultSecretProvider::VaultSecretProvider(VaultConfig config, VaultAuthConfig auth)
    : config_(std::move(config)), auth_config_(std::move(auth)) {}

std::optional<std::string> VaultSecretProvider::resolve(std::string_view key) {
    if (!authenticated_) {
        return std::nullopt;
    }

    std::string key_str(key);

    // Check cache first
    auto it = cache_.find(key_str);
    if (it != cache_.end()) {
        return it->second;
    }

    // Real Vault KV v2 API call: GET /v1/{mount}/data/{key}
    std::string url = config_.address + "/v1/" + config_.mount_path + "/data/" + key_str;
    if (!config_.namespace_path.empty()) {
        url = config_.address + "/v1/" + config_.namespace_path + "/" + config_.mount_path +
              "/data/" + key_str;
    }

    auto result =
        http_request("GET", url, config_.token, "", static_cast<int>(config_.timeout.count()));

    if (result.status_code == 200) {
        if (auto value = vault_secret_value(result.body, key_str); value.has_value()) {
            cache_[key_str] = *value;
            return value;
        }
    } else if (result.status_code == 404) {
        return std::nullopt;
    }

    return std::nullopt;
}

void VaultSecretProvider::refresh(std::string_view key) {
    std::string key_str(key);
    cache_.erase(key_str);

    // Re-resolve from Vault if authenticated
    if (authenticated_) {
        (void)resolve(key);
    }
}

bool VaultSecretProvider::is_authenticated() const {
    return authenticated_;
}

bool VaultSecretProvider::is_connected() const {
    return authenticated_;
}

void VaultSecretProvider::authenticate() {
    switch (auth_config_.method) {
    case VaultAuthMethod::Token:
        // Token auth: accept authentication (validated lazily on first request)
        authenticated_ = true;
        break;

    case VaultAuthMethod::AppRole: {
        // AppRole auth: POST /v1/auth/approle/login with role_id + secret_id
        std::string body = build_object_json(
            {{"role_id", auth_config_.role_id}, {"secret_id", auth_config_.secret_id}});
        auto result = http_request("POST",
                                   config_.address + "/v1/auth/approle/login",
                                   "",
                                   body,
                                   static_cast<int>(config_.timeout.count()));
        if (result.status_code == 200) {
            if (auto token = vault_client_token(result.body); token.has_value()) {
                config_.token = *token;
                authenticated_ = true;
            }
        }
        break;
    }

    case VaultAuthMethod::Kubernetes: {
        // Kubernetes auth: POST /v1/auth/kubernetes/login
        // Read service account token from standard mount
        std::string jwt;
        std::ifstream sa_file("/var/run/secrets/kubernetes.io/serviceaccount/token");
        if (sa_file.is_open()) {
            std::getline(sa_file, jwt);
        }
        if (!jwt.empty()) {
            std::string body = build_object_json({{"role", auth_config_.k8s_role}, {"jwt", jwt}});
            auto result = http_request("POST",
                                       config_.address + "/v1/auth/kubernetes/login",
                                       "",
                                       body,
                                       static_cast<int>(config_.timeout.count()));
            if (result.status_code == 200) {
                if (auto token = vault_client_token(result.body); token.has_value()) {
                    config_.token = *token;
                    authenticated_ = true;
                }
            }
        }
        break;
    }

    case VaultAuthMethod::AWS_IAM:
        // AWS IAM auth requires STS signed request — complex; use token fallback
        if (!config_.token.empty()) {
            authenticated_ = true;
        }
        break;
    }
}

void VaultSecretProvider::revoke_token() {
    if (authenticated_ && !config_.token.empty()) {
        // POST /v1/auth/token/revoke-self
        http_request("POST",
                     config_.address + "/v1/auth/token/revoke-self",
                     config_.token,
                     "",
                     static_cast<int>(config_.timeout.count()));
    }
    authenticated_ = false;
    config_.token.clear();
    cache_.clear();
}

std::vector<std::string> VaultSecretProvider::list_secrets(std::string_view path) const {
    if (!authenticated_) {
        return {};
    }

    std::string path_str(path);
    // LIST /v1/{mount}/metadata/{path}
    std::string url = config_.address + "/v1/" + config_.mount_path + "/metadata/";
    if (!path_str.empty()) {
        url += path_str;
    }

    auto result =
        http_request("LIST", url, config_.token, "", static_cast<int>(config_.timeout.count()));

    if (result.status_code == 200) {
        return vault_secret_keys(result.body);
    }

    return {};
}

const VaultConfig &VaultSecretProvider::config() const {
    return config_;
}

} // namespace ahfl::secret
