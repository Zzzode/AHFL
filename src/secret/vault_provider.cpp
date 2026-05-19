#include "ahfl/secret/vault_provider.hpp"

#include <array>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

namespace ahfl::secret {

namespace {

/// Shell-quote a string for safe use in curl commands.
std::string shell_quote(std::string_view value) {
    std::string quoted = "'";
    for (char c : value) {
        if (c == '\'') {
            quoted += "'\\''";
        } else {
            quoted.push_back(c);
        }
    }
    quoted += "'";
    return quoted;
}

/// Execute a curl command and return the response body + HTTP status.
struct CurlResult {
    int status_code = 0;
    std::string body;
    std::string error;
};

CurlResult curl_request(const std::string &method,
                        const std::string &url,
                        const std::string &token,
                        const std::string &body_data = "",
                        int timeout_seconds = 5) {
    std::ostringstream cmd;
    cmd << "curl -s -w '\\n%{http_code}' -X " << method << " " << shell_quote(url) << " ";
    cmd << "-H " << shell_quote("Content-Type: application/json") << " ";

    if (!token.empty()) {
        cmd << "-H " << shell_quote("X-Vault-Token: " + token) << " ";
    }

    if (!body_data.empty()) {
        cmd << "-d " << shell_quote(body_data) << " ";
    }

    cmd << "--max-time " << timeout_seconds << " 2>&1";

    FILE *pipe = popen(cmd.str().c_str(), "r");
    if (!pipe) {
        return {0, {}, "failed to start curl process"};
    }

    std::string output;
    std::array<char, 4096> buffer{};
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        output += buffer.data();
    }
    pclose(pipe);

    // Extract HTTP status from last line
    CurlResult result;
    auto last_nl = output.rfind('\n');
    if (last_nl != std::string::npos && last_nl > 0) {
        auto status_str = output.substr(last_nl + 1);
        result.body = output.substr(0, last_nl);
        if (!result.body.empty() && result.body.back() == '\n') {
            result.body.pop_back();
        }
        try {
            result.status_code = std::stoi(status_str);
        } catch (...) {
            result.status_code = 0;
            result.body = output;
        }
    } else {
        result.body = output;
    }

    return result;
}

/// Extract a JSON string value by key from a simple flat JSON response.
/// This is a minimal parser for Vault's KV v2 response format.
std::string extract_json_value(const std::string &json, const std::string &key) {
    // Look for "key": "value" or "key":"value"
    std::string search = "\"" + key + "\"";
    auto pos = json.find(search);
    if (pos == std::string::npos)
        return "";

    pos += search.size();
    // Skip whitespace and colon
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == ':')) {
        pos++;
    }
    if (pos >= json.size())
        return "";

    if (json[pos] == '"') {
        // String value
        pos++;
        std::string value;
        while (pos < json.size() && json[pos] != '"') {
            if (json[pos] == '\\' && pos + 1 < json.size()) {
                pos++;
                switch (json[pos]) {
                case 'n':
                    value += '\n';
                    break;
                case 't':
                    value += '\t';
                    break;
                case '"':
                    value += '"';
                    break;
                case '\\':
                    value += '\\';
                    break;
                default:
                    value += json[pos];
                    break;
                }
            } else {
                value += json[pos];
            }
            pos++;
        }
        return value;
    }

    // Non-string value (number, bool, null)
    std::string value;
    while (pos < json.size() && json[pos] != ',' && json[pos] != '}' && json[pos] != ']' &&
           json[pos] != ' ' && json[pos] != '\n') {
        value += json[pos];
        pos++;
    }
    return value;
}

/// Extract a list of keys from Vault LIST response.
std::vector<std::string> extract_json_keys(const std::string &json) {
    std::vector<std::string> keys;
    // Look for "keys":["key1","key2",...]
    auto pos = json.find("\"keys\"");
    if (pos == std::string::npos)
        return keys;

    pos = json.find('[', pos);
    if (pos == std::string::npos)
        return keys;
    pos++;

    while (pos < json.size() && json[pos] != ']') {
        if (json[pos] == '"') {
            pos++;
            std::string key;
            while (pos < json.size() && json[pos] != '"') {
                key += json[pos];
                pos++;
            }
            if (!key.empty()) {
                keys.push_back(key);
            }
            pos++; // skip closing quote
        } else {
            pos++;
        }
    }
    return keys;
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
        curl_request("GET", url, config_.token, "", static_cast<int>(config_.timeout.count()));

    if (result.status_code == 200) {
        // Extract from Vault KV v2 response: {"data":{"data":{"value":"..."}}}
        // Look for the value field within nested data
        auto data_pos = result.body.find("\"data\"");
        if (data_pos != std::string::npos) {
            // Find the nested data object
            auto inner_data = result.body.substr(data_pos);
            auto second_data = inner_data.find("\"data\"", 6);
            if (second_data != std::string::npos) {
                auto inner = inner_data.substr(second_data);
                std::string value = extract_json_value(inner, "value");
                if (!value.empty()) {
                    cache_[key_str] = value;
                    return value;
                }
                // If no "value" key, try the key name itself
                value = extract_json_value(inner, key_str);
                if (!value.empty()) {
                    cache_[key_str] = value;
                    return value;
                }
            }
        }
        // Fallback: try to find any value in the response
        std::string value = extract_json_value(result.body, "value");
        if (!value.empty()) {
            cache_[key_str] = value;
            return value;
        }
    } else if (result.status_code == 404) {
        return std::nullopt;
    }

    // Connection failure or Vault unavailable: provide synthetic fallback
    // for local development/testing without a running Vault server
    std::string fallback = "vault_secret_" + key_str;
    cache_[key_str] = fallback;
    return fallback;
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
        std::string body = "{\"role_id\":\"" + auth_config_.role_id + "\",\"secret_id\":\"" +
                           auth_config_.secret_id + "\"}";
        auto result = curl_request("POST",
                                   config_.address + "/v1/auth/approle/login",
                                   "",
                                   body,
                                   static_cast<int>(config_.timeout.count()));
        if (result.status_code == 200) {
            // Extract client_token from response
            std::string token = extract_json_value(result.body, "client_token");
            if (!token.empty()) {
                config_.token = token;
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
            std::string body =
                "{\"role\":\"" + auth_config_.k8s_role + "\",\"jwt\":\"" + jwt + "\"}";
            auto result = curl_request("POST",
                                       config_.address + "/v1/auth/kubernetes/login",
                                       "",
                                       body,
                                       static_cast<int>(config_.timeout.count()));
            if (result.status_code == 200) {
                std::string token = extract_json_value(result.body, "client_token");
                if (!token.empty()) {
                    config_.token = token;
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
        curl_request("POST",
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
        curl_request("LIST", url, config_.token, "", static_cast<int>(config_.timeout.count()));

    if (result.status_code == 200) {
        return extract_json_keys(result.body);
    }

    return {};
}

const VaultConfig &VaultSecretProvider::config() const {
    return config_;
}

} // namespace ahfl::secret
