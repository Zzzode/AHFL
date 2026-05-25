#pragma once

#include <abi/version_info.hpp>
#include <string>

namespace ahfl::abi {

enum class CompatibilityLevel {
    FullyCompatible,
    BackwardCompatible,
    BreakingChange,
    Unknown
};

struct CompatibilityResult {
    CompatibilityLevel level;
    std::string description;
    AbiVersion source;
    AbiVersion target;
};

[[nodiscard]] CompatibilityResult check_compatibility(
    AbiDomain domain,
    const AbiVersion& source,
    const AbiVersion& target);

[[nodiscard]] bool is_compatible(
    const AbiVersion& source,
    const AbiVersion& target);

} // namespace ahfl::abi
