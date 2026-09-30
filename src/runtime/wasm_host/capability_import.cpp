#include "runtime/wasm_host/capability_import.hpp"

#include "runtime/engine/core_wire_codec.hpp"
#include "runtime/engine/wire_value.hpp"
#include "runtime/wasm_host/frame_packer.hpp"
#include "runtime/wasm_host/frame_reader.hpp"
#include "runtime/wasm_host/frame_walk.hpp"
#include "runtime/value/value.hpp"

#include "ahfl/compiler/ir/core_wasm_abi_constants.hpp"
#include "ahfl/runtime/ahfl_host.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace ahfl::runtime::wasm_host {

namespace {

namespace eng = ::ahfl::runtime::core_wasm_resume_engine;
namespace sm = ::ahfl::runtime::core_wasm_schema_module;
namespace ir = ::ahfl::ir;

// Map a CapabilityCallStatus to the raw ahfl_cap_status word. The engine
// carries this verbatim; the guest's compiled code classifies it (the opaque
// lane's graceful ERROR/PENDING arms; the bridge lane's unreachable on any
// non-zero).
[[nodiscard]] std::uint32_t
map_status_to_raw(runtime::CapabilityCallStatus status) noexcept {
    switch (status) {
    case runtime::CapabilityCallStatus::Success:
        return AHFL_CAP_OK;
    case runtime::CapabilityCallStatus::Pending:
        return AHFL_CAP_PENDING;
    case runtime::CapabilityCallStatus::Error:
    case runtime::CapabilityCallStatus::Timeout:
    case runtime::CapabilityCallStatus::RetryExhausted:
    case runtime::CapabilityCallStatus::CircuitOpen:
        return AHFL_CAP_ERROR;
    }
    return AHFL_CAP_ERROR; // fail-closed: any unrecognized status is ERROR
}

// Find the wire-schema capability record for a source_symbol. Returns nullptr
// if not found.
[[nodiscard]] const ir::core::CoreWireCapabilitySchema *
find_capability(const ir::core::CoreWireSchemaTable &wire,
                std::uint64_t source_symbol) noexcept {
    for (const auto &cap : wire.capabilities) {
        if (cap.source_symbol == source_symbol) {
            return &cap;
        }
    }
    return nullptr;
}

// The bridge parameter classification (mirrors the JS oracle's
// bridgeParamKind). A scalar / tag-only enum / String PtrLen is spilled into
// the call site's private 8-byte slots; a struct / tuple / option /
// payload-bearing enum stays at its aggregate root address. Float / decimal /
// duration / timestamp / uuid / map / sequence are outside the frame-bridge
// subset.
enum class BridgeParamKind { Spill, Root, Reject };

[[nodiscard]] BridgeParamKind
bridge_param_kind(const ir::core::CoreWireSchemaNode &node) noexcept {
    return std::visit(
        [](const auto &shape) -> BridgeParamKind {
            using T = std::decay_t<decltype(shape)>;
            if constexpr (std::is_same_v<T, ir::core::CoreWireSchemaBool> ||
                          std::is_same_v<T, ir::core::CoreWireSchemaInt> ||
                          std::is_same_v<T, ir::core::CoreWireSchemaUnit> ||
                          std::is_same_v<T, ir::core::CoreWireSchemaString>) {
                return BridgeParamKind::Spill;
            } else if constexpr (std::is_same_v<T, ir::core::CoreWireSchemaStruct> ||
                                 std::is_same_v<T, ir::core::CoreWireSchemaOption> ||
                                 std::is_same_v<T, ir::core::CoreWireSchemaTuple>) {
                return BridgeParamKind::Root;
            } else if constexpr (std::is_same_v<T, ir::core::CoreWireSchemaEnum>) {
                const bool tag_only =
                    std::all_of(shape.variants.begin(), shape.variants.end(),
                                [](const ir::core::CoreWireSchemaVariant &v) {
                                    return v.payload_kind ==
                                           ir::core::CoreWirePayloadKind::Unit;
                                });
                return tag_only ? BridgeParamKind::Spill : BridgeParamKind::Root;
            } else {
                return BridgeParamKind::Reject;
            }
        },
        node.shape);
}

// Read a little-endian u32 from the page at `addr`. Returns nullopt on
// out-of-bounds.
[[nodiscard]] std::optional<std::uint32_t>
read_u32_le(std::span<const std::uint8_t> page, std::uint32_t addr) noexcept {
    if (addr > page.size() || page.size() - addr < 4) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(page[addr]) |
           (static_cast<std::uint32_t>(page[addr + 1]) << 8) |
           (static_cast<std::uint32_t>(page[addr + 2]) << 16) |
           (static_cast<std::uint32_t>(page[addr + 3]) << 24);
}

// Read a little-endian i32 from the page at `addr`. Returns nullopt on
// out-of-bounds.
[[nodiscard]] std::optional<std::int32_t>
read_i32_le(std::span<const std::uint8_t> page, std::uint32_t addr) noexcept {
    auto u = read_u32_le(page, addr);
    if (!u.has_value()) {
        return std::nullopt;
    }
    return static_cast<std::int32_t>(*u);
}

// Build the authorized String-payload regions for a BRIDGE argument walk: the
// packed input-payload arena, the rodata region, and every OTHER call site's
// result placement payload arena. The CURRENT call site's own result region is
// excluded (its block is this call's output, unwritten before the invocation).
[[nodiscard]] std::vector<StringRegion>
build_bridge_string_regions(const ir::core::CoreFrameLayoutSection &section,
                            std::uint32_t current_site_id) {
    std::vector<StringRegion> regions;
    const auto arena_end =
        checked_add_u32(section.payload_arena_base,
                        static_cast<std::uint64_t>(section.payload_arena_capacity));
    if (arena_end.has_value()) {
        regions.push_back({section.payload_arena_base, *arena_end});
    }
    // The workflow entry payload arena holds the packed input frame; String
    // arguments spilled from a bridge call site point into it.
    if (section.entry_payload_capacity > 0) {
        const auto entry_end =
            checked_add_u32(section.entry_payload_base,
                            static_cast<std::uint64_t>(section.entry_payload_capacity));
        if (entry_end.has_value()) {
            regions.push_back({section.entry_payload_base, *entry_end});
        }
    }
    if (section.rodata_base != 0) {
        const auto rodata_end =
            checked_add_u32(section.rodata_base,
                            static_cast<std::uint64_t>(section.rodata_extent));
        if (rodata_end.has_value()) {
            regions.push_back({section.rodata_base, *rodata_end});
        }
    }
    for (const auto &site : section.bridge_call_sites) {
        if (site.call_site_id == current_site_id) {
            continue;
        }
        if (site.result_payload_capacity == 0) {
            continue;
        }
        const auto end =
            checked_add_u32(site.result_payload_base,
                            static_cast<std::uint64_t>(site.result_payload_capacity));
        if (end.has_value()) {
            regions.push_back({site.result_payload_base, *end});
        }
    }
    return regions;
}

// Build the authorized aggregate-root regions for a BRIDGE argument walk:
//   * workflow lane: every packaged node's I_k / C_k / scratch_k block span;
//   * agent lane: the packed input frame, the fixed context frame, the
//     constructor scratch arena;
// plus every OTHER call site's disjoint result placement. The current site's
// own result is excluded in both modes.
[[nodiscard]] std::vector<StringRegion>
build_bridge_root_regions(const ir::core::CoreFrameLayoutSection &section,
                          std::uint32_t current_site_id) {
    std::vector<StringRegion> regions;

    if (!section.node_blocks.empty()) {
        // Workflow lane: every packaged node's input/context/scratch span.
        for (const auto &block : section.node_blocks) {
            const auto input_end =
                checked_add_u32(block.input_base, static_cast<std::uint64_t>(block.input_size));
            if (input_end.has_value()) {
                regions.push_back({block.input_base, *input_end});
            }
            if (block.context_size > 0) {
                const auto ctx_end = checked_add_u32(
                    block.context_base, static_cast<std::uint64_t>(block.context_size));
                if (ctx_end.has_value()) {
                    regions.push_back({block.context_base, *ctx_end});
                }
            }
            if (block.scratch_size > 0) {
                const auto scratch_end = checked_add_u32(
                    block.scratch_base, static_cast<std::uint64_t>(block.scratch_size));
                if (scratch_end.has_value()) {
                    regions.push_back({block.scratch_base, *scratch_end});
                }
            }
        }
    } else {
        // Agent lane: the packed input frame, the fixed context frame, the
        // constructor scratch arena.
        const auto input_layout_size =
            (section.input_layout.value < section.table.layouts.size())
                ? section.table.layouts[section.input_layout.value].size
                : 0;
        const auto input_end = checked_add_u32(
            ir::core::kP6AggregateInputBase, static_cast<std::uint64_t>(input_layout_size));
        if (input_end.has_value()) {
            regions.push_back({ir::core::kP6AggregateInputBase, *input_end});
        }
        regions.push_back(
            {ir::core::kP6AggregateContextBase, ir::core::kP6AggregateScratchBase});
        regions.push_back(
            {ir::core::kP6AggregateScratchBase, ir::core::kP6AggregateOutputBase});
    }

    // Every OTHER call site's disjoint result placement.
    for (const auto &site : section.bridge_call_sites) {
        if (site.call_site_id == current_site_id) {
            continue;
        }
        const auto end =
            checked_add_u32(site.result_base, static_cast<std::uint64_t>(site.result_extent));
        if (end.has_value()) {
            regions.push_back({site.result_base, *end});
        }
    }
    return regions;
}

// The opaque lane: the module wrote a wire-JSON argument envelope at
// (ptr, len); the host decodes it under the wire-schema param binding,
// invokes the capability, validates the result under the result binding,
// serializes it to wire JSON, alloc_then_writes it, and replies
// (status, result_ptr, result_len).
[[nodiscard]] eng::ImportCallbackResult
handle_opaque(const CapabilityImportConfig &config,
              const sm::VerifiedCoreWasmCallSite &call_site,
              const eng::ImportObservation &obs) {
    const auto param_binding = call_site.param_binding();
    const auto &wire = param_binding.table();
    const auto *cap = find_capability(wire, call_site.source_symbol());
    if (cap == nullptr) {
        config.state.last_error = CapabilityImportError::CapabilityNotInWireSchema;
        return eng::ImportAbort{};
    }

    const auto name = config.name_resolver(call_site.source_symbol());
    if (!name.has_value()) {
        config.state.last_error = CapabilityImportError::CapabilityNameUnknown;
        return eng::ImportAbort{};
    }

    // Schema-bound decode of the wire-JSON argument envelope. A2 admission
    // guarantees exactly-one-parameter (the opaque lane is always arity-1),
    // so the envelope is either a bare struct JSON (Struct param) or a
    // {"value":...} wrapper (non-Struct param).
    const std::string_view json_text(
        reinterpret_cast<const char *>(obs.param_frame.data()), obs.param_frame.size());
    auto parsed_env = parse_args_from_wire_json(json_text, wire, cap->params);
    if (!parsed_env.has_value() || parsed_env->args.size() != 1) {
        config.state.last_error = CapabilityImportError::ParamSchemaInvalid;
        return eng::ImportAbort{};
    }

    // Schema-bound decode: rejects JSON that does not match the param binding.
    // Pre-effect: the capability is NEVER invoked on a param-schema violation.
    auto decoded = wire_codec::decode_json(*parsed_env->args[0], param_binding);
    if (!decoded.ok()) {
        config.state.last_error = CapabilityImportError::ParamSchemaInvalid;
        return eng::ImportAbort{};
    }

    // Invoke the capability with the single arity-1 decoded parameter.
    // P2-2: set the per-call source_symbol so the session layer resolves the
    // owning agent/node per-import, not by name-keyed first-wins.
    config.context.source_capability_symbol_id = call_site.source_symbol();
    std::vector<runtime::Value> args;
    args.push_back(std::move(*decoded.value));
    auto result = config.invoker(config.context, *name, args);
    const auto raw_status = map_status_to_raw(result.status);

    if (result.status != runtime::CapabilityCallStatus::Success) {
        // A capability that executed but failed/pending is a raw non-OK reply,
        // never an abort (2026-09-30 decision, Option A).
        return eng::ImportReply{raw_status, eng::GuestPointer{0}, 0};
    }

    // Schema-bound validate of the result against the result binding.
    // Post-effect: the capability WAS invoked and returned Success, but the
    // host rejects the result as a host fault (never a capability fault).
    auto value =
        std::move(result.value).value_or(runtime::Value{runtime::NoneValue{}});
    auto validated = wire_codec::validate_value(value, call_site.result_binding());
    if (!validated.valid) {
        config.state.last_error = CapabilityImportError::ResultSchemaInvalid;
        return eng::ImportAbort{};
    }

    // Serialize the result to wire JSON.
    auto body = runtime::serialize_value_for_wire_json(value);
    if (!body.has_value()) {
        config.state.last_error = CapabilityImportError::ResultEncodeFailed;
        return eng::ImportAbort{};
    }

    // alloc_then_write the result frame.
    const std::span<const std::uint8_t> bytes(
        reinterpret_cast<const std::uint8_t *>(body->data()), body->size());
    auto ptr = config.engine.alloc_then_write(bytes);
    if (!ptr.has_value()) {
        config.state.last_error = CapabilityImportError::EngineAllocFailed;
        return eng::ImportAbort{};
    }

    return eng::ImportReply{raw_status, *ptr, static_cast<std::uint32_t>(body->size())};
}

// The bridge lane: the single arg is the control-block pointer; the host walks
// the dense P4-D argument spans, invokes the capability, packs the result at
// the call site's disjoint result placement, and replies (status, result_base).
[[nodiscard]] eng::ImportCallbackResult
handle_bridge(const CapabilityImportConfig &config,
              const sm::VerifiedCoreWasmCallSite &call_site,
              const eng::ImportObservation &obs) {
    const auto &section = config.frame_section;

    // Resolve the bridge site from the control-block address (never from the
    // ordinal alone: one capability legally occupies one import shared by MANY
    // dense call sites).
    const auto block_ptr = obs.scalar_arg;
    if (section.bridge_block_stride == 0) {
        config.state.last_error = CapabilityImportError::BridgeBlockOutOfRange;
        return eng::ImportAbort{};
    }
    if (block_ptr < section.bridge_control_base) {
        config.state.last_error = CapabilityImportError::BridgeBlockOutOfRange;
        return eng::ImportAbort{};
    }
    const auto rel = block_ptr - section.bridge_control_base;
    const auto control_extent = checked_mul_u32(
        static_cast<std::uint64_t>(section.bridge_call_sites.size()),
        static_cast<std::uint64_t>(section.bridge_block_stride));
    if (!control_extent.has_value() || rel >= *control_extent) {
        config.state.last_error = CapabilityImportError::BridgeBlockOutOfRange;
        return eng::ImportAbort{};
    }
    if (rel % section.bridge_block_stride != 0) {
        config.state.last_error = CapabilityImportError::BridgeBlockStride;
        return eng::ImportAbort{};
    }
    const auto block_index = rel / section.bridge_block_stride;
    if (block_index >= section.bridge_call_sites.size()) {
        config.state.last_error = CapabilityImportError::BridgeSiteNotFound;
        return eng::ImportAbort{};
    }
    const auto &site = section.bridge_call_sites[block_index];
    const auto expected_addr =
        checked_add_u32(section.bridge_control_base,
                        static_cast<std::uint64_t>(site.block_offset));
    if (!expected_addr.has_value() || block_ptr != *expected_addr) {
        config.state.last_error = CapabilityImportError::BridgeBlockStride;
        return eng::ImportAbort{};
    }

    // Cross-check: the bridge site's source_symbol must match the call site's
    // (the call site was resolved from the import ordinal).
    if (site.source_symbol != call_site.source_symbol()) {
        config.state.last_error = CapabilityImportError::BridgeSiteNotFound;
        return eng::ImportAbort{};
    }

    // Read and validate the control block header.
    const auto call_site_id = read_u32_le(obs.whole_memory, block_ptr);
    if (!call_site_id.has_value() || *call_site_id != site.call_site_id) {
        config.state.last_error = CapabilityImportError::BridgeCallSiteIdMismatch;
        return eng::ImportAbort{};
    }
    const auto arg_count = read_u32_le(obs.whole_memory, block_ptr + 4);
    if (!arg_count.has_value() || *arg_count != site.arity) {
        config.state.last_error = CapabilityImportError::BridgeArgCountMismatch;
        return eng::ImportAbort{};
    }

    // Resolve the wire-schema capability record for the arg wire node ids.
    const auto param_binding = call_site.param_binding();
    const auto &wire = param_binding.table();
    const auto *cap = find_capability(wire, call_site.source_symbol());
    if (cap == nullptr) {
        config.state.last_error = CapabilityImportError::CapabilityNotInWireSchema;
        return eng::ImportAbort{};
    }
    if (cap->params.size() != site.arity) {
        config.state.last_error = CapabilityImportError::BridgeWireSchemaMismatch;
        return eng::ImportAbort{};
    }

    const auto name = config.name_resolver(call_site.source_symbol());
    if (!name.has_value()) {
        config.state.last_error = CapabilityImportError::CapabilityNameUnknown;
        return eng::ImportAbort{};
    }

    // Build the walk context and the authorized regions.
    FrameWalkContext ctx(section, wire);
    const auto string_regions = build_bridge_string_regions(section, site.call_site_id);
    const auto root_regions = build_bridge_root_regions(section, site.call_site_id);

    // Walk the arguments.
    std::vector<runtime::Value> args;
    args.reserve(site.arity);
    for (std::uint32_t i = 0; i < site.arity; ++i) {
        const auto desc_addr = block_ptr + 8 + 8 * i;
        const auto ptr = read_i32_le(obs.whole_memory, desc_addr);
        const auto len = read_u32_le(obs.whole_memory, desc_addr + 4);
        if (!ptr.has_value() || !len.has_value()) {
            config.state.last_error = CapabilityImportError::ArgDecodeFailed;
            return eng::ImportAbort{};
        }
        const auto ptr_u = static_cast<std::uint32_t>(*ptr);

        // Bounds-check the span against the page.
        if (ptr_u > obs.whole_memory.size() ||
            *len > obs.whole_memory.size() - ptr_u) {
            config.state.last_error = CapabilityImportError::ArgDecodeFailed;
            return eng::ImportAbort{};
        }

        // Classify the parameter by its wire shape.
        if (cap->params[i].value >= wire.nodes.size()) {
            config.state.last_error = CapabilityImportError::ArgDecodeFailed;
            return eng::ImportAbort{};
        }
        const auto &wire_node = wire.nodes[cap->params[i].value];
        const auto kind = bridge_param_kind(wire_node);
        if (kind == BridgeParamKind::Reject) {
            config.state.last_error = CapabilityImportError::ArgDecodeFailed;
            return eng::ImportAbort{};
        }

        // Get the layout for this parameter.
        if (i >= site.param_layouts.size()) {
            config.state.last_error = CapabilityImportError::ArgDecodeFailed;
            return eng::ImportAbort{};
        }
        const auto layout_id = site.param_layouts[i];
        const auto *layout = ctx.layout(layout_id);
        if (layout == nullptr) {
            config.state.last_error = CapabilityImportError::ArgDecodeFailed;
            return eng::ImportAbort{};
        }

        if (kind == BridgeParamKind::Spill) {
            // Every spilled slot is one 8-byte private slot in THIS site's
            // spill window.
            const bool layout_ok =
                std::holds_alternative<ir::core::CoreLayoutScalar>(layout->shape) ||
                std::holds_alternative<ir::core::CoreLayoutPtrLen>(layout->shape) ||
                std::holds_alternative<ir::core::CoreLayoutEnum>(layout->shape);
            if (!layout_ok) {
                config.state.last_error = CapabilityImportError::ArgDecodeFailed;
                return eng::ImportAbort{};
            }
            if (*len != 8) {
                config.state.last_error = CapabilityImportError::ArgDecodeFailed;
                return eng::ImportAbort{};
            }
            const StringRegion spill_region{site.spill_base,
                                            site.spill_base + site.spill_extent};
            if (spill_region.hi <= spill_region.lo ||
                !region_contains(spill_region, ptr_u, 8)) {
                config.state.last_error = CapabilityImportError::ArgDecodeFailed;
                return eng::ImportAbort{};
            }
        } else {
            // An aggregate root stays at its existing stable address.
            const bool layout_ok =
                std::holds_alternative<ir::core::CoreLayoutStruct>(layout->shape) ||
                std::holds_alternative<ir::core::CoreLayoutEnum>(layout->shape);
            if (!layout_ok) {
                config.state.last_error = CapabilityImportError::ArgDecodeFailed;
                return eng::ImportAbort{};
            }
            if (static_cast<std::uint64_t>(*len) != layout->size) {
                config.state.last_error = CapabilityImportError::ArgDecodeFailed;
                return eng::ImportAbort{};
            }
            if (!string_in_regions(root_regions, ptr_u, *len)) {
                config.state.last_error = CapabilityImportError::ArgDecodeFailed;
                return eng::ImportAbort{};
            }
        }

        // Walk the P4-D span into a host Value.
        auto value = read_value_at(ctx, obs.whole_memory, cap->params[i], layout_id,
                                   ptr_u, string_regions);
        if (!value.has_value()) {
            config.state.last_error = CapabilityImportError::ArgDecodeFailed;
            return eng::ImportAbort{};
        }
        args.push_back(std::move(*value));
    }

    // Invoke the capability.
    // P2-2: set the per-call source_symbol so the session layer resolves the
    // owning agent/node per-import, not by name-keyed first-wins.
    config.context.source_capability_symbol_id = call_site.source_symbol();
    auto result = config.invoker(config.context, *name, args);
    const auto raw_status = map_status_to_raw(result.status);

    if (result.status != runtime::CapabilityCallStatus::Success) {
        // A capability that executed but failed/pending is a raw non-OK reply;
        // the bridge lane's module traps on any non-zero status (the bridge has
        // no graceful ERROR/PENDING arm).
        return eng::ImportReply{raw_status, eng::GuestPointer{0}, 0};
    }

    // Pack the result at the call site's disjoint result placement.
    auto page = config.engine.mutable_whole_memory();
    if (!page.has_value()) {
        config.state.last_error = CapabilityImportError::EngineMemoryFailed;
        return eng::ImportAbort{};
    }

    // Zero the whole result placement (padding reads back deterministically).
    if (site.result_base > page->size() ||
        site.result_extent > page->size() - site.result_base) {
        config.state.last_error = CapabilityImportError::BridgeResultPackFailed;
        return eng::ImportAbort{};
    }
    std::fill(page->begin() + site.result_base,
              page->begin() + site.result_base + site.result_extent,
              std::uint8_t{0});

    auto value =
        std::move(result.value).value_or(runtime::Value{runtime::NoneValue{}});
    auto arena_cursor = site.result_payload_base;
    auto packed = pack_value_at(ctx, *page, cap->result, site.result_layout, value,
                                site.result_base, arena_cursor,
                                site.result_payload_base,
                                site.result_payload_capacity);
    if (!packed.has_value()) {
        config.state.last_error = CapabilityImportError::BridgeResultPackFailed;
        return eng::ImportAbort{};
    }

    return eng::ImportReply{raw_status, eng::GuestPointer{site.result_base},
                            site.result_extent};
}

} // anonymous namespace

