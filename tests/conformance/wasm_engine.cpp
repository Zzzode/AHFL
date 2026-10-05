#include "conformance/wasm_engine.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>

#include "ahfl/compiler/handoff/package.hpp"
#include "ahfl/compiler/ir/core_ir.hpp"
#include "ahfl/compiler/ir/core_layout.hpp"
#include "conformance/compile_source.hpp"

namespace ahfl::conformance {

namespace {

namespace json = ahfl::json;
using ahfl::backends::CoreWasmFrameContract;
namespace irc = ahfl::ir::core;

[[nodiscard]] std::unique_ptr<json::JsonValue> jstr(std::string value) {
    return json::JsonValue::make_string(std::move(value));
}

[[nodiscard]] std::unique_ptr<json::JsonValue> juint(std::uint64_t value) {
    return json::JsonValue::make_int(static_cast<std::int64_t>(value));
}

[[nodiscard]] const char *mock_status_name(CapabilityOutcomeStatus status) {
    switch (status) {
    case CapabilityOutcomeStatus::Ok:
        return "ok";
    case CapabilityOutcomeStatus::Error:
        return "error";
    case CapabilityOutcomeStatus::Pending:
        return "pending";
    }
    return "error";
}

[[nodiscard]] const char *frame_contract_name(CoreWasmFrameContract contract) {
    switch (contract) {
    case CoreWasmFrameContract::WireJson:
        return "wire_json";
    case CoreWasmFrameContract::P6Frame:
        return "p6_frame";
    }
    return "wire_json";
}

// Builds the scenario + capability-mock section. The frame values cross the
// opaque Core-Wasm ABI boundary as CANONICAL WIRE BYTES, so the descriptor
// carries those exact bytes as `input_wire` / mock `result_wire` strings (not a
// parsed DOM): an integral float's "2.0" spelling is only recoverable from the
// bytes, and the module forwards/writes them verbatim. The manifest parser has
// already byte-verified they are canonical.
[[nodiscard]] std::unique_ptr<json::JsonValue> build_scenarios(const ConformanceCase &manifest) {
    auto scenarios = json::JsonValue::make_array();

    // Index the mock table by canonical capability name.
    std::unordered_map<std::string, const CapabilityExpectation *> mocks_by_name;
    mocks_by_name.reserve(manifest.capabilities.size());
    for (const auto &capability : manifest.capabilities) {
        mocks_by_name.emplace(capability.name, &capability);
    }

    for (const auto &scenario : manifest.scenarios) {
        auto scenario_node = json::JsonValue::make_object();
        scenario_node->set("name", jstr(scenario.name));
        scenario_node->set("input_wire", jstr(scenario.input_json));

        const char *expected_status = "failed";
        switch (scenario.expect.run_status) {
        case ExpectedRunStatus::Completed:
            expected_status = "completed";
            break;
        case ExpectedRunStatus::Suspended:
            expected_status = "suspended";
            break;
        case ExpectedRunStatus::Failed:
            expected_status = "failed";
            break;
        }
        scenario_node->set("expected_run_status", jstr(expected_status));

        // Per-scenario ordered capability mocks, in the scenario's declared
        // invocation order. The host replays each invocation against the named
        // capability's outcome; an `ok` mock carries its result frame bytes.
        auto mocks = json::JsonValue::make_array();
        for (const auto &called_name : scenario.expect.capability_sequence) {
            const auto found = mocks_by_name.find(called_name);
            if (found == mocks_by_name.end()) {
                return nullptr; // manifest parser already cross-checked this
            }
            const CapabilityExpectation &capability = *found->second;
            auto mock = json::JsonValue::make_object();
            mock->set("name", jstr(capability.name));
            mock->set("status", jstr(mock_status_name(capability.status)));
            if (capability.result_json.has_value()) {
                mock->set("result_wire", jstr(*capability.result_json));
            }
            mocks->push(std::move(mock));
        }
        scenario_node->set("mocks", std::move(mocks));
        scenarios->push(std::move(scenario_node));
    }
    return scenarios;
}

// Builds the reachable-import catalogue (ordinal -> wasm field + canonical
// name), the stable seam the host binds ahfl_cap through.
[[nodiscard]] std::unique_ptr<json::JsonValue>
build_imports(const ahfl::backends::CoreWasmExecutionDescriptor &descriptor) {
    auto imports = json::JsonValue::make_array();
    for (const auto &import_descriptor : descriptor.imports) {
        auto node = json::JsonValue::make_object();
        node->set("ordinal", juint(import_descriptor.ordinal));
        node->set("field", jstr(import_descriptor.field));
        node->set("name", jstr(import_descriptor.canonical_name));
        // V2-C: "bridge" imports use the additive (i32)->(i32,i32)
        // control-block functype; "opaque" is the E2 tuple forward.
        node->set("mode", jstr(import_descriptor.mode));
        imports->push(std::move(node));
    }
    return imports;
}

[[nodiscard]] std::unique_ptr<json::JsonValue>
build_agent_lane(const ahfl::backends::CoreWasmExecutionDescriptor &descriptor) {
    auto lane = json::JsonValue::make_object();
    lane->set("agent", jstr(descriptor.agent_name));
    lane->set("initial_state", juint(descriptor.initial_state));
    auto states = json::JsonValue::make_array();
    for (const auto &state : descriptor.states) {
        states->push(jstr(state));
    }
    lane->set("states", std::move(states));
    return lane;
}

[[nodiscard]] std::unique_ptr<json::JsonValue>
build_workflow_lane(const ahfl::backends::CoreWasmExecutionDescriptor &descriptor) {
    auto lane = json::JsonValue::make_object();

    auto agents = json::JsonValue::make_array();
    for (const auto &walk : descriptor.agents) {
        auto agent_node = json::JsonValue::make_object();
        agent_node->set("agent", jstr(walk.agent));
        auto walk_states = json::JsonValue::make_array();
        for (const auto &state : walk.walk) {
            walk_states->push(jstr(state));
        }
        agent_node->set("walk", std::move(walk_states));
        auto all_states = json::JsonValue::make_array();
        for (const auto &state : walk.all_states) {
            all_states->push(jstr(state));
        }
        agent_node->set("all_states", std::move(all_states));
        agents->push(std::move(agent_node));
    }
    lane->set("agents", std::move(agents));

    auto nodes = json::JsonValue::make_array();
    for (const auto &node : descriptor.nodes) {
        auto node_node = json::JsonValue::make_object();
        node_node->set("node_id", juint(node.node_id));
        node_node->set("schedule_pos", juint(node.schedule_pos));
        node_node->set("runner", juint(node.runner));
        node_node->set("has_capability", json::JsonValue::make_bool(node.has_capability));
        node_node->set("capability_ordinal", juint(node.capability_ordinal));
        node_node->set("source_symbol", juint(node.source_symbol));
        // WH-5b.3: the P6-dense block ordinal the host uses to resolve a
        // transcode site's node boundary root (node_inputs/node_outputs are
        // P6-dense, parallel to node_blocks). False/0 on an opaque node.
        node_node->set("is_p6", json::JsonValue::make_bool(node.is_p6));
        node_node->set("p6_block_ordinal", juint(node.p6_block_ordinal));
        nodes->push(std::move(node_node));
    }
    lane->set("nodes", std::move(nodes));

    // V2-D: the per-packaged-runner node-frame blocks and the entry/output
    // coordinates the p6 workflow host packs/encodes against. Present only for
    // a p6-frame workflow (the section carries the same records).
    if (descriptor.frame_section.has_value() &&
        !descriptor.frame_section->node_blocks.empty()) {
        auto blocks = json::JsonValue::make_array();
        for (const auto &block : descriptor.frame_section->node_blocks) {
            auto b = json::JsonValue::make_object();
            b->set("input_base", juint(block.input_base));
            b->set("input_size", juint(block.input_size));
            b->set("input_layout", juint(block.input_layout.value));
            b->set("context_base", juint(block.context_base));
            b->set("context_size", juint(block.context_size));
            b->set("context_layout", juint(block.context_layout.value));
            b->set("scratch_base", juint(block.scratch_base));
            b->set("scratch_size", juint(block.scratch_size));
            b->set("output_base", juint(block.output_base));
            b->set("output_size", juint(block.output_size));
            b->set("output_layout", juint(block.output_layout.value));
            blocks->push(std::move(b));
        }
        lane->set("node_blocks", std::move(blocks));
        lane->set("entry_payload_base",
                  juint(descriptor.frame_section->entry_payload_base));
        lane->set("entry_payload_capacity",
                  juint(descriptor.frame_section->entry_payload_capacity));
        lane->set("state_trace_base",
                  juint(descriptor.frame_section->state_trace_base));
        lane->set("state_trace_capacity",
                  juint(descriptor.frame_section->state_trace_capacity));
    }
    return lane;
}

// --- RFC 0026 P6-7 rung E: descriptor rendering of the verified P4-D layout
// table and the boundary wire-schema table. These mirror, value-for-value, the
// module's `ahfl.core-layout.v1` / `ahfl.wire-schema.v1` custom sections (the
// same in-memory tables used to canonically emit those sections), so the Node
// embedded host packs input and encodes output from descriptor facts with no
// second trust channel. Layout / schema node identity is a dense array index.

[[nodiscard]] std::unique_ptr<json::JsonValue> jlayout(const irc::CoreLayoutTable &table,
                                                       irc::CoreLayoutId id) {
    if (id.value >= table.layouts.size()) {
        return nullptr;
    }
    const auto &layout = table.layouts[id.value];
    auto node = json::JsonValue::make_object();
    node->set("size", juint(layout.size));
    node->set("align", juint(layout.align));
    node->set("zero_sized", json::JsonValue::make_bool(layout.is_zero_sized));
    std::visit(
        ahfl::Overloaded{
            [&](const irc::CoreLayoutScalar &s) {
                node->set("t", jstr("scalar"));
                node->set("repr",
                          jstr(s.repr == irc::CoreScalarRepr::I32
                                   ? "i32"
                                   : s.repr == irc::CoreScalarRepr::I64 ? "i64" : "f64"));
            },
            [&](const irc::CoreLayoutBytes &b) {
                node->set("t", jstr("bytes"));
                node->set("n", juint(b.byte_count));
            },
            [&](const irc::CoreLayoutPtrLen &) { node->set("t", jstr("ptrlen")); },
            [&](const irc::CoreLayoutFnRef &) { node->set("t", jstr("fnref")); },
            [&](const irc::CoreLayoutClosure &c) {
                node->set("t", jstr("closure"));
                if (c.environment.has_value()) {
                    node->set("env", juint(c.environment->value));
                } else {
                    node->set("env", json::JsonValue::make_null());
                }
            },
            [&](const irc::CoreLayoutStruct &s) {
                node->set("t", jstr("struct"));
                auto fields = json::JsonValue::make_array();
                for (std::size_t i = 0; i < s.field_offsets.size(); ++i) {
                    auto field = json::JsonValue::make_object();
                    field->set("off", juint(s.field_offsets[i]));
                    field->set("lay", juint(s.field_layouts[i].value));
                    fields->push(std::move(field));
                }
                node->set("fields", std::move(fields));
            },
            [&](const irc::CoreLayoutEnum &e) {
                node->set("t", jstr("enum"));
                node->set("tag_size", juint(e.tag_size));
                node->set("payload_offset", juint(e.payload_offset));
                auto variants = json::JsonValue::make_array();
                for (std::size_t i = 0; i < e.variant_payload_layouts.size(); ++i) {
                    auto variant = json::JsonValue::make_object();
                    variant->set("lay", juint(e.variant_payload_layouts[i].value));
                    variant->set("size", juint(i < e.variant_payload_sizes.size()
                                                   ? e.variant_payload_sizes[i]
                                                   : std::uint64_t{0}));
                    variants->push(std::move(variant));
                }
                node->set("variants", std::move(variants));
            },
            [&](const irc::CoreLayoutContainer &c) {
                node->set("t", jstr("container"));
                node->set("element", juint(c.element.value));
                if (c.value.has_value()) {
                    node->set("value", juint(c.value->value));
                } else {
                    node->set("value", json::JsonValue::make_null());
                }
                node->set("capacity", juint(c.capacity));
                node->set("stride", juint(c.stride));
                node->set("value_offset", juint(c.value_offset));
                node->set("backing_size", juint(c.backing_size));
            },
            [&](const irc::CoreLayoutUninhabited &) { node->set("t", jstr("uninhabited")); },
            [&](const irc::CoreLayoutPending &) { node->set("t", jstr("pending")); },
        },
        layout.shape);
    return node;
}

[[nodiscard]] std::unique_ptr<json::JsonValue>
jwire_field(const irc::CoreWireSchemaField &field) {
    auto node = json::JsonValue::make_object();
    node->set("name", jstr(field.wire_name));
    node->set("type", juint(field.type.value));
    return node;
}

[[nodiscard]] std::unique_ptr<json::JsonValue>
jwire_variant(const irc::CoreWireSchemaVariant &variant) {
    auto node = json::JsonValue::make_object();
    node->set("name", jstr(variant.wire_name));
    node->set("kind",
              jstr(variant.payload_kind == irc::CoreWirePayloadKind::Unit
                       ? "unit"
                       : variant.payload_kind == irc::CoreWirePayloadKind::Tuple ? "tuple"
                                                                                  : "struct"));
    auto slots = json::JsonValue::make_array();
    for (const auto &slot : variant.slots) {
        slots->push(jwire_field(slot));
    }
    node->set("slots", std::move(slots));
    return node;
}

[[nodiscard]] std::unique_ptr<json::JsonValue>
jwire_node(const irc::CoreWireSchemaNode &wire_node) {
    auto node = json::JsonValue::make_object();
    std::visit(
        ahfl::Overloaded{
            [&](const irc::CoreWireSchemaUnit &) { node->set("t", jstr("unit")); },
            [&](const irc::CoreWireSchemaBool &) { node->set("t", jstr("bool")); },
            [&](const irc::CoreWireSchemaInt &i) {
                node->set("t", jstr("int"));
                if (i.bounds.has_value()) {
                    node->set("lo", json::JsonValue::make_int(i.bounds->first));
                    node->set("hi", json::JsonValue::make_int(i.bounds->second));
                }
            },
            [&](const irc::CoreWireSchemaFloat &) { node->set("t", jstr("float")); },
            [&](const irc::CoreWireSchemaString &s) {
                node->set("t", jstr("string"));
                if (s.length_bounds.has_value()) {
                    node->set("lo", json::JsonValue::make_int(s.length_bounds->first));
                    node->set("hi", json::JsonValue::make_int(s.length_bounds->second));
                }
            },
            [&](const irc::CoreWireSchemaDecimal &d) {
                node->set("t", jstr("decimal"));
                node->set("scale", json::JsonValue::make_int(d.scale));
            },
            [&](const irc::CoreWireSchemaDuration &) { node->set("t", jstr("duration")); },
            [&](const irc::CoreWireSchemaTimestamp &) { node->set("t", jstr("timestamp")); },
            [&](const irc::CoreWireSchemaUuid &) { node->set("t", jstr("uuid")); },
            [&](const irc::CoreWireSchemaOption &o) {
                node->set("t", jstr("option"));
                node->set("value", juint(o.value.value));
            },
            [&](const irc::CoreWireSchemaSequence &s) {
                node->set("t", jstr("sequence"));
                node->set("kind", jstr(s.kind == irc::CoreWireSequenceKind::List ? "list"
                                                                                  : "set"));
                node->set("element", juint(s.element.value));
                if (s.capacity.has_value()) {
                    node->set("capacity", juint(*s.capacity));
                }
            },
            [&](const irc::CoreWireSchemaMap &m) {
                node->set("t", jstr("map"));
                node->set("key", juint(m.key.value));
                node->set("value", juint(m.value.value));
                if (m.capacity.has_value()) {
                    node->set("capacity", juint(*m.capacity));
                }
            },
            [&](const irc::CoreWireSchemaStruct &s) {
                node->set("t", jstr("struct"));
                node->set("name", jstr(s.wire_name));
                auto fields = json::JsonValue::make_array();
                for (const auto &field : s.fields) {
                    fields->push(jwire_field(field));
                }
                node->set("fields", std::move(fields));
            },
            [&](const irc::CoreWireSchemaEnum &e) {
                node->set("t", jstr("enum"));
                node->set("name", jstr(e.wire_name));
                auto variants = json::JsonValue::make_array();
                for (const auto &variant : e.variants) {
                    variants->push(jwire_variant(variant));
                }
                node->set("variants", std::move(variants));
            },
            [&](const irc::CoreWireSchemaTuple &t) {
                node->set("t", jstr("tuple"));
                auto elements = json::JsonValue::make_array();
                for (const auto element : t.elements) {
                    elements->push(juint(element.value));
                }
                node->set("elements", std::move(elements));
            },
        },
        wire_node.shape);
    return node;
}

[[nodiscard]] std::unique_ptr<json::JsonValue>
build_frame_lane(const ahfl::backends::CoreWasmExecutionDescriptor &descriptor) {
    const auto &lane = *descriptor.frame;
    auto node = json::JsonValue::make_object();
    node->set("final_kind", jstr(lane.final_kind));
    node->set("input_base", juint(lane.input_base));
    node->set("input_size", juint(lane.input_size));
    node->set("output_base", juint(lane.output_base));
    node->set("output_size", juint(lane.output_size));
    node->set("payload_arena_base", juint(lane.payload_arena_base));
    node->set("payload_arena_capacity", juint(lane.payload_arena_capacity));
    // V2-B: the rodata span the read walker authorizes for String PtrLen
    // payloads in addition to the packed input-payload arena.
    node->set("rodata_base", juint(lane.rodata_base));
    node->set("rodata_extent", juint(lane.rodata_extent));
    // V2-C: the capability bridge control page frame and the dense
    // per-call-site facts the host callback walks.
    node->set("bridge_control_base", juint(lane.bridge_control_base));
    node->set("bridge_block_stride", juint(lane.bridge_block_stride));
    node->set("bridge_control_extent", juint(lane.bridge_control_extent));
    node->set("bridge_spill_base", juint(lane.bridge_spill_base));
    node->set("bridge_spill_extent", juint(lane.bridge_spill_extent));
    auto bridge_sites = json::JsonValue::make_array();
    for (const auto &site : lane.bridge_call_sites) {
        auto s = json::JsonValue::make_object();
        s->set("call_site_id", juint(site.call_site_id));
        s->set("import_ordinal", juint(site.import_ordinal));
        s->set("source_symbol", juint(site.source_symbol));
        s->set("arity", juint(site.arity));
        s->set("block_offset", juint(site.block_offset));
        auto params = json::JsonValue::make_array();
        for (const auto param : site.param_wire) {
            params->push(juint(param));
        }
        s->set("params", std::move(params));
        auto param_layouts = json::JsonValue::make_array();
        for (const auto param_layout : site.param_layout) {
            param_layouts->push(juint(param_layout));
        }
        s->set("param_layout", std::move(param_layouts));
        s->set("result", juint(site.result_wire));
        s->set("result_layout", juint(site.result_layout));
        s->set("result_base", juint(site.result_base));
        s->set("result_extent", juint(site.result_extent));
        s->set("result_payload_base", juint(site.result_payload_base));
        s->set("result_payload_capacity", juint(site.result_payload_capacity));
        // v3 frame section: the site's private scalar/PtrLen spill window.
        s->set("spill_base", juint(site.spill_base));
        s->set("spill_extent", juint(site.spill_extent));
        bridge_sites->push(std::move(s));
    }
    node->set("bridge_call_sites", std::move(bridge_sites));
    const auto &section = *descriptor.frame_section;
    auto placements = json::JsonValue::make_array();
    for (const auto &placement : section.placements) {
        auto p = json::JsonValue::make_object();
        p->set("edge", juint(placement.edge_index));
        p->set("lay", juint(placement.container_layout.value));
        p->set("base", juint(placement.base));
        p->set("extent", juint(placement.extent));
        placements->push(std::move(p));
    }
    node->set("placements", std::move(placements));

    node->set("input_layout", juint(section.input_layout.value));
    node->set("output_layout", juint(section.output_layout.value));
    auto layouts = json::JsonValue::make_array();
    for (std::uint32_t i = 0; i < section.table.layouts.size(); ++i) {
        layouts->push(jlayout(section.table, irc::CoreLayoutId{i}));
    }
    node->set("layouts", std::move(layouts));
    // KR6.6: the runtime construct-heap base (String concat result region).
    // Zero on a module without String concat.
    node->set("construct_heap_base", juint(lane.construct_heap_base));
    // WH-5b.3: the deterministic encoding-boundary transcode sites and the
    // shadow/payload regions the host codec adapter packs/reads against.
    // All spans are zero on a module with no transcode sites.
    auto transcode_sites = json::JsonValue::make_array();
    for (const auto &site : section.transcode_sites) {
        auto s = json::JsonValue::make_object();
        s->set("import_ordinal", juint(site.import_ordinal));
        s->set("direction",
               jstr(site.direction ==
                            irc::CoreFrameTranscodeSite::Direction::P4DToJson
                        ? "p4d_to_json"
                        : "json_to_p4d"));
        s->set("source",
               jstr(site.source ==
                            irc::CoreFrameTranscodeSite::Source::Entry
                        ? "entry"
                        : site.source ==
                                    irc::CoreFrameTranscodeSite::Source::NodeOutput
                              ? "node_output"
                              : "capability_param"));
        s->set("param_ordinal", juint(site.param_ordinal));
        s->set("source_node_ordinal", juint(site.source_node_ordinal));
        s->set("target_node_ordinal", juint(site.target_node_ordinal));
        s->set("layout", juint(site.layout.value));
        transcode_sites->push(std::move(s));
    }
    node->set("transcode_sites", std::move(transcode_sites));
    node->set("transcode_shadow_base", juint(section.transcode_shadow_base));
    node->set("transcode_shadow_extent",
              juint(section.transcode_shadow_extent));
    node->set("transcode_payload_base",
              juint(section.transcode_payload_base));
    node->set("transcode_payload_capacity",
              juint(section.transcode_payload_capacity));
    node->set("transcode_entry_shadow_base",
              juint(section.transcode_entry_shadow_base));
    node->set("transcode_entry_shadow_extent",
              juint(section.transcode_entry_shadow_extent));
    return node;
}

[[nodiscard]] std::unique_ptr<json::JsonValue>
build_wire_schema(const irc::CoreWireSchemaTable &schema) {
    auto node = json::JsonValue::make_object();
    auto nodes = json::JsonValue::make_array();
    for (const auto &wire_node : schema.nodes) {
        nodes->push(jwire_node(wire_node));
    }
    node->set("nodes", std::move(nodes));
    if (schema.frame_roots.has_value()) {
        auto roots = json::JsonValue::make_object();
        roots->set("input", juint(schema.frame_roots->input.value));
        roots->set("output", juint(schema.frame_roots->output.value));
        // V2-D: per-packaged-runner node boundary roots (parallel to the
        // workflow_lane.node_blocks array).
        if (!schema.frame_roots->node_inputs.empty()) {
            auto node_inputs = json::JsonValue::make_array();
            for (const auto root : schema.frame_roots->node_inputs) {
                node_inputs->push(juint(root.value));
            }
            roots->set("node_inputs", std::move(node_inputs));
            auto node_outputs = json::JsonValue::make_array();
            for (const auto root : schema.frame_roots->node_outputs) {
                node_outputs->push(juint(root.value));
            }
            roots->set("node_outputs", std::move(node_outputs));
        }
        node->set("roots", std::move(roots));
    }
    // WH-5b.3: the capability wire schemas the host resolves a
    // CapabilityParam transcode binding against (source_symbol -> the
    // capability's param/result wire node ids). Empty on a module with no
    // construct-capability terminals.
    if (!schema.capabilities.empty()) {
        auto caps = json::JsonValue::make_array();
        for (const auto &cap : schema.capabilities) {
            auto c = json::JsonValue::make_object();
            c->set("source_symbol", juint(cap.source_symbol));
            auto params = json::JsonValue::make_array();
            for (const auto param : cap.params) {
                params->push(juint(param.value));
            }
            c->set("params", std::move(params));
            c->set("result", juint(cap.result.value));
            caps->push(std::move(c));
        }
        node->set("capabilities", std::move(caps));
    }
    return node;
}

[[nodiscard]] std::string
render_descriptor(const ConformanceCase &manifest,
                  const ahfl::backends::CoreWasmExecutionDescriptor &descriptor) {
    auto root = json::JsonValue::make_object();
    root->set("schema", jstr(std::string(kConformanceWasmDescriptorFormat)));
    root->set("case", jstr(manifest.source));
    root->set("entry", jstr(manifest.entry));
    root->set("kind", jstr(manifest.kind == CaseKind::Workflow ? "workflow" : "agent"));
    root->set("frame_contract", jstr(frame_contract_name(descriptor.frame_contract)));
    root->set("imports", build_imports(descriptor));

    auto event = json::JsonValue::make_object();
    event->set("log_base", juint(descriptor.event_log_base));
    event->set("header_bytes", juint(descriptor.event_header_bytes));
    event->set("record_bytes", juint(descriptor.event_record_bytes));
    event->set("records_base", juint(descriptor.event_records_base));
    root->set("event_buffer", std::move(event));
    root->set("heap_base", juint(descriptor.heap_base));
    root->set("workflow_node_count", juint(descriptor.workflow_node_count));

    // RFC 0026 P6-7 rung E: a p6-frame module carries the verified layout lane
    // and boundary wire schema the Node host packs/encodes from.
    if (descriptor.frame.has_value() && descriptor.frame_section.has_value()) {
        root->set("frame_lane", build_frame_lane(descriptor));
    }
    if (descriptor.wire_schema.has_value()) {
        root->set("wire_schema", build_wire_schema(*descriptor.wire_schema));
    }

    if (descriptor.is_workflow) {
        root->set("workflow_lane", build_workflow_lane(descriptor));
    } else {
        root->set("agent_lane", build_agent_lane(descriptor));
    }

    auto scenarios = build_scenarios(manifest);
    if (scenarios == nullptr) {
        return {};
    }
    root->set("scenarios", std::move(scenarios));
    return json::serialize_json(*root);
}

} // namespace

WasmProduceResult produce_conformance_wasm(const LoadedConformanceCase &loaded,
                                           const ConformanceScenario &scenario) {
    (void)scenario; // module emission is case-level; scenario data is in the descriptor
    const ConformanceCase &manifest = loaded.manifest;
    WasmProduceResult result;

    auto program = compile_conformance_source(loaded.source_path, result.reason);
    if (!program.has_value()) {
        result.skip = WasmProduceSkip::Blocked;
        result.code = "source";
        return result;
    }

    const auto core = ahfl::ir::core::lower_ahfl_to_core(*program);
    if (!core.ok()) {
        result.skip = WasmProduceSkip::Blocked;
        result.code = core.diagnostics.front().code;
        result.reason = core.diagnostics.front().message;
        return result;
    }

    const auto layouts = ahfl::ir::core::compute_core_layouts(core.program);
    if (!layouts.ok() || !layouts.table.has_value()) {
        result.skip = WasmProduceSkip::Blocked;
        if (!layouts.diagnostics.empty()) {
            result.code = layouts.diagnostics.front().code;
            result.reason = layouts.diagnostics.front().message;
        } else {
            result.code = "core.layout.INVALID";
        }
        return result;
    }

    ahfl::handoff::PackageMetadata package_metadata;
    package_metadata.entry_target = ahfl::handoff::ExecutableRef{
        manifest.kind == CaseKind::Agent ? ahfl::handoff::ExecutableKind::Agent
                                         : ahfl::handoff::ExecutableKind::Workflow,
        manifest.entry,
    };
    const auto entry = ahfl::backends::resolve_core_wasm_entry(core.program, &package_metadata);
    if (!entry.has_value()) {
        result.skip = WasmProduceSkip::Blocked;
        result.code = entry.error().code;
        result.reason = entry.error().message;
        return result;
    }

    const auto emitted = ahfl::backends::emit_core_wasm(
        core.program, *layouts.table, {*entry, ahfl::backends::WasmProfileKind::Wasi});
    if (!emitted.ok() || !emitted.artifact.has_value()) {
        result.skip = WasmProduceSkip::Blocked;
        if (!emitted.diagnostics.empty()) {
            result.code = emitted.diagnostics.front().code;
            result.reason = emitted.diagnostics.front().message;
        } else {
            result.code = "wasm.INTERNAL_INVALID";
        }
        return result;
    }

    if (!emitted.descriptor.has_value()) {
        result.skip = WasmProduceSkip::Blocked;
        result.code = "wasm.INTERNAL_INVALID";
        result.reason = "emitted artifact carries no execution descriptor";
        return result;
    }

    const std::string descriptor_json = render_descriptor(manifest, *emitted.descriptor);
    if (descriptor_json.empty()) {
        result.skip = WasmProduceSkip::Blocked;
        result.code = "wasm.INTERNAL_INVALID";
        result.reason = "failed to render the execution descriptor";
        return result;
    }

    result.ok = true;
    result.artifact_bytes = emitted.artifact->bytes;
    result.descriptor_json = std::move(descriptor_json);
    return result;
}

} // namespace ahfl::conformance
