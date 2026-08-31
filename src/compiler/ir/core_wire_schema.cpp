#include "ahfl/compiler/ir/core_wire_schema.hpp"

#include "ahfl/base/support/overloaded.hpp"
#include "ahfl/compiler/ir/core_verify.hpp"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace ahfl::ir::core {
namespace {

[[nodiscard]] CoreLowerDiagnostic diagnostic(std::string_view code,
                                             std::string message,
                                             SourceRangeOpt range = std::nullopt) {
    return CoreLowerDiagnostic{CoreDiagnosticSeverity::Error,
                               std::string(code),
                               std::move(message),
                               std::move(range)};
}

[[nodiscard]] std::optional<CoreWirePayloadKind>
wire_payload_kind(CoreTypeDecl::VariantPayload::Kind kind) {
    switch (kind) {
    case CoreTypeDecl::VariantPayload::Kind::Unit:
        return CoreWirePayloadKind::Unit;
    case CoreTypeDecl::VariantPayload::Kind::Tuple:
        return CoreWirePayloadKind::Tuple;
    case CoreTypeDecl::VariantPayload::Kind::Struct:
        return CoreWirePayloadKind::Struct;
    }
    return std::nullopt;
}

class SchemaBuilder {
  public:
    SchemaBuilder(const CoreProgram &program,
                  const std::vector<CoreCapabilityId> &selected_capabilities)
        : program_(program), selected_(selected_capabilities), scratch_types_(program.value_types) {
        schema_ids_.resize(scratch_types_.size());
    }

    [[nodiscard]] std::optional<CoreWireSchemaTable> run() {
        const auto core_verification = verify_core_program(program_);
        if (!core_verification.ok()) {
            fail(wire_schema::kInvalidCore,
                 "wire-schema projection requires a verified CoreProgram");
            return std::nullopt;
        }
        if (!selection_is_canonical()) {
            return std::nullopt;
        }

        table_.capabilities.reserve(selected_.size());
        for (const CoreCapabilityId capability_id : selected_) {
            const CoreCapabilityDecl &capability = program_.capabilities[capability_id.value];
            if (!capability.symbol_ref.id.has_value()) {
                fail(wire_schema::kInvalidCore,
                     "selected capability has no resolved SymbolId",
                     capability.source_range);
                return std::nullopt;
            }
            CoreWireCapabilitySchema projected;
            projected.capability = capability_id;
            projected.source_symbol = static_cast<std::uint64_t>(*capability.symbol_ref.id);
            projected.params.reserve(capability.param_types.size());
            for (const CoreValueTypeId param : capability.param_types) {
                const auto root = project_type(param, capability.source_range);
                if (!root.has_value()) {
                    return std::nullopt;
                }
                projected.params.push_back(*root);
            }
            const auto result = project_type(capability.return_type, capability.source_range);
            if (!result.has_value()) {
                return std::nullopt;
            }
            projected.result = *result;
            table_.capabilities.push_back(std::move(projected));
        }
        return std::move(table_);
    }

    [[nodiscard]] std::vector<CoreLowerDiagnostic> take_diagnostics() {
        return std::move(diagnostics_);
    }

  private:
    [[nodiscard]] bool selection_is_canonical() {
        std::optional<std::uint32_t> previous;
        for (const CoreCapabilityId id : selected_) {
            if (id.value >= program_.capabilities.size()) {
                fail(wire_schema::kInvalidSelection,
                     "selected capability id is out of range");
                return false;
            }
            if (previous.has_value() && id.value <= *previous) {
                fail(wire_schema::kInvalidSelection,
                     "selected capability ids must be strictly increasing and unique");
                return false;
            }
            previous = id.value;
        }
        return true;
    }

    void fail(std::string_view code, std::string message, SourceRangeOpt range = std::nullopt) {
        if (diagnostics_.empty()) {
            diagnostics_.push_back(diagnostic(code, std::move(message), std::move(range)));
        }
    }

    void sync_schema_ids() {
        if (schema_ids_.size() < scratch_types_.size()) {
            schema_ids_.resize(scratch_types_.size());
        }
    }