std::string_view to_string(CapabilityImportError error) noexcept {
    switch (error) {
    case CapabilityImportError::CallSiteNotFound:
        return "CallSiteNotFound";
    case CapabilityImportError::CapabilityNotInWireSchema:
        return "CapabilityNotInWireSchema";
    case CapabilityImportError::CapabilityNameUnknown:
        return "CapabilityNameUnknown";
    case CapabilityImportError::BridgeBlockOutOfRange:
        return "BridgeBlockOutOfRange";
    case CapabilityImportError::BridgeBlockStride:
        return "BridgeBlockStride";
    case CapabilityImportError::BridgeCallSiteIdMismatch:
        return "BridgeCallSiteIdMismatch";
    case CapabilityImportError::BridgeArgCountMismatch:
        return "BridgeArgCountMismatch";
    case CapabilityImportError::BridgeSiteNotFound:
        return "BridgeSiteNotFound";
    case CapabilityImportError::BridgeWireSchemaMismatch:
        return "BridgeWireSchemaMismatch";
    case CapabilityImportError::ArgDecodeFailed:
        return "ArgDecodeFailed";
    case CapabilityImportError::ParamSchemaInvalid:
        return "ParamSchemaInvalid";
    case CapabilityImportError::ResultSchemaInvalid:
        return "ResultSchemaInvalid";
    case CapabilityImportError::ResultEncodeFailed:
        return "ResultEncodeFailed";
    case CapabilityImportError::BridgeResultPackFailed:
        return "BridgeResultPackFailed";
    case CapabilityImportError::EngineAllocFailed:
        return "EngineAllocFailed";
    case CapabilityImportError::EngineMemoryFailed:
        return "EngineMemoryFailed";
    }
    return "Unknown";
}

