// RFC 0026 KR6.8 WH-3: unit tests for the capability-import executor
// (src/runtime/wasm_host/capability_import.cpp). The executor is the production
// ImportCallback the engine delivers every ahfl_cap import to; this TU drives
// it DIRECTLY (hand-built ImportObservation values) against a real wasm3 engine
// session, proving both lanes end-to-end:
//
//   * opaque  (i32,i32)->(i32,i32,i32): the host decodes the wire-JSON argument
//     envelope, invokes the capability through the ContextualCapabilityInvoker,
//     serializes the result to wire JSON, alloc_then_writes it, and replies
//     (status, result_ptr, result_len). The module's compiled code has graceful
//     ERROR/PENDING arms.
//   * bridge (i32)->(i32,i32): the host walks the dense P4-D argument spans
//     (spill/root classification + region authorization), invokes, packs the
//     result at the call site's disjoint result placement, and replies
//     (status, result_base). Any non-zero status traps the module.
//
// ImportAbort stays HOST-DECISION-failure-only: a capability that executed but
// failed/pending is a raw non-OK reply, never an abort.

#include "runtime/wasm_host/capability_import.hpp"
#include "runtime/wasm_host/wasm3_engine.hpp"

#include "unit/runtime/wasm_host/wasm_host_test_support.hpp"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace eng = ahfl::runtime::core_wasm_resume_engine;
namespace wht = ahfl::runtime::wasm_host_test_support;
namespace csm = ahfl::runtime::core_wasm_schema_module;
namespace wh = ahfl::runtime::wasm_host;
namespace ir = ahfl::ir;
namespace runtime = ahfl::runtime;

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

// Write a little-endian u32 into a byte buffer.
void put_u32_le(std::span<std::uint8_t> buf, std::size_t off, std::uint32_t v) {
    buf[off] = static_cast<std::uint8_t>(v & 0xFF);
    buf[off + 1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
    buf[off + 2] = static_cast<std::uint8_t>((v >> 16) & 0xFF);
    buf[off + 3] = static_cast<std::uint8_t>((v >> 24) & 0xFF);
}

// Write a little-endian i32 into a byte buffer.
void put_i32_le(std::span<std::uint8_t> buf, std::size_t off, std::int32_t v) {
    put_u32_le(buf, off, static_cast<std::uint32_t>(v));
}

// A mock invoker that returns a scripted result.
struct ScriptedInvoker {
    runtime::CapabilityCallResult result;
    int call_count{0};
    std::string last_name;
    std::vector<runtime::Value> last_args;

    runtime::ContextualCapabilityInvoker as_invoker() {
        return [this](const runtime::CapabilityInvocationContext &,
                      const std::string &name,
                      const std::vector<runtime::Value> &args) -> runtime::CapabilityCallResult {
            ++call_count;
            last_name = name;
            last_args.clear();
            for (const auto &a : args) {
                last_args.push_back(runtime::clone_value(a));
            }
            return std::move(result);
        };
    }
};

// Build a minimal frame layout section with one bridge call site (Int param,
// Int result). The bridge control block is at kControlBase, the spill window
// at kSpillBase, and the result placement at kResultBase.
constexpr std::uint32_t kControlBase = 1024;
constexpr std::uint32_t kBlockStride = 16; // 8 header + 1 arg descriptor
constexpr std::uint32_t kSpillBase = 2048;
constexpr std::uint32_t kSpillExtent = 8;
constexpr std::uint32_t kResultBase = 4096;
constexpr std::uint32_t kResultExtent = 4;

[[nodiscard]] ir::core::CoreFrameLayoutSection
make_bridge_section(std::uint64_t source_symbol) {
    ir::core::CoreFrameLayoutSection section;

    // Layout table: layout 0 = Int (scalar i32, size 4).
    ir::core::CoreLayout int_layout;
    int_layout.size = 4;
    int_layout.align = 4;
    int_layout.is_zero_sized = false;
    int_layout.shape = ir::core::CoreLayoutScalar{ir::core::CoreScalarRepr::I32};
    section.table.layouts.push_back(int_layout);
    section.input_layout = ir::core::CoreLayoutId{0};
    section.output_layout = ir::core::CoreLayoutId{0};

    // Bridge control/spill regions.
    section.bridge_control_base = kControlBase;
    section.bridge_block_stride = kBlockStride;
    section.bridge_control_extent = kBlockStride; // one call site
    section.bridge_spill_base = kSpillBase;
    section.bridge_spill_extent = kSpillExtent;

    // One bridge call site: Int param, Int result.
    ir::core::CoreFrameBridgeCallSite site;
    site.call_site_id = 0;
    site.source_symbol = source_symbol;
    site.arity = 1;
    site.block_offset = 0;
    site.param_layouts = {ir::core::CoreLayoutId{0}};
    site.result_layout = ir::core::CoreLayoutId{0};
    site.result_base = kResultBase;
    site.result_extent = kResultExtent;
    site.result_payload_base = 0; // no String payload
    site.result_payload_capacity = 0;
    site.spill_base = kSpillBase;
    site.spill_extent = kSpillExtent;
    section.bridge_call_sites.push_back(site);

    return section;
}

// Set up the engine's memory with a bridge control block at kControlBase and
// an Int arg value at the spill window. Returns the control block pointer.
std::uint32_t setup_bridge_control_block(wh::Wasm3ResumeEngine &engine,
                                         std::int32_t arg_value) {
    auto page = engine.mutable_whole_memory();
    if (!page.has_value()) {
        return 0;
    }
    // Control block at kControlBase:
    //   word 0: call_site_id = 0
    //   word 1: arg_count = 1
    //   arg 0 descriptor: ptr = kSpillBase, len = 8
    put_u32_le(*page, kControlBase, 0);     // call_site_id
    put_u32_le(*page, kControlBase + 4, 1); // arg_count
    put_i32_le(*page, kControlBase + 8, static_cast<std::int32_t>(kSpillBase)); // ptr
    put_u32_le(*page, kControlBase + 12, 8);                                     // len

    // Spill slot at kSpillBase: the Int value (4 bytes LE, padded to 8).
    put_i32_le(*page, kSpillBase, arg_value);
    std::fill(page->begin() + kSpillBase + 4, page->begin() + kSpillBase + 8,
              std::uint8_t{0});

    return kControlBase;
}

// ==== opaque lane tests ====

// 1. Opaque OK: the mock returns Success with IntValue(42); the executor
//    serializes "42", alloc_then_writes it, and replies (0, ptr, 2).
void test_opaque_ok() {
    constexpr std::uint64_t kSymbol = 100;
    auto bytes = wht::with_a2_admission(wht::capability_module(kSymbol, 2), kSymbol);
    auto admitted = csm::make_verified_core_wasm_schema_module(bytes);
    check(admitted.ok(), "opaque ok: A2 admission");

    wh::Wasm3ResumeEngine engine;
    auto inst = engine.fresh_instance(
        bytes, [](const eng::ImportObservation &) -> eng::ImportCallbackResult {
            return eng::ImportReply{};
        });
    check(inst.has_value(), "opaque ok: engine instantiated");

    ScriptedInvoker mock;
    mock.result.status = runtime::CapabilityCallStatus::Success;
    mock.result.value = runtime::Value{runtime::IntValue{42}};
    auto invoker = mock.as_invoker();

    wh::CapabilityImportState state;
    ir::core::CoreFrameLayoutSection section; // unused by opaque lane
    runtime::CapabilityInvocationContext context;
    wh::CapabilityImportConfig config{
        .engine = engine,
        .module = *admitted.module,
        .frame_section = section,
        .invoker = invoker,
        .context = context,
        .name_resolver = [](std::uint64_t) { return std::optional<std::string>{"test_cap"}; },
        .state = state,
    };
    auto callback = wh::make_capability_import_callback(std::move(config));

    // The arg envelope for 1 Int param: {"value":42}
    const std::string arg_json = "{\"value\":42}";
    eng::ImportObservation obs;
    obs.import_ordinal = 0;
    obs.param_frame = std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t *>(arg_json.data()), arg_json.size());
    auto mem = engine.read_whole_memory();
    check(mem.has_value(), "opaque ok: read memory");
    obs.whole_memory = *mem;

    auto reply = callback(obs);
    check(std::holds_alternative<eng::ImportReply>(reply), "opaque ok: reply is ImportReply");
    if (const auto *r = std::get_if<eng::ImportReply>(&reply)) {
        check(r->raw_status == 0, "opaque ok: raw_status == 0");
        check(r->result_ptr.value != 0, "opaque ok: result_ptr != 0");
        check(r->result_len == 2, "opaque ok: result_len == 2");
        auto mem2 = engine.read_whole_memory();
        std::string result_json(
            mem2->begin() + r->result_ptr.value,
            mem2->begin() + r->result_ptr.value + r->result_len);
        check(result_json == "42", "opaque ok: result == '42'");
    }
    check(mock.call_count == 1, "opaque ok: invoker called once");
    check(mock.last_name == "test_cap", "opaque ok: capability name");
    check(!state.last_error.has_value(), "opaque ok: no error");
}

