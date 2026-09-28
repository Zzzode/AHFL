// RFC 0026 P6-1 (KR6.6) same-frontend scalar-computation producer. One checked
// program is forked to native AgentRuntime and AHFL->Core->P4-D->wasm. Unlike
// the E1 identity probe, the non-final handlers carry real scalar computation
// (Literal/ValueRef/Unary/Binary) that drives computed gotos; the result is
// observable purely through the state ids step()/run transition through, with
// no frame encoding. The probe prints the native state-entry id sequence and
// final state so the Node embedded host can drive step() and assert the same
// ids and transition_count (real Node-engine evidence, NOT wasmtime).

#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/ir/core_ir.hpp"
#include "ahfl/compiler/ir/core_layout.hpp"
#include "ahfl/compiler/ir/core_wasm_abi_constants.hpp"
#include "ahfl/compiler/ir/lowering.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"
#include "ahfl/compiler/semantics/validate.hpp"
#include "compiler/backends/wasm/core_wasm_codegen.hpp"
#include "common/project_input_support.hpp"
#include "compiler/syntax/frontend/project.hpp"
#include "runtime/engine/agent_runtime.hpp"
#include "runtime/evaluator/value.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

using namespace ahfl;

// RFC 0026 P6-4: the input-frame values this probe writes for a fixture whose
// input struct is `{ a: Int; b: Int }`, in DECLARATION order. A fixture that
// wants a specific branch supplies its own by editing these two constants; the
// probe prints the P4-D offsets it used so the Node host writes the SAME bytes.
constexpr std::int64_t kAggregateInputA = 100;
constexpr std::int64_t kAggregateInputB = 7;

// RFC 0026 P6-5: the bounded-collection fixture's element values, in INDEX order.
// Element 0 (`kCollectionInputHigh`) is ABOVE the fixture's threshold and
// element 1 (`kCollectionInputLow`) is below it, so reading the WRONG element
// (a wrong stride, a wrong header offset, or a mis-sized load) routes the branch
// the other way and is visible in the state-id path.
constexpr std::int64_t kCollectionInputHigh = 90;
constexpr std::int64_t kCollectionInputLow = 1;
// The element count the host writes into the header's `len` word. It is 2 (the
// number of meaningful elements), NOT the capacity 4: the fixture's `n > 0` test
// reads the length word, so a host that wrote the capacity instead of the count
// would still pass a `> 0` test — hence the fixture also reads an element, and
// the length word's exact value is checked by the host against this constant.
constexpr std::uint32_t kCollectionInputLen = 2;

[[nodiscard]] std::optional<ir::Program> compile_fixture(const std::filesystem::path &path) {
    const Frontend frontend;
    // CORE-GAPS: a fixture whose first line is `// @repo-std` compiles through
    // a project parse with the REPO std module root. This is the only way a
    // single fixture can declare a bounded `std::collections::Map<K,V>` frame:
    // the standalone parse does not see the std package, and the builtin
    // descriptor SSOT pins Map's (Invariant K, Covariant V) variance, which a
    // single-file fieldless declaration cannot satisfy through the exact
    // metadata-drift gate. Every other fixture keeps the plain file parse.
    bool use_repo_std = false;
    {
        std::ifstream marker(path, std::ios::binary);
        std::string first_line;
        if (marker.good()) {
            std::getline(marker, first_line);
            static constexpr std::string_view kMarker = "// @repo-std";
            if (first_line.compare(0, kMarker.size(), kMarker) == 0) {
                use_repo_std = true;
            }
        }
    }

    if (use_repo_std) {
        namespace fs = std::filesystem;
        const auto repo_root = test_support::repo_root_from_source_file(path);
        const auto root = fs::temp_directory_path() /
                          ("ahfl_p6_probe_repo_std_" + path.stem().string());
        std::error_code ec;
        fs::remove_all(root, ec);
        const auto main_path = root / "app" / "main.ahfl";
        fs::create_directories(main_path.parent_path(), ec);
        {
            std::ifstream in(path, std::ios::binary);
            std::ofstream out(main_path, std::ios::binary | std::ios::trunc);
            out << in.rdbuf();
        }
        const auto parse = parse_project(
            frontend,
            test_support::project_input_with_repo_std_for_test_file(main_path, root, main_path));
        if (parse.has_errors()) {
            parse.diagnostics.render(std::cerr);
            return std::nullopt;
        }
        const Resolver resolver;
        const auto resolve = resolver.resolve(parse.graph);
        if (resolve.has_errors()) {
            resolve.diagnostics.render(std::cerr);
            return std::nullopt;
        }
        const TypeChecker checker;
        const auto typecheck = checker.check(parse.graph, resolve);
        if (typecheck.has_errors()) {
            typecheck.diagnostics.render(std::cerr);
            return std::nullopt;
        }
        const Validator validator;
        const auto validation = validator.validate(parse.graph, resolve, typecheck);
        if (validation.has_errors()) {
            validation.diagnostics.render(std::cerr);
            return std::nullopt;
        }
        return lower_program_ir(parse.graph, resolve, typecheck);
    }

    const auto parse = frontend.parse_file(path);
    if (parse.has_errors() || parse.program == nullptr) {
        parse.diagnostics.render(std::cerr);
        return std::nullopt;
    }
    const Resolver resolver;
    const auto resolve = resolver.resolve(*parse.program);
    if (resolve.has_errors()) {
        resolve.diagnostics.render(std::cerr);
        return std::nullopt;
    }
    const TypeChecker checker;
    const auto typecheck = checker.check(*parse.program, resolve);
    if (typecheck.has_errors()) {
        typecheck.diagnostics.render(std::cerr);
        return std::nullopt;
    }
    const Validator validator;
    const auto validation = validator.validate(*parse.program, resolve, typecheck);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cerr);
        return std::nullopt;
    }
    return lower_program_ir(*parse.program, resolve, typecheck);
}

