#include "runtime/engine/core_wasm_schema_transport.hpp"

#include "ahfl/compiler/ir/core_wire_schema.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ahfl::runtime::core_wasm_schema {

using ir::core::CoreLowerDiagnostic;
using ir::core::CoreDiagnosticSeverity;
using ir::core::CoreWireRootKind;
using ir::core::CoreWireRootSelector;
using ir::core::CoreWireSchemaTable;

bool CoreWasmWireBindingResult::has_errors() const noexcept {
    for (const auto &d : diagnostics) {
        if (d.severity == CoreDiagnosticSeverity::Error) {
            return true;
        }
    }
    return false;
}

namespace {

// The wire-schema custom section carried by the C2 writer. Kept in sync with the
// backend writer's `kWireSchemaSectionName`; the two are separate compilation
// units, so this reader validates the name it observes against this constant.
constexpr std::string_view kWireSchemaSectionName = "ahfl.wire-schema.v1";

// The exact capability import contract the C2 writer emits.
constexpr std::string_view kCapabilityImportModule = "ahfl_cap";
constexpr std::string_view kCapabilityImportFieldPrefix = "cap_";
constexpr std::uint8_t kImportKindFunction = 0;
constexpr std::uint8_t kFuncTypeForm = 0x60;
constexpr std::uint8_t kValueTypeI32 = 0x7f;

constexpr std::uint8_t kSectionCustom = 0;
constexpr std::uint8_t kSectionType = 1;
constexpr std::uint8_t kSectionImport = 2;

// Every framing / cross-check error this inspector raises itself uses ONE fixed
// code with a null source range and no echo of any observed byte, name, or value.
// C1-decoder and typed-factory diagnostics are propagated verbatim instead.
[[nodiscard]] CoreLowerDiagnostic framing_error(std::string message) {
    return CoreLowerDiagnostic{CoreDiagnosticSeverity::Error,
                               std::string(ir::core::wire_schema::kInvalid),
                               std::move(message), std::nullopt};
}

// A bounds-checked forward cursor over the module bytes. Every read validates
// against `remaining()` before advancing, so a truncated or oversized field can
// never read out of range. The u32 LEB reader enforces canonical (shortest) form.
class ByteCursor {
  public:
    explicit ByteCursor(std::span<const std::uint8_t> bytes) noexcept : bytes_(bytes) {}

    [[nodiscard]] std::size_t remaining() const noexcept { return bytes_.size() - offset_; }
    [[nodiscard]] bool at_end() const noexcept { return offset_ == bytes_.size(); }

    // Read one byte, or nullopt at end of input.
    [[nodiscard]] std::optional<std::uint8_t> byte() noexcept {
        if (remaining() == 0) {
            return std::nullopt;
        }
        return bytes_[offset_++];
    }

    // Match an exact literal prefix (e.g. the module magic + version).
    [[nodiscard]] bool match(std::span<const std::uint8_t> literal) noexcept {
        if (remaining() < literal.size()) {
            return false;
        }
        for (std::size_t i = 0; i < literal.size(); ++i) {
            if (bytes_[offset_ + i] != literal[i]) {
                return false;
            }
        }
        offset_ += literal.size();
        return true;
    }

    // Canonical unsigned LEB128 in [0, 2^32). Rejects truncation, an overlong
    // encoding (a trailing 0x00 group that a shorter encoding would omit), and any
    // value that does not fit in 32 bits.
    [[nodiscard]] std::optional<std::uint32_t> u32() noexcept {
        std::uint64_t value = 0;
        std::uint32_t shift = 0;
        while (true) {
            const auto b = byte();
            if (!b.has_value()) {
                return std::nullopt; // truncated
            }
            const std::uint8_t group = *b;
            if (shift == 28 && (group & 0x70u) != 0) {
                return std::nullopt; // does not fit in 32 bits
            }
            value |= static_cast<std::uint64_t>(group & 0x7fu) << shift;
            if ((group & 0x80u) == 0) {
                if (group == 0 && shift != 0) {
                    return std::nullopt; // overlong: a shorter encoding exists
                }
                return static_cast<std::uint32_t>(value);
            }
            shift += 7;
            if (shift >= 35) {
                return std::nullopt; // more groups than a 32-bit value can hold
            }
        }
    }