// 2. Opaque ERROR: the mock returns Error; the executor replies (1, 0, 0) — a
//    raw non-OK reply, NOT an ImportAbort.
void test_opaque_error() {
    constexpr std::uint64_t kSymbol = 101;
    auto bytes = wht::with_a2_admission(wht::capability_module(kSymbol, 2), kSymbol);
    auto admitted = csm::make_verified_core_wasm_schema_module(bytes);
    check(admitted.ok(), "opaque error: A2 admission");

    wh::Wasm3ResumeEngine engine;
    auto inst = engine.fresh_instance(
        bytes, [](const eng::ImportObservation &) -> eng::ImportCallbackResult {
            return eng::ImportReply{};
        });
    check(inst.has_value(), "opaque error: engine instantiated");

    ScriptedInvoker mock;
    mock.result.status = runtime::CapabilityCallStatus::Error;
    auto invoker = mock.as_invoker();

    wh::CapabilityImportState state;
    ir::core::CoreFrameLayoutSection section;
    runtime::CapabilityInvocationContext context;
    wh::CapabilityImportConfig config{
        .engine = engine,
        .module = *admitted.module,
        .frame_section = section,
        .invoker = invoker,
        .context = context,
        .name_resolver = [](std::uint64_t) { return std::optional<std::string>{"test_cap"}; },
        .state = state,
    };
    auto callback = wh::make_capability_import_callback(std::move(config));

    const std::string arg_json = "{\"value\":42}";
    eng::ImportObservation obs;
    obs.import_ordinal = 0;
    obs.param_frame = std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t *>(arg_json.data()), arg_json.size());
    auto mem = engine.read_whole_memory();
    obs.whole_memory = *mem;

    auto reply = callback(obs);
    check(std::holds_alternative<eng::ImportReply>(reply),
          "opaque error: reply is ImportReply (not ImportAbort)");
    if (const auto *r = std::get_if<eng::ImportReply>(&reply)) {
        check(r->raw_status == 1, "opaque error: raw_status == 1");
        check(r->result_ptr.value == 0, "opaque error: result_ptr == 0");
        check(r->result_len == 0, "opaque error: result_len == 0");
    }
    check(mock.call_count == 1, "opaque error: invoker called once");
    check(!state.last_error.has_value(), "opaque error: no host-decision error");
}

