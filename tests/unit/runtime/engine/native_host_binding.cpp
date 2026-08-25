// native_host_binding.cpp — tests for the native projection of the ahfl_host.h
// capability ABI (RFC 0021 slice 2).
//
// A minimal in-process stub host implements ahfl_invoke / ahfl_alloc /
// ahfl_dealloc directly (no WASM, no process boundary). The tests drive it
// through make_native_capability_invoker() and assert:
//   - AHFL_CAP_OK round-trips a Value losslessly across the JSON wire frame,
//   - the callee-allocated result frame is freed exactly once (alloc/dealloc
//     call counts balance),
//   - AHFL_CAP_ERROR and any unrecognized status fail closed (Error),
//   - AHFL_CAP_PENDING fails closed with the RFC 0022 "no resume yet" diagnostic,
//   - an unconfigured binding fails closed rather than crashing.

#include "runtime/engine/native_host_binding.hpp"
#include "runtime/evaluator/value.hpp"
#include "runtime/evaluator/value_json.hpp"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace {

using namespace ahfl;
using namespace ahfl::evaluator;
using namespace ahfl::runtime;

int test_count = 0;
int pass_count = 0;

void check(bool condition, const std::string &name) {
    ++test_count;
    if (condition) {
        ++pass_count;
    } else {
        std::cerr << "FAIL: " << name << "\n";
    }
}

// ---------------------------------------------------------------------------
// Stub host: a concrete implementation of the ahfl_host.h ABI surface.
// ---------------------------------------------------------------------------

// What the next ahfl_invoke should do.
enum class StubMode {
    EchoOk,     // return AHFL_CAP_OK, echoing a fixed JSON result frame
    Error,      // return AHFL_CAP_ERROR with no result frame
    Pending,    // return AHFL_CAP_PENDING with no result frame
    BadStatus,  // return an unrecognized status code (fail-closed path)
};

struct StubHostState {
    StubMode mode{StubMode::EchoOk};
    std::string result_json;   // frame bytes to hand back on EchoOk
    int alloc_calls{0};
    int dealloc_calls{0};
    std::string last_args;     // args frame the runtime marshaled, for asserts
    uint32_t last_cap_id{0};
};

// ahfl_host is opaque in the ABI; our concrete host type is StubHostState.
StubHostState *as_state(ahfl_host *host) {
    return reinterpret_cast<StubHostState *>(host);
}

uint8_t *stub_alloc(ahfl_host *host, uint32_t len) {
    as_state(host)->alloc_calls++;
    return static_cast<uint8_t *>(std::malloc(len == 0 ? 1 : len));
}

void stub_dealloc(ahfl_host *host, uint8_t *ptr, uint32_t /*len*/) {
    as_state(host)->dealloc_calls++;
    std::free(ptr);
}

ahfl_cap_status stub_invoke(ahfl_host *host, ahfl_invoke_args *args) {
    auto *state = as_state(host);
    // Record what the runtime marshaled so tests can assert the wire framing.
    state->last_cap_id = args->cap_id;
    state->last_args.assign(reinterpret_cast<const char *>(args->args_ptr), args->args_len);

    switch (state->mode) {
    case StubMode::EchoOk: {
        const auto len = static_cast<uint32_t>(state->result_json.size());
        uint8_t *buf = stub_alloc(host, len);
        std::memcpy(buf, state->result_json.data(), len);
        *args->result_ptr = buf;
        *args->result_len = len;
        return AHFL_CAP_OK;
    }
    case StubMode::Error:
        *args->result_ptr = nullptr;
        *args->result_len = 0;
        return AHFL_CAP_ERROR;
    case StubMode::Pending:
        *args->result_ptr = nullptr;
        *args->result_len = 0;
        return AHFL_CAP_PENDING;
    case StubMode::BadStatus:
        *args->result_ptr = nullptr;
        *args->result_len = 0;
        return static_cast<ahfl_cap_status>(0x9999u);
    }
    return AHFL_CAP_ERROR;
}

NativeHostBinding make_binding(StubHostState &state) {
    NativeHostBinding binding;
    binding.host = reinterpret_cast<ahfl_host *>(&state);
    binding.invoke = &stub_invoke;
    binding.alloc = &stub_alloc;
    binding.dealloc = &stub_dealloc;
    binding.wire_format = AHFL_WIRE_VALUE_JSON;
    return binding;
}