// WH-4b: exposed for the session memo-replay layer (see the header comment).
std::optional<sm::VerifiedCoreWasmCallSite>
resolve_import_call_site(const sm::VerifiedCoreWasmSchemaModule &module,
                         std::uint32_t import_ordinal) {
    for (std::size_t i = 0; i < module.call_site_count(); ++i) {
        auto result = module.resolve(sm::ManifestCallSiteIndex{i});
        if (!result.ok()) {
            continue;
        }
        if (result.call_site->import_ordinal().value == import_ordinal) {
            return *result.call_site;
        }
    }
    return std::nullopt;
}

std::optional<OpaqueImportArgs>
decode_opaque_import_args(const sm::VerifiedCoreWasmSchemaModule &module,
                          std::uint32_t import_ordinal,
                          std::span<const std::uint8_t> param_frame) {
    auto call_site = resolve_import_call_site(module, import_ordinal);
    if (!call_site.has_value()) {
        return std::nullopt;
    }
    const auto param_binding = call_site->param_binding();
    const auto &wire = param_binding.table();
    const auto *cap = find_capability(wire, call_site->source_symbol());
    if (cap == nullptr) {
        return std::nullopt;
    }
    const std::string_view json_text(
        reinterpret_cast<const char *>(param_frame.data()), param_frame.size());
    auto parsed_env = parse_args_from_wire_json(json_text, wire, cap->params);
    if (!parsed_env.has_value() || parsed_env->args.size() != 1) {
        return std::nullopt;
    }
    auto decoded = wire_codec::decode_json(*parsed_env->args[0], param_binding);
    if (!decoded.ok()) {
        return std::nullopt;
    }
    OpaqueImportArgs result;
    result.source_symbol = call_site->source_symbol();
    result.args.push_back(std::move(*decoded.value));
    return result;
}

eng::ImportCallback
make_capability_import_callback(CapabilityImportConfig config) {
    return [config = std::move(config)](const eng::ImportObservation &obs)
               -> eng::ImportCallbackResult {
        // Resolve the call site from the import ordinal.
        auto call_site = resolve_import_call_site(config.module, obs.import_ordinal);
        if (!call_site.has_value()) {
            config.state.last_error = CapabilityImportError::CallSiteNotFound;
            return eng::ImportAbort{};
        }

        // Lane detection: the engine's trampoline sets param_frame only for the
        // 2-param opaque lane, so param_frame.size() > 0 selects opaque and
        // param_frame.size() == 0 selects bridge.
        if (!obs.param_frame.empty()) {
            return handle_opaque(config, *call_site, obs);
        }
        return handle_bridge(config, *call_site, obs);
    };
}

} // namespace ahfl::runtime::wasm_host