// 3. Opaque PENDING: the mock returns Pending; the executor replies (2, 0, 0)
//    — a raw non-OK reply, NOT an ImportAbort.
void test_opaque_pending() {
    constexpr std::uint64_t kSymbol = 102;
    auto bytes = wht::with_a2_admission(wht::capability_module(kSymbol, 2), kSymbol);
    auto admitted = csm::make_verified_core_wasm_schema_module(bytes);
    check(admitted.ok(), "opaque pending: A2 admission");

    wh::Wasm3ResumeEngine engine;
    auto inst = engine.fresh_instance(
        bytes, [](const eng::ImportObservation &) -> eng::ImportCallbackResult {
            return eng::ImportReply{};
        });
    check(inst.has_value(), "opaque pending: engine instantiated");

    ScriptedInvoker mock;
    mock.result.status = runtime::CapabilityCallStatus::Pending;
    auto invoker = mock.as_invoker();

    wh::CapabilityImportState state;
    ir::core::CoreFrameLayoutSection section;
    runtime::CapabilityInvocationContext context;
    wh::CapabilityImportConfig config{
        .engine = engine,
        .module = *admitted.module,
        .frame_section = section,
        .invoker = invoker,
        .context = context,
        .name_resolver = [](std::uint64_t) { return std::optional<std::string>{"test_cap"}; },
        .state = state,
    };
    auto callback = wh::make_capability_import_callback(std::move(config));

    const std::string arg_json = "{\"value\":42}";
    eng::ImportObservation obs;
    obs.import_ordinal = 0;
    obs.param_frame = std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t *>(arg_json.data()), arg_json.size());
    auto mem = engine.read_whole_memory();
    obs.whole_memory = *mem;

    auto reply = callback(obs);
    check(std::holds_alternative<eng::ImportReply>(reply),
          "opaque pending: reply is ImportReply (not ImportAbort)");
    if (const auto *r = std::get_if<eng::ImportReply>(&reply)) {
        check(r->raw_status == 2, "opaque pending: raw_status == 2");
        check(r->result_ptr.value == 0, "opaque pending: result_ptr == 0");
        check(r->result_len == 0, "opaque pending: result_len == 0");
    }
    check(mock.call_count == 1, "opaque pending: invoker called once");
    check(!state.last_error.has_value(), "opaque pending: no host-decision error");
}

// ==== bridge lane tests ====

// 4. Bridge OK: the mock returns Success with IntValue(42); the executor packs
//    it at the result base and replies (0, result_base, result_extent).
void test_bridge_ok() {
    constexpr std::uint64_t kSymbol = 200;
    auto bytes = wht::with_a2_admission(wht::capability_module(kSymbol, 2), kSymbol);
    auto admitted = csm::make_verified_core_wasm_schema_module(bytes);
    check(admitted.ok(), "bridge ok: A2 admission");

    wh::Wasm3ResumeEngine engine;
    auto inst = engine.fresh_instance(
        bytes, [](const eng::ImportObservation &) -> eng::ImportCallbackResult {
            return eng::ImportReply{};
        });
    check(inst.has_value(), "bridge ok: engine instantiated");

    auto section = make_bridge_section(kSymbol);

    ScriptedInvoker mock;
    mock.result.status = runtime::CapabilityCallStatus::Success;
    mock.result.value = runtime::Value{runtime::IntValue{42}};
    auto invoker = mock.as_invoker();

    wh::CapabilityImportState state;
    runtime::CapabilityInvocationContext context;
    wh::CapabilityImportConfig config{
        .engine = engine,
        .module = *admitted.module,
        .frame_section = section,
        .invoker = invoker,
        .context = context,
        .name_resolver = [](std::uint64_t) { return std::optional<std::string>{"test_cap"}; },
        .state = state,
    };
    auto callback = wh::make_capability_import_callback(std::move(config));

    // Set up the control block + spill arg in the engine's memory.
    const auto block_ptr = setup_bridge_control_block(engine, 42);
    check(block_ptr == kControlBase, "bridge ok: control block set up");

    // Build the observation (bridge lane: param_frame is empty).
    eng::ImportObservation obs;
    obs.import_ordinal = 0;
    obs.scalar_arg = block_ptr;
    obs.param_frame = {};
    auto mem = engine.read_whole_memory();
    obs.whole_memory = *mem;

    auto reply = callback(obs);
    check(std::holds_alternative<eng::ImportReply>(reply), "bridge ok: reply is ImportReply");
    if (const auto *r = std::get_if<eng::ImportReply>(&reply)) {
        check(r->raw_status == 0, "bridge ok: raw_status == 0");
        check(r->result_ptr.value == kResultBase, "bridge ok: result_ptr == result_base");
        check(r->result_len == kResultExtent, "bridge ok: result_len == result_extent");
        // Verify the packed result at result_base.
        auto mem2 = engine.read_whole_memory();
        const auto packed = static_cast<std::int32_t>(
            static_cast<std::uint32_t>((*mem2)[kResultBase]) |
            (static_cast<std::uint32_t>((*mem2)[kResultBase + 1]) << 8) |
            (static_cast<std::uint32_t>((*mem2)[kResultBase + 2]) << 16) |
            (static_cast<std::uint32_t>((*mem2)[kResultBase + 3]) << 24));
        check(packed == 42, "bridge ok: packed result == 42");
    }
    check(mock.call_count == 1, "bridge ok: invoker called once");
    check(mock.last_name == "test_cap", "bridge ok: capability name");
    check(!state.last_error.has_value(), "bridge ok: no error");
}

