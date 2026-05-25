#include <abi/compat_check.hpp>

namespace ahfl::abi {

[[nodiscard]] CompatibilityResult check_compatibility(
    AbiDomain domain,
    const AbiVersion& source,
    const AbiVersion& target) {

    CompatibilityResult result;
    result.source = source;
    result.target = target;

    if (source.major != target.major) {
        result.level = CompatibilityLevel::BreakingChange;
        result.description = "Breaking change in " + domain_name(domain) +
                             ": major version mismatch (" +
                             format_version(source) + " -> " +
                             format_version(target) + ")";
        return result;
    }

    if (source.minor == target.minor && source.patch == target.patch) {
        if (source.schema_hash != target.schema_hash) {
            result.level = CompatibilityLevel::BackwardCompatible;
            result.description = "Schema evolved in " + domain_name(domain) +
                                 ": versions match but schema hash differs";
            return result;
        }
        result.level = CompatibilityLevel::FullyCompatible;
        result.description = "Fully compatible in " + domain_name(domain) +
                             ": identical version " + format_version(source);
        return result;
    }

    if (target.minor > source.minor ||
        (target.minor == source.minor && target.patch > source.patch)) {
        result.level = CompatibilityLevel::BackwardCompatible;
        result.description = "Backward compatible in " + domain_name(domain) +
                             ": " + format_version(source) + " -> " +
                             format_version(target);
        return result;
    }

    result.level = CompatibilityLevel::BackwardCompatible;
    result.description = "Backward compatible in " + domain_name(domain) +
                         ": same major version";
    return result;
}

[[nodiscard]] bool is_compatible(
    const AbiVersion& source,
    const AbiVersion& target) {
    return source.major == target.major;
}

} // namespace ahfl::abi
