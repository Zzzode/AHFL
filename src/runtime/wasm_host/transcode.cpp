#include "runtime/wasm_host/transcode.hpp"

#include "runtime/engine/core_wire_codec.hpp"
#include "runtime/engine/wire_value.hpp"
#include "runtime/wasm_host/frame_packer.hpp"
#include "runtime/wasm_host/frame_reader.hpp"
#include "runtime/wasm_host/frame_walk.hpp"
#include "runtime/value/value.hpp"

#include "ahfl/runtime/ahfl_host.h"
#include "base/json/json_value.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ahfl::runtime::wasm_host {

namespace {

namespace irc = ahfl::ir::core;
namespace eng = ahfl::runtime::core_wasm_resume_engine;
namespace wc = ahfl::runtime::wire_codec;

// Build the String regions for a P4D_TO_JSON read. The source is a P4-D frame
// (the host-packed entry shadow or a P6 producer's O_k); its String PtrLen
// payload bytes can lie in the rodata pool, the host-packed entry-payload
// arena, a bridge result-placement arena, or the transcode payload arena (a P6
// node may forward a String whose PtrLen was transcoded from an opaque input).
[[nodiscard]] std::vector<StringRegion>
build_transcode_string_regions(const irc::CoreFrameLayoutSection &section) {
    std::vector<StringRegion> regions;
    if (section.rodata_extent > 0) {
        regions.push_back(
            {section.rodata_base, section.rodata_base + section.rodata_extent});
    }
    if (section.entry_payload_capacity > 0) {
        regions.push_back({section.entry_payload_base,
                           section.entry_payload_base +
                               section.entry_payload_capacity});
    }
    for (const auto &site : section.bridge_call_sites) {
        if (site.result_payload_capacity > 0) {
            regions.push_back({site.result_payload_base,
                               site.result_payload_base +
                                   site.result_payload_capacity});
        }
    }
    if (section.transcode_payload_capacity > 0) {
        regions.push_back({section.transcode_payload_base,
                           section.transcode_payload_base +
                               section.transcode_payload_capacity});
    }
    return regions;
}

// A nonzero reply: the guest's compiled scheduler traps on it, converting the
// transcode failure to NodeFailed WITHOUT writing an event record or bumping
// completed_count (the transcode call sits before the node's event write).
[[nodiscard]] eng::ImportReply transcode_fail() {
    return eng::ImportReply{.raw_status = AHFL_CAP_ERROR};
}

// Mint the typed binding for the transcode site's boundary root. The root is
// DERIVED from the direction + source kind (never a caller-supplied NodeId),
// so a corrupt site can only name a legitimate boundary root of its own
// direction.
[[nodiscard]] std::optional<irc::VerifiedWireSchemaBinding>
mint_transcode_binding(const TranscodeConfig &config,
                       const irc::CoreFrameTranscodeSite &site) {
    std::vector<irc::CoreLowerDiagnostic> diags;
    const auto &verified = config.verified_wire;
    const auto &descriptor = config.descriptor;

    if (site.direction ==
        irc::CoreFrameTranscodeSite::Direction::P4DToJson) {
        if (site.source == irc::CoreFrameTranscodeSite::Source::Entry) {
            // The host-packed entry shadow is the workflow INPUT boundary.
            return irc::make_frame_binding_from_verified_table(
                verified, {irc::CoreWireFrameRootKind::Input}, diags);
        }
        // WH-5c.4 P1-2: a construct-capability terminal's self-transcode is
        // EXPLICITLY marked Source::CapabilityParam (the codegen sets it; the
        // host never infers a param binding from a capability-bearing node,
        // which over-minted for ordinary P6 bridge nodes). The site's node is
        // the construct terminal itself; its descriptor carries the capability
        // whose PARAM type is the transcode source.
        if (site.source == irc::CoreFrameTranscodeSite::Source::CapabilityParam) {
            if (site.source_node_ordinal >= descriptor.nodes.size()) {
                return std::nullopt;
            }
            const auto &node_desc = descriptor.nodes[site.source_node_ordinal];
            if (node_desc.all_capabilities.empty()) {
                return std::nullopt;
            }
            const auto source_symbol = node_desc.all_capabilities.front().second;
            for (const auto &cap : config.wire_table.capabilities) {
                if (cap.source_symbol == source_symbol &&
                    !cap.params.empty()) {
                    // WH-5c.4 fix-forward P2-E: never let a frame-layout site
                    // name a parameter the capability schema does not declare.
                    // Codegen emits ordinal 0 against the single-param
                    // construct shape, but the host treats an out-of-range
                    // ordinal as a corrupt module and fails closed.
                    if (site.param_ordinal >= cap.params.size()) {
                        return std::nullopt;
                    }
                    return irc::make_wire_binding_from_verified_table(
                        verified,
                        {cap.capability, source_symbol,
                         irc::CoreWireRootKind::Param, site.param_ordinal},
                        diags);
                }
            }
            return std::nullopt;
        }
        // NodeOutput: the P6 producer's own OUTPUT boundary root.
        if (site.source_node_ordinal >= descriptor.nodes.size()) {
            return std::nullopt;
        }
        const auto &node_desc = descriptor.nodes[site.source_node_ordinal];
        const auto p6_ord = node_desc.p6_block_ordinal;
        return irc::make_node_frame_binding_from_verified_table(
            verified, {p6_ord, irc::CoreWireNodeRootKind::Output}, diags);
    }

    // JSON_TO_P4D
    // WH-5c.4 P2-F: a CapabilityParam source is P4DToJson-only (the construct
    // terminal's self-transcode). A JsonToP4D site carrying it is a corrupt
    // module; fail closed rather than falling through to the NodeOutput branch
    // and minting a node-frame binding for an impossible transcode.
    if (site.source == irc::CoreFrameTranscodeSite::Source::CapabilityParam) {
        return std::nullopt;
    }
    if (site.source == irc::CoreFrameTranscodeSite::Source::Entry) {
        // The wire-JSON entry decodes to the workflow INPUT boundary.
        return irc::make_frame_binding_from_verified_table(
            verified, {irc::CoreWireFrameRootKind::Input}, diags);
    }
    if (site.target_node_ordinal == irc::kTranscodeWorkflowOutput) {
        // An opaque node's result transcoded for the workflow OUTPUT slot.
        return irc::make_frame_binding_from_verified_table(
            verified, {irc::CoreWireFrameRootKind::Output}, diags);
    }
    // NodeOutput -> a P6 consumer's INPUT boundary root.
    if (site.target_node_ordinal >= descriptor.nodes.size()) {
        return std::nullopt;
    }
    const auto p6_ord =
        descriptor.nodes[site.target_node_ordinal].p6_block_ordinal;
    return irc::make_node_frame_binding_from_verified_table(
        verified, {p6_ord, irc::CoreWireNodeRootKind::Input}, diags);
}

} // namespace