// 5. Bridge ERROR: the mock returns Error; the executor replies (1, 0, 0) — a
//    raw non-OK reply that traps the bridge module (the bridge has no graceful
//    ERROR arm).
void test_bridge_error() {
    constexpr std::uint64_t kSymbol = 201;
    auto bytes = wht::with_a2_admission(wht::capability_module(kSymbol, 2), kSymbol);
    auto admitted = csm::make_verified_core_wasm_schema_module(bytes);
    check(admitted.ok(), "bridge error: A2 admission");

    wh::Wasm3ResumeEngine engine;
    auto inst = engine.fresh_instance(
        bytes, [](const eng::ImportObservation &) -> eng::ImportCallbackResult {
            return eng::ImportReply{};
        });
    check(inst.has_value(), "bridge error: engine instantiated");

    auto section = make_bridge_section(kSymbol);

    ScriptedInvoker mock;
    mock.result.status = runtime::CapabilityCallStatus::Error;
    auto invoker = mock.as_invoker();

    wh::CapabilityImportState state;
    runtime::CapabilityInvocationContext context;
    wh::CapabilityImportConfig config{
        .engine = engine,
        .module = *admitted.module,
        .frame_section = section,
        .invoker = invoker,
        .context = context,
        .name_resolver = [](std::uint64_t) { return std::optional<std::string>{"test_cap"}; },
        .state = state,
    };
    auto callback = wh::make_capability_import_callback(std::move(config));

    const auto block_ptr = setup_bridge_control_block(engine, 42);
    check(block_ptr == kControlBase, "bridge error: control block set up");

    eng::ImportObservation obs;
    obs.import_ordinal = 0;
    obs.scalar_arg = block_ptr;
    obs.param_frame = {};
    auto mem = engine.read_whole_memory();
    obs.whole_memory = *mem;

    auto reply = callback(obs);
    check(std::holds_alternative<eng::ImportReply>(reply),
          "bridge error: reply is ImportReply (not ImportAbort)");
    if (const auto *r = std::get_if<eng::ImportReply>(&reply)) {
        check(r->raw_status == 1, "bridge error: raw_status == 1");
        check(r->result_ptr.value == 0, "bridge error: result_ptr == 0");
        check(r->result_len == 0, "bridge error: result_len == 0");
    }
    check(mock.call_count == 1, "bridge error: invoker called once");
    check(!state.last_error.has_value(), "bridge error: no host-decision error");
}

// ==== ImportAbort tests ====

// 6. ImportAbort on unknown import ordinal: no call site matches.
void test_abort_unknown_ordinal() {
    constexpr std::uint64_t kSymbol = 300;
    auto bytes = wht::with_a2_admission(wht::capability_module(kSymbol, 2), kSymbol);
    auto admitted = csm::make_verified_core_wasm_schema_module(bytes);
    check(admitted.ok(), "abort ordinal: A2 admission");

    wh::Wasm3ResumeEngine engine;
    auto inst = engine.fresh_instance(
        bytes, [](const eng::ImportObservation &) -> eng::ImportCallbackResult {
            return eng::ImportReply{};
        });
    check(inst.has_value(), "abort ordinal: engine instantiated");

    ScriptedInvoker mock;
    mock.result.status = runtime::CapabilityCallStatus::Success;
    auto invoker = mock.as_invoker();

    wh::CapabilityImportState state;
    ir::core::CoreFrameLayoutSection section;
    runtime::CapabilityInvocationContext context;
    wh::CapabilityImportConfig config{
        .engine = engine,
        .module = *admitted.module,
        .frame_section = section,
        .invoker = invoker,
        .context = context,
        .name_resolver = [](std::uint64_t) { return std::optional<std::string>{"test_cap"}; },
        .state = state,
    };
    auto callback = wh::make_capability_import_callback(std::move(config));

    const std::string arg_json = "{\"value\":42}";
    eng::ImportObservation obs;
    obs.import_ordinal = 999; // unknown ordinal
    obs.param_frame = std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t *>(arg_json.data()), arg_json.size());
    auto mem = engine.read_whole_memory();
    obs.whole_memory = *mem;

    auto reply = callback(obs);
    check(std::holds_alternative<eng::ImportAbort>(reply),
          "abort ordinal: reply is ImportAbort");
    check(state.last_error.has_value() &&
              *state.last_error == wh::CapabilityImportError::CallSiteNotFound,
          "abort ordinal: error is CallSiteNotFound");
    check(mock.call_count == 0, "abort ordinal: invoker not called");
}

// 7. ImportAbort on unknown capability name: the name_resolver returns nullopt.
void test_abort_unknown_name() {
    constexpr std::uint64_t kSymbol = 301;
    auto bytes = wht::with_a2_admission(wht::capability_module(kSymbol, 2), kSymbol);
    auto admitted = csm::make_verified_core_wasm_schema_module(bytes);
    check(admitted.ok(), "abort name: A2 admission");

    wh::Wasm3ResumeEngine engine;
    auto inst = engine.fresh_instance(
        bytes, [](const eng::ImportObservation &) -> eng::ImportCallbackResult {
            return eng::ImportReply{};
        });
    check(inst.has_value(), "abort name: engine instantiated");

    ScriptedInvoker mock;
    mock.result.status = runtime::CapabilityCallStatus::Success;
    auto invoker = mock.as_invoker();

    wh::CapabilityImportState state;
    ir::core::CoreFrameLayoutSection section;
    runtime::CapabilityInvocationContext context;
    wh::CapabilityImportConfig config{
        .engine = engine,
        .module = *admitted.module,
        .frame_section = section,
        .invoker = invoker,
        .context = context,
        .name_resolver = [](std::uint64_t) { return std::nullopt; }, // unknown name
        .state = state,
    };
    auto callback = wh::make_capability_import_callback(std::move(config));

    const std::string arg_json = "{\"value\":42}";
    eng::ImportObservation obs;
    obs.import_ordinal = 0;
    obs.param_frame = std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t *>(arg_json.data()), arg_json.size());
    auto mem = engine.read_whole_memory();
    obs.whole_memory = *mem;

    auto reply = callback(obs);
    check(std::holds_alternative<eng::ImportAbort>(reply),
          "abort name: reply is ImportAbort");
    check(state.last_error.has_value() &&
              *state.last_error == wh::CapabilityImportError::CapabilityNameUnknown,
          "abort name: error is CapabilityNameUnknown");
    check(mock.call_count == 0, "abort name: invoker not called");
}

