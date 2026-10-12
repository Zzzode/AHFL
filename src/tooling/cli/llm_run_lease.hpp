#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace ahfl::cli {

// An exclusive command lease over one public binding's managed active run.
// The lease never contains model credentials and does not authenticate session contents.
class LLMRunLease {
  public:
    [[nodiscard]] static std::expected<std::unique_ptr<LLMRunLease>, std::string>
    open(std::string_view binding_hex,
         std::optional<std::filesystem::path> explicit_recovery_path = std::nullopt);

    ~LLMRunLease();
    LLMRunLease(const LLMRunLease &) = delete;
    LLMRunLease &operator=(const LLMRunLease &) = delete;

    [[nodiscard]] const std::filesystem::path &recovery_path() const {
        return recovery_path_;
    }
    [[nodiscard]] const std::filesystem::path &session_root() const {
        return session_root_;
    }
    [[nodiscard]] std::expected<void, std::string>
    complete(bool discard_fresh_explicit_run = false);

  private:
    LLMRunLease() = default;
    std::filesystem::path recovery_path_;
    std::filesystem::path session_root_;
    std::filesystem::path binding_root_;
    std::filesystem::path run_root_;
    std::string binding_hex_;
    std::uint64_t namespace_id_{};
    int lock_fd_{-1};
    bool managed_{};
    bool completed_{};
};

} // namespace ahfl::cli