    [[nodiscard]] std::optional<CoreWireSchemaNodeId>
    reserve_node(CoreValueTypeId type, SourceRangeOpt range) {
        if (table_.nodes.size() >= CoreWireSchemaNodeId::kInvalid) {
            fail(wire_schema::kOverflow,
                 "wire-schema arena exceeded its 32-bit id space",
                 std::move(range));
            return std::nullopt;
        }
        const auto id = CoreWireSchemaNodeId{static_cast<std::uint32_t>(table_.nodes.size())};
        table_.nodes.push_back(CoreWireSchemaNode{});
        schema_ids_[type.value] = id;
        return id;
    }

    [[nodiscard]] std::optional<CoreWireSchemaNodeId>
    project_type(CoreValueTypeId type, SourceRangeOpt range) {
        sync_schema_ids();
        if (type.value >= scratch_types_.size()) {
            fail(wire_schema::kInvalid,
                 "wire-schema logical value type id is out of range",
                 std::move(range));
            return std::nullopt;
        }
        if (schema_ids_[type.value].has_value()) {
            return schema_ids_[type.value];
        }
        const auto id = reserve_node(type, range);
        if (!id.has_value()) {
            return std::nullopt;
        }
        auto shape = build_shape(type, range);
        if (!shape.has_value()) {
            return std::nullopt;
        }
        table_.nodes[id->value].shape = std::move(*shape);
        return id;
    }

    [[nodiscard]] std::optional<CoreWireSchemaShape>
    build_shape(CoreValueTypeId type, SourceRangeOpt range) {
        const CoreValueTypeNode &node = scratch_types_[type.value].node;
        return std::visit(
            Overloaded{
                [](const CoreVtUnit &) -> std::optional<CoreWireSchemaShape> {
                    return CoreWireSchemaUnit{};
                },
                [&](const CoreVtNever &) -> std::optional<CoreWireSchemaShape> {
                    fail(wire_schema::kUnsupported,
                         "Never is not a materialized wire value",
                         std::move(range));
                    return std::nullopt;
                },
                [](const CoreVtBool &) -> std::optional<CoreWireSchemaShape> {
                    return CoreWireSchemaBool{};
                },
                [](const CoreVtInt &value) -> std::optional<CoreWireSchemaShape> {
                    return CoreWireSchemaInt{value.bounds};
                },
                [](const CoreVtFloat &) -> std::optional<CoreWireSchemaShape> {
                    return CoreWireSchemaFloat{};
                },
                [](const CoreVtString &value) -> std::optional<CoreWireSchemaShape> {
                    return CoreWireSchemaString{value.length_bounds};
                },
                [](const CoreVtDecimal &value) -> std::optional<CoreWireSchemaShape> {
                    return CoreWireSchemaDecimal{value.scale};
                },
                [](const CoreVtDuration &) -> std::optional<CoreWireSchemaShape> {
                    return CoreWireSchemaDuration{};
                },
                [](const CoreVtTimestamp &) -> std::optional<CoreWireSchemaShape> {
                    return CoreWireSchemaTimestamp{};
                },
                [](const CoreVtUuid &) -> std::optional<CoreWireSchemaShape> {
                    return CoreWireSchemaUuid{};
                },
                [&](const CoreVtNominal &value) -> std::optional<CoreWireSchemaShape> {
                    return build_nominal(value, range);
                },
                [&](const CoreVtTuple &value) -> std::optional<CoreWireSchemaShape> {
                    CoreWireSchemaTuple tuple;
                    tuple.elements.reserve(value.elements.size());
                    for (const CoreValueTypeId element : value.elements) {
                        const auto child = project_type(element, range);
                        if (!child.has_value()) {
                            return std::nullopt;
                        }
                        tuple.elements.push_back(*child);
                    }
                    return tuple;
                },
                [&](const CoreVtFn &) -> std::optional<CoreWireSchemaShape> {
                    fail(wire_schema::kUnsupported,
                         "function values are not supported by value_json",
                         std::move(range));
                    return std::nullopt;
                },
                [&](const CoreVtClosure &) -> std::optional<CoreWireSchemaShape> {
                    fail(wire_schema::kUnsupported,
                         "closure values are not supported by value_json",
                         std::move(range));
                    return std::nullopt;
                },
            },
            node);
    }