// 8. ImportAbort on bridge control-block out of range: the scalar_arg points
//    outside the control-block region.
void test_abort_bridge_block_oob() {
    constexpr std::uint64_t kSymbol = 302;
    auto bytes = wht::with_a2_admission(wht::capability_module(kSymbol, 2), kSymbol);
    auto admitted = csm::make_verified_core_wasm_schema_module(bytes);
    check(admitted.ok(), "abort bridge oob: A2 admission");

    wh::Wasm3ResumeEngine engine;
    auto inst = engine.fresh_instance(
        bytes, [](const eng::ImportObservation &) -> eng::ImportCallbackResult {
            return eng::ImportReply{};
        });
    check(inst.has_value(), "abort bridge oob: engine instantiated");

    auto section = make_bridge_section(kSymbol);

    ScriptedInvoker mock;
    mock.result.status = runtime::CapabilityCallStatus::Success;
    auto invoker = mock.as_invoker();

    wh::CapabilityImportState state;
    runtime::CapabilityInvocationContext context;
    wh::CapabilityImportConfig config{
        .engine = engine,
        .module = *admitted.module,
        .frame_section = section,
        .invoker = invoker,
        .context = context,
        .name_resolver = [](std::uint64_t) { return std::optional<std::string>{"test_cap"}; },
        .state = state,
    };
    auto callback = wh::make_capability_import_callback(std::move(config));

    eng::ImportObservation obs;
    obs.import_ordinal = 0;
    obs.scalar_arg = 9999; // outside the control-block region
    obs.param_frame = {};
    auto mem = engine.read_whole_memory();
    obs.whole_memory = *mem;

    auto reply = callback(obs);
    check(std::holds_alternative<eng::ImportAbort>(reply),
          "abort bridge oob: reply is ImportAbort");
    check(state.last_error.has_value() &&
              *state.last_error == wh::CapabilityImportError::BridgeBlockOutOfRange,
          "abort bridge oob: error is BridgeBlockOutOfRange");
    check(mock.call_count == 0, "abort bridge oob: invoker not called");
}

// 9. ImportAbort on bridge call_site_id mismatch: the control block's
//    call_site_id word disagrees with its dense block address.
void test_abort_bridge_site_id_mismatch() {
    constexpr std::uint64_t kSymbol = 303;
    auto bytes = wht::with_a2_admission(wht::capability_module(kSymbol, 2), kSymbol);
    auto admitted = csm::make_verified_core_wasm_schema_module(bytes);
    check(admitted.ok(), "abort bridge id: A2 admission");

    wh::Wasm3ResumeEngine engine;
    auto inst = engine.fresh_instance(
        bytes, [](const eng::ImportObservation &) -> eng::ImportCallbackResult {
            return eng::ImportReply{};
        });
    check(inst.has_value(), "abort bridge id: engine instantiated");

    auto section = make_bridge_section(kSymbol);

    ScriptedInvoker mock;
    mock.result.status = runtime::CapabilityCallStatus::Success;
    auto invoker = mock.as_invoker();

    wh::CapabilityImportState state;
    runtime::CapabilityInvocationContext context;
    wh::CapabilityImportConfig config{
        .engine = engine,
        .module = *admitted.module,
        .frame_section = section,
        .invoker = invoker,
        .context = context,
        .name_resolver = [](std::uint64_t) { return std::optional<std::string>{"test_cap"}; },
        .state = state,
    };
    auto callback = wh::make_capability_import_callback(std::move(config));

    // Set up the control block with a WRONG call_site_id.
    const auto block_ptr = setup_bridge_control_block(engine, 42);
    auto page = engine.mutable_whole_memory();
    put_u32_le(*page, kControlBase, 999); // wrong call_site_id

    eng::ImportObservation obs;
    obs.import_ordinal = 0;
    obs.scalar_arg = block_ptr;
    obs.param_frame = {};
    auto mem = engine.read_whole_memory();
    obs.whole_memory = *mem;

    auto reply = callback(obs);
    check(std::holds_alternative<eng::ImportAbort>(reply),
          "abort bridge id: reply is ImportAbort");
    check(state.last_error.has_value() &&
              *state.last_error == wh::CapabilityImportError::BridgeCallSiteIdMismatch,
          "abort bridge id: error is BridgeCallSiteIdMismatch");
    check(mock.call_count == 0, "abort bridge id: invoker not called");
}

// ==== P2-6: missing executor abort-path tests ====

// Build a module with A2 admission sections whose wire schema has a String
// result type (node 1) instead of the default Int (node 0). The opaque lane's
// result validation accepts any-length String (unbounded), so the executor
// reaches the alloc_then_write step — used by the oversized-frame test.
[[nodiscard]] std::vector<std::uint8_t>
with_a2_admission_string_result(std::vector<std::uint8_t> module_bytes,
                                std::uint64_t source_symbol) {
    wht::put_section(module_bytes, 0,
                wht::custom_payload("ahfl.wasm-exec-manifest.v1",
                                   wht::exec_manifest_body(
                                       7, {{.workflow_node_id = 1,
                                            .cap_call_count = 1,
                                            .capability = 0,
                                            .source_symbol = source_symbol}})));
    wht::put_section(module_bytes, 0,
                wht::custom_payload("ahfl.wire-schema.v1",
                                   wht::encode_schema(wht::schema_table(
                                       {ir::core::CoreWireSchemaNode{
                                           ir::core::CoreWireSchemaString{}}},
                                       {wht::CapSpec{
                                           .cap_id = 0,
                                           .symbol = source_symbol,
                                           .result = ir::core::CoreWireSchemaNodeId{1}}}))));
    return module_bytes;
}