[[nodiscard]] eng::ImportCallbackResult
handle_transcode(const TranscodeConfig &config,
                 const irc::CoreFrameTranscodeSite &site,
                 const eng::ImportObservation &obs) {
    const auto &section = config.frame_section;
    const auto &wire = config.wire_table;

    auto binding = mint_transcode_binding(config, site);
    if (!binding.has_value()) {
        return transcode_fail();
    }
    const auto wire_node = binding->root();

    FrameWalkContext ctx(section, wire);

    if (site.direction ==
        irc::CoreFrameTranscodeSite::Direction::P4DToJson) {
        // P4D_TO_JSON: read the P4-D source frame at the guest-pushed address,
        // validate the reconstructed Value against the wire binding, serialize
        // to wire JSON, alloc_then_write, and reply (0, json_ptr, json_len).
        // The source address is obs.scalar_arg (the first i32 arg, which for a
        // 2-param import is the frame pointer).
        const auto regions = build_transcode_string_regions(section);
        auto value = read_value_at(ctx, obs.whole_memory, wire_node,
                                   site.layout, obs.scalar_arg, regions);
        if (!value.has_value()) {
            return transcode_fail();
        }
        // Defense-in-depth: read_value_at already walks the wire schema, but
        // the decision mandates a structural validate pass so a Value that
        // somehow escaped the read checks is rejected before serialization.
        const auto validation = wc::validate_value(*value, *binding);
        if (!validation.valid) {
            return transcode_fail();
        }
        auto json = serialize_value_for_wire_json(*value);
        if (!json.has_value()) {
            return transcode_fail();
        }
        const auto json_span = std::span<const std::uint8_t>(
            reinterpret_cast<const std::uint8_t *>(json->data()),
            json->size());
        auto ptr = config.engine.alloc_then_write(json_span);
        if (!ptr.has_value()) {
            return transcode_fail();
        }
        return eng::ImportReply{
            .raw_status = AHFL_CAP_OK,
            .result_ptr = *ptr,
            .result_len = static_cast<std::uint32_t>(json->size()),
        };
    }

    // JSON_TO_P4D: parse the wire-JSON source the guest pushed, decode it
    // under the typed binding, pack the Value INLINE into the reused static
    // shadow region (String bytes bump-allocate in the disjoint payload arena
    // via the in/out arena_cursor), and reply (0, shadow_base, shadow_extent).
    // The materializer then reads from the shadow via emit_root_base (for a
    // P6 consumer) or via shadow_root_base (for the workflow-output crossing,
    // where the materializer copies from the shadow to wf_output_base).
    const auto json_text = std::string_view(
        reinterpret_cast<const char *>(obs.param_frame.data()),
        obs.param_frame.size());
    auto parsed = ahfl::json::parse_json(json_text);
    if (!parsed.has_value()) {
        return transcode_fail();
    }
    const auto decoded = wc::decode_json(**parsed, *binding);
    if (!decoded.ok()) {
        return transcode_fail();
    }
    // Host-side defense at the actual write site: a corrupt or foreign module
    // whose frame section zeroed or misplaces the JSON_TO_P4D shadow/payload
    // spans must fail closed here, before any page write (the session admits
    // modules without the verified-schema gate, so the host cannot assume the
    // section passed the frame-section verifier). The needed extent is
    // derived from the site's layout exactly as the verifier does, and the
    // predicates mirror verify_workflow_spans: nonzero, 8-aligned, and fully
    // inside the fixed page. Without this guard the packer would write the
    // frame at guest address zero (the reserved zero page) or across the
    // page boundary for a section the verifier would have rejected.
    const auto *site_layout = ctx.layout(site.layout);
    if (site_layout == nullptr) {
        return transcode_fail();
    }
    const std::uint64_t needed_extent =
        (static_cast<std::uint64_t>(site_layout->size) + 7u) &
        ~std::uint64_t{7u};
    auto page = config.engine.mutable_whole_memory();
    if (!page.has_value()) {
        return transcode_fail();
    }
    const std::uint64_t shadow_base = section.transcode_shadow_base;
    const std::uint64_t shadow_extent = section.transcode_shadow_extent;
    const std::uint64_t payload_base = section.transcode_payload_base;
    const std::uint64_t payload_capacity =
        section.transcode_payload_capacity;
    if (shadow_base == 0 || shadow_base % 8u != 0 ||
        shadow_extent < needed_extent ||
        shadow_base + shadow_extent > page->size() ||
        payload_base == 0 || payload_base % 8u != 0 ||
        payload_capacity == 0 ||
        payload_base + payload_capacity > page->size()) {
        return transcode_fail();
    }
    const auto packed = pack_value_at(
        ctx, *page, wire_node, site.layout, *decoded.value,
        section.transcode_shadow_base, config.arena_cursor,
        section.transcode_payload_base,
        section.transcode_payload_capacity);
    if (!packed.has_value()) {
        return transcode_fail();
    }
    return eng::ImportReply{
        .raw_status = AHFL_CAP_OK,
        .result_ptr = eng::GuestPointer{section.transcode_shadow_base},
        .result_len = section.transcode_shadow_extent,
    };
}

} // namespace ahfl::runtime::wasm_host
