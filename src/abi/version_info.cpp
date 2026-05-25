#include <abi/version_info.hpp>

namespace ahfl::abi {

[[nodiscard]] AbiVersion current_version(AbiDomain domain) {
    switch (domain) {
        case AbiDomain::DurableStore:
            return AbiVersion{1, 0, 0, "a1b2c3d4e5f67890"};
        case AbiDomain::Executor:
            return AbiVersion{1, 1, 0, "b2c3d4e5f6789012"};
        case AbiDomain::Persistence:
            return AbiVersion{1, 0, 1, "c3d4e5f678901234"};
        case AbiDomain::IR:
            return AbiVersion{2, 0, 0, "d4e5f67890123456"};
        case AbiDomain::Package:
            return AbiVersion{1, 2, 0, "e5f6789012345678"};
        case AbiDomain::RuntimeSession:
            return AbiVersion{1, 0, 0, "f678901234567890"};
    }
    return AbiVersion{0, 0, 0, "0000000000000000"};
}

[[nodiscard]] std::string domain_name(AbiDomain domain) {
    switch (domain) {
        case AbiDomain::DurableStore:   return "DurableStore";
        case AbiDomain::Executor:       return "Executor";
        case AbiDomain::Persistence:    return "Persistence";
        case AbiDomain::IR:             return "IR";
        case AbiDomain::Package:        return "Package";
        case AbiDomain::RuntimeSession: return "RuntimeSession";
    }
    return "Unknown";
}

[[nodiscard]] std::string format_version(const AbiVersion& v) {
    return std::to_string(v.major) + "." +
           std::to_string(v.minor) + "." +
           std::to_string(v.patch);
}

} // namespace ahfl::abi