// 10. Opaque closure-in-result: the mock returns Success with a closure Value.
//     The executor validates the result against the Int result binding and
//     rejects it as ResultSchemaInvalid. Post-effect: the invoker WAS called
//     (the capability executed and returned a value the host must not wire).
void test_abort_opaque_closure_result() {
    constexpr std::uint64_t kSymbol = 400;
    auto bytes = wht::with_a2_admission(wht::capability_module(kSymbol, 2), kSymbol);
    auto admitted = csm::make_verified_core_wasm_schema_module(bytes);
    check(admitted.ok(), "abort closure result: A2 admission");

    wh::Wasm3ResumeEngine engine;
    auto inst = engine.fresh_instance(
        bytes, [](const eng::ImportObservation &) -> eng::ImportCallbackResult {
            return eng::ImportReply{};
        });
    check(inst.has_value(), "abort closure result: engine instantiated");

    ScriptedInvoker mock;
    mock.result.status = runtime::CapabilityCallStatus::Success;
    mock.result.value = runtime::Value{
        runtime::InterpreterClosureHandle{.id = 1, .descriptor = nullptr}};
    auto invoker = mock.as_invoker();

    wh::CapabilityImportState state;
    ir::core::CoreFrameLayoutSection section;
    runtime::CapabilityInvocationContext context;
    wh::CapabilityImportConfig config{
        .engine = engine,
        .module = *admitted.module,
        .frame_section = section,
        .invoker = invoker,
        .context = context,
        .name_resolver = [](std::uint64_t) { return std::optional<std::string>{"test_cap"}; },
        .state = state,
    };
    auto callback = wh::make_capability_import_callback(std::move(config));

    const std::string arg_json = "{\"value\":42}";
    eng::ImportObservation obs;
    obs.import_ordinal = 0;
    obs.param_frame = std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t *>(arg_json.data()), arg_json.size());
    auto mem = engine.read_whole_memory();
    obs.whole_memory = *mem;

    auto reply = callback(obs);
    check(std::holds_alternative<eng::ImportAbort>(reply),
          "abort closure result: reply is ImportAbort");
    check(state.last_error.has_value() &&
              *state.last_error == wh::CapabilityImportError::ResultSchemaInvalid,
          "abort closure result: error is ResultSchemaInvalid");
    check(mock.call_count == 1, "abort closure result: invoker called once (post-effect)");
}

// 11. Opaque oversized frame: the mock returns Success with a StringValue whose
//     JSON serialization exceeds the fixed single-page capacity (65536). The
//     executor validates the result (String, unbounded), serializes it to
//     65537 bytes, and rejects the alloc_then_write as EngineAllocFailed.
//     Post-effect: the invoker WAS called.
void test_abort_opaque_oversized_frame() {
    constexpr std::uint64_t kSymbol = 401;
    auto bytes = with_a2_admission_string_result(
        wht::capability_module(kSymbol, 2), kSymbol);
    auto admitted = csm::make_verified_core_wasm_schema_module(bytes);
    check(admitted.ok(), "abort oversized: A2 admission");

    wh::Wasm3ResumeEngine engine;
    auto inst = engine.fresh_instance(
        bytes, [](const eng::ImportObservation &) -> eng::ImportCallbackResult {
            return eng::ImportReply{};
        });
    check(inst.has_value(), "abort oversized: engine instantiated");

    ScriptedInvoker mock;
    mock.result.status = runtime::CapabilityCallStatus::Success;
    // 65535 chars -> JSON is 65537 bytes > 65536 (fixed single-page capacity).
    mock.result.value = runtime::Value{runtime::StringValue{std::string(65535, 'x')}};
    auto invoker = mock.as_invoker();

    wh::CapabilityImportState state;
    ir::core::CoreFrameLayoutSection section;
    runtime::CapabilityInvocationContext context;
    wh::CapabilityImportConfig config{
        .engine = engine,
        .module = *admitted.module,
        .frame_section = section,
        .invoker = invoker,
        .context = context,
        .name_resolver = [](std::uint64_t) { return std::optional<std::string>{"test_cap"}; },
        .state = state,
    };
    auto callback = wh::make_capability_import_callback(std::move(config));

    const std::string arg_json = "{\"value\":42}";
    eng::ImportObservation obs;
    obs.import_ordinal = 0;
    obs.param_frame = std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t *>(arg_json.data()), arg_json.size());
    auto mem = engine.read_whole_memory();
    obs.whole_memory = *mem;

    auto reply = callback(obs);
    check(std::holds_alternative<eng::ImportAbort>(reply),
          "abort oversized: reply is ImportAbort");
    check(state.last_error.has_value() &&
              *state.last_error == wh::CapabilityImportError::EngineAllocFailed,
          "abort oversized: error is EngineAllocFailed");
    check(mock.call_count == 1, "abort oversized: invoker called once (post-effect)");
}

