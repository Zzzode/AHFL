#pragma once

#include <cstdint>
#include <string>

namespace ahfl::abi {

enum class AbiDomain {
    DurableStore,
    Executor,
    Persistence,
    IR,
    Package,
    RuntimeSession
};

struct AbiVersion {
    uint32_t major;
    uint32_t minor;
    uint32_t patch;
    std::string schema_hash; // SHA-256 hex prefix (first 16 chars)
};

[[nodiscard]] AbiVersion current_version(AbiDomain domain);
[[nodiscard]] std::string domain_name(AbiDomain domain);
[[nodiscard]] std::string format_version(const AbiVersion &v);

} // namespace ahfl::abi
