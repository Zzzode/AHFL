// native_wasm_differential.cpp — RFC 0021 slice 5 reference host + binding
// equivalence, plus RFC 0026 E4-B1 C3 the runtime wire-schema module inspector.
//
// SCOPE — two distinct concerns share this target because both are runtime-side
// reference hosts over the published contracts:
//
// A. RFC 0019/0021 native differential: at that slice AHFL emitted WAT and shipped
//    no executing WASM VM, so a differential against an *executed* module was out
//    of scope. What this half proves is the achievable part:
//   1. A minimal C-style reference host implementing the ahfl_host.h ABI drives
//      a real capability workflow through the native binding (slice 2) and the
//      workflow completes with the expected output -- executable evidence that
//      the published contract works end to end.
//   2. That native-binding run is EQUIVALENT (same L1 workflow status + output,
//      same L2 capability-invocation sequence) to the same workflow driven by a
//      directly-wired invoker returning the same value. The two dispatch paths
//      must be observationally identical -- the property RFC 0020 Q4 requires of
//      any two bindings of one workflow.
//   3. Golden negative: an unbound capability fails closed (workflow does not
//      complete), never silently succeeding.
//   The executed-WASM differential and the PENDING/resume path are tracked
//   separately (an executing WASM runtime is unscoped; resume is RFC 0022).
//
// B. RFC 0026 E4-B1 C3 module-byte inspector (the `c3` namespace below): a generic
//    host receives transported Wasm MODULE BYTES (not a CoreProgram) and mints a
//    verified wire binding from the `ahfl.wire-schema.v1` custom section. This half
//    frames a canonical module via a TEST-ONLY builder and exercises the inspector
//    positive controls, resource gates, and single-gate negatives; the real
//    admission authority is the inspector + the C1 decoder + the typed factory.

#include "runtime/engine/core_wasm_schema_transport.hpp"
#include "runtime/engine/native_host_binding.hpp"
#include "runtime/engine/workflow_recovery.hpp"
#include "runtime/engine/workflow_runtime.hpp"
#include "runtime/value/value.hpp"
#include "runtime/value/value_json.hpp"

#include "ahfl/compiler/ir/core_wire_schema.hpp"
#include "ahfl/compiler/ir/ir.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <variant>
#include <vector>

