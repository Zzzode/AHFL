#pragma once

#include "ahfl/secret/secret_provider.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace ahfl::secret {

/// Authentication scheme types.
enum class AuthScheme {
    None,
    BearerToken,
    OAuth2ClientCredentials,
    MTLS,
};

/// Configuration for authentication.
struct AuthConfig {
    AuthScheme scheme{AuthScheme::None};
    std::string token_key;       // Secret key for bearer token or OAuth2 client secret
    std::string client_id_key;   // Secret key for OAuth2 client ID
    std::string cert_path_key;   // Secret key for mTLS certificate path
    std::string key_path_key;    // Secret key for mTLS private key path
    std::string token_url;       // OAuth2 token endpoint
};

/// Resolved authentication credentials ready for use.
struct ResolvedAuth {
    std::unordered_map<std::string, std::string> headers;
    std::vector<std::string> curl_flags;
};

/// Resolve authentication configuration using the secret manager.
/// Returns resolved headers and curl flags for the configured auth scheme.
[[nodiscard]] ResolvedAuth resolve_auth(const AuthConfig &config, SecretManager &secrets);

} // namespace ahfl::secret
