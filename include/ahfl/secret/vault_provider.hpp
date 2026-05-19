#pragma once

#include "ahfl/secret/secret_provider.hpp"

#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ahfl::secret {

struct VaultConfig {
    std::string address = "http://127.0.0.1:8200";
    std::string token;
    std::string mount_path = "secret";
    std::string namespace_path;
    std::chrono::seconds timeout{5};
    bool verify_tls = true;
};

enum class VaultAuthMethod {
    Token,
    AppRole,
    Kubernetes,
    AWS_IAM,
};

struct VaultAuthConfig {
    VaultAuthMethod method = VaultAuthMethod::Token;
    std::string role_id;
    std::string secret_id;
    std::string k8s_role;
    std::string aws_role;
};

/// Abstract Vault client — real implementation would use HTTP.
/// This is a compilable stub for the interface.
class VaultSecretProvider final : public SecretProvider {
  public:
    explicit VaultSecretProvider(VaultConfig config, VaultAuthConfig auth = {});

    [[nodiscard]] std::optional<std::string> resolve(std::string_view key) override;
    void refresh(std::string_view key) override;

    // Vault-specific operations
    [[nodiscard]] bool is_authenticated() const;
    [[nodiscard]] bool is_connected() const;
    void authenticate();
    void revoke_token();
    [[nodiscard]] std::vector<std::string> list_secrets(std::string_view path) const;

    [[nodiscard]] const VaultConfig &config() const;

  private:
    VaultConfig config_;
    VaultAuthConfig auth_config_;
    bool authenticated_ = false;
    mutable std::unordered_map<std::string, std::string> cache_;
};

} // namespace ahfl::secret