    // Borrow exactly `count` bytes as a sub-span, advancing past them.
    [[nodiscard]] std::optional<std::span<const std::uint8_t>> take(std::size_t count) noexcept {
        if (remaining() < count) {
            return std::nullopt;
        }
        const auto view = bytes_.subspan(offset_, count);
        offset_ += count;
        return view;
    }

  private:
    std::span<const std::uint8_t> bytes_;
    std::size_t offset_ = 0;
};

// Parse `cap_<decimal>` into its uint64 source symbol. The decimal spelling must
// be canonical: `0` alone is legal, but a leading zero, a sign, a non-digit, or a
// value that overflows uint64 is rejected. Mirrors the writer's to_string(symbol).
[[nodiscard]] std::optional<std::uint64_t>
parse_capability_field(std::span<const std::uint8_t> field) noexcept {
    const std::string_view text(reinterpret_cast<const char *>(field.data()), field.size());
    if (text.substr(0, kCapabilityImportFieldPrefix.size()) != kCapabilityImportFieldPrefix) {
        return std::nullopt;
    }
    const std::string_view digits = text.substr(kCapabilityImportFieldPrefix.size());
    if (digits.empty()) {
        return std::nullopt;
    }
    if (digits.size() > 1 && digits.front() == '0') {
        return std::nullopt; // leading zero is non-canonical
    }
    std::uint64_t value = 0;
    for (const char c : digits) {
        if (c < '0' || c > '9') {
            return std::nullopt; // sign or non-digit
        }
        const auto digit = static_cast<std::uint64_t>(c - '0');
        if (value > (UINT64_MAX - digit) / 10u) {
            return std::nullopt; // overflow
        }
        value = value * 10u + digit;
    }
    return value;
}

// One capability import decoded far enough to cross-check against the schema table.
struct ParsedImport {
    std::uint64_t source_symbol = 0;
    std::uint32_t type_index = 0;
};

// The framing facts extracted from a module: for each Type entry whether it is the
// exact ahfl_cap tuple signature (unreferenced value-type bytes are consumed as
// framing only, never stored), the capability imports, and the raw wire-schema
// table bytes (name framing stripped, borrowed from the caller's module_bytes).
// Populated only when framing fully succeeds.
struct ModuleFraming {
    std::vector<bool> type_is_capability_tuple;
    // WH-2: the bridge (i32)->(i32,i32) functype, accepted alongside the opaque
    // capability tuple (decision doc section 11.3). The schema table carries no
    // mode field, so the transport layer cannot distinguish a bridge capability
    // from an opaque one; it accepts BOTH functypes and rejects anything else.
    // The mode cross-check (bridge vs opaque) lives in the frame-section
    // verify_frame_bridge_sites, which sees the bridge_call_sites.
    std::vector<bool> type_is_bridge_signature;
    std::vector<ParsedImport> imports;
    std::span<const std::uint8_t> table_bytes;
};

// The exact ahfl_cap function signature: (i32, i32) -> (i32, i32, i32). Compared
// directly against the Type entry's borrowed param/result spans, so no per-entry
// value-type vector is ever allocated.
[[nodiscard]] bool
spans_are_capability_tuple(std::span<const std::uint8_t> params,
                           std::span<const std::uint8_t> results) noexcept {
    static constexpr std::array<std::uint8_t, 2> kParams{kValueTypeI32, kValueTypeI32};
    static constexpr std::array<std::uint8_t, 3> kResults{kValueTypeI32, kValueTypeI32,
                                                          kValueTypeI32};
    if (params.size() != kParams.size() || results.size() != kResults.size()) {
        return false;
    }
    for (std::size_t i = 0; i < kParams.size(); ++i) {
        if (params[i] != kParams[i]) {
            return false;
        }
    }
    for (std::size_t i = 0; i < kResults.size(); ++i) {
        if (results[i] != kResults[i]) {
            return false;
        }
    }
    return true;
}

// The bridge ahfl_cap function signature: (i32) -> (i32, i32). The P6-frame
// bridge lane (WH-2 String region authorization trusts the declared bridge
// call sites/placements) uses this 2-result shape; the transport admission
// layer must accept it alongside the opaque 3-result tuple (decision doc
// section 11.3).
[[nodiscard]] bool
spans_are_bridge_signature(std::span<const std::uint8_t> params,
                           std::span<const std::uint8_t> results) noexcept {
    static constexpr std::array<std::uint8_t, 1> kParams{kValueTypeI32};
    static constexpr std::array<std::uint8_t, 2> kResults{kValueTypeI32,
                                                          kValueTypeI32};
    if (params.size() != kParams.size() || results.size() != kResults.size()) {
        return false;
    }
    for (std::size_t i = 0; i < kParams.size(); ++i) {
        if (params[i] != kParams[i]) {
            return false;
        }
    }
    for (std::size_t i = 0; i < kResults.size(); ++i) {
        if (results[i] != kResults[i]) {
            return false;
        }
    }
    return true;
}
// ahfl_cap tuple signature. Only the func form 0x60 is supported; value-type bytes
// are consumed as framing and NOT semantically validated (full type legality is
// WebAssembly.validate's job).
[[nodiscard]] bool
parse_type_section(std::span<const std::uint8_t> payload, ModuleFraming &framing) {
    ByteCursor cursor(payload);
    const auto count = cursor.u32();
    if (!count.has_value()) {
        return false;
    }
    // Resource gate: each entry consumes at least 3 bytes (form + zero param-count
    // + zero result-count), so a count exceeding remaining()/3 cannot be real.
    // Bound BEFORE reserve/grow so an attacker count can never trigger bad_alloc.
    constexpr std::size_t kMinTypeEntryBytes = 3;
    if (*count > cursor.remaining() / kMinTypeEntryBytes) {
        return false;
    }
    framing.type_is_capability_tuple.reserve(*count);
    framing.type_is_bridge_signature.reserve(*count);
    for (std::uint32_t i = 0; i < *count; ++i) {
        const auto form = cursor.byte();
        if (!form.has_value() || *form != kFuncTypeForm) {
            return false;
        }
        const auto param_count = cursor.u32();
        if (!param_count.has_value()) {
            return false;
        }
        const auto params = cursor.take(*param_count);
        if (!params.has_value()) {
            return false;
        }
        const auto result_count = cursor.u32();
        if (!result_count.has_value()) {
            return false;
        }
        const auto results = cursor.take(*result_count);
        if (!results.has_value()) {
            return false;
        }
        framing.type_is_capability_tuple.push_back(
            spans_are_capability_tuple(*params, *results));
        framing.type_is_bridge_signature.push_back(
            spans_are_bridge_signature(*params, *results));
    }
    return cursor.at_end(); // exact-consume
}

// Parse the Import section payload. Every import must be an `ahfl_cap` function
// import with a canonical `cap_<uint64>` field; anything else fails closed.
[[nodiscard]] bool
parse_import_section(std::span<const std::uint8_t> payload, ModuleFraming &framing) {
    ByteCursor cursor(payload);
    const auto count = cursor.u32();
    if (!count.has_value()) {
        return false;
    }
    // Resource gate: each import consumes at least 4 bytes (module-len + field-len
    // + kind + typeidx); bound BEFORE reserve so an attacker count cannot bad_alloc.
    constexpr std::size_t kMinImportEntryBytes = 4;
    if (*count > cursor.remaining() / kMinImportEntryBytes) {
        return false;
    }
    framing.imports.reserve(*count);
    for (std::uint32_t i = 0; i < *count; ++i) {
        const auto module_len = cursor.u32();
        if (!module_len.has_value()) {
            return false;
        }
        const auto module_name = cursor.take(*module_len);
        if (!module_name.has_value()) {
            return false;
        }
        const std::string_view module_view(
            reinterpret_cast<const char *>(module_name->data()), module_name->size());
        if (module_view != kCapabilityImportModule) {
            return false; // non-ahfl_cap import is not part of this artifact contract
        }
        const auto field_len = cursor.u32();
        if (!field_len.has_value()) {
            return false;
        }
        const auto field_name = cursor.take(*field_len);
        if (!field_name.has_value()) {
            return false;
        }
        const auto kind = cursor.byte();
        if (!kind.has_value() || *kind != kImportKindFunction) {
            return false; // only function imports
        }
        const auto type_index = cursor.u32();
        if (!type_index.has_value()) {
            return false;
        }
        const auto source_symbol = parse_capability_field(*field_name);
        if (!source_symbol.has_value()) {
            return false;
        }
        framing.imports.push_back(ParsedImport{*source_symbol, *type_index});
    }
    return cursor.at_end(); // exact-consume
}

// Walk the module: validate header, parse Type + Import exactly once (Type first),
// size-skip every other non-custom section, and locate exactly one target custom
// section fixed at module EOF (stripping its name framing to the raw table bytes).
// Returns nullopt (with a diagnostic pushed) on any framing violation.
[[nodiscard]] std::optional<ModuleFraming>
frame_module(std::span<const std::uint8_t> module_bytes,
             std::vector<CoreLowerDiagnostic> &diagnostics) {
    static constexpr std::array<std::uint8_t, 8> kModuleHeader{
        0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00};

    ByteCursor cursor(module_bytes);
    if (!cursor.match(kModuleHeader)) {
        diagnostics.push_back(framing_error("transported module header is not wasm v1"));
        return std::nullopt;
    }

    ModuleFraming framing;
    bool have_type = false;
    bool have_import = false;
    bool have_target = false;
    std::span<const std::uint8_t> table_bytes;

    while (!cursor.at_end()) {
        // The target wire-schema custom section must be the module's FINAL section.
        // Any remaining bytes after it — a second section, a duplicate target, or a
        // stray trailing byte — trip this single fixed gate BEFORE the next section
        // header is read, so target-not-EOF is never masked by a later framing
        // error in those bytes.
        if (have_target) {
            diagnostics.push_back(
                framing_error("a section follows the wire-schema custom section"));
            return std::nullopt;
        }

        const auto section_id = cursor.byte();
        if (!section_id.has_value()) {
            diagnostics.push_back(framing_error("truncated transported module section id"));
            return std::nullopt;
        }
        const auto size = cursor.u32();
        if (!size.has_value()) {
            diagnostics.push_back(
                framing_error("transported module section size is not a canonical u32"));
            return std::nullopt;
        }
        const auto payload = cursor.take(*size);
        if (!payload.has_value()) {
            diagnostics.push_back(framing_error("transported module section exceeds its bounds"));
            return std::nullopt;
        }

        if (*section_id == kSectionCustom) {
            ByteCursor name_cursor(*payload);
            const auto name_len = name_cursor.u32();
            if (!name_len.has_value()) {
                diagnostics.push_back(
                    framing_error("custom section name length is not a canonical u32"));
                return std::nullopt;
            }
            const auto name = name_cursor.take(*name_len);
            if (!name.has_value()) {
                diagnostics.push_back(
                    framing_error("custom section name exceeds its section bounds"));
                return std::nullopt;
            }
            const std::string_view name_view(
                reinterpret_cast<const char *>(name->data()), name->size());
            if (name_view != kWireSchemaSectionName) {
                continue; // unknown non-target custom: skip after canonical framing
            }
            // Everything after the name is the raw table payload, borrowed from the
            // caller's module_bytes and consumed immediately by the C1 decoder.
            const auto table = name_cursor.take(name_cursor.remaining());
            if (!table.has_value()) {
                diagnostics.push_back(
                    framing_error("wire-schema custom section payload is malformed"));
                return std::nullopt;
            }
            table_bytes = *table;
            have_target = true;
            continue;
        }

        if (*section_id == kSectionType) {
            if (have_type) {
                diagnostics.push_back(
                    framing_error("transported module has more than one Type section"));
                return std::nullopt;
            }
            if (have_import) {
                diagnostics.push_back(
                    framing_error("transported module Type section follows its Import section"));
                return std::nullopt;
            }
            if (!parse_type_section(*payload, framing)) {
                diagnostics.push_back(
                    framing_error("transported module Type section is malformed"));
                return std::nullopt;
            }
            have_type = true;
            continue;
        }

        if (*section_id == kSectionImport) {
            if (have_import) {
                diagnostics.push_back(
                    framing_error("transported module has more than one Import section"));
                return std::nullopt;
            }
            if (!parse_import_section(*payload, framing)) {
                diagnostics.push_back(
                    framing_error("transported module Import section is malformed"));
                return std::nullopt;
            }
            have_import = true;
            continue;
        }

        // Any other non-zero section is skipped by its canonical size only. The
        // bytes were already consumed by take(); nothing else to do.
    }

    if (!have_type) {
        diagnostics.push_back(framing_error("transported module has no Type section"));
        return std::nullopt;
    }
    if (!have_import) {
        diagnostics.push_back(framing_error("transported module has no capability Import section"));
        return std::nullopt;
    }
    if (!have_target) {
        diagnostics.push_back(
            framing_error("transported module has no wire-schema custom section"));
        return std::nullopt;
    }
    framing.table_bytes = table_bytes;
    return framing;
}

} // namespace

