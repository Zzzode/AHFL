#include "ahfl/compiler/ir/core_wire_schema.hpp"

#include "ahfl/base/support/overloaded.hpp"
#include "ahfl/compiler/ir/core_verify.hpp"

#include <algorithm>
#include <array>
#include <bit>
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
        canonicalize();
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
        node_source_.push_back(type);
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
        // Snapshot the scratch value-type node BY VALUE. The visitor below
        // descends through `project_type` (which reserves nodes) and `instantiate`
        // (P4-C member instantiation), and the latter hash-conses into
        // `scratch_types_`, reallocating it. A reference into the arena would
        // dangle after the first generic field/slot / child append, so the next
        // read of a nominal/tuple payload would be a use-after-free (ASan-proven).
        // The copy is cheap (small inline vectors) and this is not a hot path.
        const CoreValueTypeNode node = scratch_types_[type.value].node;
        // RFC 0027 Q1 (KR6.13-X): the handler list is generated from the
        // core_value_types.def X-list; each entry token-pastes to an explicit
        // local lambda below. No catch-all: a 15th node added to the .def
        // without a `ws_##Name` lambda fails to compile. Per-node wire behavior
        // (including the Never/Fn/Closure unsupported diagnostics) is
        // unchanged.
        const auto ws_Unit = [](const CoreVtUnit &) -> std::optional<CoreWireSchemaShape> {
            return CoreWireSchemaUnit{};
        };
        const auto ws_Never = [&](const CoreVtNever &) -> std::optional<CoreWireSchemaShape> {
            fail(wire_schema::kUnsupported,
                 "Never is not a materialized wire value",
                 std::move(range));
            return std::nullopt;
        };
        const auto ws_Bool = [](const CoreVtBool &) -> std::optional<CoreWireSchemaShape> {
            return CoreWireSchemaBool{};
        };
        const auto ws_Int = [](const CoreVtInt &value) -> std::optional<CoreWireSchemaShape> {
            return CoreWireSchemaInt{value.bounds};
        };
        const auto ws_Float = [](const CoreVtFloat &) -> std::optional<CoreWireSchemaShape> {
            return CoreWireSchemaFloat{};
        };
        const auto ws_String = [](const CoreVtString &value)
            -> std::optional<CoreWireSchemaShape> {
            return CoreWireSchemaString{value.length_bounds};
        };
        const auto ws_Decimal = [](const CoreVtDecimal &value)
            -> std::optional<CoreWireSchemaShape> {
            return CoreWireSchemaDecimal{value.scale};
        };
        const auto ws_Duration = [](const CoreVtDuration &) -> std::optional<CoreWireSchemaShape> {
            return CoreWireSchemaDuration{};
        };
        const auto ws_Timestamp = [](const CoreVtTimestamp &)
            -> std::optional<CoreWireSchemaShape> {
            return CoreWireSchemaTimestamp{};
        };
        const auto ws_Uuid = [](const CoreVtUuid &) -> std::optional<CoreWireSchemaShape> {
            return CoreWireSchemaUuid{};
        };
        const auto ws_Nominal = [&](const CoreVtNominal &value)
            -> std::optional<CoreWireSchemaShape> {
            return build_nominal(value, range);
        };
        const auto ws_Tuple = [&](const CoreVtTuple &value) -> std::optional<CoreWireSchemaShape> {
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
        };
        const auto ws_Fn = [&](const CoreVtFn &) -> std::optional<CoreWireSchemaShape> {
            fail(wire_schema::kUnsupported,
                 "function values are not supported by value_json",
                 std::move(range));
            return std::nullopt;
        };
        const auto ws_Closure = [&](const CoreVtClosure &) -> std::optional<CoreWireSchemaShape> {
            fail(wire_schema::kUnsupported,
                 "closure values are not supported by value_json",
                 std::move(range));
            return std::nullopt;
        };
#define HANDLE_CORE_VT(Name, Wire) ws_##Name,
        return std::visit(
            Overloaded{
#include "ahfl/compiler/ir/core_value_types.def"
            },
            node);