    [[nodiscard]] std::optional<CoreWireSchemaShape>
    build_nominal(const CoreVtNominal &value, SourceRangeOpt range) {
        if (value.base.value >= program_.types.size()) {
            fail(wire_schema::kInvalid,
                 "wire-schema nominal base is out of range",
                 std::move(range));
            return std::nullopt;
        }
        const CoreTypeDecl &decl = program_.types[value.base.value];
        if (value.args.size() != decl.type_param_count) {
            fail(wire_schema::kInvalid,
                 "wire-schema nominal argument count does not match its declaration",
                 decl.source_range);
            return std::nullopt;
        }
        switch (decl.role) {
        case CoreNominalRole::Option: {
            if (decl.kind != CoreTypeDecl::Kind::Enum || value.args.size() != 1) {
                fail(wire_schema::kInvalid,
                     "Option wire schema has malformed nominal metadata",
                     decl.source_range);
                return std::nullopt;
            }
            const auto child = project_type(value.args[0], decl.source_range);
            if (!child.has_value()) {
                return std::nullopt;
            }
            return CoreWireSchemaOption{*child};
        }
        case CoreNominalRole::List:
        case CoreNominalRole::Set: {
            if (decl.kind != CoreTypeDecl::Kind::Struct || value.args.size() != 1) {
                fail(wire_schema::kInvalid,
                     "sequence wire schema has malformed nominal metadata",
                     decl.source_range);
                return std::nullopt;
            }
            const auto child = project_type(value.args[0], decl.source_range);
            if (!child.has_value()) {
                return std::nullopt;
            }
            return CoreWireSchemaSequence{
                decl.role == CoreNominalRole::List ? CoreWireSequenceKind::List
                                                   : CoreWireSequenceKind::Set,
                *child,
                value.capacity};
        }
        case CoreNominalRole::Map: {
            if (decl.kind != CoreTypeDecl::Kind::Struct || value.args.size() != 2) {
                fail(wire_schema::kInvalid,
                     "Map wire schema has malformed nominal metadata",
                     decl.source_range);
                return std::nullopt;
            }
            if (value.args[0].value >= scratch_types_.size() ||
                !std::holds_alternative<CoreVtString>(
                    scratch_types_[value.args[0].value].node)) {
                fail(wire_schema::kUnsupportedMapKey,
                     "value_json supports only String keys for Map wire schemas",
                     decl.source_range);
                return std::nullopt;
            }
            const auto key = project_type(value.args[0], decl.source_range);
            const auto mapped = project_type(value.args[1], decl.source_range);
            if (!key.has_value() || !mapped.has_value()) {
                return std::nullopt;
            }
            return CoreWireSchemaMap{*key, *mapped, value.capacity};
        }
        case CoreNominalRole::Result:
        case CoreNominalRole::Ordinary:
            break;
        }
        if (decl.kind == CoreTypeDecl::Kind::Struct) {
            return build_struct(value, decl);
        }
        if (decl.kind == CoreTypeDecl::Kind::Enum) {
            return build_enum(value, decl);
        }
        fail(wire_schema::kInvalid,
             "wire-schema nominal declaration has an unknown kind",
             decl.source_range);
        return std::nullopt;
    }

    [[nodiscard]] std::optional<CoreValueTypeId>
    instantiate(const CoreVtNominal &owner,
                CoreMemberTypeTemplateNodeId root,
                SourceRangeOpt range) {
        std::string reason;
        auto result = instantiate_member_template_into(scratch_types_,
                                                       program_.types,
                                                       owner.base,
                                                       root,
                                                       owner.args,
                                                       &reason);
        if (!result.has_value()) {
            fail(wire_schema::kInvalid,
                 "wire-schema member template is invalid: " + reason,
                 std::move(range));
            return std::nullopt;
        }
        sync_schema_ids();
        return result;
    }