CoreWasmWireBindingResult
make_wire_binding_from_core_wasm(std::span<const std::uint8_t> module_bytes,
                                 std::uint32_t capability_import_ordinal,
                                 CoreWireRootKind root_kind,
                                 std::uint32_t param_index) {
    CoreWasmWireBindingResult result;

    // 1. Frame the module: header + Type/Import + sole target custom at EOF.
    auto framing = frame_module(module_bytes, result.diagnostics);
    if (!framing.has_value()) {
        return result;
    }

    // 2. Decode the raw table through the C1 admission authority (magic / version
    //    / local verify / canonical re-encode). Propagate its diagnostics verbatim.
    auto decoded = ir::core::decode_core_wire_schema_table(framing->table_bytes);
    if (!decoded.ok() || !decoded.table.has_value()) {
        result.diagnostics = std::move(decoded.diagnostics);
        if (!result.has_errors()) {
            result.diagnostics.push_back(
                framing_error("transported wire-schema table failed C1 admission"));
        }
        return result;
    }
    const CoreWireSchemaTable &table = *decoded.table;

    // 3. Cross-check the module import table against the schema table. A target
    //    wire-schema section is only ever emitted for a module with reachable
    //    capability imports, so a present target with NO capability imports (or a
    //    table carrying no capabilities) is writer-impossible; reject it explicitly
    //    here — before the count-mismatch message and before the requested ordinal
    //    is honored — so the no-cap/target case has its own fixed gate rather than
    //    falling through to an ordinal-out-of-range error.
    if (framing->imports.empty()) {
        result.diagnostics.push_back(framing_error(
            "transported module has a wire-schema section but no capability imports"));
        return result;
    }
    if (table.capabilities.empty()) {
        result.diagnostics.push_back(framing_error(
            "transported wire-schema table carries no capability for the module's imports"));
        return result;
    }
    if (framing->imports.size() != table.capabilities.size()) {
        result.diagnostics.push_back(framing_error(
            "transported module import count does not match the wire-schema table"));
        return result;
    }
    // Every ordinal's source_symbol + capability-tuple/bridge signature must
    // agree before we honor the requested ordinal. The schema table carries no
    // mode field, so the transport layer accepts BOTH the opaque
    // (i32,i32)->(i32,i32,i32) tuple and the bridge (i32)->(i32,i32) shape;
    // anything else is rejected (decision doc section 11.3).
    for (std::size_t i = 0; i < framing->imports.size(); ++i) {
        const auto &imported = framing->imports[i];
        if (imported.source_symbol != table.capabilities[i].source_symbol) {
            result.diagnostics.push_back(framing_error(
                "transported module import source symbol does not match the wire-schema table"));
            return result;
        }
        const bool type_in_range =
            imported.type_index < framing->type_is_capability_tuple.size();
        const bool is_tuple =
            type_in_range && framing->type_is_capability_tuple[imported.type_index];
        const bool is_bridge =
            type_in_range && framing->type_is_bridge_signature[imported.type_index];
        if (!is_tuple && !is_bridge) {
            result.diagnostics.push_back(framing_error(
                "transported module capability import does not use the ahfl_cap tuple or bridge signature"));
            return result;
        }
    }

    // 4. Honor the requested ordinal and derive a typed selector from the TABLE
    //    (never from a caller-supplied cap id / source symbol / node id).
    if (capability_import_ordinal >= table.capabilities.size()) {
        result.diagnostics.push_back(
            framing_error("requested capability import ordinal is out of range"));
        return result;
    }
    const auto &entry = table.capabilities[capability_import_ordinal];
    const CoreWireRootSelector selector{entry.capability, entry.source_symbol, root_kind,
                                        param_index};

    // 5. Move the table ONCE into the transported-table factory, which re-runs the
    //    local verifier, cross-checks the selector against the table, and derives
    //    the root (invalid root-kind / Result-nonzero-param / Param-OOR are its
    //    SSOT). A returned nullopt carries exactly this call's factory diagnostics.
    std::vector<CoreLowerDiagnostic> binding_diagnostics;
    auto binding = ir::core::make_wire_binding_from_transported_table(
        std::move(*decoded.table), selector, binding_diagnostics);
    if (!binding.has_value()) {
        result.diagnostics = std::move(binding_diagnostics);
        if (!result.has_errors()) {
            result.diagnostics.push_back(
                framing_error("transported wire-schema binding could not be minted"));
        }
        return result;
    }
    result.binding = std::move(*binding);
    return result;
}

} // namespace ahfl::runtime::core_wasm_schema