[[nodiscard]] evaluator::Value fixture_input() {
    evaluator::FieldMap fields;
    fields.set("value", std::make_unique<evaluator::Value>(evaluator::make_string("identity")));
    return evaluator::Value{evaluator::StructValue{"wasm::p6::Frame", std::move(fields)}};
}

// RFC 0026 P6-4: the aggregate fixture's input frame. The struct is
// `{ a: Int; b: Int }` (both unbounded Int -> i64), so the frame is written with
// those two typed values in DECLARATION order and the native run reads them
// through the same `input.a` / `input.b` projections the wasm path uses.
[[nodiscard]] evaluator::Value aggregate_input() {
    evaluator::FieldMap fields;
    fields.set("a", std::make_unique<evaluator::Value>(evaluator::make_int(kAggregateInputA)));
    fields.set("b", std::make_unique<evaluator::Value>(evaluator::make_int(kAggregateInputB)));
    return evaluator::Value{evaluator::StructValue{"wasm::p6::Frame", std::move(fields)}};
}

// RFC 0026 P6-5: the bounded-collection fixture's input frame. The struct is
// `{ items: List<Int>(4) }` — a four-element bounded list whose elements are
// `[kCollectionInputLow, kCollectionInputHigh, ...]`. The native run reads
// through the same `input.items[i]` / `input.items.length` surfaces the wasm
// path uses, so the branch it takes pins the element offsets AND the length word
// end to end.
[[nodiscard]] evaluator::Value collection_input() {
    evaluator::FieldMap fields;
    std::vector<evaluator::Value> items;
    items.push_back(evaluator::make_int(kCollectionInputHigh));
    items.push_back(evaluator::make_int(kCollectionInputLow));
    fields.set("items",
               std::make_unique<evaluator::Value>(evaluator::make_list(std::move(items))));
    return evaluator::Value{evaluator::StructValue{"wasm::p6::Frame", std::move(fields)}};
}

// CORE-GAPS: the bounded-Map keyed-lookup fixture's input frame. The struct is
// `{ table: Map<K, V>(4) }` with two live entries; only the second entry's
// keyed lookup is used by the fixture. Entries are positional in the value
// model (the host lays them out one stride apart in key order). The exact
// keys/values are chosen by `MapSpec` from the field's K/V TypeRefs so the
// same lane covers Int/Int and Bool keys/values.
struct MapSpec {
    bool key_is_bool{false};
    bool value_is_bool{false};
    std::vector<std::int64_t> keys;
    std::vector<std::int64_t> values;
};