#undef HANDLE_CORE_VT
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

    // Rewrite every CoreWireSchemaNodeId inside one shape through `remap`.
    static void remap_shape(CoreWireSchemaShape &shape,
                            const std::vector<CoreWireSchemaNodeId> &remap) {
        const auto fix = [&](CoreWireSchemaNodeId &id) { id = remap[id.value]; };
        std::visit(Overloaded{
                       [](CoreWireSchemaUnit &) {},
                       [](CoreWireSchemaBool &) {},
                       [](CoreWireSchemaInt &) {},
                       [](CoreWireSchemaFloat &) {},
                       [](CoreWireSchemaString &) {},
                       [](CoreWireSchemaDecimal &) {},
                       [](CoreWireSchemaDuration &) {},
                       [](CoreWireSchemaTimestamp &) {},
                       [](CoreWireSchemaUuid &) {},
                       [&](CoreWireSchemaOption &s) { fix(s.value); },
                       [&](CoreWireSchemaSequence &s) { fix(s.element); },
                       [&](CoreWireSchemaMap &s) {
                           fix(s.key);
                           fix(s.value);
                       },
                       [&](CoreWireSchemaStruct &s) {
                           for (auto &field : s.fields) {
                               fix(field.type);
                           }
                       },
                       [&](CoreWireSchemaEnum &s) {
                           for (auto &variant : s.variants) {
                               for (auto &slot : variant.slots) {
                                   fix(slot.type);
                               }
                           }
                       },
                       [&](CoreWireSchemaTuple &s) {
                           for (auto &element : s.elements) {
                               fix(element);
                           }
                       },
                   },
                   shape);
    }

    // Canonicalize node order to ascending program-global source CoreValueTypeId
    // (design §2.2): projection discovers nodes in capability params/result DFS
    // order, but the published table's node order is a stable function of the
    // Core value-type ids alone, so repeated projection is byte-identical
    // regardless of capability declaration order. Builds an explicit
    // old-id -> new-id map, then remaps every node edge and capability root in one
    // pass before publishing (no intermediate partially-remapped state escapes).
    void canonicalize() {
        const std::size_t count = table_.nodes.size();
        std::vector<std::uint32_t> order(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            order[i] = i;
        }
        std::sort(order.begin(), order.end(), [&](std::uint32_t a, std::uint32_t b) {
            return node_source_[a].value < node_source_[b].value;
        });
        // remap[old] = new position. `order[new] = old`.
        std::vector<CoreWireSchemaNodeId> remap(count);
        for (std::uint32_t new_id = 0; new_id < count; ++new_id) {
            remap[order[new_id]] = CoreWireSchemaNodeId{new_id};
        }
        std::vector<CoreWireSchemaNode> reordered(count);
        for (std::uint32_t new_id = 0; new_id < count; ++new_id) {
            reordered[new_id] = std::move(table_.nodes[order[new_id]]);
            remap_shape(reordered[new_id].shape, remap);
        }
        table_.nodes = std::move(reordered);
        for (auto &capability : table_.capabilities) {
            for (auto &param : capability.params) {
                param = remap[param.value];
            }
            capability.result = remap[capability.result.value];
        }
    }

    const CoreProgram &program_;
    const std::vector<CoreCapabilityId> &selected_;
    std::vector<CoreValueType> scratch_types_;
    std::vector<std::optional<CoreWireSchemaNodeId>> schema_ids_;
    // Parallel to `table_.nodes` during projection: the program-global scratch
    // CoreValueTypeId each node was discovered from. Used to canonicalize node
    // order to ascending source id (design §2.2) before the table is published.
    std::vector<CoreValueTypeId> node_source_;
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
        // Size the reachability set unconditionally: an empty capability table
        // must still run the orphan gate below (a table may legally have zero
        // capabilities, but then it must also have zero nodes — every node is a
        // root-reachable descendant of some capability param/result).
        reachable_.assign(table_.nodes.size(), false);
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

    // Code-aware failure for the rare case where a legal-but-unsupported wire
    // shape must be rejected with a code other than the default kInvalid (e.g.
    // the Option nullable-child gate, which reports kUnsupported). Existing fail()
    // callers keep kInvalid and their ordering; only the explicitly code-tagged
    // sites differ.
    void fail_code(std::string_view code, std::string message) {
        if (diagnostics_.empty()) {
            diagnostics_.push_back(diagnostic(code, std::move(message)));
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
        // Iterative DFS (explicit worklist) rather than recursion: a decoder feeds
        // this verifier untrusted transported payloads, and a legal but very deep
        // acyclic Struct/Tuple chain would otherwise exhaust the C++ call stack.
        //
        // Ordering equivalence with the previous recursion: a node's validity is
        // checked when it is POPPED (never when merely collected as a child), and
        // children are pushed in reverse so they pop left-to-right. So the very node
        // the old recursion would have descended into next is the next one popped —
        // reproducing its first-visit / first-error order exactly, including a
        // first child's deep descendant being reported before a later sibling.
        // Already-marked nodes are skipped, so a cyclic (recursive nominal) graph
        // still terminates.
        std::vector<CoreWireSchemaNodeId> stack;
        stack.push_back(id);
        while (!stack.empty()) {
            const auto current = stack.back();
            stack.pop_back();
            if (!valid_id(current)) {
                return false;
            }
            if (reachable_[current.value]) {
                continue;
            }
            reachable_[current.value] = true;
            std::vector<CoreWireSchemaNodeId> children;
            (void)for_each_child(current, [&](CoreWireSchemaNodeId child) {
                children.push_back(child);
                return true;
            });
            for (auto it = children.rbegin(); it != children.rend(); ++it) {
                stack.push_back(*it);
            }
        }
        return true;
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
                [&](const CoreWireSchemaOption &value) {
                    if (!valid_id(value.value)) {
                        return false;
                    }
                    // Nullable-child gate (RFC 0026 C2b P0-9): value_json encodes
                    // Option::None as JSON `null` AND Option::Some(x) as `x`'s own
                    // encoding. If the direct child itself encodes as `null` — a
                    // Unit (encodes null) or another Option (its None encodes null)
                    // — then None and Some(child-null) collide on the wire and the
                    // decoder cannot recover which was meant. Reject such a schema
                    // as legal-but-unsupported rather than silently lose data. Only
                    // the DIRECT child needs checking: any nested Option already
                    // trips this rule at its own node. A recursive Struct child
                    // (e.g. Node{next: Option<Node>}) stays legal — Struct does not
                    // encode as null.
                    const auto &child_shape = table_.nodes[value.value.value].shape;
                    if (std::holds_alternative<CoreWireSchemaUnit>(child_shape) ||
                        std::holds_alternative<CoreWireSchemaOption>(child_shape)) {
                        fail_code(wire_schema::kUnsupported,
                                  "Option wire schema child encodes as JSON null (Unit or Option); "
                                  "None and Some would be indistinguishable");
                        return false;
                    }
                    return true;
                },
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
                    for (const auto &field : value.fields) {
                        if (!valid_id(field.type)) {
                            return false;
                        }
                    }
                    // Reserved-name gate (RFC 0026 C2b P0-11): the value_json writer
                    // emits a "_type" discriminator before a struct's own fields, so
                    // a field literally named "_type" would produce a duplicate wire
                    // key and cannot round-trip. Normal source-derived Core never
                    // produces this; any synthetic (projector-API) or transported
                    // attempt is fail-closed here as legal-but-unsupported. This runs
                    // AFTER the structural checks so a malformed struct still reports
                    // its structural error first.
                    for (const auto &field : value.fields) {
                        if (field.wire_name == "_type") {
                            fail_code(wire_schema::kUnsupported,
                                      "wire-schema struct field uses the reserved wire name '_type'");
                            return false;
                        }
                    }
                    return true;
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
                    // Reserved-name gate (RFC 0026 C2b P0-11): an ordinary Enum
                    // whose wire_name is "std::option::Option" collides with the
                    // value_json Option special-case (value.hpp treats any EnumValue
                    // with that enum_name as an Option and encodes None->null /
                    // Some(x)->x), which is incompatible with the ordinary-enum
                    // `_enum` object form the codec expects. A real Option projects
                    // as the distinct CoreWireSchemaOption shape, so normal
                    // source-derived Core never produces this; any synthetic
                    // (projector-API) or transported attempt is fail-closed here as
                    // legal-but-unsupported. Runs AFTER the structural checks so a
                    // malformed enum reports first.
                    if (value.wire_name == "std::option::Option") {
                        fail_code(wire_schema::kUnsupported,
                                  "wire-schema ordinary enum uses the reserved wire name "
                                  "'std::option::Option'");
                        return false;
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

// Mirror of SchemaEncoder: parse the deterministic payload back into a table with
// strict bounds/overflow gates. Every read is bounds-checked against `remaining`
// BEFORE it consumes; LEB reads reject truncation and >64-bit overflow.
// Shortest-form canonicality is NOT judged here — that is the public entry
// point's canonical re-encode byte-equality gate (decode_core_wire_schema_table).
// Any reader failure latches a single fixed diagnostic (no raw bytes / string /
// name echo) and stops. A successful parse must consume EXACTLY the whole payload.
class SchemaDecoder {
  public:
    [[nodiscard]] std::optional<CoreWireSchemaTable>
    run(std::span<const std::uint8_t> bytes) {
        data_ = bytes;
        pos_ = 0;
        static constexpr std::array<std::uint8_t, 6> kMagic = {'A', 'H', 'F', 'L', 'W', 'S'};
        for (const auto expected : kMagic) {
            if (pos_ >= data_.size() || data_[pos_] != expected) {
                fail("wire-schema payload has a bad magic header");
                return std::nullopt;
            }
            ++pos_;
        }
        CoreWireSchemaTable table;
        table.format_version = u32();
        if (failed()) {
            return std::nullopt;
        }
        if (table.format_version != 1) {
            fail("wire-schema payload has an unsupported format version");
            return std::nullopt;
        }
        const std::uint32_t node_count = u32();
        if (failed() || !bounded_count(node_count)) {
            return std::nullopt;
        }
        table.nodes.reserve(node_count);
        for (std::uint32_t i = 0; i < node_count; ++i) {
            auto node = decode_node();
            if (failed()) {
                return std::nullopt;
            }
            table.nodes.push_back(std::move(*node));
        }
        const std::uint32_t cap_count = u32();
        if (failed() || !bounded_count(cap_count)) {
            return std::nullopt;
        }
        table.capabilities.reserve(cap_count);
        for (std::uint32_t i = 0; i < cap_count; ++i) {
            CoreWireCapabilitySchema capability;
            capability.capability = CoreCapabilityId{u32()};
            capability.source_symbol = u64();
            capability.params = ids();
            capability.result = CoreWireSchemaNodeId{u32()};
            if (failed()) {
                return std::nullopt;
            }
            table.capabilities.push_back(std::move(capability));
        }
        if (failed()) {
            return std::nullopt;
        }
        if (pos_ != data_.size()) {
            fail("wire-schema payload has trailing bytes");
            return std::nullopt;
        }
        return table;
    }

    [[nodiscard]] std::vector<CoreLowerDiagnostic> take_diagnostics() {
        return std::move(diagnostics_);
    }

  private:
    [[nodiscard]] bool failed() const noexcept { return !diagnostics_.empty(); }

    void fail(std::string message) {
        if (!failed()) {
            diagnostics_.push_back(diagnostic(wire_schema::kInvalid, std::move(message)));
        }
    }

    [[nodiscard]] std::size_t remaining() const noexcept { return data_.size() - pos_; }

    // A count/length can never exceed the bytes left to read (each element costs
    // at least one byte), and a legal node/id count is strictly below the reserved
    // 32-bit invalid sentinel. Reject an attacker-inflated count before any reserve.
    [[nodiscard]] bool bounded_count(std::uint32_t count) {
        if (count >= CoreWireSchemaNodeId::kInvalid) {
            fail("wire-schema payload declares a count at the reserved 32-bit sentinel");
            return false;
        }
        if (count > remaining()) {
            fail("wire-schema payload declares more elements than remaining bytes");
            return false;
        }
        return true;
    }

    [[nodiscard]] std::uint8_t byte() {
        if (failed()) {
            return 0;
        }
        if (pos_ >= data_.size()) {
            fail("wire-schema payload is truncated");
            return 0;
        }
        return data_[pos_++];
    }

    [[nodiscard]] std::uint64_t u64() {
        std::uint64_t result = 0;
        std::uint32_t shift = 0;
        while (true) {
            if (failed()) {
                return 0;
            }
            if (shift >= 64) {
                fail("wire-schema payload has an overlong LEB128 integer");
                return 0;
            }
            const std::uint8_t current = byte();
            if (failed()) {
                return 0;
            }
            const std::uint64_t payload = current & 0x7fU;
            // On the last representable group (shift == 63) only bit 0 fits.
            if (shift == 63 && payload > 1U) {
                fail("wire-schema payload has an out-of-range LEB128 integer");
                return 0;
            }
            result |= payload << shift;
            if ((current & 0x80U) == 0) {
                break;
            }
            shift += 7;
        }
        // Non-shortest unsigned encodings (a redundant trailing 0x00 group) are
        // rejected by the authoritative canonical re-encode byte-equality gate in
        // decode_core_wire_schema_table; the reader here only guarantees no UB /
        // overflow / truncation.
        return result;
    }

    [[nodiscard]] std::uint32_t u32() {
        const std::uint64_t value = u64();
        if (failed()) {
            return 0;
        }
        if (value > std::numeric_limits<std::uint32_t>::max()) {
            fail("wire-schema payload has an integer outside the 32-bit domain");
            return 0;
        }
        return static_cast<std::uint32_t>(value);
    }

    [[nodiscard]] std::int64_t s64() {
        std::uint64_t bits = 0;
        std::uint32_t shift = 0;
        std::uint8_t current = 0;
        while (true) {
            if (failed()) {
                return 0;
            }
            if (shift >= 64) {
                fail("wire-schema payload has an overlong signed LEB128 integer");
                return 0;
            }
            current = byte();
            if (failed()) {
                return 0;
            }
            const std::uint64_t payload = current & 0x7fU;
            // On the last representable group (shift == 63) only a pure sign group
            // (0x00 or 0x7f) fits; anything else overflows int64.
            if (shift == 63 && payload != 0 && payload != 0x7fU) {
                fail("wire-schema payload has an out-of-range signed LEB128 integer");
                return 0;
            }
            bits |= payload << shift;
            shift += 7;
            if ((current & 0x80U) == 0) {
                break;
            }
        }
        // Sign-extend in the unsigned domain (no signed left shift / no UB), then
        // bit_cast to two's-complement int64. Non-shortest signed encodings are
        // rejected by the authoritative canonical re-encode byte-equality gate; the
        // reader here only guarantees no UB / overflow / truncation.
        if (shift < 64 && (current & 0x40U) != 0) {
            bits |= (~std::uint64_t{0}) << shift;
        }
        return std::bit_cast<std::int64_t>(bits);
    }

    [[nodiscard]] std::string string() {
        const std::uint32_t length = u32();
        if (failed()) {
            return {};
        }
        if (length > remaining()) {
            fail("wire-schema payload declares a string longer than remaining bytes");
            return {};
        }
        std::string value(reinterpret_cast<const char *>(data_.data() + pos_), length);
        pos_ += length;
        return value;
    }

    [[nodiscard]] std::vector<CoreWireSchemaNodeId> ids() {
        const std::uint32_t count = u32();
        if (failed() || !bounded_count(count)) {
            return {};
        }
        std::vector<CoreWireSchemaNodeId> values;
        values.reserve(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            values.push_back(CoreWireSchemaNodeId{u32()});
            if (failed()) {
                return {};
            }
        }
        return values;
    }

    [[nodiscard]] std::optional<std::uint64_t> optional_u64() {
        const std::uint8_t present = byte();
        if (failed()) {
            return std::nullopt;
        }
        if (present == 0) {
            return std::nullopt;
        }
        if (present != 1) {
            fail("wire-schema payload has a non-canonical optional tag");
            return std::nullopt;
        }
        const std::uint64_t value = u64();
        if (failed()) {
            return std::nullopt;
        }
        return value;
    }

    [[nodiscard]] std::optional<std::pair<std::int64_t, std::int64_t>> optional_bounds() {
        const std::uint8_t present = byte();
        if (failed()) {
            return std::nullopt;
        }
        if (present == 0) {
            return std::nullopt;
        }
        if (present != 1) {
            fail("wire-schema payload has a non-canonical optional tag");
            return std::nullopt;
        }
        const std::int64_t lo = s64();
        const std::int64_t hi = s64();
        if (failed()) {
            return std::nullopt;
        }
        return std::pair<std::int64_t, std::int64_t>{lo, hi};
    }

    [[nodiscard]] std::vector<CoreWireSchemaField> decode_fields() {
        const std::uint32_t count = u32();
        if (failed() || !bounded_count(count)) {
            return {};
        }
        std::vector<CoreWireSchemaField> values;
        values.reserve(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            CoreWireSchemaField field;
            field.wire_name = string();
            field.type = CoreWireSchemaNodeId{u32()};
            if (failed()) {
                return {};
            }
            values.push_back(std::move(field));
        }
        return values;
    }

    [[nodiscard]] std::optional<CoreWireSchemaNode> decode_node() {
        const std::uint8_t tag = byte();
        if (failed()) {
            return std::nullopt;
        }
        CoreWireSchemaNode node;
        switch (tag) {
        case 0:
            node.shape = CoreWireSchemaUnit{};
            break;
        case 1:
            node.shape = CoreWireSchemaBool{};
            break;
        case 2: {
            CoreWireSchemaInt value;
            value.bounds = optional_bounds();
            node.shape = value;
            break;
        }
        case 3:
            node.shape = CoreWireSchemaFloat{};
            break;
        case 4: {
            CoreWireSchemaString value;
            value.length_bounds = optional_bounds();
            node.shape = value;
            break;
        }
        case 5: {
            CoreWireSchemaDecimal value;
            value.scale = s64();
            node.shape = value;
            break;
        }
        case 6:
            node.shape = CoreWireSchemaDuration{};
            break;
        case 7:
            node.shape = CoreWireSchemaTimestamp{};
            break;
        case 8:
            node.shape = CoreWireSchemaUuid{};
            break;
        case 9: {
            CoreWireSchemaOption value;
            value.value = CoreWireSchemaNodeId{u32()};
            node.shape = value;
            break;
        }
        case 10: {
            CoreWireSchemaSequence value;
            const std::uint8_t kind = byte();
            if (failed()) {
                return std::nullopt;
            }
            if (kind == 0) {
                value.kind = CoreWireSequenceKind::List;
            } else if (kind == 1) {
                value.kind = CoreWireSequenceKind::Set;
            } else {
                fail("wire-schema payload has an unknown sequence kind");
                return std::nullopt;
            }
            value.element = CoreWireSchemaNodeId{u32()};
            value.capacity = optional_u64();
            node.shape = value;
            break;
        }
        case 11: {
            CoreWireSchemaMap value;
            value.key = CoreWireSchemaNodeId{u32()};
            value.value = CoreWireSchemaNodeId{u32()};
            value.capacity = optional_u64();
            node.shape = value;
            break;
        }
        case 12: {
            CoreWireSchemaStruct value;
            value.wire_name = string();
            value.fields = decode_fields();
            node.shape = value;
            break;
        }
        case 13: {
            CoreWireSchemaEnum value;
            value.wire_name = string();
            const std::uint32_t variant_count = u32();
            if (failed() || !bounded_count(variant_count)) {
                return std::nullopt;
            }
            value.variants.reserve(variant_count);
            for (std::uint32_t i = 0; i < variant_count; ++i) {
                CoreWireSchemaVariant variant;
                variant.wire_name = string();
                const std::uint8_t payload_kind = byte();
                if (failed()) {
                    return std::nullopt;
                }
                switch (payload_kind) {
                case 0:
                    variant.payload_kind = CoreWirePayloadKind::Unit;
                    break;
                case 1:
                    variant.payload_kind = CoreWirePayloadKind::Tuple;
                    break;
                case 2:
                    variant.payload_kind = CoreWirePayloadKind::Struct;
                    break;
                default:
                    fail("wire-schema payload has an unknown enum payload kind");
                    return std::nullopt;
                }
                variant.slots = decode_fields();
                if (failed()) {
                    return std::nullopt;
                }
                value.variants.push_back(std::move(variant));
            }
            node.shape = value;
            break;
        }
        case 14: {
            CoreWireSchemaTuple value;
            value.elements = ids();
            node.shape = value;
            break;
        }
        default:
            fail("wire-schema payload has an unknown node kind");
            return std::nullopt;
        }
        if (failed()) {
            return std::nullopt;
        }
        return node;
    }

    std::span<const std::uint8_t> data_{};
    std::size_t pos_{0};
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

bool CoreWireSchemaDecodeResult::has_errors() const noexcept {
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
verify_core_wire_schema_table_local(const CoreWireSchemaTable &table) {
    return verify_local(table);
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

CoreWireSchemaDecodeResult
decode_core_wire_schema_table(std::span<const std::uint8_t> bytes) {
    CoreWireSchemaDecodeResult result;
    SchemaDecoder decoder;
    auto table = decoder.run(bytes);
    result.diagnostics = decoder.take_diagnostics();
    if (!table.has_value()) {
        return result;
    }
    // Structural admission: the same local verifier a generic host applies to any
    // table it did not itself project (format/id-space/strict-ordered roots/
    // per-node legality/cycle-safe reachability, plus the reserved-name and
    // nullable-Option gates).
    auto local = verify_local(*table);
    if (!local.empty()) {
        result.diagnostics.insert(result.diagnostics.end(),
                                  std::make_move_iterator(local.begin()),
                                  std::make_move_iterator(local.end()));
        return result;
    }
    // Canonical admission: re-encode the decoded table and require byte-for-byte
    // equality with the input. This is the single authority for shortest-form /
    // canonical LEB and field ordering, so the byte readers above only need to be
    // memory-safe. Any non-canonical or otherwise inequivalent payload is rejected.
    auto reencoded = encode_core_wire_schema_table(*table);
    if (!reencoded.ok() || !reencoded.bytes.has_value()) {
        result.diagnostics.insert(result.diagnostics.end(),
                                  std::make_move_iterator(reencoded.diagnostics.begin()),
                                  std::make_move_iterator(reencoded.diagnostics.end()));
        if (!result.has_errors()) {
            result.diagnostics.push_back(diagnostic(
                wire_schema::kInvalid, "wire-schema payload failed canonical re-encoding"));
        }
        return result;
    }
    const auto &canonical = *reencoded.bytes;
    if (canonical.size() != bytes.size() ||
        !std::equal(canonical.begin(), canonical.end(), bytes.begin())) {
        result.diagnostics.push_back(diagnostic(
            wire_schema::kInvalid, "wire-schema payload is not canonical"));
        return result;
    }
    result.table = std::move(*table);
    return result;
}

} // namespace ahfl::ir::core