    [[nodiscard]] std::optional<CoreWireSchemaShape>
    build_struct(const CoreVtNominal &owner, const CoreTypeDecl &decl) {
        if (decl.fields.size() != decl.field_type_template_roots.size()) {
            fail(wire_schema::kInvalid,
                 "wire-schema struct fields and template roots are not parallel",
                 decl.source_range);
            return std::nullopt;
        }
        CoreWireSchemaStruct result;
        result.wire_name = decl.name;
        result.fields.reserve(decl.fields.size());
        for (std::size_t i = 0; i < decl.fields.size(); ++i) {
            const auto field_type =
                instantiate(owner, decl.field_type_template_roots[i], decl.source_range);
            if (!field_type.has_value()) {
                return std::nullopt;
            }
            const auto child = project_type(*field_type, decl.source_range);
            if (!child.has_value()) {
                return std::nullopt;
            }
            result.fields.push_back(CoreWireSchemaField{decl.fields[i], *child});
        }
        return result;
    }

    [[nodiscard]] std::optional<CoreWireSchemaShape>
    build_enum(const CoreVtNominal &owner, const CoreTypeDecl &decl) {
        if (decl.variants.size() != decl.variant_payloads.size()) {
            fail(wire_schema::kInvalid,
                 "wire-schema enum variants and payload metadata are not parallel",
                 decl.source_range);
            return std::nullopt;
        }
        CoreWireSchemaEnum result;
        result.wire_name = decl.name;
        result.variants.reserve(decl.variants.size());
        for (std::size_t i = 0; i < decl.variants.size(); ++i) {
            const auto &payload = decl.variant_payloads[i];
            const auto kind = wire_payload_kind(payload.kind);
            if (!kind.has_value()) {
                fail(wire_schema::kInvalid,
                     "wire-schema enum payload has an unknown kind",
                     decl.source_range);
                return std::nullopt;
            }
            if (*kind == CoreWirePayloadKind::Struct &&
                payload.field_names.size() != payload.slot_type_template_roots.size()) {
                fail(wire_schema::kInvalid,
                     "wire-schema struct payload fields and slots are not parallel",
                     decl.source_range);
                return std::nullopt;
            }
            CoreWireSchemaVariant variant;
            variant.wire_name = decl.variants[i];
            variant.payload_kind = *kind;
            variant.slots.reserve(payload.slot_type_template_roots.size());
            for (std::size_t slot = 0; slot < payload.slot_type_template_roots.size(); ++slot) {
                const auto slot_type = instantiate(
                    owner, payload.slot_type_template_roots[slot], decl.source_range);
                if (!slot_type.has_value()) {
                    return std::nullopt;
                }
                const auto child = project_type(*slot_type, decl.source_range);
                if (!child.has_value()) {
                    return std::nullopt;
                }
                variant.slots.push_back(CoreWireSchemaField{
                    *kind == CoreWirePayloadKind::Struct ? payload.field_names[slot]
                                                        : std::string{},
                    *child});
            }
            result.variants.push_back(std::move(variant));
        }
        return result;
    }

    const CoreProgram &program_;
    const std::vector<CoreCapabilityId> &selected_;
    std::vector<CoreValueType> scratch_types_;
    std::vector<std::optional<CoreWireSchemaNodeId>> schema_ids_;
    CoreWireSchemaTable table_;
    std::vector<CoreLowerDiagnostic> diagnostics_;
};

[[nodiscard]] std::optional<CoreWireSchemaTable>
build_raw(const CoreProgram &program,
          const std::vector<CoreCapabilityId> &selected,
          std::vector<CoreLowerDiagnostic> &diagnostics) {
    SchemaBuilder builder(program, selected);
    auto table = builder.run();
    diagnostics = builder.take_diagnostics();
    return table;
}

class LocalSchemaVerifier {
  public:
    explicit LocalSchemaVerifier(const CoreWireSchemaTable &table) : table_(table) {}