// 12. Bridge arg_count mismatch: the control block's arg_count word disagrees
//     with the site's arity. Pre-effect: the invoker is NEVER called.
void test_abort_bridge_arg_count() {
    constexpr std::uint64_t kSymbol = 402;
    auto bytes = wht::with_a2_admission(wht::capability_module(kSymbol, 2), kSymbol);
    auto admitted = csm::make_verified_core_wasm_schema_module(bytes);
    check(admitted.ok(), "abort bridge arg_count: A2 admission");

    wh::Wasm3ResumeEngine engine;
    auto inst = engine.fresh_instance(
        bytes, [](const eng::ImportObservation &) -> eng::ImportCallbackResult {
            return eng::ImportReply{};
        });
    check(inst.has_value(), "abort bridge arg_count: engine instantiated");

    auto section = make_bridge_section(kSymbol);

    ScriptedInvoker mock;
    mock.result.status = runtime::CapabilityCallStatus::Success;
    auto invoker = mock.as_invoker();

    wh::CapabilityImportState state;
    runtime::CapabilityInvocationContext context;
    wh::CapabilityImportConfig config{
        .engine = engine,
        .module = *admitted.module,
        .frame_section = section,
        .invoker = invoker,
        .context = context,
        .name_resolver = [](std::uint64_t) { return std::optional<std::string>{"test_cap"}; },
        .state = state,
    };
    auto callback = wh::make_capability_import_callback(std::move(config));

    // Set up the control block, then overwrite arg_count to a wrong value.
    setup_bridge_control_block(engine, 42);
    auto page = engine.mutable_whole_memory();
    put_u32_le(*page, kControlBase + 4, 2); // wrong: site.arity is 1

    eng::ImportObservation obs;
    obs.import_ordinal = 0;
    obs.scalar_arg = kControlBase;
    obs.param_frame = {};
    auto mem = engine.read_whole_memory();
    obs.whole_memory = *mem;

    auto reply = callback(obs);
    check(std::holds_alternative<eng::ImportAbort>(reply),
          "abort bridge arg_count: reply is ImportAbort");
    check(state.last_error.has_value() &&
              *state.last_error == wh::CapabilityImportError::BridgeArgCountMismatch,
          "abort bridge arg_count: error is BridgeArgCountMismatch");
    check(mock.call_count == 0, "abort bridge arg_count: invoker not called");
}

// 13. Bridge stride failure: the control block pointer is not at a
//     stride-aligned offset within the control region. Pre-effect: the
//     invoker is NEVER called.
void test_abort_bridge_stride() {
    constexpr std::uint64_t kSymbol = 403;
    auto bytes = wht::with_a2_admission(wht::capability_module(kSymbol, 2), kSymbol);
    auto admitted = csm::make_verified_core_wasm_schema_module(bytes);
    check(admitted.ok(), "abort bridge stride: A2 admission");

    wh::Wasm3ResumeEngine engine;
    auto inst = engine.fresh_instance(
        bytes, [](const eng::ImportObservation &) -> eng::ImportCallbackResult {
            return eng::ImportReply{};
        });
    check(inst.has_value(), "abort bridge stride: engine instantiated");

    auto section = make_bridge_section(kSymbol);

    ScriptedInvoker mock;
    mock.result.status = runtime::CapabilityCallStatus::Success;
    auto invoker = mock.as_invoker();

    wh::CapabilityImportState state;
    runtime::CapabilityInvocationContext context;
    wh::CapabilityImportConfig config{
        .engine = engine,
        .module = *admitted.module,
        .frame_section = section,
        .invoker = invoker,
        .context = context,
        .name_resolver = [](std::uint64_t) { return std::optional<std::string>{"test_cap"}; },
        .state = state,
    };
    auto callback = wh::make_capability_import_callback(std::move(config));

    // Set up the control block (for consistency), but point at a
    // non-stride-aligned address.
    setup_bridge_control_block(engine, 42);

    eng::ImportObservation obs;
    obs.import_ordinal = 0;
    obs.scalar_arg = kControlBase + 4; // rel=4, 4 % 16 != 0
    obs.param_frame = {};
    auto mem = engine.read_whole_memory();
    obs.whole_memory = *mem;

    auto reply = callback(obs);
    check(std::holds_alternative<eng::ImportAbort>(reply),
          "abort bridge stride: reply is ImportAbort");
    check(state.last_error.has_value() &&
              *state.last_error == wh::CapabilityImportError::BridgeBlockStride,
          "abort bridge stride: error is BridgeBlockStride");
    check(mock.call_count == 0, "abort bridge stride: invoker not called");
}

// 14. Bridge expected_addr failure: the site's block_offset disagrees with the
//     dense block address. Pre-effect: the invoker is NEVER called.
void test_abort_bridge_expected_addr() {
    constexpr std::uint64_t kSymbol = 404;
    auto bytes = wht::with_a2_admission(wht::capability_module(kSymbol, 2), kSymbol);
    auto admitted = csm::make_verified_core_wasm_schema_module(bytes);
    check(admitted.ok(), "abort bridge expected_addr: A2 admission");

    wh::Wasm3ResumeEngine engine;
    auto inst = engine.fresh_instance(
        bytes, [](const eng::ImportObservation &) -> eng::ImportCallbackResult {
            return eng::ImportReply{};
        });
    check(inst.has_value(), "abort bridge expected_addr: engine instantiated");

    // The site's block_offset is wrong (16 instead of 0), so the expected
    // address (control_base + 16) != block_ptr (control_base).
    auto section = make_bridge_section(kSymbol);
    section.bridge_call_sites[0].block_offset = 16;

    ScriptedInvoker mock;
    mock.result.status = runtime::CapabilityCallStatus::Success;
    auto invoker = mock.as_invoker();

    wh::CapabilityImportState state;
    runtime::CapabilityInvocationContext context;
    wh::CapabilityImportConfig config{
        .engine = engine,
        .module = *admitted.module,
        .frame_section = section,
        .invoker = invoker,
        .context = context,
        .name_resolver = [](std::uint64_t) { return std::optional<std::string>{"test_cap"}; },
        .state = state,
    };
    auto callback = wh::make_capability_import_callback(std::move(config));

    setup_bridge_control_block(engine, 42);

    eng::ImportObservation obs;
    obs.import_ordinal = 0;
    obs.scalar_arg = kControlBase;
    obs.param_frame = {};
    auto mem = engine.read_whole_memory();
    obs.whole_memory = *mem;

    auto reply = callback(obs);
    check(std::holds_alternative<eng::ImportAbort>(reply),
          "abort bridge expected_addr: reply is ImportAbort");
    check(state.last_error.has_value() &&
              *state.last_error == wh::CapabilityImportError::BridgeBlockStride,
          "abort bridge expected_addr: error is BridgeBlockStride");
    check(mock.call_count == 0, "abort bridge expected_addr: invoker not called");
}