// Derive the two live Map entries from the input struct's leading (Map) field
// TypeRef: a Bool K or V is encoded as the i32 words 1/0 the wasm scan
// compares against, an Int K or V keeps the 3 -> 9 / 7 -> 42 evidence shape.
// Defaults to Int/Int when the TypeRef is not structurally available.
[[nodiscard]] MapSpec map_spec_for(const ir::StructDecl *input_struct) {
    MapSpec spec;
    if (input_struct != nullptr && !input_struct->fields.empty()) {
        const ir::TypeRef &map = input_struct->fields.front().type_ref;
        if (map.params.size() >= 2) {
            spec.key_is_bool = map.params[0] != nullptr &&
                               map.params[0]->kind == ir::TypeRefKind::Bool;
            spec.value_is_bool = map.params[1] != nullptr &&
                                 map.params[1]->kind == ir::TypeRefKind::Bool;
        }
    }
    if (spec.key_is_bool) {
        // slot 0: false -> 9, slot 1: true -> 42. The fixture looks up the
        // PRESENT key at slot 1 (true), so a positional miscompile that
        // returned slot 0 would route Low — the same evidence the Int map pins.
        spec.keys = {0, 1};
    } else {
        spec.keys = {3, 7};
    }
    if (spec.value_is_bool) {
        // 3 -> false (slot 0), 7 -> true (slot 1); the lookup of 7 yields true.
        spec.values = {0, 1};
    } else {
        spec.values = {9, 42};
    }
    return spec;
}

[[nodiscard]] evaluator::Value map_input(const MapSpec &spec) {
    evaluator::FieldMap fields;
    std::vector<std::pair<evaluator::Value, evaluator::Value>> entries;
    for (std::size_t i = 0; i < spec.keys.size(); ++i) {
        auto key = spec.key_is_bool ? evaluator::make_bool(spec.keys[i] != 0)
                                    : evaluator::make_int(spec.keys[i]);
        auto value = spec.value_is_bool
                         ? evaluator::make_bool(spec.values[i] != 0)
                         : evaluator::make_int(spec.values[i]);
        entries.emplace_back(std::move(key), std::move(value));
    }
    fields.set(
        "table",
        std::make_unique<evaluator::Value>(evaluator::make_map(std::move(entries))));
    return evaluator::Value{evaluator::StructValue{"app::main::Frame", std::move(fields)}};
}

// The single argument-less nominal value type naming core type `type` (index
// identity, never a name — Principle 2).
[[nodiscard]] std::optional<ir::core::CoreValueTypeId>
nominal_value_type(const ir::core::CoreProgram &program, ir::core::CoreTypeId type) {
    for (std::uint32_t i = 0; i < program.value_types.size(); ++i) {
        const auto *nominal = std::get_if<ir::core::CoreVtNominal>(&program.value_types[i].node);
        if (nominal != nullptr && nominal->base == type && nominal->args.empty()) {
            return ir::core::CoreValueTypeId{i};
        }
    }
    return std::nullopt;
}

} // namespace

