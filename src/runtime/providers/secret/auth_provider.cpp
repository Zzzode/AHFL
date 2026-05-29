#include "runtime/providers/secret/auth_provider.hpp"

namespace ahfl::secret {

ResolvedAuth resolve_auth(const AuthConfig &config, SecretManager &secrets) {
    ResolvedAuth result;

    switch (config.scheme) {
    case AuthScheme::None:
        break;

    case AuthScheme::BearerToken: {
        auto token = secrets.get(config.token_key, "auth_provider");
        if (token.has_value()) {
            result.headers["Authorization"] = "Bearer " + *token;
        }
        break;
    }

    case AuthScheme::OAuth2ClientCredentials: {
        // For OAuth2, resolve client credentials.
        // In production this would exchange for an access token;
        // here we resolve the stored token directly.
        auto token = secrets.get(config.token_key, "auth_provider");
        if (token.has_value()) {
            result.headers["Authorization"] = "Bearer " + *token;
        }
        break;
    }

    case AuthScheme::MTLS: {
        auto cert = secrets.get(config.cert_path_key, "auth_provider");
        auto key = secrets.get(config.key_path_key, "auth_provider");
        if (cert.has_value()) {
            result.curl_flags.push_back("--cert");
            result.curl_flags.push_back(*cert);
        }
        if (key.has_value()) {
            result.curl_flags.push_back("--key");
            result.curl_flags.push_back(*key);
        }
        break;
    }
    }

    return result;
}

} // namespace ahfl::secret
