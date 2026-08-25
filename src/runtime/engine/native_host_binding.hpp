#pragma once

// native_host_binding — the NATIVE projection of the ahfl_host.h capability ABI
// (RFC 0021 slice 2).
//
// RFC 0021 defines one language-agnostic C contract (include/ahfl/runtime/
// ahfl_host.h) by which a host provides capabilities to an embedded AHFL agent
// workflow. That header has two projections: the WASM binding (ahfl_cap imports,
// RFC 0019) and THIS native binding — a C++ function-pointer table matching the
// ABI's `ahfl_invoke` / `ahfl_alloc` / `ahfl_dealloc` symbols over an opaque
// `ahfl_host*`.
//
// A NativeHostBinding is adapted into the runtime's ContextualCapabilityInvoker
// seam by make_native_capability_invoker(): it marshals Value arguments into a
// JSON wire frame (the AHFL_WIRE_VALUE_JSON format), calls ahfl_invoke across
// the ABI, and decodes the status + result frame back into a
// CapabilityCallResult. This replaces the previous arrangement where a host
// wired a ContextualCapabilityInvoker std::function directly with no published
// contract — now the contract IS ahfl_host.h, and both bindings derive from it.

#include "ahfl/runtime/ahfl_host.h"
#include "runtime/engine/capability_bridge.hpp"

namespace ahfl::runtime {

// The native function-pointer table: a concrete C++ instance of the ahfl_host.h
// ABI. Every field mirrors a published symbol in that header; the opaque host
// pointer is threaded to every call so the host can carry per-connection state
// without a signature break (ahfl_host.h opaque-context rule).
struct NativeHostBinding {
    // Opaque per-connection host context (ahfl_host.h: `typedef struct ahfl_host
    // ahfl_host;`). The runtime treats it as opaque and passes it verbatim.
    ahfl_host *host{nullptr};

    // The capability call (ahfl_host.h: ahfl_invoke). Required.
    ahfl_cap_status (*invoke)(ahfl_host *host, ahfl_invoke_args *args){nullptr};

    // Frame allocator pair (ahfl_host.h: ahfl_alloc / ahfl_dealloc). The result
    // frame is callee-allocated with `alloc` and freed by the caller (this
    // runtime) with `dealloc` — single-owner rule. Required.
    uint8_t *(*alloc)(ahfl_host *host, uint32_t len){nullptr};
    void (*dealloc)(ahfl_host *host, uint8_t *ptr, uint32_t len){nullptr};

    // Wire format negotiated for this connection (ahfl_host.h: ahfl_wire_format).
    // Only AHFL_WIRE_VALUE_JSON is defined today; carried explicitly so a future
    // compact format can be negotiated without changing this struct's shape.
    ahfl_wire_format wire_format{AHFL_WIRE_VALUE_JSON};

    // True once `invoke`, `alloc`, and `dealloc` are all populated.
    [[nodiscard]] bool is_valid() const noexcept {
        return invoke != nullptr && alloc != nullptr && dealloc != nullptr;
    }
};

// Adapt a NativeHostBinding into the runtime's ContextualCapabilityInvoker.
//
// The returned invoker, per call:
//   1. marshals `name` (as cap_id via the invocation context's source symbol id)
//      and `args` into a JSON argument frame,
//   2. fills an ahfl_invoke_args (size-prefixed) and calls binding.invoke,
//   3. decodes the status:
//        AHFL_CAP_OK      -> parse the result frame into a Value (Success),
//        AHFL_CAP_ERROR   -> CapabilityCallStatus::Error (fail-closed: any
//                            unrecognized status is also treated as Error),
//        AHFL_CAP_PENDING -> CapabilityCallStatus::Error + a diagnostic that
//                            durable resume is not yet implemented (RFC 0022).
//      The caller frees the result frame via binding.dealloc exactly once.
//
// Precondition: binding.is_valid(). An invalid binding yields an invoker that
// fails closed with an Error result on every call.
[[nodiscard]] ContextualCapabilityInvoker
make_native_capability_invoker(const NativeHostBinding &binding);

} // namespace ahfl::runtime