// 15. Bridge wire-schema-arity failure: the wire schema's param count
//     disagrees with the site's arity. Pre-effect: the invoker is NEVER called.
void test_abort_bridge_wire_schema_arity() {
    constexpr std::uint64_t kSymbol = 405;
    auto bytes = wht::with_a2_admission(wht::capability_module(kSymbol, 2), kSymbol);
    auto admitted = csm::make_verified_core_wasm_schema_module(bytes);
    check(admitted.ok(), "abort bridge ws-arity: A2 admission");

    wh::Wasm3ResumeEngine engine;
    auto inst = engine.fresh_instance(
        bytes, [](const eng::ImportObservation &) -> eng::ImportCallbackResult {
            return eng::ImportReply{};
        });
    check(inst.has_value(), "abort bridge ws-arity: engine instantiated");

    // The site's arity is 2, but the wire schema has 1 param (Int).
    auto section = make_bridge_section(kSymbol);
    section.bridge_call_sites[0].arity = 2;

    ScriptedInvoker mock;
    mock.result.status = runtime::CapabilityCallStatus::Success;
    auto invoker = mock.as_invoker();

    wh::CapabilityImportState state;
    runtime::CapabilityInvocationContext context;
    wh::CapabilityImportConfig config{
        .engine = engine,
        .module = *admitted.module,
        .frame_section = section,
        .invoker = invoker,
        .context = context,
        .name_resolver = [](std::uint64_t) { return std::optional<std::string>{"test_cap"}; },
        .state = state,
    };
    auto callback = wh::make_capability_import_callback(std::move(config));

    // Set arg_count=2 to pass the arg_count check (2 == site.arity), so the
    // wire-schema-arity check is the one that fires.
    setup_bridge_control_block(engine, 42);
    auto page = engine.mutable_whole_memory();
    put_u32_le(*page, kControlBase + 4, 2); // arg_count = 2 (matches site.arity)

    eng::ImportObservation obs;
    obs.import_ordinal = 0;
    obs.scalar_arg = kControlBase;
    obs.param_frame = {};
    auto mem = engine.read_whole_memory();
    obs.whole_memory = *mem;

    auto reply = callback(obs);
    check(std::holds_alternative<eng::ImportAbort>(reply),
          "abort bridge ws-arity: reply is ImportAbort");
    check(state.last_error.has_value() &&
              *state.last_error == wh::CapabilityImportError::BridgeWireSchemaMismatch,
          "abort bridge ws-arity: error is BridgeWireSchemaMismatch");
    check(mock.call_count == 0, "abort bridge ws-arity: invoker not called");
}

// 16. Bridge span-region failure: an arg span lies outside the authorized
//     spill region. Pre-effect: the invoker is NEVER called.
void test_abort_bridge_span_region() {
    constexpr std::uint64_t kSymbol = 406;
    auto bytes = wht::with_a2_admission(wht::capability_module(kSymbol, 2), kSymbol);
    auto admitted = csm::make_verified_core_wasm_schema_module(bytes);
    check(admitted.ok(), "abort bridge span-region: A2 admission");

    wh::Wasm3ResumeEngine engine;
    auto inst = engine.fresh_instance(
        bytes, [](const eng::ImportObservation &) -> eng::ImportCallbackResult {
            return eng::ImportReply{};
        });
    check(inst.has_value(), "abort bridge span-region: engine instantiated");

    auto section = make_bridge_section(kSymbol);

    ScriptedInvoker mock;
    mock.result.status = runtime::CapabilityCallStatus::Success;
    auto invoker = mock.as_invoker();

    wh::CapabilityImportState state;
    runtime::CapabilityInvocationContext context;
    wh::CapabilityImportConfig config{
        .engine = engine,
        .module = *admitted.module,
        .frame_section = section,
        .invoker = invoker,
        .context = context,
        .name_resolver = [](std::uint64_t) { return std::optional<std::string>{"test_cap"}; },
        .state = state,
    };
    auto callback = wh::make_capability_import_callback(std::move(config));

    // Set up the control block, then overwrite the arg descriptor's ptr to
    // address 0 (outside the spill region [kSpillBase, kSpillBase+8)).
    setup_bridge_control_block(engine, 42);
    auto page = engine.mutable_whole_memory();
    put_i32_le(*page, kControlBase + 8, 0); // ptr = 0 (outside spill region)

    eng::ImportObservation obs;
    obs.import_ordinal = 0;
    obs.scalar_arg = kControlBase;
    obs.param_frame = {};
    auto mem = engine.read_whole_memory();
    obs.whole_memory = *mem;

    auto reply = callback(obs);
    check(std::holds_alternative<eng::ImportAbort>(reply),
          "abort bridge span-region: reply is ImportAbort");
    check(state.last_error.has_value() &&
              *state.last_error == wh::CapabilityImportError::ArgDecodeFailed,
          "abort bridge span-region: error is ArgDecodeFailed");
    check(mock.call_count == 0, "abort bridge span-region: invoker not called");
}

} // anonymous namespace

int main() {
    test_opaque_ok();
    test_opaque_error();
    test_opaque_pending();
    test_bridge_ok();
    test_bridge_error();
    test_abort_unknown_ordinal();
    test_abort_unknown_name();
    test_abort_bridge_block_oob();
    test_abort_bridge_site_id_mismatch();
    test_abort_opaque_closure_result();
    test_abort_opaque_oversized_frame();
    test_abort_bridge_arg_count();
    test_abort_bridge_stride();
    test_abort_bridge_expected_addr();
    test_abort_bridge_wire_schema_arity();
    test_abort_bridge_span_region();

    std::cout << pass_count << "/" << test_count << " checks passed\n";
    return (pass_count == test_count) ? 0 : 1;
}
