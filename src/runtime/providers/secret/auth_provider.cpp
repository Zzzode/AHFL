#include "runtime/providers/secret/auth_provider.hpp"

namespace ahfl::secret {

ResolvedAuth resolve_auth(const AuthConfig &config, SecretManager &secrets) {
    ResolvedAuth result;

    switch (config.scheme) {
    case AuthScheme::None:
        break;

    case AuthScheme::BearerToken: {
        if (config.token_key.empty()) {
            result.success = false;
            result.error_message = "bearer token secret key is empty";
            break;
        }
        auto token = secrets.get(config.token_key, "auth_provider");
        if (token.has_value()) {
            result.headers["Authorization"] = "Bearer " + *token;
        } else {
            result.success = false;
            result.error_message = "bearer token secret not found: " + config.token_key;
        }
        break;
    }

    case AuthScheme::OAuth2ClientCredentials: {
        // For OAuth2, resolve client credentials.
        // In production this would exchange for an access token;
        // here we resolve the stored token directly.
        if (config.token_key.empty()) {
            result.success = false;
            result.error_message = "OAuth2 token secret key is empty";
            break;
        }
        auto token = secrets.get(config.token_key, "auth_provider");
        if (token.has_value()) {
            result.headers["Authorization"] = "Bearer " + *token;
        } else {
            result.success = false;
            result.error_message = "OAuth2 token secret not found: " + config.token_key;
        }
        break;
    }

    case AuthScheme::MTLS: {
        if (config.cert_path_key.empty()) {
            result.success = false;
            result.error_message = "mTLS certificate path secret key is empty";
            break;
        }
        if (config.key_path_key.empty()) {
            result.success = false;
            result.error_message = "mTLS private key path secret key is empty";
            break;
        }
        auto cert = secrets.get(config.cert_path_key, "auth_provider");
        auto key = secrets.get(config.key_path_key, "auth_provider");
        if (!cert.has_value()) {
            result.success = false;
            result.error_message =
                "mTLS certificate path secret not found: " + config.cert_path_key;
            break;
        }
        if (!key.has_value()) {
            result.success = false;
            result.error_message = "mTLS private key path secret not found: " + config.key_path_key;
            break;
        }
        result.tls_client_certificate_path = *cert;
        result.tls_client_key_path = *key;
        result.curl_flags.push_back("--cert");
        result.curl_flags.push_back(*cert);
        result.curl_flags.push_back("--key");
        result.curl_flags.push_back(*key);
        break;
    }
    }

    return result;
}

} // namespace ahfl::secret
