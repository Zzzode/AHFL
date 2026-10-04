#pragma once

// Engine-independent conformance case manifest (RFC 0026 P7 / KR6.7).
//
// A conformance case is a JSON sidecar (schema `ahfl.conformance-case.v1`)
// paired with an AHFL source file. It is the neutral contract consumed by
// every execution engine (orchestration wasm, and the
// future KR6.6 computation wasm lane). This header deliberately depends on
// neither: it only parses, validates, and canonicalizes manifest data.
//
// A case carries one or more *scenarios*: one source program plus a shared
// mocked-capability table, exercised under different inputs with independently
// blessed expectations (e.g. the Priority::Low and Priority::High paths of one
// workflow). Every scenario is independently runnable by every engine adapter.
//
// Identity inside a manifest is *name-only*: canonical entry targets, state
// names, node names, and canonical capability names. Numeric engine-internal
// ids never appear in a case file (CLAUDE.md Principle 2).
//
// Wire JSON fragments (`input`, `expect.output_json`, capability
// `result_json`) must be CANONICAL wire JSON: compact bytes matching the
// `value_json` emission order (`_type`/`_enum` discriminators first, struct
// fields lexicographically sorted, no insignificant whitespace). The parser
// enforces byte equality against a canonical re-serialization of the parsed
// fragment, so a case can never pin an engine to a non-canonical encoding.
//
// The canonical float spelling is the runtime SSOT
// (`json::format_wire_float`, the same renderer behind `value_to_json`): an
// integral float keeps a decimal point ("1.0", never the bare "1"), matching
// the input wire codec which rejects integer tokens at Float nodes ("no int
// widening").

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "ahfl/base/support/diagnostics.hpp"
#include "base/json/json_value.hpp"