namespace {

using namespace ahfl;
using namespace ahfl::evaluator;
using namespace ahfl::runtime;
using namespace ahfl::ir;

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

// --- IR builders (same shape as the workflow_runtime capability test) --------

ExprArena &arena() {
    static ExprArena a;
    return a;
}
ExprRef expr(ExprNode node) {
    return arena().make(std::move(node));
}
TypeRef named_type(const std::string &name) {
    return TypeRef{.kind = TypeRefKind::Struct, .display_name = name, .canonical_name = name};
}

// A workflow whose return value is `is_ready() == true` -- one capability call.
Program make_capability_workflow() {
    Program program;
    WorkflowDecl workflow;
    workflow.name = "CapabilityReturnWorkflow";
    workflow.input_type_ref = named_type("Input");
    workflow.output_type_ref = named_type("Output");

    CallExpr ready_call;
    ready_call.callee = "is_ready";
    BinaryExpr condition;
    condition.op = ExprBinaryOp::Equal;
    condition.lhs = expr(std::move(ready_call));
    condition.rhs = expr(BoolLiteralExpr{true});
    workflow.return_value = expr(std::move(condition));

    program.declarations.push_back(std::move(workflow));
    return program;
}

// --- The reference host: a concrete ahfl_host.h implementation ---------------
//
// Dispatches by inspecting the JSON argument frame's capability id is not needed
// here (single capability); it answers `is_ready` with `true` as a wire frame.
struct ReferenceHost {
    int invoke_calls{0};
};

ReferenceHost *as_host(ahfl_host *h) {
    return reinterpret_cast<ReferenceHost *>(h);
}

uint8_t *ref_alloc(ahfl_host * /*host*/, uint32_t len) {
    return static_cast<uint8_t *>(std::malloc(len == 0 ? 1 : len));
}
void ref_dealloc(ahfl_host * /*host*/, uint8_t *ptr, uint32_t /*len*/) {
    std::free(ptr);
}
ahfl_cap_status ref_invoke(ahfl_host *host, ahfl_invoke_args *args) {
    as_host(host)->invoke_calls++;
    // The reference host answers the single capability with `true`.
    const std::string frame = value_to_json(make_bool(true));
    const auto len = static_cast<uint32_t>(frame.size());
    uint8_t *buf = ref_alloc(host, len);
    std::memcpy(buf, frame.data(), len);
    *args->result_ptr = buf;
    *args->result_len = len;
    return AHFL_CAP_OK;
}

NativeHostBinding make_reference_binding(ReferenceHost &host) {
    NativeHostBinding binding;
    binding.host = reinterpret_cast<ahfl_host *>(&host);
    binding.invoke = &ref_invoke;
    binding.alloc = &ref_alloc;
    binding.dealloc = &ref_dealloc;
    return binding;
}

// --- L2 extraction: the ordered capability-invocation sequence ---------------

std::vector<std::string> capability_sequence(const WorkflowResult &result) {
    std::vector<std::string> sequence;
    for (const auto &event : result.events.events()) {
        if (const auto *started = std::get_if<CapabilityStarted>(&event.payload)) {
            const auto *meta = result.metadata.capability(started->capability);
            sequence.push_back(meta != nullptr ? meta->display_name : std::string{"<unknown>"});
        }
    }
    return sequence;
}

// --- Tests -------------------------------------------------------------------

void test_reference_host_runs_capability_workflow() {
    ReferenceHost host;
    WorkflowRuntimeConfig config;
    config.native_host_binding = make_reference_binding(host);

    // WorkflowRuntime holds the Program by const reference, so it must outlive
    // the runtime -- bind it to a named local, never a temporary.
    const Program program = make_capability_workflow();
    WorkflowRuntime runtime(program, std::move(config));
    const auto result = runtime.run("CapabilityReturnWorkflow", make_none());

    check(result.status() == WorkflowStatus::Completed, "ref_host.completed");
    check(!result.has_errors(), "ref_host.no_errors");
    const auto *out = result.output();
    const auto *b = out != nullptr ? std::get_if<BoolValue>(&out->node) : nullptr;
    check(b != nullptr && b->value, "ref_host.output_true");
    check(host.invoke_calls == 1, "ref_host.invoked_once");
}

void test_native_and_direct_invoker_are_equivalent() {
    // Path A: native binding (reference host over the ahfl_host.h ABI).
    ReferenceHost host;
    WorkflowRuntimeConfig native_config;
    native_config.native_host_binding = make_reference_binding(host);
    const Program native_program = make_capability_workflow();
    WorkflowRuntime native_runtime(native_program, std::move(native_config));
    const auto native = native_runtime.run("CapabilityReturnWorkflow", make_none());

    // Path B: directly-wired invoker returning the same value, no ABI frame.
    WorkflowRuntimeConfig direct_config;
    direct_config.capability_invoker =
        [](const std::string &name, const std::vector<Value> & /*args*/) -> CapabilityCallResult {
        if (name == "is_ready") {
            return CapabilityCallResult{.status = CapabilityCallStatus::Success,
                                        .value = make_bool(true),
                                        .error_message = {},
                                        .attempts = 1};
        }
        return CapabilityCallResult{.status = CapabilityCallStatus::Error,
                                    .value = {},
                                    .error_message = "unexpected capability",
                                    .attempts = 1};
    };
    const Program direct_program = make_capability_workflow();
    WorkflowRuntime direct_runtime(direct_program, std::move(direct_config));
    const auto direct = direct_runtime.run("CapabilityReturnWorkflow", make_none());

    // L1 equivalence: same status.
    check(native.status() == direct.status(), "equiv.same_status");
    check(native.status() == WorkflowStatus::Completed, "equiv.both_completed");

    // L1 equivalence: byte-identical serialized output (uses the deterministic
    // value_json from prereqs 1a/1b).
    const auto *native_out = native.output();
    const auto *direct_out = direct.output();
    check(native_out != nullptr && direct_out != nullptr, "equiv.both_have_output");
    if (native_out != nullptr && direct_out != nullptr) {
        check(value_to_json(*native_out) == value_to_json(*direct_out), "equiv.same_output_bytes");
    }

    // L2 equivalence: same capability-invocation sequence.
    check(capability_sequence(native) == capability_sequence(direct), "equiv.same_l2_sequence");
    check(capability_sequence(native) == std::vector<std::string>{"is_ready"}, "equiv.l2_is_ready");
}

void test_unbound_capability_fails_closed() {
    // A reference host that returns ERROR for everything (nothing bound).
    struct UnboundHost {};
    UnboundHost unbound;
    NativeHostBinding binding;
    binding.host = reinterpret_cast<ahfl_host *>(&unbound);
    binding.invoke = [](ahfl_host * /*h*/, ahfl_invoke_args *args) -> ahfl_cap_status {
        *args->result_ptr = nullptr;
        *args->result_len = 0;
        return AHFL_CAP_ERROR;
    };
    binding.alloc = &ref_alloc;
    binding.dealloc = &ref_dealloc;

    WorkflowRuntimeConfig config;
    config.native_host_binding = binding;
    const Program program = make_capability_workflow();
    WorkflowRuntime runtime(program, std::move(config));
    const auto result = runtime.run("CapabilityReturnWorkflow", make_none());

    // Fail-closed: the workflow must NOT complete successfully.
    check(result.status() != WorkflowStatus::Completed || result.has_errors(),
          "unbound.fails_closed");
}

// A single-node workflow whose node input is `{ready: is_ready()}`; the echo
// agent returns input.ready, so the workflow output IS the capability result.
// The capability lives in a NODE dispatch site, so a PENDING there is durably
// resumable (unlike a return-value expression, which has no node identity).
Program make_node_capability_workflow() {
    Program program;

    // RFC 0026 C2b stage3 (Option B): declare `is_ready` (Bool) with a stable
    // SymbolId so the durable-resume trust boundary can resolve + identity-check it.
    CapabilityDecl is_ready_decl;
    is_ready_decl.name = "is_ready";
    is_ready_decl.return_type_ref = TypeRef{.kind = TypeRefKind::Bool};
    is_ready_decl.symbol_ref = SymbolRef{.kind = SymbolRefKind::Capability,
                                         .canonical_name = "is_ready",
                                         .local_name = "is_ready",
                                         .id = 61};
    program.declarations.push_back(std::move(is_ready_decl));

    AgentDecl agent;
    agent.name = "EchoAgent";
    agent.symbol_ref = SymbolRef{.kind = SymbolRefKind::Agent, .canonical_name = "EchoAgent",
                                 .local_name = "EchoAgent"};
    agent.input_type_ref = named_type("EchoInput");
    agent.context_type_ref = named_type("EchoCtx");
    agent.output_type_ref = named_type("EchoOutput");
    agent.states = {"Init", "Done"};
    agent.initial_state = "Init";
    agent.final_states = {"Done"};
    agent.transitions = {{"Init", "Done"}};
    program.declarations.push_back(std::move(agent));

    FlowDecl flow;
    flow.target_ref = SymbolRef{.kind = SymbolRefKind::Agent, .canonical_name = "EchoAgent",
                                .local_name = "EchoAgent"};
    StateHandler init_handler;
    init_handler.state_name = "Init";
    init_handler.body.statements.push_back(
        std::make_unique<Statement>(Statement{GotoStatement{"Done"}, {}}));
    flow.state_handlers.push_back(std::move(init_handler));
    StateHandler done_handler;
    done_handler.state_name = "Done";
    PathExpr ready_path;
    ready_path.path.root_kind = PathRootKind::Input;
    ready_path.path.root_name = "input";
    ready_path.path.members = {"ready"};
    done_handler.body.statements.push_back(
        std::make_unique<Statement>(Statement{ReturnStatement{expr(std::move(ready_path))}, {}}));
    flow.state_handlers.push_back(std::move(done_handler));
    program.declarations.push_back(std::move(flow));

    WorkflowDecl workflow;
    workflow.name = "NodePendingWorkflow";
    workflow.input_type_ref = named_type("Input");
    workflow.output_type_ref = named_type("Output");
    WorkflowNode node;
    node.name = "n";
    node.target_ref = SymbolRef{.kind = SymbolRefKind::Agent, .canonical_name = "EchoAgent",
                                .local_name = "EchoAgent"};
    StructLiteralExpr node_input;
    node_input.type_name = "EchoInput";
    CallExpr ready_call;
    ready_call.callee = "is_ready";
    ready_call.callee_ref = SymbolRef{.kind = SymbolRefKind::Capability,
                                      .canonical_name = "is_ready",
                                      .local_name = "is_ready",
                                      .id = 61};
    node_input.fields.push_back(StructFieldInit{.name = "ready", .value = expr(std::move(ready_call))});
    node.input = expr(std::move(node_input));
    workflow.nodes.push_back(std::move(node));
    PathExpr return_path;
    return_path.path.root_kind = PathRootKind::Identifier;
    return_path.path.root_name = "n";
    workflow.return_value = expr(std::move(return_path));
    program.declarations.push_back(std::move(workflow));
    return program;
}

// A host that returns PENDING for the capability (the async / "await an LLM"
// case), transferring frame ownership per ahfl_host.h.
struct PendingHost {
    int invoke_calls{0};
};
ahfl_cap_status pending_invoke(ahfl_host *host, ahfl_invoke_args *args) {
    reinterpret_cast<PendingHost *>(host)->invoke_calls++;
    *args->result_ptr = nullptr;
    *args->result_len = 0;
    return AHFL_CAP_PENDING;
}

// RFC 0022 through the native ahfl_host.h ABI: a host returning AHFL_CAP_PENDING
// suspends the workflow (Suspended + resume record), and a fresh runtime resumes
// from the persisted record to a deterministic final. The ABI-level analog of
// the durable-resume capstone.
void test_native_pending_suspends_and_resumes() {
    const Program program = make_node_capability_workflow();

    // Process A: the native host returns PENDING -> the workflow suspends.
    PendingHost host;
    NativeHostBinding binding;
    binding.host = reinterpret_cast<ahfl_host *>(&host);
    binding.invoke = &pending_invoke;
    binding.alloc = &ref_alloc;
    binding.dealloc = &ref_dealloc;
    WorkflowRuntimeConfig suspend_config;
    suspend_config.native_host_binding = binding;
    WorkflowRuntime suspend_runtime(program, std::move(suspend_config));
    auto suspended = suspend_runtime.run("NodePendingWorkflow", make_none());

    check(suspended.status() == WorkflowStatus::Suspended, "native_pending.suspended");
    check(host.invoke_calls == 1, "native_pending.host_called_once");
    check(suspended.suspended.has_value(), "native_pending.resume_record");

    // Process B (cold start): a fresh runtime resumes from the snapshot with the
    // injected result. The native host must NOT be invoked again.
    PendingHost resume_host;
    NativeHostBinding resume_binding;
    resume_binding.host = reinterpret_cast<ahfl_host *>(&resume_host);
    resume_binding.invoke = &pending_invoke; // would re-suspend if wrongly called
    resume_binding.alloc = &ref_alloc;
    resume_binding.dealloc = &ref_dealloc;
    WorkflowRuntimeConfig resume_config;
    resume_config.native_host_binding = resume_binding;
    resume_config.recovery_snapshot = std::move(suspended.suspended);
    resume_config.resume_pending_result = make_bool(true);
    WorkflowRuntime resume_runtime(program, std::move(resume_config));
    auto resumed = resume_runtime.run("NodePendingWorkflow", make_none());

    check(resumed.status() == WorkflowStatus::Completed, "native_pending.resumed_completed");
    check(resume_host.invoke_calls == 0, "native_pending.not_reinvoked_on_resume");
    const auto *out =
        resumed.output() != nullptr ? std::get_if<BoolValue>(&resumed.output()->node) : nullptr;
    check(out != nullptr && out->value, "native_pending.deterministic_final");
}

// =========================================================================
// RFC 0026 E4-B1 C3: the runtime wire-schema module inspector.
//
// A TEST-ONLY canonical Wasm module builder. It is NOT a production authority or
// helper: it only assembles the byte framing a conforming C2 writer would emit,
// so each negative case can perturb exactly ONE gate at a known offset/section.
// The real admission authority remains the inspector + the C1 decoder + the typed
// factory; this builder never validates anything.
// =========================================================================
namespace c3 {

using ahfl::ir::core::CoreCapabilityId;
using ahfl::ir::core::CoreLowerDiagnostic;
using ahfl::ir::core::CoreWireCapabilitySchema;
using ahfl::ir::core::CoreWireRootKind;
using ahfl::ir::core::CoreWireSchemaInt;
using ahfl::ir::core::CoreWireSchemaNode;
using ahfl::ir::core::CoreWireSchemaNodeId;
using ahfl::ir::core::CoreWireSchemaTable;
using ahfl::runtime::core_wasm_schema::CoreWasmWireBindingResult;
using ahfl::runtime::core_wasm_schema::make_wire_binding_from_core_wasm;

// Append a canonical unsigned LEB128 encoding of `value`.
void put_uleb(std::vector<std::uint8_t> &out, std::uint64_t value) {
    do {
        auto byte = static_cast<std::uint8_t>(value & 0x7fu);
        value >>= 7u;
        if (value != 0) {
            byte |= 0x80u;
        }
        out.push_back(byte);
    } while (value != 0);
}

// Append a section = id + canonical size + payload.
void put_section(std::vector<std::uint8_t> &out, std::uint8_t id,
                 const std::vector<std::uint8_t> &payload) {
    out.push_back(id);
    put_uleb(out, payload.size());
    out.insert(out.end(), payload.begin(), payload.end());
}

// One func type entry: form 0x60 + params + results (value-type byte arrays).
std::vector<std::uint8_t> func_type(const std::vector<std::uint8_t> &params,
                                    const std::vector<std::uint8_t> &results) {
    std::vector<std::uint8_t> out;
    out.push_back(0x60);
    put_uleb(out, params.size());
    out.insert(out.end(), params.begin(), params.end());
    put_uleb(out, results.size());
    out.insert(out.end(), results.begin(), results.end());
    return out;
}

// The canonical ahfl_cap tuple signature (i32,i32)->(i32,i32,i32).
std::vector<std::uint8_t> capability_tuple_type() {
    return func_type({0x7f, 0x7f}, {0x7f, 0x7f, 0x7f});
}

// WH-2: the bridge ahfl_cap signature (i32)->(i32,i32). The transport admission
// accepts this alongside the opaque 3-result tuple (decision doc section 11.3).
std::vector<std::uint8_t> bridge_signature_type() {
    return func_type({0x7f}, {0x7f, 0x7f});
}

// Encode a real wire-schema table to its canonical payload via the C1 encoder, so
// the wrapped module carries genuine `AHFLWS...` bytes (no hand-rolled table).
std::vector<std::uint8_t> encode_table(const CoreWireSchemaTable &table) {
    auto encoded = ahfl::ir::core::encode_core_wire_schema_table(table);
    if (!encoded.ok() || !encoded.bytes.has_value()) {
        return {};
    }
    return *encoded.bytes;
}

// A one-capability table whose Result root is a bare Int, source_symbol = 42.
CoreWireSchemaTable single_int_table(std::uint64_t source_symbol = 42) {
    CoreWireSchemaTable table;
    table.nodes.push_back(CoreWireSchemaNode{CoreWireSchemaInt{}});
    CoreWireCapabilitySchema cap;
    cap.capability = CoreCapabilityId{0};
    cap.source_symbol = source_symbol;
    cap.result = CoreWireSchemaNodeId{0};
    table.capabilities.push_back(cap);
    return table;
}

// Knobs for the canonical module builder. Each negative test flips exactly one.
struct ModuleSpec {
    // Type section: the func-type entries, and whether to emit the section at all.
    std::vector<std::vector<std::uint8_t>> types = {capability_tuple_type()};
    bool emit_type = true;
    bool duplicate_type = false;
    // Import section: (source_symbol, typeidx) per ahfl_cap import; emit toggle.
    std::vector<std::pair<std::uint64_t, std::uint32_t>> imports = {{42, 0}};
    bool emit_import = true;
    bool duplicate_import = false;
    bool type_after_import = false; // emit Import before Type (order violation)
    // The wire-schema table encoded into the target custom section (empty = none).
    std::optional<std::vector<std::uint8_t>> target_table = std::nullopt;
    std::string target_name = "ahfl.wire-schema.v1";
    bool duplicate_target = false;
    bool section_after_target = false; // append a nonzero section AFTER the target
    // Unknown non-target custom sections emitted BEFORE the target.
    std::vector<std::string> unknown_customs_before_target;
    // An extra bounded nonzero standard section the writer never emits (DataCount,
    // id 12) to prove the reader generically size-skips it.
    bool emit_datacount_skip = false;
};

std::vector<std::uint8_t> import_payload(
    const std::vector<std::pair<std::uint64_t, std::uint32_t>> &imports) {
    std::vector<std::uint8_t> payload;
    put_uleb(payload, imports.size());
    for (const auto &[symbol, typeidx] : imports) {
        const std::string module_name = "ahfl_cap";
        const std::string field_name = "cap_" + std::to_string(symbol);
        put_uleb(payload, module_name.size());
        payload.insert(payload.end(), module_name.begin(), module_name.end());
        put_uleb(payload, field_name.size());
        payload.insert(payload.end(), field_name.begin(), field_name.end());
        payload.push_back(0x00); // kind = function
        put_uleb(payload, typeidx);
    }
    return payload;
}

std::vector<std::uint8_t> type_payload(const std::vector<std::vector<std::uint8_t>> &types) {
    std::vector<std::uint8_t> payload;
    put_uleb(payload, types.size());
    for (const auto &entry : types) {
        payload.insert(payload.end(), entry.begin(), entry.end());
    }
    return payload;
}

std::vector<std::uint8_t> custom_payload(const std::string &name,
                                         const std::vector<std::uint8_t> &body) {
    std::vector<std::uint8_t> payload;
    put_uleb(payload, name.size());
    payload.insert(payload.end(), name.begin(), name.end());
    payload.insert(payload.end(), body.begin(), body.end());
    return payload;
}

// Assemble a canonical module per spec. Section ids: Type=1, Import=2,
// DataCount=12, Custom=0.
std::vector<std::uint8_t> build_module(const ModuleSpec &spec) {
    std::vector<std::uint8_t> module{0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00};

    const auto emit_type = [&] {
        if (spec.emit_type) {
            put_section(module, 1, type_payload(spec.types));
            if (spec.duplicate_type) {
                put_section(module, 1, type_payload(spec.types));
            }
        }
    };
    const auto emit_import = [&] {
        if (spec.emit_import) {
            put_section(module, 2, import_payload(spec.imports));
            if (spec.duplicate_import) {
                put_section(module, 2, import_payload(spec.imports));
            }
        }
    };

    if (spec.type_after_import) {
        emit_import();
        emit_type();
    } else {
        emit_type();
        emit_import();
    }

    if (spec.emit_datacount_skip) {
        // A DataCount section (id 12) the C2 writer never emits: one canonical u32
        // body. The reader must size-skip it without interpreting it.
        std::vector<std::uint8_t> datacount;
        put_uleb(datacount, 0);
        put_section(module, 12, datacount);
    }

    for (const auto &name : spec.unknown_customs_before_target) {
        put_section(module, 0, custom_payload(name, {0x01, 0x02, 0x03}));
    }

    if (spec.target_table.has_value()) {
        put_section(module, 0, custom_payload(spec.target_name, *spec.target_table));
        if (spec.duplicate_target) {
            put_section(module, 0, custom_payload(spec.target_name, *spec.target_table));
        }
        if (spec.section_after_target) {
            std::vector<std::uint8_t> datacount;
            put_uleb(datacount, 0);
            put_section(module, 12, datacount);
        }
    }
    return module;
}

// A conforming module: one ahfl_cap import (symbol 42, typeidx 0 = tuple) + the
// single-Int table in the target custom section at EOF.
std::vector<std::uint8_t> conforming_module() {
    ModuleSpec spec;
    spec.target_table = encode_table(single_int_table());
    return build_module(spec);
}

[[nodiscard]] bool has_error_diag(const std::vector<CoreLowerDiagnostic> &bag) {
    for (const auto &d : bag) {
        if (d.severity == ahfl::ir::core::CoreDiagnosticSeverity::Error) {
            return true;
        }
    }
    return false;
}

// Generic negative: no binding + at least one Error diagnostic.
[[nodiscard]] bool clean_failure(const CoreWasmWireBindingResult &result) {
    return !result.binding.has_value() && !result.ok() && has_error_diag(result.diagnostics);
}

// Targeted negative: a clean failure whose diagnostic bag contains `fragment`,
// so each case is proven to trip the specific gate it targets (and cannot be a
// later gate's generic failure).
[[nodiscard]] bool
fails_with(const CoreWasmWireBindingResult &result, std::string_view fragment) {
    if (!clean_failure(result)) {
        return false;
    }
    for (const auto &d : result.diagnostics) {
        if (d.message.find(fragment) != std::string::npos) {
            return true;
        }
    }
    return false;
}

// A distinctive marker that a genuine no-echo case seeds into the module so the
// full-bag no-echo assertion is not vacuous.
constexpr std::string_view kSecretMarker = "C3_SECRET_MARKER_ZZZ";

[[nodiscard]] bool bag_has_no_marker(const CoreWasmWireBindingResult &result) {
    for (const auto &d : result.diagnostics) {
        if (d.message.find(kSecretMarker) != std::string::npos) {
            return false;
        }
    }
    return true;
}

void test_wire_schema_module_inspector() {
    // --- positive: a conforming module mints the Result binding once. ---------
    {
        const auto module = conforming_module();
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Result, 0);
        check(result.ok() && result.binding.has_value(),
              "c3.positive.conforming_module_mints_result_binding");
        if (result.binding.has_value()) {
            const auto &selector = result.binding->selector();
            check(selector.capability == CoreCapabilityId{0} &&
                      selector.expected_source_symbol == 42 &&
                      selector.kind == CoreWireRootKind::Result && selector.param_index == 0,
                  "c3.positive.selector_identity_from_table");
        }
    }

