#include "runtime/engine/core_wasm_frame_module.hpp"

#include "base/support/sha256.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ahfl::runtime::core_wasm_frame_module {
namespace {

using ahfl::ir::core::CoreFrameLayoutSection;
using ahfl::ir::core::CoreLowerDiagnostic;
using ahfl::ir::core::CoreWireFrameRootKind;
using ahfl::ir::core::CoreWireFrameRootSelector;
using ahfl::ir::core::VerifiedWireSchemaBinding;
using ahfl::ir::core::VerifiedWireSchemaTable;

namespace irc = ahfl::ir::core;

constexpr std::uint8_t kSectionCustom = 0;
constexpr std::string_view kCoreLayoutSectionName = "ahfl.core-layout.v1";
constexpr std::string_view kWireSchemaSectionName = "ahfl.wire-schema.v1";

void add_error(std::vector<CoreLowerDiagnostic> &diagnostics, std::string message) {
    diagnostics.push_back(CoreLowerDiagnostic{ahfl::ir::core::CoreDiagnosticSeverity::Error,
                                              "core.frame-module",
                                              std::move(message),
                                              std::nullopt});
}

// Canonical-LEB section cursor. No byte is ever echoed in a diagnostic.
class Cursor {
  public:
    explicit Cursor(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

    [[nodiscard]] bool at_end() const noexcept { return offset_ == bytes_.size(); }
    [[nodiscard]] std::size_t offset() const noexcept { return offset_; }

    [[nodiscard]] std::optional<std::uint8_t> byte() {
        if (offset_ >= bytes_.size()) {
            return std::nullopt;
        }
        return bytes_[offset_++];
    }

    [[nodiscard]] std::optional<std::uint32_t> u32() {
        std::uint64_t result = 0;
        std::uint32_t shift = 0;
        while (true) {
            const auto b = byte();
            if (!b.has_value()) {
                return std::nullopt;
            }
            if (shift == 35 && (*b & 0x7fU) > 0x0fU) {
                return std::nullopt;
            }
            if (shift > 35) {
                return std::nullopt;
            }
            result |= static_cast<std::uint64_t>(*b & 0x7fU) << shift;
            if ((*b & 0x80U) == 0) {
                if (result > UINT32_MAX) {
                    return std::nullopt;
                }
                return static_cast<std::uint32_t>(result);
            }
            shift += 7;
        }
    }

    [[nodiscard]] std::optional<std::span<const std::uint8_t>> take(std::uint32_t size) {
        if (static_cast<std::uint64_t>(offset_) + size > bytes_.size()) {
            return std::nullopt;
        }
        const auto slice = bytes_.subspan(offset_, size);
        offset_ += size;
        return slice;
    }

    [[nodiscard]] bool
    match(const std::array<std::uint8_t, 8> &expected) {
        const auto slice = take(8);
        if (!slice.has_value()) {
            return false;
        }
        for (std::size_t i = 0; i < expected.size(); ++i) {
            if ((*slice)[i] != expected[i]) {
                return false;
            }
        }
        return true;
    }

  private:
    std::span<const std::uint8_t> bytes_;
    std::size_t offset_{0};
};

struct FrameFraming {
    std::span<const std::uint8_t> layout_bytes; // payload after the name framing
    std::span<const std::uint8_t> schema_bytes;
};

// Structural walk: wasm header, then sections by canonical size only. Custom
// sections are name-framed. The wire-schema section must be the FINAL section
// and the core-layout section must be the section IMMEDIATELY BEFORE it; any
// other custom section fails closed (the frame module carries exactly the two).
[[nodiscard]] std::optional<FrameFraming>
frame_module(std::span<const std::uint8_t> module_bytes,
             std::vector<CoreLowerDiagnostic> &diagnostics) {
    static constexpr std::array<std::uint8_t, 8> kHeader{0x00, 0x61, 0x73, 0x6d,
                                                        0x01, 0x00, 0x00, 0x00};
    Cursor cursor(module_bytes);
    if (!cursor.match(kHeader)) {
        add_error(diagnostics, "frame module header is not wasm v1");
        return std::nullopt;
    }

    FrameFraming framing;
    bool have_layout = false;
    bool have_schema = false;
    // The custom-section slot immediately preceding the current one: tracks the
    // "immediately before" ordering requirement.
    bool last_custom_was_layout = false;

    while (!cursor.at_end()) {
        if (have_schema) {
            add_error(diagnostics, "a section follows the frame wire-schema custom section");
            return std::nullopt;
        }
        const auto id = cursor.byte();
        const auto size = cursor.u32();
        if (!id.has_value() || !size.has_value()) {
            add_error(diagnostics, "frame module section header is truncated");
            return std::nullopt;
        }
        const auto payload = cursor.take(*size);
        if (!payload.has_value()) {
            add_error(diagnostics, "frame module section exceeds its bounds");
            return std::nullopt;
        }

        if (*id != kSectionCustom) {
            last_custom_was_layout = false;
            continue; // non-custom sections are skipped by canonical size
        }

        Cursor name_cursor(*payload);
        const auto name_len = name_cursor.u32();
        if (!name_len.has_value()) {
            add_error(diagnostics, "frame custom section name length is not a canonical u32");
            return std::nullopt;
        }
        const auto name = name_cursor.take(*name_len);
        if (!name.has_value()) {
            add_error(diagnostics, "frame custom section name exceeds its section bounds");
            return std::nullopt;
        }
        // The section body is everything after the LEB name length and the name
        // bytes inside the custom-section payload.
        const std::size_t header = name_cursor.offset();
        if (header > (*payload).size()) {
            add_error(diagnostics, "frame custom section framing is malformed");
            return std::nullopt;
        }
        const std::span<const std::uint8_t> raw_body = (*payload).subspan(header);

        const std::string_view name_view(reinterpret_cast<const char *>(name->data()),
                                         name->size());
        if (name_view == kWireSchemaSectionName) {
            framing.schema_bytes = raw_body;
            if (!have_layout || !last_custom_was_layout) {
                add_error(diagnostics,
                          "frame wire-schema section is not immediately preceded by the "
                          "core-layout section");
                return std::nullopt;
            }
            have_schema = true;
            last_custom_was_layout = false;
            continue;
        }
        if (name_view == kCoreLayoutSectionName) {
            if (have_layout) {
                add_error(diagnostics, "frame module has more than one core-layout section");
                return std::nullopt;
            }
            framing.layout_bytes = raw_body;
            have_layout = true;
            last_custom_was_layout = true;
            continue;
        }
        add_error(diagnostics, "frame module carries an unrecognized custom section");
        return std::nullopt;
    }

    if (!have_schema || !have_layout) {
        add_error(diagnostics, "frame module is missing a required frame section");
        return std::nullopt;
    }
    return framing;
}

} // namespace

bool AdmitFrameResult::has_errors() const noexcept {
    for (const auto &d : diagnostics) {
        if (d.severity == ahfl::ir::core::CoreDiagnosticSeverity::Error) {
            return true;
        }
    }
    return false;
}

AdmitFrameResult
admit_core_wasm_frame_sections(std::span<const std::uint8_t> module_bytes) {
    AdmitFrameResult result;

    auto framing = frame_module(module_bytes, result.diagnostics);
    if (!framing.has_value()) {
        return result;
    }

    // 1. Decode the core-layout payload (magic/version/local verify + canonical
    //    re-encode equality).
    auto layout_decoded = irc::decode_core_frame_layout_section(framing->layout_bytes);
    if (!layout_decoded.ok() || !layout_decoded.section.has_value()) {
        for (auto &d : layout_decoded.diagnostics) {
            result.diagnostics.push_back(std::move(d));
        }
        if (!result.has_errors()) {
            add_error(result.diagnostics, "frame core-layout section failed admission");
        }
        return result;
    }

    // 2. Decode the wire-schema payload (same canonical admission family).
    auto schema_decoded = irc::decode_core_wire_schema_table(framing->schema_bytes);
    if (!schema_decoded.ok() || !schema_decoded.table.has_value()) {
        for (auto &d : schema_decoded.diagnostics) {
            result.diagnostics.push_back(std::move(d));
        }
        if (!result.has_errors()) {
            add_error(result.diagnostics, "frame wire-schema section failed admission");
        }
        return result;
    }
    const CoreFrameLayoutSection &layout_section = *layout_decoded.section;
    const auto &wire_table = *schema_decoded.table;
    if (!wire_table.frame_roots.has_value()) {
        add_error(result.diagnostics, "frame wire-schema section carries no boundary roots");
        return result;
    }

    // 3. Layout/wire consistency at BOTH boundary roots.
    auto input_diags = irc::verify_frame_layout_wire_consistency(
        layout_section.table, layout_section.input_layout, wire_table,
        wire_table.frame_roots->input);
    auto output_diags = irc::verify_frame_layout_wire_consistency(
        layout_section.table, layout_section.output_layout, wire_table,
        wire_table.frame_roots->output);
    if (!input_diags.empty() || !output_diags.empty()) {
        auto &bag = input_diags.empty() ? output_diags : input_diags;
        for (auto &d : bag) {
            result.diagnostics.push_back(std::move(d));
        }
        add_error(result.diagnostics,
                  "frame layout and wire schema disagree at a boundary root");
        return result;
    }

    // 4. Mint the immutable wire authority and the typed frame bindings.
    auto verified_table_result = irc::make_verified_wire_schema_table(*schema_decoded.table);
    if (!verified_table_result.ok()) {
        for (auto &d : verified_table_result.diagnostics) {
            result.diagnostics.push_back(std::move(d));
        }
        return result;
    }
    std::vector<CoreLowerDiagnostic> mint_diags;
    CoreWireFrameRootSelector input_selector;
    input_selector.kind = CoreWireFrameRootKind::Input;
    CoreWireFrameRootSelector output_selector;
    output_selector.kind = CoreWireFrameRootKind::Output;
    auto input_binding = irc::make_frame_binding_from_verified_table(
        *verified_table_result.table, input_selector, mint_diags);
    auto output_binding = irc::make_frame_binding_from_verified_table(
        *verified_table_result.table, output_selector, mint_diags);
    if (!input_binding.has_value() || !output_binding.has_value()) {
        for (auto &d : mint_diags) {
            result.diagnostics.push_back(std::move(d));
        }
        add_error(result.diagnostics, "frame boundary bindings could not be minted");
        return result;
    }

    AdmittedFrameSections admitted{
        std::move(*layout_decoded.section),
        std::move(*input_binding),
        std::move(*output_binding),
        ahfl::support::sha256(module_bytes),
        ahfl::support::sha256(framing->layout_bytes),
        ahfl::support::sha256(framing->schema_bytes)};
    result.sections = std::move(admitted);
    return result;
}

} // namespace ahfl::runtime::core_wasm_frame_module