    [[nodiscard]] std::vector<CoreLowerDiagnostic> run() {
        if (table_.format_version != 1) {
            fail("wire-schema table has an unsupported format version");
            return std::move(diagnostics_);
        }
        if (table_.nodes.size() >= CoreWireSchemaNodeId::kInvalid) {
            fail("wire-schema arena exceeds its 32-bit id space");
            return std::move(diagnostics_);
        }
        std::optional<std::uint32_t> previous;
        for (const auto &capability : table_.capabilities) {
            if (capability.capability.value == CoreCapabilityId::kInvalid ||
                (previous.has_value() && capability.capability.value <= *previous)) {
                fail("wire-schema capability roots are not strictly ordered and unique");
                return std::move(diagnostics_);
            }
            previous = capability.capability.value;
            for (const auto root : capability.params) {
                if (!mark(root)) {
                    return std::move(diagnostics_);
                }
            }
            if (!mark(capability.result)) {
                return std::move(diagnostics_);
            }
        }
        for (std::size_t i = 0; i < table_.nodes.size(); ++i) {
            if (!validate_node(CoreWireSchemaNodeId{static_cast<std::uint32_t>(i)})) {
                return std::move(diagnostics_);
            }
        }
        for (std::size_t i = 0; i < reachable_.size(); ++i) {
            if (!reachable_[i]) {
                fail("wire-schema table contains an orphan node");
                return std::move(diagnostics_);
            }
        }
        return std::move(diagnostics_);
    }

  private:
    void fail(std::string message) {
        if (diagnostics_.empty()) {
            diagnostics_.push_back(diagnostic(wire_schema::kInvalid, std::move(message)));
        }
    }

    [[nodiscard]] bool valid_id(CoreWireSchemaNodeId id) {
        if (id.value >= table_.nodes.size()) {
            fail("wire-schema node reference is out of range");
            return false;
        }
        return true;
    }

    [[nodiscard]] bool mark(CoreWireSchemaNodeId id) {
        if (!valid_id(id)) {
            return false;
        }
        if (reachable_.empty()) {
            reachable_.resize(table_.nodes.size(), false);
        }
        if (reachable_[id.value]) {
            return true;
        }
        reachable_[id.value] = true;
        return for_each_child(id, [&](CoreWireSchemaNodeId child) { return mark(child); });
    }

    template <typename Callback>
    [[nodiscard]] bool for_each_child(CoreWireSchemaNodeId id, Callback callback) {
        const auto each = [&](const auto &ids) {
            for (const auto child : ids) {
                if (!callback(child)) {
                    return false;
                }
            }
            return true;
        };
        return std::visit(
            Overloaded{
                [](const CoreWireSchemaUnit &) { return true; },
                [](const CoreWireSchemaBool &) { return true; },
                [](const CoreWireSchemaInt &) { return true; },
                [](const CoreWireSchemaFloat &) { return true; },
                [](const CoreWireSchemaString &) { return true; },
                [](const CoreWireSchemaDecimal &) { return true; },
                [](const CoreWireSchemaDuration &) { return true; },
                [](const CoreWireSchemaTimestamp &) { return true; },
                [](const CoreWireSchemaUuid &) { return true; },
                [&](const CoreWireSchemaOption &value) { return callback(value.value); },
                [&](const CoreWireSchemaSequence &value) { return callback(value.element); },
                [&](const CoreWireSchemaMap &value) {
                    return callback(value.key) && callback(value.value);
                },
                [&](const CoreWireSchemaStruct &value) {
                    std::vector<CoreWireSchemaNodeId> ids;
                    ids.reserve(value.fields.size());
                    for (const auto &field : value.fields) {
                        ids.push_back(field.type);
                    }
                    return each(ids);
                },
                [&](const CoreWireSchemaEnum &value) {
                    for (const auto &variant : value.variants) {
                        for (const auto &slot : variant.slots) {
                            if (!callback(slot.type)) {
                                return false;
                            }
                        }
                    }
                    return true;
                },
                [&](const CoreWireSchemaTuple &value) { return each(value.elements); },
            },
            table_.nodes[id.value].shape);
    }

