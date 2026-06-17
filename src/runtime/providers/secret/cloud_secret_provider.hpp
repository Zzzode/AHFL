#pragma once

#include "runtime/providers/secret/secret_provider.hpp"

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace ahfl::secret {

struct CloudSecretManagerConfig {
    std::string address;
    std::string token;
    std::string project;
    std::string version{"latest"};
    std::chrono::seconds timeout{5};
};

/// HTTP-backed Secret Manager provider for cloud-style secret endpoints.
///
/// The provider intentionally depends only on AHFL's curl-based HTTP seam. It
/// can front real gateways or test stubs without pulling cloud SDKs into the
/// runtime.
class CloudSecretManagerProvider final : public SecretProvider {
  public:
    explicit CloudSecretManagerProvider(CloudSecretManagerConfig config);

    [[nodiscard]] std::optional<std::string> resolve(std::string_view key) override;
    void refresh(std::string_view key) override;

    [[nodiscard]] const CloudSecretManagerConfig &config() const;

  private:
    CloudSecretManagerConfig config_;
    std::unordered_map<std::string, std::string> cache_;
};

} // namespace ahfl::secret
