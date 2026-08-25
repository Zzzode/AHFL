#include "runtime/engine/native_host_binding.hpp"

#include "runtime/engine/wire_value.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ahfl::runtime {

namespace {

// Build a CapabilityCallResult carrying a fail-closed Error with a diagnostic.
CapabilityCallResult error_result(std::string message) {
    CapabilityCallResult result;
    result.status = CapabilityCallStatus::Error;
    result.error_message = std::move(message);
    return result;
}

} // namespace

ContextualCapabilityInvoker make_native_capability_invoker(const NativeHostBinding &binding) {
    // Copy the small POD table by value so the invoker owns a stable snapshot of
    // the function pointers and host handle for its whole lifetime.
    return [binding](const CapabilityInvocationContext &context,
                     const std::string &name,
                     const std::vector<Value> &arguments) -> CapabilityCallResult {
        if (!binding.is_valid()) {
            return error_result("native host binding is not configured (missing ahfl_invoke / "
                                "ahfl_alloc / ahfl_dealloc): capability '" +
                                name + "'");
        }

        // Marshal arguments into a JSON wire frame (AHFL_WIRE_VALUE_JSON).
        const std::string args_frame = serialize_args_for_wire_json(arguments);

        // cap_id is the capability's SymbolId (index-based identity). Use the
        // source symbol id resolved by the compiler when present; 0 is a valid
        // sentinel for hosts that dispatch by frame content rather than id.
        const uint32_t cap_id =
            context.source_capability_symbol_id.has_value()
                ? static_cast<uint32_t>(*context.source_capability_symbol_id)
                : 0U;

        uint8_t *result_ptr = nullptr;
        uint32_t result_len = 0;
        ahfl_invoke_args invoke_args;
        invoke_args.struct_size = static_cast<uint32_t>(sizeof(ahfl_invoke_args));
        invoke_args.cap_id = cap_id;
        invoke_args.args_ptr = reinterpret_cast<const uint8_t *>(args_frame.data());
        invoke_args.args_len = static_cast<uint32_t>(args_frame.size());
        invoke_args.result_ptr = &result_ptr;
        invoke_args.result_len = &result_len;

        const ahfl_cap_status status = binding.invoke(binding.host, &invoke_args);

        switch (status) {
        case AHFL_CAP_OK: {
            // Decode the callee-allocated result frame, then free it exactly once
            // via the same allocator that produced it (single-owner rule).
            std::optional<Value> decoded;
            if (result_ptr != nullptr && result_len > 0) {
                const std::string_view frame(reinterpret_cast<const char *>(result_ptr),
                                             result_len);
                decoded = parse_value_from_wire_json(frame);
            }
            if (result_ptr != nullptr) {
                binding.dealloc(binding.host, result_ptr, result_len);
            }
            if (!decoded.has_value()) {
                return error_result("native capability '" + name +
                                    "' returned AHFL_CAP_OK but its result frame was empty or "
                                    "not valid " +
                                    "wire JSON");
            }
            CapabilityCallResult result;
            result.status = CapabilityCallStatus::Success;
            result.value = std::move(decoded);
            return result;
        }
        case AHFL_CAP_PENDING: {
            // RFC 0022 (durable resume): the host accepted the call but the
            // result is not yet available. ahfl_host.h transfers argument-frame
            // ownership to the host for the suspension lifetime; the callee set
            // *result_ptr = NULL, so there is nothing to decode or free. Map to
            // a Pending result — the workflow runtime suspends at this node,
            // persists a resume record, and resumes later with the host result.
            // The pending memo coordinate (cap_id / ordinal) is stamped by the
            // runtime invoker from the invocation context downstream.
            CapabilityCallResult result;
            result.status = CapabilityCallStatus::Pending;
            return result;
        }
        case AHFL_CAP_ERROR:
        default:
            // Fail-closed: AHFL_CAP_ERROR and ANY unrecognized status collapse to
            // a terminating Error (ahfl_host.h fail-closed rule). The callee must
            // not have allocated a result frame; do not free.
            return error_result("native capability '" + name + "' failed (status " +
                                std::to_string(status) + ")");
        }
    };
}

} // namespace ahfl::runtime
