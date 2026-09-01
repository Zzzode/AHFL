// native_wasm_differential.cpp — RFC 0021 slice 5: reference host + binding
// equivalence.
//
// SCOPE (RFC 0019/0021): AHFL emits WAT but ships no WASM VM, so a differential
// against an *executed* WASM module is out of scope here. What this test proves
// is the achievable half:
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
//
// The executed-WASM differential and the PENDING/resume path are tracked
// separately (a WASM runtime is unscoped; resume is RFC 0022).

#include "runtime/engine/native_host_binding.hpp"
#include "runtime/engine/workflow_recovery.hpp"
#include "runtime/engine/workflow_runtime.hpp"
#include "runtime/evaluator/value.hpp"
#include "runtime/evaluator/value_json.hpp"

#include "ahfl/compiler/ir/ir.hpp"

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

} // namespace

int main() {
    test_reference_host_runs_capability_workflow();
    test_native_and_direct_invoker_are_equivalent();
    test_unbound_capability_fails_closed();
    test_native_pending_suspends_and_resumes();

    std::cout << pass_count << "/" << test_count << " tests passed\n";
    return (pass_count == test_count) ? EXIT_SUCCESS : EXIT_FAILURE;
}