namespace ahfl::conformance {

inline constexpr std::string_view kConformanceCaseFormatVersion = "ahfl.conformance-case.v1";

enum class CaseKind {
    Agent,
    Workflow,
};

/// Terminal status a case expects from an engine run.
enum class ExpectedRunStatus {
    Completed,
    Suspended,
    Failed,
};

/// Mocked outcome of one capability invocation.
enum class CapabilityOutcomeStatus {
    Ok,
    Error,
    Pending,
};

/// WASM lane a case is eligible for.
///  * Orchestration: state-machine / DAG orchestration executable on the P5
///    orchestration wasm subset.
///  * Computation:   requires in-wasm expression evaluation (the KR6.6
///    computation lane); the orchestration adapter skips it with `reason`.
///  * None:          permanently host-side (e.g. host capability semantics the
///    wasm contract does not cover); the adapter skips it with `reason`.
enum class WasmEligibility {
    Orchestration,
    Computation,
    None,
};

struct CapabilityExpectation {
    std::string name;
    CapabilityOutcomeStatus status{CapabilityOutcomeStatus::Ok};
    /// Canonical wire JSON returned for an `ok` outcome (or carried as the
    /// structured error payload for `error`). Absent for `pending`.
    std::optional<std::string> result_json;
};

struct CaseExpectations {
    ExpectedRunStatus run_status{ExpectedRunStatus::Completed};
    /// Declaration-order state names expected from an agent case. Empty for
    /// workflow cases, whose exact node-order observation is a separate
    /// (later) contract.
    std::vector<std::string> state_sequence;
    /// Canonical capability names in expected invocation order.
    std::vector<std::string> capability_sequence;
    /// Canonical wire JSON of the run output; absent when the run produces no
    /// inspectable output.
    std::optional<std::string> output_json;
};

/// One executable scenario inside a case: the case's program run under a
/// distinct canonical input, with its own blessed expectations. A case always
/// carries at least one scenario; multi-path programs (e.g. a low-priority and
/// a high-priority route through the same workflow) carry one scenario per
/// path.
struct ConformanceScenario {
    /// Stable name unique within the case (used in observation documents and
    /// diagnostics); never numeric engine identity. Constrained to the
    /// path-safe charset [A-Za-z0-9_-] because the runners embed it directly
    /// into a blessing filename (<stem>.<name>.json).
    std::string name;
    /// Canonical wire JSON fed to the run as input.
    std::string input_json;
    CaseExpectations expect{};
};

/// Why the Node embedded-engine observation of an otherwise wasm-eligible case
/// is withheld from the differential. This is an EXPLICIT, manifest-pinned
/// expectation: the runner hard-fails when the observed skip set does not match
/// the declared one, so a codegen change can never silently turn a compared
/// case into a skip (or vice versa).
enum class WasmNodeObservationSkip {
    /// The module emits a canonical wire-JSON (or p6-frame) observation and it
    /// is compared. The default; the manifest field is absent.
    None,
    /// The emit path rejects the case at a KR6.6 computation-lane seam, so no
    /// Node module exists to observe.
    BlockedOnKr66,
    /// The module emits and the native wasm3 lane has a checked-in blessing,
    /// but the Node embedded host lacks the ahfl_xcode transcode
    /// adapter: the C++ WH-5b.3 host-side transcode landed, but the Node
    /// oracle JS port of ahfl_xcode is still a stub (returns [1,0,0]), so
    /// the wasm module traps at its P4D_TO_JSON / JSON_TO_P4D transcode
    /// sites. The Node observation is withheld until the JS host gains
    /// transcode support.
    HostTranscodeAwaitsNodePort,
    /// The module emits and the native wasm3 lane has a checked-in blessing,
    /// but the workflow is a MULTI-NODE all-opaque schedule
    /// (identity + capability mixed, with no ahfl_xcode transcode sites)
    /// whose per-node outputs only become host-observable through the
    /// WH-5c.5 guest stash-table join. The Node oracle host predates that
    /// join: it never reads the per-node stash slots, and its capability
    /// normalization self-test assumes the capability node sits at
    /// schedule position 0. The Node observation is withheld until the JS
    /// host ports the WH-5c.5 stash-table join; this reason MUST NOT be
    /// used for a module that emits ahfl_xcode sites (that gap is
    /// HostTranscodeAwaitsNodePort).
    NodeHostAwaitsMultiNodeStashJoin,
    /// The module emits and the native wasm3 lane has a checked-in blessing,
    /// but the workflow's wire-type matrix carries shapes the
    /// Node oracle host's P6 frame packer/reader does not yet implement
    /// (Map / Decimal / Duration / Float are outside the JS host's
    /// rung-E frame subset). The C++ host supports them (WH-5c.7); the
    /// Node observation is withheld until the JS host ports the rich
    /// wire-type matrix.
    NodeHostAwaitsRichWireTypes,
};

struct WasmEngineEligibility {
    WasmEligibility eligibility{WasmEligibility::None};
    /// Human-readable, structured skip/selection reason. Always required so the
    /// wasm adapter can report WHY a case did not run instead of merely
    /// skipping it.
    std::string reason;
    /// Pinned Node-observation expectation. `None` for every compared case; a
    /// case whose module skips the differential MUST declare the exact reason.
    WasmNodeObservationSkip node_observation_skip{WasmNodeObservationSkip::None};
};

struct EngineMatrix {
    WasmEngineEligibility wasm{};
};

struct ConformanceCase {
    std::string format_version{std::string(kConformanceCaseFormatVersion)};
    /// Repo-relative POSIX path to the AHFL source (e.g.
    /// "tests/golden/wasm/e1_identity_agent.ahfl"). Never absolute, never
    /// parent-escaping.
    std::string source;
    CaseKind kind{CaseKind::Agent};
    /// Canonical entry target ("module::Agent" / "module::Workflow").
    std::string entry;
    /// At least one scenario; names are unique within the case.
    std::vector<ConformanceScenario> scenarios;
    std::vector<CapabilityExpectation> capabilities;
    EngineMatrix engines{};
};

/// A case plus the filesystem locations resolved by `load_conformance_case`.
struct LoadedConformanceCase {
    ConformanceCase manifest{};
    std::filesystem::path sidecar_path;
    std::filesystem::path source_path;
};

struct ConformanceCaseParseResult {
    std::optional<ConformanceCase> conformance_case;
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ConformanceCaseLoadResult {
    std::optional<LoadedConformanceCase> conformance_case;
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

namespace detail {

// ---------------------------------------------------------------------------
// Canonical wire JSON emission
// ---------------------------------------------------------------------------
//
// Mirrors the runtime `value_to_json` byte conventions, applied to a
// plain JSON DOM so the validator stays engine-independent:
//   * no insignificant whitespace;
//   * floats keep the runtime SSOT spelling (json::format_wire_float): an
//     integral float renders "1.0", never the bare integer "1";
//   * struct objects (`_type`): discriminator first, remaining fields sorted;
//   * enum objects (`_enum`): `_enum`, `_variant`, then a non-empty
//     `_payload` / `_named_payload` in that fixed wire order; empty payload
//     containers are omitted (exactly as value_to_json does), so an
//     explicitly-present `"_payload":[]` is non-canonical and rejected;
//   * every other object: lexicographically sorted keys (canonical maps).

inline void emit_canonical_string(std::ostream &out, std::string_view value) {
    constexpr char kHex[] = "0123456789abcdef";
    out << '"';
    for (char c : value) {
        const auto byte = static_cast<unsigned char>(c);
        switch (c) {
        case '\\':
            out << "\\\\";
            break;
        case '"':
            out << "\\\"";
            break;
        case '\b':
            out << "\\b";
            break;
        case '\f':
            out << "\\f";
            break;
        case '\n':
            out << "\\n";
            break;
        case '\r':
            out << "\\r";
            break;
        case '\t':
            out << "\\t";
            break;
        default:
            if (byte < 0x20U) {
                out << "\\u00" << kHex[(byte >> 4U) & 0x0FU] << kHex[byte & 0x0FU];
            } else {
                out << c;
            }
            break;
        }
    }
    out << '"';
}

/// Canonical key order for an object (parser already rejected duplicate keys).
[[nodiscard]] inline std::vector<std::string> canonical_object_keys(const json::JsonValue &object) {
    std::vector<std::string> keys;
    keys.reserve(object.object_fields.size());
    for (const auto &[key, value] : object.object_fields) {
        (void)value;
        keys.push_back(key);
    }

    const auto has = [&](std::string_view key) {
        for (const auto &existing : keys) {
            if (existing == key) {
                return true;
            }
        }
        return false;
    };

    std::vector<std::string> ordered;
    ordered.reserve(keys.size());

    // Every enum discriminator key present in the DOM is consumed here, even
    // when an empty `_payload` / `_named_payload` is suppressed: otherwise the
    // sorted-remainder loop below would re-emit the suppressed key.
    std::vector<std::string> consumed;
    consumed.reserve(keys.size());

    if (has("_enum")) {
        for (std::string_view discriminator : {"_enum", "_variant", "_payload", "_named_payload"}) {
            const auto *child = object.get(discriminator);
            if (child == nullptr) {
                continue;
            }
            consumed.emplace_back(discriminator);
            // Mirror value_to_json's EnumValue emission: `_payload` and
            // `_named_payload` are written only when non-empty, so an
            // explicitly-present empty container is non-canonical.
            if ((discriminator == "_payload" && child->is_array() &&
                 child->array_items.empty()) ||
                (discriminator == "_named_payload" && child->is_object() &&
                 child->object_fields.empty())) {
                continue;
            }
            ordered.emplace_back(discriminator);
        }
    } else if (has("_type")) {
        consumed.emplace_back("_type");
        ordered.emplace_back("_type");
    }

    std::sort(keys.begin(), keys.end());
    for (auto &key : keys) {
        bool is_consumed = false;
        for (const auto &discriminator : consumed) {
            if (key == discriminator) {
                is_consumed = true;
                break;
            }
        }
        if (!is_consumed) {
            ordered.push_back(std::move(key));
        }
    }
    return ordered;
}

inline void emit_canonical_json(const json::JsonValue &value, std::ostream &out) {
    switch (value.kind) {
    case json::Kind::Null:
        out << "null";
        break;
    case json::Kind::Bool:
        out << (value.bool_val ? "true" : "false");
        break;
    case json::Kind::Int:
        if (value.number_provenance == json::NumberProvenance::SignedInteger) {
            out << value.int_val;
        } else if (value.number_provenance == json::NumberProvenance::UnsignedInteger) {
            out << value.uint_val;
        } else {
            out.setstate(std::ios_base::failbit);
        }
        break;
    case json::Kind::Float: {
        if (value.number_provenance != json::NumberProvenance::FloatSyntax &&
            value.number_provenance != json::NumberProvenance::IntegerFallback) {
            out.setstate(std::ios_base::failbit);
            break;
        }
        // Runtime SSOT spelling (same renderer as value_to_json): an integral
        // float keeps ".0", so canonical wire bytes are always valid float
        // syntax for the input codec.
        out << json::format_wire_float(value.float_val);
        break;
    }
    case json::Kind::String:
        emit_canonical_string(out, value.string_val);
        break;
    case json::Kind::Array:
        out << '[';
        for (std::size_t i = 0; i < value.array_items.size(); ++i) {
            if (i > 0) {
                out << ',';
            }
            emit_canonical_json(*value.array_items[i], out);
        }
        out << ']';
        break;
    case json::Kind::Object: {
        out << '{';
        const auto ordered_keys = canonical_object_keys(value);
        for (std::size_t i = 0; i < ordered_keys.size(); ++i) {
            if (i > 0) {
                out << ',';
            }
            const auto *child = value.get(ordered_keys[i]);
            emit_canonical_string(out, ordered_keys[i]);
            out << ':';
            if (child == nullptr) {
                out.setstate(std::ios_base::failbit);
                return;
            }
            emit_canonical_json(*child, out);
        }
        out << '}';
        break;
    }
    }
}

/// Renders `node` to its canonical wire bytes. Returns an empty string only on
/// a corrupt (hand-built) DOM — the parser never produces one.
[[nodiscard]] inline std::string canonical_json(const json::JsonValue &node) {
    std::ostringstream out;
    emit_canonical_json(node, out);
    if (out.fail()) {
        return {};
    }
    return out.str();
}

[[nodiscard]] inline bool json_is_canonical(std::string_view manifest_text,
                                            const json::JsonValue &node) {
    if (node.end_offset < node.begin_offset || node.end_offset > manifest_text.size()) {
        return false;
    }
    const auto canonical = canonical_json(node);
    if (canonical.empty()) {
        return false;
    }
    const auto original =
        manifest_text.substr(node.begin_offset, node.end_offset - node.begin_offset);
    return original == canonical;
}

// ---------------------------------------------------------------------------
// Strict schema reader
// ---------------------------------------------------------------------------

class ConformanceCaseReader {
  public:
    ConformanceCaseReader(const json::JsonValue &root,
                          std::string_view manifest_text,
                          std::string_view sidecar_name,
                          DiagnosticBag &diagnostics)
        : root_(root), manifest_text_(manifest_text), sidecar_name_(sidecar_name),
          diagnostics_(diagnostics) {}

    [[nodiscard]] std::optional<ConformanceCase> parse() {
        if (!root_.is_object()) {
            error("conformance case manifest must begin with '{'", std::nullopt);
            return std::nullopt;
        }

        std::optional<std::string> format_version;
        std::optional<std::string> source;
        std::optional<CaseKind> kind;
        std::optional<std::string> entry;
        std::vector<ConformanceScenario> scenarios;
        bool scenarios_present = false;
        std::vector<CapabilityExpectation> capabilities;
        bool capabilities_present = false;
        std::optional<EngineMatrix> engines;

        for (const auto &[key, value] : root_.object_fields) {
            if (key == "format_version") {
                format_version = require_string(*value, "format_version");
            } else if (key == "source") {
                source = require_string(*value, "source");
            } else if (key == "kind") {
                kind = parse_kind(*value);
            } else if (key == "entry") {
                entry = require_string(*value, "entry");
            } else if (key == "scenarios") {
                scenarios_present = true;
                parse_scenarios(*value, scenarios);
            } else if (key == "capabilities") {
                capabilities_present = true;
                parse_capabilities(*value, capabilities);
            } else if (key == "engines") {
                engines = parse_engines(*value);
            } else {
                error("unsupported conformance case field '" + key + "'", range_of(*value));
            }
            if (diagnostics_.has_error()) {
                return std::nullopt;
            }
        }

        require_present(format_version, "format_version");
        require_present(source, "source");
        require_present(kind, "kind");
        require_present(entry, "entry");
        if (!scenarios_present) {
            error("conformance case is missing required field 'scenarios'", std::nullopt);
        }
        if (!capabilities_present) {
            error("conformance case is missing required field 'capabilities'", std::nullopt);
        }
        require_present(engines, "engines");
        if (diagnostics_.has_error()) {
            return std::nullopt;
        }

        if (*format_version != kConformanceCaseFormatVersion) {
            error("unsupported conformance case format_version '" + *format_version +
                      "' (expected '" + std::string(kConformanceCaseFormatVersion) + "')",
                  field_range("format_version"));
            return std::nullopt;
        }

        validate_source_path(*source);
        if (entry->empty()) {
            error("conformance case field 'entry' must not be empty", field_range("entry"));
        }
        if (scenarios_present && scenarios.empty()) {
            error("conformance case field 'scenarios' must contain at least one scenario",
                  field_range("scenarios"));
        }
        if (diagnostics_.has_error()) {
            return std::nullopt;
        }

        for (const auto &scenario : scenarios) {
            validate_cross_field_semantics(*kind, capabilities, scenario);
            if (diagnostics_.has_error()) {
                return std::nullopt;
            }
        }

        // A Node-observation skip must agree with the declared wasm lane: a
        // KR6.6 blocked case never produces a module.
        const WasmEngineEligibility &wasm = engines->wasm;
        if (wasm.node_observation_skip == WasmNodeObservationSkip::BlockedOnKr66 &&
            wasm.eligibility != WasmEligibility::Computation) {
            error("engines.wasm.node_observation_skip 'blocked_kr66' requires "
                  "engines.wasm.eligible 'computation' (a KR6.6 seam blocks the emit)",
                  std::nullopt);
            return std::nullopt;
        }
        if (wasm.node_observation_skip ==
                WasmNodeObservationSkip::HostTranscodeAwaitsNodePort &&
            wasm.eligibility != WasmEligibility::Orchestration) {
            error("engines.wasm.node_observation_skip 'host_transcode_awaits_node_port' "
                  "requires engines.wasm.eligible 'orchestration' (the module emits "
                  "but the Node host lacks transcode support)",
                  std::nullopt);
            return std::nullopt;
        }
        if (wasm.node_observation_skip ==
                WasmNodeObservationSkip::NodeHostAwaitsMultiNodeStashJoin &&
            wasm.eligibility != WasmEligibility::Orchestration) {
            error("engines.wasm.node_observation_skip "
                  "'node_host_awaits_multinode_stash_join' "
                  "requires engines.wasm.eligible 'orchestration' (the module emits "
                  "but the Node host lacks the WH-5c.5 multi-node stash-table join)",
                  std::nullopt);
            return std::nullopt;
        }
        if (wasm.node_observation_skip ==
                WasmNodeObservationSkip::NodeHostAwaitsRichWireTypes &&
            wasm.eligibility != WasmEligibility::Orchestration) {
            error("engines.wasm.node_observation_skip "
                  "'node_host_awaits_rich_wire_types' "
                  "requires engines.wasm.eligible 'orchestration' (the module emits "
                  "but the Node host lacks the WH-5c.7 rich wire-type matrix)",
                  std::nullopt);
            return std::nullopt;
        }

        return ConformanceCase{
            .format_version = std::move(*format_version),
            .source = std::move(*source),
            .kind = *kind,
            .entry = std::move(*entry),
            .scenarios = std::move(scenarios),
            .capabilities = std::move(capabilities),
            .engines = std::move(*engines),
        };
    }

  private:
    const json::JsonValue &root_;
    std::string_view manifest_text_;
    std::string_view sidecar_name_;
    DiagnosticBag &diagnostics_;

    [[nodiscard]] std::optional<SourceRange> range_of(const json::JsonValue &node) const {
        if (node.end_offset <= node.begin_offset) {
            return std::nullopt;
        }
        return SourceRange{
            .begin_offset = node.begin_offset,
            .end_offset = node.end_offset,
        };
    }

    [[nodiscard]] std::optional<SourceRange> field_range(std::string_view key) const {
        const auto *value = root_.get(key);
        if (value == nullptr) {
            return std::nullopt;
        }
        return range_of(*value);
    }

    void error(std::string message, std::optional<SourceRange> range) {
        DiagnosticBuilder builder = diagnostics_.error();
        if (range.has_value()) {
            builder = std::move(builder).range(*range);
        }
        std::move(builder)
            .source_name(std::string(sidecar_name_))
            .message(std::move(message))
            .emit();
    }

    template <typename T>
    void require_present(const std::optional<T> &value, std::string_view field) {
        if (!value.has_value()) {
            error("conformance case is missing required field '" + std::string(field) + "'",
                  std::nullopt);
        }
    }

    [[nodiscard]] std::optional<std::string> require_string(const json::JsonValue &node,
                                                            std::string_view field) {
        const auto value = node.as_string();
        if (!value.has_value()) {
            error("conformance case field '" + std::string(field) + "' must be a string",
                  range_of(node));
            return std::nullopt;
        }
        return std::string(*value);
    }

    [[nodiscard]] std::optional<std::string> require_nonempty_string(const json::JsonValue &node,
                                                                     std::string_view field) {
        auto value = require_string(node, field);
        if (value.has_value() && value->empty()) {
            error("conformance case field '" + std::string(field) + "' must not be empty",
                  range_of(node));
            return std::nullopt;
        }
        return value;
    }

    /// Reads a string that is used as a single filename component (e.g. a
    /// scenario name embedded into `<stem>.<name>.json`). Rejects anything
    /// outside `[A-Za-z0-9_-]`, so a name can never contain a path separator,
    /// a `..` traversal component, a leading dot, or any other byte that would
    /// let a manifest data field drive a write outside the observations dir.
    [[nodiscard]] std::optional<std::string>
    require_path_segment(const json::JsonValue &node, std::string_view field) {
        auto value = require_nonempty_string(node, field);
        if (!value.has_value()) {
            return std::nullopt;
        }
        for (const char ch : *value) {
            const bool safe = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                              (ch >= '0' && ch <= '9') || ch == '_' || ch == '-';
            if (!safe) {
                error("conformance case field '" + std::string(field) +
                          "' must be a path-safe name matching [A-Za-z0-9_-]+, got '" + *value +
                          "'",
                      range_of(node));
                return std::nullopt;
            }
        }
        return value;
    }

    /// Extracts a fragment as canonical wire bytes, rejecting insignificant
    /// whitespace, wrong key order, or any spelling that differs from the
    /// canonical `value_json` form.
    [[nodiscard]] std::optional<std::string>
    require_canonical_wire_json(const json::JsonValue &node, std::string_view field) {
        if (!json_is_canonical(manifest_text_, node)) {
            error("conformance case field '" + std::string(field) +
                      "' must be canonical compact wire JSON (no insignificant "
                      "whitespace; discriminators first, fields sorted)",
                  range_of(node));
            return std::nullopt;
        }
        return canonical_json(node);
    }

    [[nodiscard]] std::optional<CaseKind> parse_kind(const json::JsonValue &node) {
        const auto value = require_string(node, "kind");
        if (!value.has_value()) {
            return std::nullopt;
        }
        if (*value == "agent") {
            return CaseKind::Agent;
        }
        if (*value == "workflow") {
            return CaseKind::Workflow;
        }
        error("conformance case field 'kind' must be 'agent' or 'workflow', got '" + *value + "'",
              range_of(node));
        return std::nullopt;
    }

    void validate_source_path(const std::string &source) {
        if (source.empty()) {
            error("conformance case field 'source' must not be empty", field_range("source"));
            return;
        }
        if (!source.ends_with(".ahfl")) {
            error("conformance case field 'source' must reference a .ahfl file, got '" + source +
                      "'",
                  field_range("source"));
            return;
        }
        std::filesystem::path path(source);
        if (path.is_absolute()) {
            error("conformance case field 'source' must be a repo-relative path, got '" + source +
                      "'",
                  field_range("source"));
            return;
        }
        for (const auto &component : path) {
            if (component == "..") {
                error("conformance case field 'source' must not escape the repository "
                      "via '..', got '" +
                          source + "'",
                      field_range("source"));
                return;
            }
        }
    }

    void parse_scenarios(const json::JsonValue &array,
                         std::vector<ConformanceScenario> &scenarios) {
        if (!array.is_array()) {
            error("conformance case field 'scenarios' must be an array", range_of(array));
            return;
        }

        std::unordered_set<std::string> names;
        for (const auto &item : array.array_items) {
            auto scenario = parse_scenario(*item);
            if (!scenario.has_value()) {
                return;
            }
            if (!names.insert(scenario->name).second) {
                error("conformance case field 'scenarios' lists scenario '" + scenario->name +
                          "' more than once",
                      range_of(*item));
                return;
            }
            scenarios.push_back(std::move(*scenario));
        }
    }

    [[nodiscard]] std::optional<ConformanceScenario>
    parse_scenario(const json::JsonValue &object) {
        if (!object.is_object()) {
            error("conformance case field 'scenarios[]' must contain objects", range_of(object));
            return std::nullopt;
        }

        std::optional<std::string> name;
        std::optional<std::string> input_json;
        std::optional<CaseExpectations> expect;

        for (const auto &[key, value] : object.object_fields) {
            if (key == "name") {
                name = require_path_segment(*value, "scenarios[].name");
            } else if (key == "input") {
                input_json = require_canonical_wire_json(*value, "scenarios[].input");
            } else if (key == "expect") {
                expect = parse_expectations(*value);
            } else {
                error("unsupported conformance case field 'scenarios[]." + key + "'",
                      range_of(*value));
                return std::nullopt;
            }
            if (diagnostics_.has_error()) {
                return std::nullopt;
            }
        }

        if (!name.has_value()) {
            error("conformance case scenario is missing required field 'name'", range_of(object));
            return std::nullopt;
        }
        if (!input_json.has_value()) {
            error("conformance case scenario '" + *name +
                      "' is missing required field 'input'",
                  range_of(object));
            return std::nullopt;
        }
        if (!expect.has_value()) {
            error("conformance case scenario '" + *name +
                      "' is missing required field 'expect'",
                  range_of(object));
            return std::nullopt;
        }

        return ConformanceScenario{
            .name = std::move(*name),
            .input_json = std::move(*input_json),
            .expect = std::move(*expect),
        };
    }

    void parse_capabilities(const json::JsonValue &array,
                            std::vector<CapabilityExpectation> &capabilities) {
        if (!array.is_array()) {
            error("conformance case field 'capabilities' must be an array", range_of(array));
            return;
        }

        std::unordered_set<std::string> names;
        for (const auto &item : array.array_items) {
            auto capability = parse_capability(*item);
            if (!capability.has_value()) {
                return;
            }
            if (!names.insert(capability->name).second) {
                error("conformance case field 'capabilities' lists capability '" +
                          capability->name + "' more than once",
                      range_of(*item));
                return;
            }
            capabilities.push_back(std::move(*capability));
        }
    }

    [[nodiscard]] std::optional<CapabilityExpectation>
    parse_capability(const json::JsonValue &object) {
        if (!object.is_object()) {
            error("conformance case field 'capabilities[]' must contain objects", range_of(object));
            return std::nullopt;
        }

        std::optional<std::string> name;
        std::optional<CapabilityOutcomeStatus> status;
        std::optional<std::string> result_json;
        bool saw_result = false;

        for (const auto &[key, value] : object.object_fields) {
            if (key == "name") {
                name = require_nonempty_string(*value, "capabilities[].name");
            } else if (key == "status") {
                status = parse_capability_status(*value);
            } else if (key == "result_json") {
                saw_result = true;
                result_json = require_canonical_wire_json(*value, "capabilities[].result_json");
            } else {
                error("unsupported conformance case field 'capabilities[]." + key + "'",
                      range_of(*value));
                return std::nullopt;
            }
            if (diagnostics_.has_error()) {
                return std::nullopt;
            }
        }

        if (!name.has_value()) {
            error("conformance case capability is missing required field 'name'", range_of(object));
            return std::nullopt;
        }
        if (!status.has_value()) {
            error("conformance case capability '" + *name + "' is missing required field 'status'",
                  range_of(object));
            return std::nullopt;
        }
        if (*status == CapabilityOutcomeStatus::Pending && saw_result) {
            error("conformance case capability '" + *name +
                      "' with status 'pending' must not carry 'result_json'",
                  range_of(object));
            return std::nullopt;
        }
        if (*status == CapabilityOutcomeStatus::Ok && !saw_result) {
            // An ok outcome supplies the frame the flow consumes (e.g.
            // `return Echo(input)`); a case without it could not be replayed by
            // any engine adapter.
            error("conformance case capability '" + *name +
                      "' with status 'ok' must carry its result frame in "
                      "'result_json'",
                  range_of(object));
            return std::nullopt;
        }

        return CapabilityExpectation{
            .name = std::move(*name),
            .status = *status,
            .result_json = std::move(result_json),
        };
    }

    [[nodiscard]] std::optional<CapabilityOutcomeStatus>
    parse_capability_status(const json::JsonValue &node) {
        const auto value = require_string(node, "capabilities[].status");
        if (!value.has_value()) {
            return std::nullopt;
        }
        if (*value == "ok") {
            return CapabilityOutcomeStatus::Ok;
        }
        if (*value == "error") {
            return CapabilityOutcomeStatus::Error;
        }
        if (*value == "pending") {
            return CapabilityOutcomeStatus::Pending;
        }
        error("conformance case capability status must be 'ok', 'error', or 'pending', "
              "got '" +
                  *value + "'",
              range_of(node));
        return std::nullopt;
    }

    [[nodiscard]] std::optional<CaseExpectations>
    parse_expectations(const json::JsonValue &object) {
        if (!object.is_object()) {
            error("conformance case field 'expect' must be an object", range_of(object));
            return std::nullopt;
        }

        std::optional<ExpectedRunStatus> run_status;
        std::optional<std::vector<std::string>> state_sequence;
        std::optional<std::vector<std::string>> capability_sequence;
        std::optional<std::string> output_json;

        for (const auto &[key, value] : object.object_fields) {
            if (key == "run_status") {
                run_status = parse_run_status(*value);
            } else if (key == "state_sequence") {
                state_sequence = parse_name_sequence(*value, "state_sequence");
            } else if (key == "capability_sequence") {
                capability_sequence = parse_name_sequence(*value, "capability_sequence");
            } else if (key == "output_json") {
                output_json = require_canonical_wire_json(*value, "expect.output_json");
            } else {
                error("unsupported conformance case field 'expect." + key + "'", range_of(*value));
                return std::nullopt;
            }
            if (diagnostics_.has_error()) {
                return std::nullopt;
            }
        }

        if (!run_status.has_value()) {
            error("conformance case expectations are missing required field "
                  "'run_status'",
                  range_of(object));
            return std::nullopt;
        }
        if (!state_sequence.has_value()) {
            error("conformance case expectations are missing required field "
                  "'state_sequence'",
                  range_of(object));
            return std::nullopt;
        }
        if (!capability_sequence.has_value()) {
            error("conformance case expectations are missing required field "
                  "'capability_sequence'",
                  range_of(object));
            return std::nullopt;
        }

        return CaseExpectations{
            .run_status = *run_status,
            .state_sequence = std::move(*state_sequence),
            .capability_sequence = std::move(*capability_sequence),
            .output_json = std::move(output_json),
        };
    }

    [[nodiscard]] std::optional<ExpectedRunStatus> parse_run_status(const json::JsonValue &node) {
        const auto value = require_string(node, "expect.run_status");
        if (!value.has_value()) {
            return std::nullopt;
        }
        if (*value == "completed") {
            return ExpectedRunStatus::Completed;
        }
        if (*value == "suspended") {
            return ExpectedRunStatus::Suspended;
        }
        if (*value == "failed") {
            return ExpectedRunStatus::Failed;
        }
        error("conformance case field 'expect.run_status' must be 'completed', "
              "'suspended', or 'failed', got '" +
                  *value + "'",
              range_of(node));
        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::vector<std::string>>
    parse_name_sequence(const json::JsonValue &array, std::string_view field) {
        if (!array.is_array()) {
            error("conformance case field '" + std::string(field) +
                      "' must be an array of non-empty names",
                  range_of(array));
            return std::nullopt;
        }
        std::vector<std::string> names;
        names.reserve(array.array_items.size());
        for (const auto &item : array.array_items) {
            const auto name = item->as_string();
            if (!name.has_value() || name->empty()) {
                error("conformance case field '" + std::string(field) +
                          "' must contain only non-empty strings",
                      range_of(*item));
                return std::nullopt;
            }
            names.emplace_back(*name);
        }
        return names;
    }

    [[nodiscard]] std::optional<EngineMatrix> parse_engines(const json::JsonValue &object) {
        if (!object.is_object()) {
            error("conformance case field 'engines' must be an object", range_of(object));
            return std::nullopt;
        }

        std::optional<WasmEngineEligibility> wasm;

        for (const auto &[key, value] : object.object_fields) {
            if (key == "wasm") {
                wasm = parse_wasm_eligibility(*value);
            } else {
                error("unsupported conformance case field 'engines." + key + "'", range_of(*value));
                return std::nullopt;
            }
            if (diagnostics_.has_error()) {
                return std::nullopt;
            }
        }

        if (!wasm.has_value()) {
            error("conformance case field 'engines.wasm' is required", range_of(object));
            return std::nullopt;
        }

        return EngineMatrix{
            .wasm = std::move(*wasm),
        };
    }

    [[nodiscard]] std::optional<WasmEngineEligibility>
    parse_wasm_eligibility(const json::JsonValue &object) {
        if (!object.is_object()) {
            error("conformance case field 'engines.wasm' must be an object", range_of(object));
            return std::nullopt;
        }

        std::optional<WasmEligibility> eligibility;
        std::optional<std::string> reason;
        std::optional<WasmNodeObservationSkip> node_observation_skip;

        for (const auto &[key, value] : object.object_fields) {
            if (key == "eligible") {
                eligibility = parse_wasm_eligible_enum(*value);
            } else if (key == "reason") {
                reason = require_nonempty_string(*value, "engines.wasm.reason");
            } else if (key == "node_observation_skip") {
                node_observation_skip = parse_node_observation_skip(*value);
            } else {
                error("unsupported conformance case field 'engines.wasm." + key + "'",
                      range_of(*value));
                return std::nullopt;
            }
            if (diagnostics_.has_error()) {
                return std::nullopt;
            }
        }

        if (!eligibility.has_value()) {
            error("conformance case field 'engines.wasm.eligible' is required", range_of(object));
            return std::nullopt;
        }
        if (!reason.has_value()) {
            error("conformance case field 'engines.wasm.reason' is required so skipped "
                  "cases carry a structured reason",
                  range_of(object));
            return std::nullopt;
        }

        return WasmEngineEligibility{
            .eligibility = *eligibility,
            .reason = std::move(*reason),
            .node_observation_skip =
                node_observation_skip.value_or(WasmNodeObservationSkip::None),
        };
    }

    [[nodiscard]] std::optional<WasmNodeObservationSkip>
    parse_node_observation_skip(const json::JsonValue &node) {
        const auto value = require_string(node, "engines.wasm.node_observation_skip");
        if (!value.has_value()) {
            return std::nullopt;
        }
        if (*value == "none") {
            return WasmNodeObservationSkip::None;
        }
        if (*value == "blocked_kr66") {
            return WasmNodeObservationSkip::BlockedOnKr66;
        }
        if (*value == "host_transcode_awaits_node_port") {
            return WasmNodeObservationSkip::HostTranscodeAwaitsNodePort;
        }
        if (*value == "node_host_awaits_multinode_stash_join") {
            return WasmNodeObservationSkip::NodeHostAwaitsMultiNodeStashJoin;
        }
        if (*value == "node_host_awaits_rich_wire_types") {
            return WasmNodeObservationSkip::NodeHostAwaitsRichWireTypes;
        }
        error("conformance case field 'engines.wasm.node_observation_skip' must be 'none', "
              "'blocked_kr66', "
              "'host_transcode_awaits_node_port', "
              "'node_host_awaits_multinode_stash_join', or "
              "'node_host_awaits_rich_wire_types', got '" +
                  *value + "'",
              range_of(node));
        return std::nullopt;
    }

    [[nodiscard]] std::optional<WasmEligibility>
    parse_wasm_eligible_enum(const json::JsonValue &node) {
        const auto value = require_string(node, "engines.wasm.eligible");
        if (!value.has_value()) {
            return std::nullopt;
        }
        if (*value == "orchestration") {
            return WasmEligibility::Orchestration;
        }
        if (*value == "computation") {
            return WasmEligibility::Computation;
        }
        if (*value == "none") {
            return WasmEligibility::None;
        }
        error("conformance case field 'engines.wasm.eligible' must be 'orchestration', "
              "'computation', or 'none', got '" +
                  *value + "'",
              range_of(node));
        return std::nullopt;
    }

    void validate_cross_field_semantics(CaseKind kind,
                                        const std::vector<CapabilityExpectation> &capabilities,
                                        const ConformanceScenario &scenario) {
        // A flat state sequence is only meaningful for a single-agent case. A
        // workflow fans out across multiple agents, so a flat list would be
        // ambiguous; the exact workflow node-order observation is a separate
        // contract and must not be smuggled in here.
        if (kind == CaseKind::Agent && scenario.expect.state_sequence.empty()) {
            error("conformance case of kind 'agent' scenario '" + scenario.name +
                      "' must declare a non-empty 'expect.state_sequence'",
                  field_range("kind"));
        }
        if (kind == CaseKind::Workflow && !scenario.expect.state_sequence.empty()) {
            error("conformance case of kind 'workflow' scenario '" + scenario.name +
                      "' must leave 'expect.state_sequence' empty; workflow node "
                      "order is a separate observation contract",
                  field_range("kind"));
        }

        std::unordered_set<std::string> configured;
        configured.reserve(capabilities.size());
        for (const auto &capability : capabilities) {
            configured.insert(capability.name);
        }
        for (const auto &invoked : scenario.expect.capability_sequence) {
            if (!configured.contains(invoked)) {
                error("conformance case scenario '" + scenario.name +
                          "' 'expect.capability_sequence' invokes capability '" + invoked +
                          "' that has no entry in 'capabilities'",
                      std::nullopt);
            }
        }
    }
};

inline constexpr std::string_view kConformanceCaseFileSuffix = ".case.json";

/// True when `path` names a conformance case sidecar by suffix.
[[nodiscard]] inline bool is_conformance_case_sidecar(const std::filesystem::path &path) {
    return path.string().ends_with(kConformanceCaseFileSuffix);
}

} // namespace detail

// ---------------------------------------------------------------------------
// Public entry points
// ---------------------------------------------------------------------------

/// Parses and validates a conformance case manifest. `manifest_text` must be
/// the exact sidecar bytes (offsets in diagnostics are relative to it);
/// `sidecar_name` is the display label used on diagnostics.
[[nodiscard]] inline ConformanceCaseParseResult
parse_conformance_case_json(std::string_view manifest_text, std::string_view sidecar_name) {
    ConformanceCaseParseResult result;
    result.conformance_case = std::nullopt;

    auto root = json::parse_json(manifest_text);
    if (!root.has_value() || !*root) {
        DiagnosticBuilder builder = result.diagnostics.error();
        std::move(builder)
            .source_name(std::string(sidecar_name))
            .message("conformance case manifest must be valid JSON beginning with '{'")
            .emit();
        return result;
    }

    detail::ConformanceCaseReader reader(**root, manifest_text, sidecar_name, result.diagnostics);
    auto parsed = reader.parse();
    if (!parsed.has_value()) {
        return result;
    }

    result.conformance_case = std::move(*parsed);
    return result;
}

/// Loads a sidecar from disk, parses it, and resolves its repo-relative
/// `source` against `repo_root`. Fails (fail-closed) when the sidecar or its
/// referenced AHFL source cannot be read.
[[nodiscard]] inline ConformanceCaseLoadResult
load_conformance_case(const std::filesystem::path &sidecar_path,
                      const std::filesystem::path &repo_root) {
    ConformanceCaseLoadResult result;
    result.conformance_case = std::nullopt;

    std::ifstream in(sidecar_path, std::ios::binary);
    if (!in) {
        DiagnosticBuilder builder = result.diagnostics.error();
        std::move(builder)
            .source_name(sidecar_path.string())
            .message("cannot open conformance case sidecar")
            .emit();
        return result;
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    auto parsed = parse_conformance_case_json(text, sidecar_path.string());
    result.diagnostics.append(parsed.diagnostics);
    if (!parsed.conformance_case.has_value()) {
        return result;
    }

    const auto source_path = repo_root / parsed.conformance_case->source;
    std::error_code error;
    if (!std::filesystem::is_regular_file(source_path, error)) {
        DiagnosticBuilder builder = result.diagnostics.error();
        std::move(builder)
            .source_name(sidecar_path.string())
            .message("conformance case source '" + parsed.conformance_case->source +
                     "' does not exist relative to the repository root")
            .emit();
        return result;
    }

    result.conformance_case = LoadedConformanceCase{
        .manifest = std::move(*parsed.conformance_case),
        .sidecar_path = sidecar_path,
        .source_path = source_path,
    };
    return result;
}

} // namespace ahfl::conformance
