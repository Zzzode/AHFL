#pragma once

// standard_capabilities — default host implementations of the canonical
// standard capability set (RFC 0022 slice 1c / RFC 0020 OQ2).
//
// The nondeterministic time/uuid operations were reclassified from language
// builtins to host capabilities (Clock, UuidV4) so their results flow through
// the InvocationId / memo path and durable replay is sound (RFC 0022). To keep
// the zero-config developer path working — `ahflc run` on a workflow that calls
// std::time::now() / std::uuid::new_v4() must Just Work without the user wiring
// a clock — the native/dev runtime auto-registers these DEFAULT implementations
// (system clock, random_device), which a real host may override.

#include "runtime/engine/capability_bridge.hpp"

#include <string>
#include <string_view>

namespace ahfl::runtime {

// Canonical names of the standard capabilities (must match std/time.ahfl,
// std/uuid.ahfl).
inline constexpr std::string_view kStdCapabilityClock{"Clock"};
inline constexpr std::string_view kStdCapabilityUuidV4{"UuidV4"};

// True if `name` is a standard capability with a built-in default provider.
[[nodiscard]] bool is_standard_capability(std::string_view name);

// Invoke a standard capability's default implementation. Precondition:
// is_standard_capability(name).
[[nodiscard]] CapabilityCallResult invoke_standard_capability(std::string_view name);

// A ContextualCapabilityInvoker that answers the standard capabilities and
// delegates everything else to `fallback`. Insert into the runtime's invoker
// chain so Clock / UuidV4 resolve with zero host configuration while
// host-provided capabilities keep flowing to the fallback.
[[nodiscard]] ContextualCapabilityInvoker
with_standard_capabilities(ContextualCapabilityInvoker fallback);

} // namespace ahfl::runtime