int main(int argc, char **argv) {
    if (argc != 3 && argc != 4) {
        std::cerr << "usage: ahfl_core_wasm_p6_probe <source.ahfl> <output.wasm> [agent_index]\n";
        return 2;
    }
    const char *source_path = argv[1];
    const char *output_path = argv[2];
    // Optional explicit Core agent ordinal to compile. The default (0) keeps
    // the historical single-agent native-differential path; an explicit index
    // skips the single-agent native run and emits + reports that ONE agent's
    // module imports (used by the multi-agent least-privilege regression,
    // where compiling one agent must not plan another agent's capabilities).
    bool multi_agent_inspect = false;
    std::uint32_t target_agent_index = 0;
    if (argc == 4) {
        multi_agent_inspect = true;
        target_agent_index = static_cast<std::uint32_t>(std::strtoul(argv[3], nullptr, 10));
    }

    auto program = compile_fixture(source_path);
    if (!program.has_value()) {
        return 1;
    }

    const ir::AgentDecl *agent = nullptr;
    const ir::FlowDecl *flow = nullptr;
    std::vector<const ir::AgentDecl *> all_agents;
    std::vector<const ir::FlowDecl *> all_flows;
    for (const auto &decl : program->declarations) {
        if (const auto *candidate = std::get_if<ir::AgentDecl>(&decl)) {
            all_agents.push_back(candidate);
        } else if (const auto *candidate = std::get_if<ir::FlowDecl>(&decl)) {
            all_flows.push_back(candidate);
        }
    }
    if (target_agent_index >= all_agents.size() || target_agent_index >= all_flows.size()) {
        std::cerr << "requested agent index " << target_agent_index
                  << " is out of range for " << all_agents.size() << " agent(s)/"
                  << all_flows.size() << " flow(s)\n";
        return 1;
    }
    agent = all_agents[target_agent_index];
    flow = all_flows[target_agent_index];
    if (!multi_agent_inspect && (all_agents.size() != 1 || all_flows.size() != 1)) {
        std::cerr << "expected exactly one agent (pass an explicit agent index for multi-agent "
                     "fixtures)\n";
        return 1;
    }
    if (agent == nullptr || flow == nullptr) {
        std::cerr << "fixture does not contain one agent and flow\n";
        return 1;
    }

    // RFC 0026 P6-4: an input struct with a leading `a` field selects the
    // aggregate frame; every other fixture keeps the identity frame.
    const ir::StructDecl *input_struct = nullptr;
    for (const auto &decl : program->declarations) {
        if (const auto *candidate = std::get_if<ir::StructDecl>(&decl)) {
            if (candidate->name == agent->input_type_ref.canonical_name ||
                candidate->name == agent->input_type_ref.display_name) {
                input_struct = candidate;
            }
        }
    }
    const bool aggregate_fixture =
        input_struct != nullptr && !input_struct->fields.empty() &&
        input_struct->fields.front().name == "a";
    // RFC 0026 P6-5: an input struct whose leading field is `items` selects the
    // bounded-collection frame. The fixture's `items: List<Int>(4)` is the
    // container the module's collection ops address.
    const bool collection_fixture =
        input_struct != nullptr && !input_struct->fields.empty() &&
        input_struct->fields.front().name == "items";
    // CORE-GAPS: an input struct whose leading field is `table` selects the
    // bounded-Map keyed-lookup frame (Map<K, V>(4), two live entries). The K/V
    // scalar types come from the field's TypeRef so one lane covers Int and
    // Bool keys/values.
    const bool map_fixture =
        input_struct != nullptr && !input_struct->fields.empty() &&
        input_struct->fields.front().name == "table";
    const MapSpec map_spec = map_fixture ? map_spec_for(input_struct) : MapSpec{};

    // Native observation: collect every entered state NAME in order. The
    // observer requires a valid invocation agent id to fire. In multi-agent
    // inspection mode the single-agent native differential is not meaningful
    // for the selected index, so it is skipped.
    std::vector<std::string> entered_names;
    std::vector<std::uint32_t> entered_ids;
    std::size_t native_transitions = 0;
    const char *native_status = "skipped";
    std::int64_t final_id = -1;
    if (!multi_agent_inspect) {
    auto input = aggregate_fixture
                    ? aggregate_input()
                    : (collection_fixture
                           ? collection_input()
                           : (map_fixture ? map_input(map_spec) : fixture_input()));
    runtime::AgentRuntime native(*agent, *flow);
    runtime::CapabilityInvocationContext context;
    context.agent_id = runtime::AgentId{0};
    native.set_invocation_context(std::move(context));
    native.set_state_entered_observer(
        [&](runtime::AgentId, std::string_view state_name) -> runtime::AgentStateId {
            entered_names.emplace_back(state_name);
            return runtime::AgentStateId{entered_names.size() - 1};
        });
    const auto native_result = native.run(std::move(input));
    switch (native_result.status) {
    case runtime::AgentStatus::Completed:
        native_status = "completed";
        break;
    case runtime::AgentStatus::Failed:
        native_status = "failed";
        break;
    default:
        native_status = "other";
        break;
    }

    // Resolve entered state names to dense ids (the same index identity Core
    // and the wasm globals use; names are display-only).
    for (const auto &name : entered_names) {
        const auto it = std::find(agent->states.begin(), agent->states.end(), name);
        if (it == agent->states.end()) {
            std::cerr << "native entered an undeclared state '" << name << "'\n";
            return 1;
        }
        entered_ids.push_back(static_cast<std::uint32_t>(it - agent->states.begin()));
    }
    const auto final_it =
        std::find(agent->states.begin(), agent->states.end(), native_result.current_state);
    final_id = final_it == agent->states.end()
                   ? -1
                   : static_cast<std::int64_t>(final_it - agent->states.begin());
    native_transitions = native_result.stats.state_transitions;
    }

    const auto core = ir::core::lower_ahfl_to_core(*program);
    if (!core.ok()) {
        std::cerr << core.diagnostics.front().code << ": " << core.diagnostics.front().message
                  << "\n";
        return 1;
    }
    const auto layouts = ir::core::compute_core_layouts(core.program);
    if (!layouts.ok() || !layouts.table.has_value()) {
        if (!layouts.diagnostics.empty()) {
            std::cerr << layouts.diagnostics.front().code << ": "
                      << layouts.diagnostics.front().message << "\n";
        }
        return 1;
    }
    const auto emitted = backends::emit_core_wasm(
        core.program, *layouts.table,
        {ir::core::CoreAgentId{target_agent_index}, backends::WasmProfileKind::Wasi});
    if (!emitted.ok()) {
        std::cerr << emitted.diagnostics.front().code << ": " << emitted.diagnostics.front().message
                  << "\n";
        return 1;
    }

    std::ofstream out(output_path, std::ios::binary | std::ios::trunc);
    const auto &bytes = emitted.artifact->bytes;
    out.write(reinterpret_cast<const char *>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    if (!out) {
        std::cerr << "failed to write complete wasm artifact\n";
        return 1;
    }

    if (multi_agent_inspect) {
        // Least-privilege regression: report ONLY the capability imports the
        // selected agent's module plans. A pure agent must import none even
        // when another agent in the same program reaches an effectful fn.
        std::cout << "agent=" << target_agent_index << " imports=";
        for (std::size_t i = 0; i < emitted.artifact->imports.size(); ++i) {
            std::cout << (i == 0 ? "" : ",") << emitted.artifact->imports[i];
        }
        std::cout << "\n";
        return 0;
    }

    // RFC 0026 P6-7 frame-bridge v2 rung V2-A: for a COMPUTED FINAL
    // fixture, report the exact P4-D frame facts the Node host needs to
    // pack an input and validate the runv output frame: the runv root
    // bases/root sizes, and every scalar leaf word `@offset:width`
    // (width 32/64) of the input and output nominals. The host mirrors
    // these facts instead of re-deriving offsets; it pins the expected
    // semantic values itself (a node-only lane, NOT a census case).
    if (emitted.descriptor.has_value() && emitted.descriptor->frame.has_value() &&
        emitted.descriptor->frame->final_kind == "computed") {
        const auto &frame = *emitted.descriptor->frame;
        auto emit_leaves = [&](auto &&self, ir::core::CoreLayoutId id,
                               std::uint64_t base, std::string &acc,
                               bool &first) -> void {
            const auto &lay = layouts.table->layouts[id.value];
            if (const auto *s = std::get_if<ir::core::CoreLayoutStruct>(&lay.shape)) {
                for (std::uint32_t i = 0; i < s->field_layouts.size(); ++i) {
                    self(self, s->field_layouts[i], base + s->field_offsets[i],
                         acc, first);
                }
                return;
            }
            if (const auto *e = std::get_if<ir::core::CoreLayoutEnum>(&lay.shape)) {
                // Discriminant at offset 0.
                if (!first) acc += ",";
                acc += "@" + std::to_string(base) + ":32";
                first = false;
                // Every variant payload's leaf words, at payload_offset.
                for (const ir::core::CoreLayoutId payload : e->variant_payload_layouts) {
                    self(self, payload, base + e->payload_offset, acc, first);
                }
                return;
            }
            if (const auto *sc =
                    std::get_if<ir::core::CoreLayoutScalar>(&lay.shape)) {
                if (!first) acc += ",";
                acc += "@" + std::to_string(base) + ":" +
                        (sc->repr == ir::core::CoreScalarRepr::I64 ? "64" : "32");
                first = false;
            }
        };
        auto nominal_layout = [&](ir::core::CoreTypeId type)
                                  -> std::optional<ir::core::CoreLayoutId> {
            const auto vt = nominal_value_type(core.program, type);
            if (!vt.has_value() || vt->value >= layouts.table->value_layouts.size()) {
                return std::nullopt;
            }
            return layouts.table->value_layouts[vt->value];
        };
        const auto in_id = nominal_layout(core.program.agents.front().input_type);
        const auto out_id = nominal_layout(core.program.agents.front().output_type);
        std::cout << "computed_final=1 input_base=" << frame.input_base
                  << " output_base=" << frame.output_base
                  << " input_size=" << frame.input_size
                  << " output_size=" << frame.output_size;
        if (in_id.has_value()) {
            std::string leaves;
            bool first = true;
            emit_leaves(emit_leaves, *in_id, 0, leaves, first);
            std::cout << " inputs=" << leaves;
        }
        if (out_id.has_value()) {
            std::string leaves;
            bool first = true;
            emit_leaves(emit_leaves, *out_id, 0, leaves, first);
            std::cout << " outputs=" << leaves;
        }
        std::cout << "\n";

        // RFC 0026 P6-7 frame-bridge v2 rung V2-B fix-forward: the Node hosts
        // also pack the input-payload ARENA (bounded List<String> fixtures) and
        // read inlined String PtrLen words (enum/passthrough fixtures), so
        // report:
        //   * every inline PtrLen slot offset of the input/output roots
        //     (struct fields and enum payload unions; collection backing slots
        //     are NOT inline and are reported separately);
        //   * the payload-arena span, the disjoint container backing
        //     placements (dense layout id -> base/extent), and each inline
        //     container header's offset/stride/capacity with the PtrLen slot
        //     offsets inside ONE element subtree.
        // Layout-only facts; the host pins the string contents and the arena
        // accounting itself.
        if (emitted.descriptor->frame_section.has_value()) {
            const auto &section = *emitted.descriptor->frame_section;
            auto ptrlen_slots = [&](auto &&self, ir::core::CoreLayoutId id,
                                   std::uint64_t base,
                                   std::vector<std::uint64_t> &out) -> void {
                const auto &lay = section.table.layouts[id.value];
                if (const auto *s =
                        std::get_if<ir::core::CoreLayoutStruct>(&lay.shape)) {
                    for (std::uint32_t i = 0; i < s->field_layouts.size(); ++i) {
                        self(self, s->field_layouts[i], base + s->field_offsets[i], out);
                    }
                    return;
                }
                if (const auto *e =
                        std::get_if<ir::core::CoreLayoutEnum>(&lay.shape)) {
                    for (const ir::core::CoreLayoutId payload :
                         e->variant_payload_layouts) {
                        self(self, payload, base + e->payload_offset, out);
                    }
                    return;
                }
                if (std::holds_alternative<ir::core::CoreLayoutPtrLen>(lay.shape)) {
                    out.push_back(base);
                }
                // Scalars contribute no PtrLen; containers name backing
                // storage, never an inline PtrLen slot.
            };
            std::vector<std::uint64_t> in_ptrs;
            std::vector<std::uint64_t> out_ptrs;
            ptrlen_slots(ptrlen_slots, section.input_layout, 0, in_ptrs);
            ptrlen_slots(ptrlen_slots, section.output_layout, 0, out_ptrs);
            auto join_offsets = [](const std::vector<std::uint64_t> &offsets,
                                   char sep) {
                std::string acc;
                for (std::size_t i = 0; i < offsets.size(); ++i) {
                    if (i != 0) acc += sep;
                    acc += std::to_string(offsets[i]);
                }
                return acc;
            };
            std::cout << "ptrlen_slots input=" << join_offsets(in_ptrs, ',')
                      << " output=" << join_offsets(out_ptrs, ',') << "\n";

            struct ContainerInfo {
                ir::core::CoreLayoutId layout{};
                std::uint64_t header_offset{0};
                const ir::core::CoreLayoutContainer *container{nullptr};
            };
            std::vector<ContainerInfo> container_headers;
            auto find_containers = [&](auto &&self, ir::core::CoreLayoutId id,
                                       std::uint64_t base) -> void {
                const auto &lay = section.table.layouts[id.value];
                if (const auto *s =
                        std::get_if<ir::core::CoreLayoutStruct>(&lay.shape)) {
                    for (std::uint32_t i = 0; i < s->field_layouts.size(); ++i) {
                        self(self, s->field_layouts[i], base + s->field_offsets[i]);
                    }
                    return;
                }
                if (const auto *e =
                        std::get_if<ir::core::CoreLayoutEnum>(&lay.shape)) {
                    for (const ir::core::CoreLayoutId payload :
                         e->variant_payload_layouts) {
                        self(self, payload, base + e->payload_offset);
                    }
                    return;
                }
                if (const auto *c =
                        std::get_if<ir::core::CoreLayoutContainer>(&lay.shape)) {
                    container_headers.push_back({id, base, c});
                }
            };
            find_containers(find_containers, section.input_layout, 0);

            std::cout << "frame_arena base=" << frame.payload_arena_base
                      << " cap=" << frame.payload_arena_capacity
                      << " rodata_base=" << frame.rodata_base
                      << " rodata_extent=" << frame.rodata_extent << " placements=";
            for (std::size_t i = 0; i < section.placements.size(); ++i) {
                const auto &p = section.placements[i];
                if (i != 0) std::cout << ",";
                std::cout << p.container_layout.value << ":" << p.base << ":"
                          << p.extent;
            }
            std::cout << " containers=";
            for (std::size_t i = 0; i < container_headers.size(); ++i) {
                const auto &info = container_headers[i];
                if (i != 0) std::cout << ";";
                std::vector<std::uint64_t> element_slots;
                ptrlen_slots(ptrlen_slots, info.container->element, 0, element_slots);
                std::cout << info.layout.value << ":" << info.header_offset << ":"
                          << info.container->stride << ":"
                          << info.container->capacity << ":"
                          << info.container->element.value << ":"
                          << join_offsets(element_slots, '+');
            }
            std::cout << "\n";
        }
    }

    std::cout << "native_status=" << native_status << " entered_ids=";
    for (std::size_t i = 0; i < entered_ids.size(); ++i) {
        std::cout << (i == 0 ? "" : ",") << entered_ids[i];
    }
    std::cout << " final_state_id=" << final_id
              << " transition_count=" << native_transitions
              << " initial_state_id="
              << (std::find(agent->states.begin(), agent->states.end(), agent->initial_state) -
                  agent->states.begin())
              << "\n";

    // RFC 0026 P6-4: for the aggregate fixture, report the P4-D input frame the
    // host must write (base + per-field offset/width/value) so the Node host
    // mirrors the SAME layout instead of re-deriving it. Empty for the scalar /
    // match fixtures, which keep their own observation protocol.
    if (aggregate_fixture && input_struct != nullptr) {
        const auto value_type =
            nominal_value_type(core.program, core.program.agents.front().input_type);
        if (!value_type.has_value() || value_type->value >= layouts.table->value_layouts.size()) {
            std::cerr << "aggregate fixture input struct has no interned value type\n";
            return 1;
        }
        const auto &layout =
            layouts.table->layouts[layouts.table->value_layouts[value_type->value].value];
        const auto *structure = std::get_if<ir::core::CoreLayoutStruct>(&layout.shape);
        if (structure == nullptr || structure->field_offsets.size() != input_struct->fields.size()) {
            std::cerr << "aggregate fixture input layout is not a matching struct\n";
            return 1;
        }
        std::cout << "aggregate_base=" << ir::core::kP6AggregateInputBase << " fields=";
        for (std::size_t i = 0; i < input_struct->fields.size(); ++i) {
            const std::int64_t value =
                input_struct->fields[i].name == "a" ? kAggregateInputA : kAggregateInputB;
            if (i != 0) {
                std::cout << ",";
            }
            std::cout << input_struct->fields[i].name << "@" << structure->field_offsets[i] << ":"
                      << value;
        }
        std::cout << "\n";
    }

    // RFC 0026 P6-5: for the bounded-collection fixture, report the P4-D facts
    // the Node host needs to materialize the frame — the input frame base, the
    // container header's field offset, the P4-D element layout / stride /
    // value_offset / capacity (so the host writes elements at the SAME stride),
    // the backing base, and the element values in index order. The host mirrors
    // this report; it never re-derives a layout.
    if (collection_fixture && input_struct != nullptr) {
        const auto value_type =
            nominal_value_type(core.program, core.program.agents.front().input_type);
        if (!value_type.has_value() || value_type->value >= layouts.table->value_layouts.size()) {
            std::cerr << "collection fixture input struct has no interned value type\n";
            return 1;
        }
        const auto &input_layout =
            layouts.table->layouts[layouts.table->value_layouts[value_type->value].value];
        const auto *structure = std::get_if<ir::core::CoreLayoutStruct>(&input_layout.shape);
        if (structure == nullptr || structure->field_offsets.empty()) {
            std::cerr << "collection fixture input layout is not a matching struct\n";
            return 1;
        }
        const ir::core::CoreLayoutContainer *container = nullptr;
        if (structure->field_layouts.front().value < layouts.table->layouts.size()) {
            container = std::get_if<ir::core::CoreLayoutContainer>(
                &layouts.table->layouts[structure->field_layouts.front().value].shape);
        }
        if (container == nullptr) {
            std::cerr << "collection fixture input field has no container layout\n";
            return 1;
        }
        // Map the element's logical value type to its P4-D scalar width. The
        // fixture uses `List<Int>(4)` with UNBOUNDED Int elements (i64), which is
        // the width the module's element load/store actually uses.
        std::int64_t element_wide = 0;
        if (container->element.value < layouts.table->layouts.size()) {
            const auto *scalar = std::get_if<ir::core::CoreLayoutScalar>(
                &layouts.table->layouts[container->element.value].shape);
            if (scalar != nullptr && scalar->repr == ir::core::CoreScalarRepr::I64) {
                element_wide = 1;
            }
        }
        std::cout << "collection_base=" << ir::core::kP6AggregateInputBase
                  << " backing_base=" << ir::core::kP6CollectionBackingBase << " header_offset="
                  << structure->field_offsets.front() << " ptr_offset="
                  << ir::core::kP6CollectionHeaderPtrOffset << " len_offset="
                  << ir::core::kP6CollectionHeaderLenOffset << " len=" << kCollectionInputLen
                  << " stride=" << container->stride
                  << " value_offset=" << container->value_offset
                  << " capacity=" << container->capacity << " backing_size=" << container->backing_size
                  << " element_wide=" << element_wide << " elements=";
        for (std::uint64_t i = 0; i < container->capacity; ++i) {
            if (i != 0) {
                std::cout << ",";
            }
            std::cout << (i == 0 ? kCollectionInputHigh
                                 : (i == 1 ? kCollectionInputLow : std::int64_t{0}));
        }
        std::cout << "\n";
    }

    // CORE-GAPS: for the bounded-Map keyed-lookup fixture, report the P4-D
    // facts the Node host needs: the frame field offset of the inline
    // (ptr,len) header, the backing base, entry stride / value_offset /
    // capacity, and each entry's key and value in slot order. The host mirrors
    // every fact from this report; it never re-derives a layout.
    if (map_fixture && input_struct != nullptr) {
        const auto value_type =
            nominal_value_type(core.program, core.program.agents.front().input_type);
        if (!value_type.has_value() || value_type->value >= layouts.table->value_layouts.size()) {
            std::cerr << "map fixture input struct has no interned value type\n";
            return 1;
        }
        const auto &input_layout =
            layouts.table->layouts[layouts.table->value_layouts[value_type->value].value];
        const auto *structure = std::get_if<ir::core::CoreLayoutStruct>(&input_layout.shape);
        if (structure == nullptr || structure->field_offsets.empty()) {
            std::cerr << "map fixture input layout is not a matching struct\n";
            return 1;
        }
        const ir::core::CoreLayoutContainer *container = nullptr;
        if (structure->field_layouts.front().value < layouts.table->layouts.size()) {
            container = std::get_if<ir::core::CoreLayoutContainer>(
                &layouts.table->layouts[structure->field_layouts.front().value].shape);
        }
        if (container == nullptr || !container->value.has_value()) {
            std::cerr << "map fixture input field has no Map container layout\n";
            return 1;
        }
        std::int64_t key_wide = 0;
        if (container->element.value < layouts.table->layouts.size()) {
            const auto *scalar = std::get_if<ir::core::CoreLayoutScalar>(
                &layouts.table->layouts[container->element.value].shape);
            if (scalar != nullptr && scalar->repr == ir::core::CoreScalarRepr::I64) {
                key_wide = 1;
            }
        }
        std::int64_t value_wide = 0;
        if (container->value->value < layouts.table->layouts.size()) {
            const auto *scalar = std::get_if<ir::core::CoreLayoutScalar>(
                &layouts.table->layouts[container->value->value].shape);
            if (scalar != nullptr && scalar->repr == ir::core::CoreScalarRepr::I64) {
                value_wide = 1;
            }
        }
        constexpr std::uint32_t kMapInputLen = 2;
        auto emit_word_list = [&](const std::vector<std::int64_t> &words) {
            for (std::size_t i = 0; i < words.size(); ++i) {
                std::cout << (i == 0 ? "" : ",") << words[i];
            }
        };
        std::cout << "map_base=" << ir::core::kP6AggregateInputBase
                  << " backing_base=" << ir::core::kP6CollectionBackingBase << " header_offset="
                  << structure->field_offsets.front() << " ptr_offset="
                  << ir::core::kP6CollectionHeaderPtrOffset << " len_offset="
                  << ir::core::kP6CollectionHeaderLenOffset << " len=" << kMapInputLen
                  << " stride=" << container->stride
                  << " value_offset=" << container->value_offset
                  << " capacity=" << container->capacity
                  << " backing_size=" << container->backing_size
                  << " key_wide=" << key_wide << " value_wide=" << value_wide
                  << " keys=";
        emit_word_list(map_spec.keys);
        std::cout << " values=";
        emit_word_list(map_spec.values);
        std::cout << "\n";
    }
    return 0;
}
