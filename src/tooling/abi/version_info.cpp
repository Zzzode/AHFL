#include <tooling/abi/version_info.hpp>

namespace ahfl::abi {

[[nodiscard]] AbiVersion current_version(AbiDomain domain) {
    switch (domain) {
    case AbiDomain::Executor:
        return AbiVersion{1, 1, 0, "b2c3d4e5f6789012"};
    case AbiDomain::IR:
        return AbiVersion{2, 0, 0, "d4e5f67890123456"};
    case AbiDomain::Package:
        return AbiVersion{1, 2, 0, "e5f6789012345678"};
    }
    return AbiVersion{0, 0, 0, "0000000000000000"};
}

[[nodiscard]] std::string domain_name(AbiDomain domain) {
    switch (domain) {
    case AbiDomain::Executor:
        return "Executor";
    case AbiDomain::IR:
        return "IR";
    case AbiDomain::Package:
        return "Package";
    }
    return "Unknown";
}

[[nodiscard]] std::string format_version(const AbiVersion &v) {
    return std::to_string(v.major) + "." + std::to_string(v.minor) + "." + std::to_string(v.patch);
}

} // namespace ahfl::abi