    // --- positive control: an UNREFERENCED type entry whose param/result bytes
    // are arbitrary/invalid value-type bytes still frames fine (framing-only, no
    // Wasm type validation); only the cap-import-referenced entry must be the
    // exact tuple. typeidx 0 (tuple) is referenced; typeidx 1 carries garbage
    // value-type bytes and is never referenced. ------------------------------
    {
        ModuleSpec spec;
        spec.types = {capability_tuple_type(),
                      func_type({0x01 /* not a valid value type */}, {0xfe})};
        spec.imports = {{42, 0}};
        spec.target_table = encode_table(single_int_table());
        const auto module = build_module(spec);
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Result, 0);
        check(result.ok(),
              "c3.positive.unreferenced_type_arbitrary_value_bytes_ok");
    }

    // --- positive control: the tuple signature at a NON-zero index (index 1);
    // the reader accepts any in-range typeidx whose signature is the exact tuple,
    // never hardcoding an index. ---------------------------------------------
    {
        ModuleSpec spec;
        spec.types = {func_type({0x7f}, {0x7f}) /* i32->i32 filler */,
                      capability_tuple_type() /* index 1 = tuple */};
        spec.imports = {{42, 1}};
        spec.target_table = encode_table(single_int_table());
        const auto module = build_module(spec);
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Result, 0);
        check(result.ok(), "c3.positive.tuple_signature_at_nonzero_typeidx");
    }

    // --- positive control: unknown non-target customs before the target + a
    // DataCount (id 12) section the writer never emits are both size-skipped. ---
    {
        ModuleSpec spec;
        spec.unknown_customs_before_target = {"name", "producers"};
        spec.emit_datacount_skip = true;
        spec.target_table = encode_table(single_int_table());
        const auto module = build_module(spec);
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Result, 0);
        check(result.ok(),
              "c3.positive.unknown_customs_and_datacount_section_skipped");
    }

    // --- negative: bad module header (magic). --------------------------------
    {
        std::vector<std::uint8_t> module = conforming_module();
        module[0] = 0x01;
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Result, 0);
        check(fails_with(result, "header is not wasm v1"), "c3.negative.bad_magic");
    }

    // --- negative: bad module version (byte 4). ------------------------------
    {
        std::vector<std::uint8_t> module = conforming_module();
        module[4] = 0x02; // version 2
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Result, 0);
        check(fails_with(result, "header is not wasm v1"), "c3.negative.bad_version");
    }

    // --- negative: section size LEB is non-canonical (overlong). -------------
    {
        std::vector<std::uint8_t> module{0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00};
        module.push_back(1);            // Type section id
        module.push_back(0x81);         // overlong size LEB for value 1
        module.push_back(0x00);
        module.push_back(0x00);         // 1 byte of "payload"
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Result, 0);
        check(fails_with(result, "section size is not a canonical u32"),
              "c3.negative.section_size_noncanonical");
    }

    // --- negative: section size overflows the 32-bit domain. -----------------
    {
        std::vector<std::uint8_t> module{0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00};
        module.push_back(1); // Type section id
        // Five 0xFF groups + 0x0F would exceed 32 bits; use 0xFF*5 (>32-bit shift).
        module.insert(module.end(), {0xff, 0xff, 0xff, 0xff, 0xff, 0x0f});
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Result, 0);
        check(fails_with(result, "section size is not a canonical u32"),
              "c3.negative.section_size_overflow");
    }

    // --- negative: section size exceeds remaining bytes. ---------------------
    {
        std::vector<std::uint8_t> module{0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00};
        module.push_back(1);   // Type section id
        put_uleb(module, 100); // claims 100 payload bytes
        module.push_back(0x00);
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Result, 0);
        check(fails_with(result, "section exceeds its bounds"),
              "c3.negative.section_size_exceeds_remaining");
    }

    // --- negative: a stray byte after the target trips the single target-not-EOF
    // gate (checked before the next section header is read). ------------------
    {
        std::vector<std::uint8_t> module = conforming_module();
        module.push_back(1); // any trailing byte after the target section
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Result, 0);
        check(fails_with(result, "a section follows the wire-schema custom section"),
              "c3.negative.trailing_after_target");
    }

    // --- negative: a section-size LEB that is truncated (id + 0x80 + EOF) in a
    // module with NO target yet, so the canonical-u32 size gate is what trips. --
    {
        std::vector<std::uint8_t> module{0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00};
        module.push_back(1);    // Type section id
        module.push_back(0x80); // size LEB continuation bit set, then EOF
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Result, 0);
        check(fails_with(result, "section size is not a canonical u32"),
              "c3.negative.section_size_truncated_leb");
    }

    // --- NEGATIVE control for typeidx authority: entries 0..3 fillers, index 4
    // is a WRONG tuple (result arity 1), and the import references typeidx 4. The
    // failure is the signature gate, not OOR — the true counterpart to the
    // typeidx-1 exact-tuple positive control. --------------------------------
    {
        ModuleSpec spec;
        spec.types = {func_type({0x7f}, {0x7f}), func_type({0x7f}, {0x7f}),
                      func_type({0x7f}, {0x7f}), func_type({0x7f}, {0x7f}),
                      func_type({0x7f, 0x7f}, {0x7f}) /* index 4: wrong result arity */};
        spec.imports = {{42, 4}};
        spec.target_table = encode_table(single_int_table());
        const auto module = build_module(spec);
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Result, 0);
        check(fails_with(result, "does not use the ahfl_cap tuple or bridge signature"),
              "c3.negative.typeidx4_wrong_signature");
    }

    // --- WH-2 positive: the bridge (i32)->(i32,i32) functype is accepted
    // alongside the opaque 3-result tuple (decision doc section 11.3). --------
    {
        ModuleSpec spec;
        spec.types = {bridge_signature_type()};
        spec.imports = {{42, 0}};
        spec.target_table = encode_table(single_int_table());
        const auto module = build_module(spec);
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Result, 0);
        check(result.ok(), "c3.positive.bridge_functype_accepted");
    }

    // --- WH-2 negative: a functype that is NEITHER the opaque tuple NOR the
    // bridge signature (e.g. (i32)->(i32), one result) is rejected. ----------
    {
        ModuleSpec spec;
        spec.types = {func_type({0x7f}, {0x7f}) /* i32->i32 */};
        spec.imports = {{42, 0}};
        spec.target_table = encode_table(single_int_table());
        const auto module = build_module(spec);
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Result, 0);
        check(fails_with(result,
                         "does not use the ahfl_cap tuple or bridge signature"),
              "c3.negative.neither_tuple_nor_bridge_rejected");
    }

    // --- negative: non-func type form (0x50 instead of 0x60). ----------------
    {
        ModuleSpec spec;
        spec.types = {std::vector<std::uint8_t>{0x50, 0x00, 0x00}};
        spec.target_table = encode_table(single_int_table());
        const auto module = build_module(spec);
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Result, 0);
        check(fails_with(result, "Type section is malformed"),
              "c3.negative.non_func_type_form");
    }

    // --- resource: Type count = UINT32_MAX with a short payload. Must fail with
    // the Type-malformed gate (count > remaining/min-entry) WITHOUT bad_alloc. --
    {
        std::vector<std::uint8_t> module{0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00};
        std::vector<std::uint8_t> type_body;
        put_uleb(type_body, UINT32_MAX);
        type_body.push_back(0x60);
        put_section(module, 1, type_body);
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Result, 0);
        check(fails_with(result, "Type section is malformed"),
              "c3.resource.type_count_uint32max_short_payload");
    }

    // --- resource: Import count = UINT32_MAX with a short payload. -------------
    {
        std::vector<std::uint8_t> module{0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00};
        put_section(module, 1, type_payload({capability_tuple_type()}));
        std::vector<std::uint8_t> import_body;
        put_uleb(import_body, UINT32_MAX);
        import_body.push_back(0x01);
        put_section(module, 2, import_body);
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Result, 0);
        check(fails_with(result, "Import section is malformed"),
              "c3.resource.import_count_uint32max_short_payload");
    }

    // --- negative: custom name length non-canonical (overlong LEB). ----------
    {
        std::vector<std::uint8_t> module{0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00};
        put_section(module, 1, type_payload({capability_tuple_type()}));
        put_section(module, 2, import_payload({{42, 0}}));
        put_section(module, 0, std::vector<std::uint8_t>{0x81, 0x00, 'a'});
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Result, 0);
        check(fails_with(result, "custom section name length is not a canonical u32"),
              "c3.negative.custom_name_length_noncanonical");
    }

    // --- negative: custom name length exceeds its section bounds. -------------
    {
        std::vector<std::uint8_t> module{0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00};
        put_section(module, 1, type_payload({capability_tuple_type()}));
        put_section(module, 2, import_payload({{42, 0}}));
        std::vector<std::uint8_t> body;
        put_uleb(body, 100);
        body.push_back('x');
        put_section(module, 0, body);
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Result, 0);
        check(fails_with(result, "custom section name exceeds its section bounds"),
              "c3.negative.custom_name_length_out_of_bounds");
    }

    // --- negative: a section follows the target (target not at EOF). ----------
    {
        ModuleSpec spec;
        spec.target_table = encode_table(single_int_table());
        spec.section_after_target = true;
        const auto module = build_module(spec);
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Result, 0);
        check(fails_with(result, "a section follows the wire-schema custom section"),
              "c3.negative.section_after_target");
    }

    // --- negative: duplicate target custom sections (second trips target-not-EOF).
    {
        ModuleSpec spec;
        spec.target_table = encode_table(single_int_table());
        spec.duplicate_target = true;
        const auto module = build_module(spec);
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Result, 0);
        check(fails_with(result, "a section follows the wire-schema custom section"),
              "c3.negative.duplicate_target");
    }

    // --- negative: no target custom section at all. ---------------------------
    {
        ModuleSpec spec;
        spec.target_table = std::nullopt;
        const auto module = build_module(spec);
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Result, 0);
        check(fails_with(result, "has no wire-schema custom section"),
              "c3.negative.missing_target");
    }

    // --- negative: target with the WRONG name is treated as a non-target, so the
    // module has no target -> missing-target failure. ------------------------
    {
        ModuleSpec spec;
        spec.target_name = "ahfl.wire-schema.v2"; // not the accepted name
        spec.target_table = encode_table(single_int_table());
        const auto module = build_module(spec);
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Result, 0);
        check(fails_with(result, "has no wire-schema custom section"),
              "c3.negative.target_wrong_name");
    }

    // --- negative: missing Type section. -------------------------------------
    {
        ModuleSpec spec;
        spec.emit_type = false;
        spec.target_table = encode_table(single_int_table());
        const auto module = build_module(spec);
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Result, 0);
        check(fails_with(result, "has no Type section"),
              "c3.negative.missing_type_section");
    }

    // --- negative: duplicate Type section. -----------------------------------
    {
        ModuleSpec spec;
        spec.duplicate_type = true;
        spec.target_table = encode_table(single_int_table());
        const auto module = build_module(spec);
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Result, 0);
        check(fails_with(result, "more than one Type section"),
              "c3.negative.duplicate_type_section");
    }

    // --- negative: Type section after Import (order violation). ---------------
    {
        ModuleSpec spec;
        spec.type_after_import = true;
        spec.target_table = encode_table(single_int_table());
        const auto module = build_module(spec);
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Result, 0);
        check(fails_with(result, "Type section follows its Import section"),
              "c3.negative.type_after_import");
    }

    // --- negative: missing Import section (with a target present). ------------
    {
        ModuleSpec spec;
        spec.emit_import = false;
        spec.target_table = encode_table(single_int_table());
        const auto module = build_module(spec);
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Result, 0);
        check(fails_with(result, "has no capability Import section"),
              "c3.negative.missing_import_section");
    }

    // --- negative: duplicate Import section. ----------------------------------
    {
        ModuleSpec spec;
        spec.duplicate_import = true;
        spec.target_table = encode_table(single_int_table());
        const auto module = build_module(spec);
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Result, 0);
        check(fails_with(result, "more than one Import section"),
              "c3.negative.duplicate_import_section");
    }

    // --- negative: an import whose module name is not ahfl_cap. ---------------
    {
        std::vector<std::uint8_t> module{0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00};
        put_section(module, 1, type_payload({capability_tuple_type()}));
        std::vector<std::uint8_t> import_body;
        put_uleb(import_body, 1);
        const std::string module_name = "wasi_snapshot"; // not ahfl_cap
        const std::string field_name = "cap_42";
        put_uleb(import_body, module_name.size());
        import_body.insert(import_body.end(), module_name.begin(), module_name.end());
        put_uleb(import_body, field_name.size());
        import_body.insert(import_body.end(), field_name.begin(), field_name.end());
        import_body.push_back(0x00);
        put_uleb(import_body, 0);
        put_section(module, 2, import_body);
        put_section(module, 0, custom_payload("ahfl.wire-schema.v1",
                                              encode_table(single_int_table(42))));
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Result, 0);
        check(fails_with(result, "Import section is malformed"),
              "c3.negative.non_ahfl_cap_import");
    }

    // --- negative: an import with a non-function kind (global, 0x03). ---------
    {
        std::vector<std::uint8_t> module{0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00};
        put_section(module, 1, type_payload({capability_tuple_type()}));
        std::vector<std::uint8_t> import_body;
        put_uleb(import_body, 1);
        const std::string module_name = "ahfl_cap";
        const std::string field_name = "cap_42";
        put_uleb(import_body, module_name.size());
        import_body.insert(import_body.end(), module_name.begin(), module_name.end());
        put_uleb(import_body, field_name.size());
        import_body.insert(import_body.end(), field_name.begin(), field_name.end());
        import_body.push_back(0x03); // global import kind
        put_uleb(import_body, 0);
        put_section(module, 2, import_body);
        put_section(module, 0, custom_payload("ahfl.wire-schema.v1",
                                              encode_table(single_int_table(42))));
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Result, 0);
        check(fails_with(result, "Import section is malformed"),
              "c3.negative.import_wrong_kind");
    }

    // --- negative: no capability imports but a target present (explicit gate). --
    {
        ModuleSpec spec;
        spec.imports = {};
        spec.target_table = encode_table(single_int_table());
        const auto module = build_module(spec);
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Result, 0);
        check(fails_with(result, "wire-schema section but no capability imports"),
              "c3.negative.no_cap_import_with_target");
    }

    // --- negative: nonempty imports but a locally-valid EMPTY-capability table.
    // This is the SECOND, non-XOR branch of the no-cap gate: the import table is
    // present but the decoded schema table carries no capability. ------------
    {
        CoreWireSchemaTable empty_table; // 0 nodes, 0 capabilities: locally valid
        ModuleSpec spec;
        spec.imports = {{42, 0}};
        spec.target_table = encode_table(empty_table);
        const auto module = build_module(spec);
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Result, 0);
        check(fails_with(result, "table carries no capability"),
              "c3.negative.table_empty_capabilities_with_import");
    }

    // --- negative: import count mismatches the table (two imports, one-cap table).
    {
        ModuleSpec spec;
        spec.imports = {{42, 0}, {43, 0}};
        spec.target_table = encode_table(single_int_table(42));
        const auto module = build_module(spec);
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Result, 0);
        check(fails_with(result, "import count does not match the wire-schema table"),
              "c3.negative.import_count_mismatch");
    }

    // --- negative: import source_symbol mismatches the table (count matches). --
    {
        ModuleSpec spec;
        spec.imports = {{999, 0}};
        spec.target_table = encode_table(single_int_table(42));
        const auto module = build_module(spec);
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Result, 0);
        check(fails_with(result, "import source symbol does not match the wire-schema table"),
              "c3.negative.import_source_symbol_mismatch");
    }

    // --- negative: import references a typeidx that is out of range. ----------
    {
        ModuleSpec spec;
        spec.imports = {{42, 5}};
        spec.target_table = encode_table(single_int_table());
        const auto module = build_module(spec);
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Result, 0);
        check(fails_with(result, "does not use the ahfl_cap tuple or bridge signature"),
              "c3.negative.import_typeidx_out_of_range");
    }

    // --- negative: non-canonical cap_ decimal (leading zero). ----------------
    {
        std::vector<std::uint8_t> module{0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00};
        put_section(module, 1, type_payload({capability_tuple_type()}));
        std::vector<std::uint8_t> import_body;
        put_uleb(import_body, 1);
        const std::string module_name = "ahfl_cap";
        const std::string field_name = "cap_042"; // leading zero
        put_uleb(import_body, module_name.size());
        import_body.insert(import_body.end(), module_name.begin(), module_name.end());
        put_uleb(import_body, field_name.size());
        import_body.insert(import_body.end(), field_name.begin(), field_name.end());
        import_body.push_back(0x00);
        put_uleb(import_body, 0);
        put_section(module, 2, import_body);
        put_section(module, 0, custom_payload("ahfl.wire-schema.v1",
                                              encode_table(single_int_table(42))));
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Result, 0);
        check(fails_with(result, "Import section is malformed"),
              "c3.negative.cap_field_leading_zero");
    }

    // --- uint64 cap-field boundary contract: 0 and UINT64_MAX are canonical and
    // mint; a value one past UINT64_MAX is rejected by the overflow branch. -----
    {
        for (const std::uint64_t symbol : {std::uint64_t{0}, UINT64_MAX}) {
            ModuleSpec spec;
            spec.imports = {{symbol, 0}};
            spec.target_table = encode_table(single_int_table(symbol));
            const auto module = build_module(spec);
            const auto result = make_wire_binding_from_core_wasm(
                module, 0, CoreWireRootKind::Result, 0);
            check(result.ok() && result.binding.has_value() &&
                      result.binding->selector().expected_source_symbol == symbol,
                  symbol == 0 ? "c3.positive.cap_field_zero_mints"
                              : "c3.positive.cap_field_uint64max_mints");
        }
    }
    {
        // Raw field cap_18446744073709551616 = UINT64_MAX + 1: the decimal parse
        // overflow branch rejects it; the rest of the module/table is valid.
        std::vector<std::uint8_t> module{0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00};
        put_section(module, 1, type_payload({capability_tuple_type()}));
        std::vector<std::uint8_t> import_body;
        put_uleb(import_body, 1);
        const std::string module_name = "ahfl_cap";
        const std::string field_name = "cap_18446744073709551616"; // 2^64
        put_uleb(import_body, module_name.size());
        import_body.insert(import_body.end(), module_name.begin(), module_name.end());
        put_uleb(import_body, field_name.size());
        import_body.insert(import_body.end(), field_name.begin(), field_name.end());
        import_body.push_back(0x00);
        put_uleb(import_body, 0);
        put_section(module, 2, import_body);
        put_section(module, 0, custom_payload("ahfl.wire-schema.v1",
                                              encode_table(single_int_table(42))));
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Result, 0);
        check(fails_with(result, "Import section is malformed"),
              "c3.negative.cap_field_uint64_overflow");
    }

    // --- two-cap: sparse ids + swapped source symbols. Request ordinal 0, which
    // MATCHES at ordinal 0, but ordinal 1 disagrees, proving the FULL table is
    // cross-checked before the requested ordinal is honored. ------------------
    {
        CoreWireSchemaTable table;
        table.nodes.push_back(CoreWireSchemaNode{CoreWireSchemaInt{}});
        CoreWireCapabilitySchema a;
        a.capability = CoreCapabilityId{3};
        a.source_symbol = 100;
        a.result = CoreWireSchemaNodeId{0};
        CoreWireCapabilitySchema b;
        b.capability = CoreCapabilityId{7};
        b.source_symbol = 200;
        b.result = CoreWireSchemaNodeId{0};
        table.capabilities.push_back(a);
        table.capabilities.push_back(b);

        ModuleSpec spec;
        spec.types = {capability_tuple_type()};
        spec.imports = {{100, 0}, {999, 0}}; // ordinal 1 SWAPPED (999 != 200)
        spec.target_table = encode_table(table);
        const auto module = build_module(spec);
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Result, 0);
        check(fails_with(result, "import source symbol does not match the wire-schema table"),
              "c3.negative.two_cap_swapped_ordinal1_fails_requesting_ordinal0");

        ModuleSpec ok_spec;
        ok_spec.types = {capability_tuple_type()};
        ok_spec.imports = {{100, 0}, {200, 0}};
        ok_spec.target_table = encode_table(table);
        const auto ok_module = build_module(ok_spec);
        const auto ok_result = make_wire_binding_from_core_wasm(
            ok_module, 0, CoreWireRootKind::Result, 0);
        check(ok_result.ok() && ok_result.binding.has_value() &&
                  ok_result.binding->selector().capability == CoreCapabilityId{3} &&
                  ok_result.binding->selector().expected_source_symbol == 100,
              "c3.positive.two_cap_sparse_ids_ordinal0_binds_cap3");
    }

    // --- negative: requested ordinal out of range (single-cap table). ---------
    {
        const auto module = conforming_module();
        const auto result = make_wire_binding_from_core_wasm(
            module, 5, CoreWireRootKind::Result, 0);
        check(fails_with(result, "requested capability import ordinal is out of range"),
              "c3.negative.requested_ordinal_out_of_range");
    }

    // --- negative: Result selector with a nonzero param_index (factory SSOT). --
    {
        const auto module = conforming_module();
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Result, 3);
        check(fails_with(result, "Result selector must carry param_index 0"),
              "c3.negative.result_selector_nonzero_param");
    }

    // --- negative: Param selector with an out-of-range index (no params here). -
    {
        const auto module = conforming_module();
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Param, 0);
        check(fails_with(result, "Param selector index is out of range"),
              "c3.negative.param_selector_out_of_range");
    }

    // --- negative: an invalid root_kind value reaches the factory SSOT and is
    // rejected there (the inspector forwards the caller's kind unchanged). ------
    {
        const auto module = conforming_module();
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, static_cast<CoreWireRootKind>(0xff), 0);
        check(fails_with(result, "selector kind is invalid"),
              "c3.negative.invalid_root_kind");
    }

    // --- C1 tamper attribution: a single flipped byte inside the encoded table
    // payload must surface as a C3 failure (the C1 decoder rejects it). We do NOT
    // re-test C1's whole matrix here — one integration case proves propagation. --
    {
        ModuleSpec spec;
        auto table_bytes = encode_table(single_int_table());
        if (!table_bytes.empty()) {
            table_bytes.back() ^= 0xff;
        }
        spec.target_table = table_bytes;
        const auto module = build_module(spec);
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Result, 0);
        check(clean_failure(result), "c3.negative.c1_table_tamper_attribution");
    }

    // --- no-echo (non-vacuous): a module whose sole ahfl_cap import field carries
    // the secret marker (as a non-canonical cap field) parses through the module
    // name check, fails at the cap-field decode, and must NOT echo the marker into
    // any diagnostic. The marker is genuinely present in the input bytes. -------
    {
        std::vector<std::uint8_t> module{0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00};
        put_section(module, 1, type_payload({capability_tuple_type()}));
        std::vector<std::uint8_t> import_body;
        put_uleb(import_body, 1);
        const std::string module_name = "ahfl_cap";
        const std::string field_name = "cap_" + std::string(kSecretMarker); // non-numeric
        put_uleb(import_body, module_name.size());
        import_body.insert(import_body.end(), module_name.begin(), module_name.end());
        put_uleb(import_body, field_name.size());
        import_body.insert(import_body.end(), field_name.begin(), field_name.end());
        import_body.push_back(0x00);
        put_uleb(import_body, 0);
        put_section(module, 2, import_body);
        put_section(module, 0, custom_payload("ahfl.wire-schema.v1",
                                              encode_table(single_int_table(42))));
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Result, 0);
        check(clean_failure(result) && bag_has_no_marker(result),
              "c3.negative.import_field_marker_no_echo");
    }
}

} // namespace c3

} // namespace

int main() {
    test_reference_host_runs_capability_workflow();
    test_native_and_direct_invoker_are_equivalent();
    test_unbound_capability_fails_closed();
    test_native_pending_suspends_and_resumes();
    c3::test_wire_schema_module_inspector();

    std::cout << pass_count << "/" << test_count << " tests passed\n";
    return (pass_count == test_count) ? EXIT_SUCCESS : EXIT_FAILURE;
}