CapabilityInvocationContext make_context(std::size_t cap_symbol_id) {
    CapabilityInvocationContext ctx;
    ctx.source_capability_symbol_id = cap_symbol_id;
    return ctx;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

void test_ok_roundtrip() {
    StubHostState state;
    state.mode = StubMode::EchoOk;
    // Host returns a struct value; the runtime must decode it losslessly.
    state.result_json = value_to_json(make_struct("Reply", [] {
        std::unordered_map<std::string, Value> f;
        f.emplace("ok", make_bool(true));
        f.emplace("count", make_int(3));
        return f;
    }()));

    auto invoker = make_native_capability_invoker(make_binding(state));
    std::vector<Value> args;
    args.push_back(make_string("hello"));
    const auto result = invoker(make_context(42), "reply_cap", args);

    check(result.status == CapabilityCallStatus::Success, "ok.status_success");
    check(result.value.has_value(), "ok.has_value");
    if (result.value.has_value()) {
        const auto *sv = std::get_if<StructValue>(&result.value->node);
        check(sv != nullptr && sv->type_name == "Reply", "ok.decoded_struct");
        if (sv != nullptr) {
            const auto *ok = sv->fields.get("ok");
            const auto *count = sv->fields.get("count");
            check(ok != nullptr && std::get_if<BoolValue>(&ok->node) != nullptr &&
                      std::get_if<BoolValue>(&ok->node)->value,
                  "ok.field_ok_true");
            check(count != nullptr && std::get_if<IntValue>(&count->node) != nullptr &&
                      std::get_if<IntValue>(&count->node)->value == 3,
                  "ok.field_count_3");
        }
    }
    // The result frame was allocated once (by the echo) and freed once.
    check(state.alloc_calls == 1 && state.dealloc_calls == 1, "ok.frame_freed_once");
    // cap_id was threaded through from the source symbol id.
    check(state.last_cap_id == 42U, "ok.cap_id_threaded");
    // The single string arg was marshaled as {"value":"hello"}.
    check(state.last_args == R"({"value":"hello"})", "ok.args_marshaled");
}

void test_error_fails_closed() {
    StubHostState state;
    state.mode = StubMode::Error;
    auto invoker = make_native_capability_invoker(make_binding(state));
    const auto result = invoker(make_context(1), "boom", {});
    check(result.status == CapabilityCallStatus::Error, "error.status_error");
    // No result frame on error, so dealloc must not run.
    check(state.dealloc_calls == 0, "error.no_free");
    check(result.error_message.find("boom") != std::string::npos, "error.names_capability");
}

void test_unknown_status_fails_closed() {
    StubHostState state;
    state.mode = StubMode::BadStatus;
    auto invoker = make_native_capability_invoker(make_binding(state));
    const auto result = invoker(make_context(1), "weird", {});
    check(result.status == CapabilityCallStatus::Error, "bad_status.fails_closed");
}

void test_pending_fails_closed_until_resume() {
    StubHostState state;
    state.mode = StubMode::Pending;
    auto invoker = make_native_capability_invoker(make_binding(state));
    const auto result = invoker(make_context(1), "llm_call", {});
    check(result.status == CapabilityCallStatus::Error, "pending.status_error");
    check(result.error_message.find("PENDING") != std::string::npos, "pending.mentions_pending");
    check(result.error_message.find("RFC 0022") != std::string::npos, "pending.cites_rfc");
}

void test_invalid_binding_fails_closed() {
    NativeHostBinding empty; // no function pointers set
    check(!empty.is_valid(), "invalid.is_not_valid");
    auto invoker = make_native_capability_invoker(empty);
    const auto result = invoker(make_context(1), "anything", {});
    check(result.status == CapabilityCallStatus::Error, "invalid.fails_closed");
}

} // namespace

int main() {
    test_ok_roundtrip();
    test_error_fails_closed();
    test_unknown_status_fails_closed();
    test_pending_fails_closed_until_resume();
    test_invalid_binding_fails_closed();

    std::cout << pass_count << "/" << test_count << " tests passed\n";
    return (pass_count == test_count) ? EXIT_SUCCESS : EXIT_FAILURE;
}