    [[nodiscard]] bool names_unique(const std::vector<CoreWireSchemaField> &fields,
                                    bool require_names) {
        std::set<std::string> names;
        for (const auto &field : fields) {
            if (require_names != !field.wire_name.empty()) {
                fail(require_names ? "wire-schema field name is empty"
                                   : "wire-schema tuple slot unexpectedly has a name");
                return false;
            }
            if (require_names && !names.insert(field.wire_name).second) {
                fail("wire-schema field name is duplicated");
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] bool validate_node(CoreWireSchemaNodeId id) {
        return std::visit(
            Overloaded{
                [](const CoreWireSchemaUnit &) { return true; },
                [](const CoreWireSchemaBool &) { return true; },
                [&](const CoreWireSchemaInt &value) {
                    if (value.bounds && value.bounds->first > value.bounds->second) {
                        fail("wire-schema Int bounds are reversed");
                        return false;
                    }
                    return true;
                },
                [](const CoreWireSchemaFloat &) { return true; },
                [&](const CoreWireSchemaString &value) {
                    if (value.length_bounds &&
                        (value.length_bounds->first < 0 ||
                         value.length_bounds->first > value.length_bounds->second)) {
                        fail("wire-schema String bounds are invalid");
                        return false;
                    }
                    return true;
                },
                [](const CoreWireSchemaDecimal &) { return true; },
                [](const CoreWireSchemaDuration &) { return true; },
                [](const CoreWireSchemaTimestamp &) { return true; },
                [](const CoreWireSchemaUuid &) { return true; },
                [&](const CoreWireSchemaOption &value) { return valid_id(value.value); },
                [&](const CoreWireSchemaSequence &value) {
                    switch (value.kind) {
                    case CoreWireSequenceKind::List:
                    case CoreWireSequenceKind::Set:
                        return valid_id(value.element);
                    }
                    fail("wire-schema sequence kind is invalid");
                    return false;
                },
                [&](const CoreWireSchemaMap &value) {
                    if (!valid_id(value.key) || !valid_id(value.value)) {
                        return false;
                    }
                    if (!std::holds_alternative<CoreWireSchemaString>(
                            table_.nodes[value.key.value].shape)) {
                        fail("wire-schema Map key is not String");
                        return false;
                    }
                    return true;
                },
                [&](const CoreWireSchemaStruct &value) {
                    if (value.wire_name.empty() || !names_unique(value.fields, true)) {
                        if (value.wire_name.empty()) {
                            fail("wire-schema struct name is empty");
                        }
                        return false;
                    }
                    return std::all_of(value.fields.begin(), value.fields.end(),
                                       [&](const auto &field) { return valid_id(field.type); });
                },
                [&](const CoreWireSchemaEnum &value) {
                    if (value.wire_name.empty() || value.variants.empty()) {
                        fail("wire-schema enum name or variant table is empty");
                        return false;
                    }
                    std::set<std::string> variant_names;
                    for (const auto &variant : value.variants) {
                        if (variant.wire_name.empty() ||
                            !variant_names.insert(variant.wire_name).second) {
                            fail("wire-schema enum variant name is empty or duplicated");
                            return false;
                        }
                        switch (variant.payload_kind) {
                        case CoreWirePayloadKind::Unit:
                            if (!variant.slots.empty()) {
                                fail("wire-schema unit variant has payload slots");
                                return false;
                            }
                            break;
                        case CoreWirePayloadKind::Tuple:
                            if (!names_unique(variant.slots, false)) {
                                return false;
                            }
                            break;
                        case CoreWirePayloadKind::Struct:
                            if (!names_unique(variant.slots, true)) {
                                return false;
                            }
                            break;
                        default:
                            fail("wire-schema payload kind is invalid");
                            return false;
                        }
                        for (const auto &slot : variant.slots) {
                            if (!valid_id(slot.type)) {
                                return false;
                            }
                        }
                    }
                    return true;
                },
                [&](const CoreWireSchemaTuple &value) {
                    return std::all_of(value.elements.begin(), value.elements.end(),
                                       [&](CoreWireSchemaNodeId child) { return valid_id(child); });
                },
            },
            table_.nodes[id.value].shape);
    }

    const CoreWireSchemaTable &table_;
    std::vector<bool> reachable_;
    std::vector<CoreLowerDiagnostic> diagnostics_;
};

[[nodiscard]] std::vector<CoreLowerDiagnostic>
verify_local(const CoreWireSchemaTable &table) {
    return LocalSchemaVerifier(table).run();
}

class SchemaEncoder {
  public:
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> run(const CoreWireSchemaTable &table) {
        const auto local = verify_local(table);
        if (!local.empty()) {
            diagnostics_ = local;
            return std::nullopt;
        }
        bytes_ = {'A', 'H', 'F', 'L', 'W', 'S'};
        u32(table.format_version);
        u32_size(table.nodes.size());
        if (failed()) {
            return std::nullopt;
        }
        for (const auto &node : table.nodes) {
            encode_node(node);
            if (failed()) {
                return std::nullopt;
            }
        }
        u32_size(table.capabilities.size());
        if (failed()) {
            return std::nullopt;
        }
        for (const auto &capability : table.capabilities) {
            u32(capability.capability.value);
            u64(capability.source_symbol);
            ids(capability.params);
            u32(capability.result.value);
            if (failed()) {
                return std::nullopt;
            }
        }
        return std::move(bytes_);
    }

    [[nodiscard]] std::vector<CoreLowerDiagnostic> take_diagnostics() {
        return std::move(diagnostics_);
    }

  private:
    [[nodiscard]] bool failed() const noexcept { return !diagnostics_.empty(); }

    void fail(std::string message) {
        if (!failed()) {
            diagnostics_.push_back(
                diagnostic(wire_schema::kOverflow, std::move(message)));
        }
    }

    void byte(std::uint8_t value) { bytes_.push_back(value); }

    void u32(std::uint32_t value) { u64(value); }

    void u64(std::uint64_t value) {
        do {
            std::uint8_t current = static_cast<std::uint8_t>(value & 0x7fU);
            value >>= 7U;
            if (value != 0) {
                current |= 0x80U;
            }
            byte(current);
        } while (value != 0);
    }

    void s64(std::int64_t value) {
        bool more = true;
        while (more) {
            std::uint8_t current = static_cast<std::uint8_t>(value) & 0x7fU;
            const bool sign = (current & 0x40U) != 0;
            value >>= 7;
            more = !((value == 0 && !sign) || (value == -1 && sign));
            if (more) {
                current |= 0x80U;
            }
            byte(current);
        }
    }

    void u32_size(std::size_t value) {
        if (value > std::numeric_limits<std::uint32_t>::max()) {
            fail("wire-schema vector or string exceeds its 32-bit encoding domain");
            return;
        }
        u32(static_cast<std::uint32_t>(value));
    }

    void string(const std::string &value) {
        u32_size(value.size());
        if (!failed()) {
            bytes_.insert(bytes_.end(), value.begin(), value.end());
        }
    }

    void optional_u64(const std::optional<std::uint64_t> &value) {
        byte(value.has_value() ? 1U : 0U);
        if (value.has_value()) {
            u64(*value);
        }
    }

    void optional_bounds(
        const std::optional<std::pair<std::int64_t, std::int64_t>> &value) {
        byte(value.has_value() ? 1U : 0U);
        if (value.has_value()) {
            s64(value->first);
            s64(value->second);
        }
    }

    void ids(const std::vector<CoreWireSchemaNodeId> &values) {
        u32_size(values.size());
        if (failed()) {
            return;
        }
        for (const auto value : values) {
            u32(value.value);
        }
    }

    void fields(const std::vector<CoreWireSchemaField> &values) {
        u32_size(values.size());
        if (failed()) {
            return;
        }
        for (const auto &value : values) {
            string(value.wire_name);
            u32(value.type.value);
            if (failed()) {
                return;
            }
        }
    }

    void encode_node(const CoreWireSchemaNode &node) {
        std::visit(
            Overloaded{
                [&](const CoreWireSchemaUnit &) { byte(0); },
                [&](const CoreWireSchemaBool &) { byte(1); },
                [&](const CoreWireSchemaInt &value) {
                    byte(2);
                    optional_bounds(value.bounds);
                },
                [&](const CoreWireSchemaFloat &) { byte(3); },
                [&](const CoreWireSchemaString &value) {
                    byte(4);
                    optional_bounds(value.length_bounds);
                },
                [&](const CoreWireSchemaDecimal &value) {
                    byte(5);
                    s64(value.scale);
                },
                [&](const CoreWireSchemaDuration &) { byte(6); },
                [&](const CoreWireSchemaTimestamp &) { byte(7); },
                [&](const CoreWireSchemaUuid &) { byte(8); },
                [&](const CoreWireSchemaOption &value) {
                    byte(9);
                    u32(value.value.value);
                },
                [&](const CoreWireSchemaSequence &value) {
                    byte(10);
                    byte(value.kind == CoreWireSequenceKind::List ? 0U : 1U);
                    u32(value.element.value);
                    optional_u64(value.capacity);
                },
                [&](const CoreWireSchemaMap &value) {
                    byte(11);
                    u32(value.key.value);
                    u32(value.value.value);
                    optional_u64(value.capacity);
                },
                [&](const CoreWireSchemaStruct &value) {
                    byte(12);
                    string(value.wire_name);
                    fields(value.fields);
                },
                [&](const CoreWireSchemaEnum &value) {
                    byte(13);
                    string(value.wire_name);
                    u32_size(value.variants.size());
                    if (failed()) {
                        return;
                    }
                    for (const auto &variant : value.variants) {
                        string(variant.wire_name);
                        byte(static_cast<std::uint8_t>(variant.payload_kind));
                        fields(variant.slots);
                        if (failed()) {
                            return;
                        }
                    }
                },
                [&](const CoreWireSchemaTuple &value) {
                    byte(14);
                    ids(value.elements);
                },
            },
            node.shape);
    }

    std::vector<std::uint8_t> bytes_;
    std::vector<CoreLowerDiagnostic> diagnostics_;
};

} // namespace

bool CoreWireSchemaBuildResult::has_errors() const noexcept {
    return std::any_of(diagnostics.begin(), diagnostics.end(), [](const CoreLowerDiagnostic &d) {
        return d.severity == CoreDiagnosticSeverity::Error;
    });
}

bool CoreWireSchemaEncodeResult::has_errors() const noexcept {
    return std::any_of(diagnostics.begin(), diagnostics.end(), [](const CoreLowerDiagnostic &d) {
        return d.severity == CoreDiagnosticSeverity::Error;
    });
}

CoreWireSchemaBuildResult
project_core_wire_schema(const CoreProgram &program,
                         const std::vector<CoreCapabilityId> &selected_capabilities) {
    CoreWireSchemaBuildResult result;
    result.table = build_raw(program, selected_capabilities, result.diagnostics);
    if (!result.table.has_value()) {
        return result;
    }
    auto local = verify_local(*result.table);
    if (!local.empty()) {
        result.table.reset();
        result.diagnostics.insert(result.diagnostics.end(),
                                  std::make_move_iterator(local.begin()),
                                  std::make_move_iterator(local.end()));
    }
    return result;
}

std::vector<CoreLowerDiagnostic>
verify_core_wire_schema_table(const CoreProgram &program,
                              const std::vector<CoreCapabilityId> &selected_capabilities,
                              const CoreWireSchemaTable &table) {
    auto local = verify_local(table);
    if (!local.empty()) {
        return local;
    }
    std::vector<CoreLowerDiagnostic> projection_diagnostics;
    const auto expected = build_raw(program, selected_capabilities, projection_diagnostics);
    if (!expected.has_value()) {
        return projection_diagnostics;
    }
    if (!(*expected == table)) {
        return {diagnostic(wire_schema::kInvalid,
                           "wire-schema table does not match deterministic Core reprojection")};
    }
    return {};
}

CoreWireSchemaEncodeResult
encode_core_wire_schema_table(const CoreWireSchemaTable &table) {
    SchemaEncoder encoder;
    CoreWireSchemaEncodeResult result;
    result.bytes = encoder.run(table);
    result.diagnostics = encoder.take_diagnostics();
    return result;
}

} // namespace ahfl::ir::core
