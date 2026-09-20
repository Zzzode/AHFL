#include "ahfl/compiler/ir/core_layout.hpp"

#include "ahfl/base/support/overloaded.hpp"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace ahfl::ir::core {

namespace {

enum class LayoutEdge { Inline, Indirect };

[[nodiscard]] bool valid_wasm32_target(const TargetDataLayout &target) noexcept {
    return target.id == TargetDataLayoutId::Wasm32 && target.pointer_size == 4 &&
           target.pointer_align == 4 && target.function_index_size == 4 &&
           target.function_index_align == 4;
}

[[nodiscard]] bool is_power_of_two(std::uint32_t value) noexcept {
    return value != 0 && (value & (value - 1)) == 0;
}

[[nodiscard]] std::optional<std::uint64_t> checked_add(std::uint64_t lhs,
                                                       std::uint64_t rhs) noexcept {
    if (lhs > std::numeric_limits<std::uint64_t>::max() - rhs) {
        return std::nullopt;
    }
    return lhs + rhs;
}

[[nodiscard]] std::optional<std::uint64_t> checked_mul(std::uint64_t lhs,
                                                       std::uint64_t rhs) noexcept {
    if (lhs != 0 && rhs > std::numeric_limits<std::uint64_t>::max() / lhs) {
        return std::nullopt;
    }
    return lhs * rhs;
}

[[nodiscard]] std::optional<std::uint64_t> checked_align_up(std::uint64_t value,
                                                            std::uint32_t align) noexcept {
    if (!is_power_of_two(align)) {
        return std::nullopt;
    }
    const std::uint64_t mask = static_cast<std::uint64_t>(align) - 1;
    const auto sum = checked_add(value, mask);
    if (!sum) {
        return std::nullopt;
    }
    return *sum & ~mask;
}

[[nodiscard]] CoreLayout scalar_layout(CoreScalarRepr repr) {
    switch (repr) {
    case CoreScalarRepr::I32:
        return CoreLayout{4, 4, false, CoreLayoutScalar{repr}};
    case CoreScalarRepr::I64:
    case CoreScalarRepr::F64:
        return CoreLayout{8, 8, false, CoreLayoutScalar{repr}};
    }
    return CoreLayout{};
}

class LayoutBuilder {
  public:
    LayoutBuilder(const CoreProgram &program, TargetDataLayout target)
        : program_(program), scratch_types_(program.value_types) {
        table_.target = target;
        sync_type_slots();
        table_.value_layouts = type_layouts_;
    }

    [[nodiscard]] std::optional<CoreLayoutTable> run() {
        if (!valid_wasm32_target(table_.target)) {
            fail(layout::kUnsupported,
                 "P4-D D1 supports only the canonical wasm32 target data layout", std::nullopt);
            return std::nullopt;
        }

        // This loop deliberately observes growth: member-template instantiation
        // can append layout-private logical types to scratch_types_.
        for (std::uint32_t i = 0; i < scratch_types_.size(); ++i) {
            if (!layout_type(CoreValueTypeId{i}, LayoutEdge::Inline, std::nullopt)) {
                return std::nullopt;
            }
        }
        // Closure environments first: an env is an aggregate over capture value
        // types, and building it may materialize a container layout that then
        // needs its backing finalised. Container backings depend only on their
        // element/value SHAPES (already final), never on an env, so this order
        // leaves nothing pending.
        if (!finalize_closure_envs()) {
            return std::nullopt;
        }
        if (!finalize_container_backings()) {
            return std::nullopt;
        }
        for (const CoreLayout &entry : table_.layouts) {
            if (std::holds_alternative<CoreLayoutPending>(entry.shape)) {
                fail(layout::kInvalid, "layout table contains an unfinished placeholder",
                     std::nullopt);
                return std::nullopt;
            }
        }
        return std::move(table_);
    }

    [[nodiscard]] std::vector<CoreLowerDiagnostic> take_diagnostics() {
        return std::move(diagnostics_);
    }

  private:
    void fail(std::string_view code, std::string message, SourceRangeOpt range) {
        if (!diagnostics_.empty()) {
            return;
        }
        diagnostics_.push_back(CoreLowerDiagnostic{CoreDiagnosticSeverity::Error,
                                                   std::string(code), std::move(message),
                                                   std::move(range)});
    }

    [[nodiscard]] SourceRangeOpt type_range(CoreValueTypeId id) const {
        if (id.value >= scratch_types_.size()) {
            return std::nullopt;
        }
        if (const auto *nominal =
                std::get_if<CoreVtNominal>(&scratch_types_[id.value].node);
            nominal != nullptr && nominal->base.value < program_.types.size()) {
            return program_.types[nominal->base.value].source_range;
        }
        return std::nullopt;
    }

    void sync_type_slots() {
        while (type_layouts_.size() < scratch_types_.size()) {
            if (table_.layouts.size() >= CoreLayoutId::kInvalid) {
                fail(layout::kOverflow, "layout arena exceeded its 32-bit id space",
                     std::nullopt);
                return;
            }
            const auto id = CoreLayoutId{static_cast<std::uint32_t>(table_.layouts.size())};
            table_.layouts.push_back(CoreLayout{});
            type_layouts_.push_back(id);
            states_.push_back(0);
        }
    }

    [[nodiscard]] std::optional<CoreLayoutId> reserve_internal_layout() {
        if (table_.layouts.size() >= CoreLayoutId::kInvalid) {
            fail(layout::kOverflow, "layout arena exceeded its 32-bit id space", std::nullopt);
            return std::nullopt;
        }
        const auto id = CoreLayoutId{static_cast<std::uint32_t>(table_.layouts.size())};
        table_.layouts.push_back(CoreLayout{});
        return id;
    }

    [[nodiscard]] std::optional<CoreLayoutId> layout_type(CoreValueTypeId type,
                                                          LayoutEdge edge,
                                                          SourceRangeOpt range) {
        sync_type_slots();
        if (!diagnostics_.empty()) {
            return std::nullopt;
        }
        if (type.value >= scratch_types_.size()) {
            fail(layout::kInvalid, "logical value type id is out of range", std::move(range));
            return std::nullopt;
        }
        const CoreLayoutId layout_id = type_layouts_[type.value];
        if (states_[type.value] == 2) {
            return layout_id;
        }
        if (states_[type.value] == 1) {
            if (edge == LayoutEdge::Indirect) {
                return layout_id;
            }
            fail(layout::kInfiniteRecursion,
                 "logical type has an infinite direct inline layout",
                 range.has_value() ? std::move(range) : type_range(type));
            return std::nullopt;
        }

        states_[type.value] = 1;
        const auto built = build_type(type);
        if (!built) {
            return std::nullopt;
        }
        table_.layouts[layout_id.value] = *built;
        states_[type.value] = 2;
        return layout_id;
    }

    [[nodiscard]] std::optional<CoreLayout> build_type(CoreValueTypeId type) {
        // Snapshot the scratch value type BY VALUE: the visitor below descends
        // through `instantiate` / `layout_type`, both of which hash-cons into
        // `scratch_types_` and can reallocate it. A reference into the arena would
        // dangle after the first generic field/slot append and the next read of a
        // nominal/tuple payload would be a use-after-free (ASan-proven). The copy
        // is cheap (small inline vectors) and this is not a hot path.
        const CoreValueType value_type = scratch_types_[type.value];
        // RFC 0027 Q1 (KR6.13-X): the handler list is generated from the
        // core_value_types.def X-list; each entry token-pastes to a real,
        // explicitly-typed local lambda below. There is deliberately NO
        // catch-all: adding a 15th node to the .def without defining
        // `vt_##Name` fails to compile. Per-node semantics are those of each
        // lambda below (the closure arm builds the D2 word-pair layout).
        const auto vt_Unit = [&](const CoreVtUnit &) -> std::optional<CoreLayout> {
            return CoreLayout{0, 1, true, CoreLayoutStruct{}};
        };
        const auto vt_Never = [&](const CoreVtNever &) -> std::optional<CoreLayout> {
            return CoreLayout{0, 1, true, CoreLayoutUninhabited{}};
        };
        const auto vt_Bool = [&](const CoreVtBool &) -> std::optional<CoreLayout> {
            return scalar_layout(CoreScalarRepr::I32);
        };
        const auto vt_Int = [&](const CoreVtInt &node) -> std::optional<CoreLayout> {
            constexpr auto i32_min =
                static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::min());
            constexpr auto i32_max =
                static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max());
            const bool narrow = node.bounds.has_value() && node.bounds->first >= i32_min &&
                                node.bounds->second <= i32_max;
            return scalar_layout(narrow ? CoreScalarRepr::I32 : CoreScalarRepr::I64);
        };
        const auto vt_Float = [&](const CoreVtFloat &) -> std::optional<CoreLayout> {
            return scalar_layout(CoreScalarRepr::F64);
        };
        const auto vt_String = [&](const CoreVtString &) -> std::optional<CoreLayout> {
            return CoreLayout{8, 4, false, CoreLayoutPtrLen{}};
        };
        const auto vt_Decimal = [&](const CoreVtDecimal &) -> std::optional<CoreLayout> {
            return scalar_layout(CoreScalarRepr::I64);
        };
        const auto vt_Duration = [&](const CoreVtDuration &) -> std::optional<CoreLayout> {
            return scalar_layout(CoreScalarRepr::I64);
        };
        const auto vt_Timestamp = [&](const CoreVtTimestamp &) -> std::optional<CoreLayout> {
            return scalar_layout(CoreScalarRepr::I64);
        };
        const auto vt_Uuid = [&](const CoreVtUuid &) -> std::optional<CoreLayout> {
            return CoreLayout{16, 1, false, CoreLayoutBytes{16}};
        };
        const auto vt_Nominal = [&](const CoreVtNominal &node) -> std::optional<CoreLayout> {
            return build_nominal(type, node);
        };
        const auto vt_Tuple = [&](const CoreVtTuple &node) -> std::optional<CoreLayout> {
            return build_aggregate(node.elements, type_range(type));
        };
        const auto vt_Fn = [&](const CoreVtFn &) -> std::optional<CoreLayout> {
            return CoreLayout{4, 4, false, CoreLayoutFnRef{}};
        };
        const auto vt_Closure = [&](const CoreVtClosure &node) -> std::optional<CoreLayout> {
            // RFC 0026 P6-8a (P4-D D2). A closure's representation is ALWAYS the
            // `(func_index:i32, env_ptr:i32)` word pair — size 8 / alignment 4 —
            // so its size never depends on how much it captured. The captured
            // environment is a separate aggregate reached through an INDIRECT
            // edge, which is exactly why the closure's size is fixed: the env is
            // finalised AFTER every root layout (like a collection backing), so
            // `struct S { f: Closure<capturing S> }` finalises — the closure
            // FIELD is one word pair, and the env is a separate aggregate that
            // inlines S — instead of reporting a spurious
            // core.layout.INFINITE_RECURSION.
            CoreLayoutClosure shape;
            if (!node.captures.empty()) {
                const auto environment = reserve_internal_layout();
                if (!environment) {
                    return std::nullopt;
                }
                shape.environment = *environment;
                pending_closures_.push_back(PendingClosure{
                    type_layouts_[type.value], node.captures, type_range(type)});
            }
            return CoreLayout{8, 4, false, std::move(shape)};
        };
#define HANDLE_CORE_VT(Name) vt_##Name,
        return std::visit(
            Overloaded{
#include "ahfl/compiler/ir/core_value_types.def"
            },
            value_type.node);
#undef HANDLE_CORE_VT
    }

    [[nodiscard]] std::optional<CoreValueTypeId>
    instantiate(CoreTypeId owner,
                CoreMemberTypeTemplateNodeId root,
                const std::vector<CoreValueTypeId> &args,
                SourceRangeOpt range) {
        std::string reason;
        const auto value = instantiate_member_template_into(scratch_types_, program_.types, owner,
                                                            root, args, &reason);
        if (!value) {
            fail(layout::kInvalid, "member type template is invalid: " + reason,
                 std::move(range));
            return std::nullopt;
        }
        sync_type_slots();
        if (!diagnostics_.empty()) {
            return std::nullopt;
        }
        return value;
    }

    [[nodiscard]] std::optional<CoreLayout> build_nominal(CoreValueTypeId type,
                                                          const CoreVtNominal &node) {
        if (node.base.value >= program_.types.size()) {
            fail(layout::kInvalid, "nominal layout base is out of range", type_range(type));
            return std::nullopt;
        }
        const CoreTypeDecl &decl = program_.types[node.base.value];
        if (node.args.size() != decl.type_param_count) {
            fail(layout::kInvalid,
                 "nominal layout argument count does not match its declaration", decl.source_range);
            return std::nullopt;
        }
        if (decl.role == CoreNominalRole::List || decl.role == CoreNominalRole::Set ||
            decl.role == CoreNominalRole::Map) {
            return build_container(type, node, decl);
        }
        if (node.capacity.has_value()) {
            fail(layout::kInvalid, "non-collection nominal carries a capacity", decl.source_range);
            return std::nullopt;
        }

        if (decl.kind == CoreTypeDecl::Kind::Struct) {
            if (decl.field_type_template_roots.size() != decl.fields.size()) {
                fail(layout::kInvalid,
                     "struct member-template roots are not parallel to fields", decl.source_range);
                return std::nullopt;
            }
            std::vector<CoreValueTypeId> fields;
            fields.reserve(decl.field_type_template_roots.size());
            for (const auto root : decl.field_type_template_roots) {
                const auto field = instantiate(node.base, root, node.args, decl.source_range);
                if (!field) {
                    return std::nullopt;
                }
                fields.push_back(*field);
            }
            return build_aggregate(fields, decl.source_range);
        }

        if (decl.variant_payloads.size() != decl.variants.size()) {
            fail(layout::kInvalid,
                 "enum payload metadata is not parallel to variants", decl.source_range);
            return std::nullopt;
        }
        std::vector<CoreLayoutId> payload_layouts;
        std::vector<std::uint64_t> payload_sizes;
        payload_layouts.reserve(decl.variant_payloads.size());
        payload_sizes.reserve(decl.variant_payloads.size());
        std::uint32_t payload_align = 1;
        std::uint64_t payload_size = 0;
        for (const auto &payload : decl.variant_payloads) {
            const auto aggregate_id = reserve_internal_layout();
            if (!aggregate_id) {
                return std::nullopt;
            }
            std::vector<CoreValueTypeId> slots;
            slots.reserve(payload.slot_type_template_roots.size());
            for (const auto root : payload.slot_type_template_roots) {
                const auto slot = instantiate(node.base, root, node.args, decl.source_range);
                if (!slot) {
                    return std::nullopt;
                }
                slots.push_back(*slot);
            }
            const auto aggregate = build_aggregate(slots, decl.source_range);
            if (!aggregate) {
                return std::nullopt;
            }
            table_.layouts[aggregate_id->value] = *aggregate;
            payload_layouts.push_back(*aggregate_id);
            payload_sizes.push_back(aggregate->size);
            payload_size = std::max(payload_size, aggregate->size);
            payload_align = std::max(payload_align, aggregate->align);
        }
        const auto payload_offset = checked_align_up(4, payload_align);
        const auto unaligned_size = payload_offset ? checked_add(*payload_offset, payload_size)
                                                   : std::nullopt;
        const auto total_align = std::max<std::uint32_t>(4, payload_align);
        const auto total_size = unaligned_size ? checked_align_up(*unaligned_size, total_align)
                                               : std::nullopt;
        if (!payload_offset || !unaligned_size || !total_size) {
            fail(layout::kOverflow, "enum layout arithmetic overflow", decl.source_range);
            return std::nullopt;
        }
        CoreLayoutEnum shape;
        shape.tag_size = 4;
        shape.payload_offset = *payload_offset;
        shape.variant_payload_layouts = std::move(payload_layouts);
        shape.variant_payload_sizes = std::move(payload_sizes);
        return CoreLayout{*total_size, total_align, *total_size == 0, std::move(shape)};
    }

    [[nodiscard]] std::optional<CoreLayout> build_container(CoreValueTypeId type,
                                                            const CoreVtNominal &node,
                                                            const CoreTypeDecl &decl) {
        if (!node.capacity.has_value()) {
            fail(layout::kUnbounded,
                 "bounded wasm32 layout requires a static collection capacity",
                 decl.source_range);
            return std::nullopt;
        }
        const std::size_t expected = decl.role == CoreNominalRole::Map ? 2 : 1;
        if (node.args.size() != expected) {
            fail(layout::kInvalid, "collection role has an invalid logical arity",
                 decl.source_range);
            return std::nullopt;
        }
        const auto element = layout_type(node.args[0], LayoutEdge::Indirect, decl.source_range);
        if (!element) {
            return std::nullopt;
        }
        std::optional<CoreLayoutId> value;
        if (decl.role == CoreNominalRole::Map) {
            value = layout_type(node.args[1], LayoutEdge::Indirect, decl.source_range);
            if (!value) {
                return std::nullopt;
            }
        }
        CoreLayoutContainer shape;
        shape.element = *element;
        shape.value = value;
        shape.capacity = *node.capacity;
        const CoreLayoutId owner_layout = type_layouts_[type.value];
        pending_containers_.push_back(PendingContainer{owner_layout, decl.source_range});
        return CoreLayout{8, 4, false, std::move(shape)};
    }

    [[nodiscard]] std::optional<CoreLayout>
    build_aggregate(const std::vector<CoreValueTypeId> &members, SourceRangeOpt range) {
        CoreLayoutStruct shape;
        shape.field_offsets.reserve(members.size());
        shape.field_layouts.reserve(members.size());
        std::uint64_t size = 0;
        std::uint32_t align = 1;
        for (const auto member : members) {
            const auto child = layout_type(member, LayoutEdge::Inline, range);
            if (!child) {
                return std::nullopt;
            }
            const CoreLayout &child_layout = table_.layouts[child->value];
            const auto offset = checked_align_up(size, child_layout.align);
            const auto next = offset ? checked_add(*offset, child_layout.size) : std::nullopt;
            if (!offset || !next) {
                fail(layout::kOverflow, "aggregate layout arithmetic overflow", std::move(range));
                return std::nullopt;
            }
            shape.field_offsets.push_back(*offset);
            shape.field_layouts.push_back(*child);
            size = *next;
            align = std::max(align, child_layout.align);
        }
        const auto padded = checked_align_up(size, align);
        if (!padded) {
            fail(layout::kOverflow, "aggregate tail-padding overflow", std::move(range));
            return std::nullopt;
        }
        return CoreLayout{*padded, align, *padded == 0, std::move(shape)};
    }

    // Finalise every closure's environment aggregate (RFC 0026 P6-8a). The env
    // is a `CoreLayoutStruct` over the capture slots in canonical env-slot order
    // (vector index == slot order, design §"Closure capture order"), reached
    // from the closure through an INDIRECT edge. It is built HERE rather than
    // inside `build_type` for the same reason a container backing is: an
    // indirect edge must not descend while a root is still `Visiting`, or a
    // closure capturing its own enclosing struct type would be misreported as
    // infinite inline recursion. Building an env may materialize further
    // layout-private types (a captured `List<T>(n)`), so this consumes a growing
    // `pending_closures_` list exactly as the container pass does.
    [[nodiscard]] bool finalize_closure_envs() {
        for (std::size_t cursor = 0; cursor < pending_closures_.size(); ++cursor) {
            const PendingClosure pending = pending_closures_[cursor];
            if (pending.id.value >= table_.layouts.size()) {
                fail(layout::kInvalid, "closure environment layout id is out of range",
                     pending.range);
                return false;
            }
            auto *closure = std::get_if<CoreLayoutClosure>(&table_.layouts[pending.id.value].shape);
            if (closure == nullptr || !closure->environment.has_value()) {
                fail(layout::kInvalid, "closure environment edge is invalid", pending.range);
                return false;
            }
            const CoreLayoutId env_id = *closure->environment;
            if (env_id.value >= table_.layouts.size()) {
                fail(layout::kInvalid, "closure environment layout id is out of range",
                     pending.range);
                return false;
            }
            std::vector<CoreValueTypeId> slots;
            slots.reserve(pending.captures.size());
            for (const CoreClosureCapture &capture : pending.captures) {
                slots.push_back(capture.value_type);
            }
            const auto environment = build_aggregate(slots, pending.range);
            if (!environment) {
                return false;
            }
            table_.layouts[env_id.value] = *environment;
        }
        return true;
    }

    [[nodiscard]] bool finalize_container_backings() {
        for (const PendingContainer &pending : pending_containers_) {
            const CoreLayoutId id = pending.id;
            if (id.value >= table_.layouts.size()) {
                fail(layout::kInvalid, "container layout id is out of range", pending.range);
                return false;
            }
            auto *container = std::get_if<CoreLayoutContainer>(&table_.layouts[id.value].shape);
            if (container == nullptr || container->element.value >= table_.layouts.size()) {
                fail(layout::kInvalid, "container backing edge is invalid", pending.range);
                return false;
            }
            const CoreLayout &element = table_.layouts[container->element.value];
            if (std::holds_alternative<CoreLayoutPending>(element.shape)) {
                fail(layout::kInvalid,
                     "container element placeholder was not finalized", pending.range);
                return false;
            }
            std::uint64_t raw_size = element.size;
            std::uint32_t entry_align = element.align;
            container->value_offset = 0;
            if (container->value.has_value()) {
                if (container->value->value >= table_.layouts.size()) {
                    fail(layout::kInvalid, "Map value layout id is out of range", pending.range);
                    return false;
                }
                const CoreLayout &value_layout = table_.layouts[container->value->value];
                if (std::holds_alternative<CoreLayoutPending>(value_layout.shape)) {
                    fail(layout::kInvalid,
                         "Map value placeholder was not finalized", pending.range);
                    return false;
                }
                const auto value_offset = checked_align_up(element.size, value_layout.align);
                const auto entry_size = value_offset
                                            ? checked_add(*value_offset, value_layout.size)
                                            : std::nullopt;
                if (!value_offset || !entry_size) {
                    fail(layout::kOverflow, "Map entry layout arithmetic overflow", pending.range);
                    return false;
                }
                container->value_offset = *value_offset;
                raw_size = *entry_size;
                entry_align = std::max(entry_align, value_layout.align);
            }
            const auto stride = checked_align_up(raw_size, entry_align);
            const auto backing = stride ? checked_mul(*stride, container->capacity) : std::nullopt;
            if (!stride || !backing) {
                fail(layout::kOverflow,
                     "collection backing stride * capacity overflows uint64", pending.range);
                return false;
            }
            container->stride = *stride;
            container->backing_size = *backing;
        }
        return true;
    }

    const CoreProgram &program_;
    struct PendingContainer {
        CoreLayoutId id;
        SourceRangeOpt range;
    };
    // RFC 0026 P6-8a: a closure's environment aggregate is finalised only after
    // every root layout (like a container backing), because the env is an
    // INDIRECT edge — descending into it eagerly would make a closure that
    // captures itself report a spurious infinite inline recursion.
    struct PendingClosure {
        CoreLayoutId id;
        std::vector<CoreClosureCapture> captures;
        SourceRangeOpt range;
    };
    std::vector<CoreValueType> scratch_types_;
    CoreLayoutTable table_;
    std::vector<CoreLayoutId> type_layouts_;
    std::vector<std::uint8_t> states_;
    std::vector<PendingContainer> pending_containers_;
    std::vector<PendingClosure> pending_closures_;
    std::vector<CoreLowerDiagnostic> diagnostics_;
};

} // namespace

bool CoreLayoutBuildResult::has_errors() const noexcept {
    return std::any_of(diagnostics.begin(), diagnostics.end(), [](const CoreLowerDiagnostic &d) {
        return d.severity == CoreDiagnosticSeverity::Error;
    });
}

CoreLayoutBuildResult compute_core_layouts(const CoreProgram &program, TargetDataLayout target) {
    LayoutBuilder builder(program, target);
    CoreLayoutBuildResult result;
    result.table = builder.run();
    result.diagnostics = builder.take_diagnostics();
    if (!result.table) {
        return result;
    }
    auto verification = verify_core_layout_table(program, *result.table);
    if (!verification.empty()) {
        result.table.reset();
        result.diagnostics.insert(result.diagnostics.end(),
                                  std::make_move_iterator(verification.begin()),
                                  std::make_move_iterator(verification.end()));
    }
    return result;
}

namespace {

class LayoutVerifier {
  public:
    LayoutVerifier(const CoreProgram &program, const CoreLayoutTable &table)
        : program_(program), table_(table) {}

    [[nodiscard]] std::vector<CoreLowerDiagnostic> run() {
        if (!valid_wasm32_target(table_.target)) {
            error("layout table target is not the canonical wasm32 data layout",
                  layout::kUnsupported);
        }
        if (table_.value_layouts.size() != program_.value_types.size()) {
            error("value_layouts is not parallel to CoreProgram::value_types");
        }
        std::set<std::uint32_t> roots;
        for (const CoreLayoutId root : table_.value_layouts) {
            if (!in_range(root)) {
                error("value layout root is out of range");
            } else if (!roots.insert(root.value).second) {
                error("two logical value types share one non-canonical layout root");
            }
        }
        for (std::uint32_t i = 0; i < table_.layouts.size(); ++i) {
            verify_entry(CoreLayoutId{i});
        }
        verify_inline_acyclic();
        verify_reachability();
        return std::move(diagnostics_);
    }

  private:
    void error(std::string message, std::string_view code = layout::kInvalid) {
        diagnostics_.push_back(CoreLowerDiagnostic{CoreDiagnosticSeverity::Error,
                                                   std::string(code),
                                                   std::move(message), std::nullopt});
    }

    [[nodiscard]] bool in_range(CoreLayoutId id) const noexcept {
        return id.value != CoreLayoutId::kInvalid && id.value < table_.layouts.size();
    }

    [[nodiscard]] bool child(CoreLayoutId id, std::string_view what) {
        if (!in_range(id)) {
            error(std::string(what) + " layout id is out of range");
            return false;
        }
        return true;
    }

    [[nodiscard]] std::vector<CoreLayoutId> inline_children(const CoreLayout &entry) const {
        if (const auto *record = std::get_if<CoreLayoutStruct>(&entry.shape)) {
            return record->field_layouts;
        }
        if (const auto *tagged = std::get_if<CoreLayoutEnum>(&entry.shape)) {
            return tagged->variant_payload_layouts;
        }
        return {};
    }

    [[nodiscard]] std::vector<CoreLayoutId> all_children(const CoreLayout &entry) const {
        auto result = inline_children(entry);
        if (const auto *container = std::get_if<CoreLayoutContainer>(&entry.shape)) {
            result.push_back(container->element);
            if (container->value) {
                result.push_back(*container->value);
            }
        }
        // RFC 0026 P6-8a: a closure's environment is an INDIRECT edge, so it is
        // reached (never an inline-cycle participant) but must still be walked
        // for reachability/orphan detection like a container backing edge.
        if (const auto *closure = std::get_if<CoreLayoutClosure>(&entry.shape)) {
            if (closure->environment) {
                result.push_back(*closure->environment);
            }
        }
        return result;
    }

    void verify_entry(CoreLayoutId id) {
        const CoreLayout &entry = table_.layouts[id.value];
        if (!is_power_of_two(entry.align)) {
            error("layout entry has a zero or non-power-of-two alignment");
        }
        if (entry.is_zero_sized != (entry.size == 0)) {
            error("layout entry is_zero_sized disagrees with size");
        }
        if (entry.shape.valueless_by_exception()) {
            error("layout entry has a valueless shape variant");
            return;
        }
        std::visit(
            Overloaded{
                [&](const CoreLayoutPending &) {
                    error("layout table contains an unfinished placeholder");
                },
                [&](const CoreLayoutScalar &shape) {
                    const bool i32 = shape.repr == CoreScalarRepr::I32;
                    const bool i64 = shape.repr == CoreScalarRepr::I64;
                    const bool f64 = shape.repr == CoreScalarRepr::F64;
                    if ((!i32 && !i64 && !f64) ||
                        (i32 && (entry.size != 4 || entry.align != 4)) ||
                        ((i64 || f64) && (entry.size != 8 || entry.align != 8))) {
                        error("scalar layout size/alignment/repr is inconsistent");
                    }
                },
                [&](const CoreLayoutBytes &shape) {
                    if (entry.size != shape.byte_count || entry.align != 1) {
                        error("opaque byte layout size/alignment is inconsistent");
                    }
                },
                [&](const CoreLayoutPtrLen &) {
                    if (entry.size != 8 || entry.align != 4) {
                        error("PtrLen layout must be size 8 alignment 4 on wasm32");
                    }
                },
                [&](const CoreLayoutFnRef &) {
                    if (entry.size != 4 || entry.align != 4) {
                        error("function reference layout must be i32 on wasm32");
                    }
                },
                [&](const CoreLayoutClosure &shape) { verify_closure(entry, shape); },
                [&](const CoreLayoutStruct &shape) { verify_struct(entry, shape); },
                [&](const CoreLayoutEnum &shape) { verify_enum(entry, shape); },
                [&](const CoreLayoutContainer &shape) { verify_container(entry, shape); },
                [&](const CoreLayoutUninhabited &) {
                    if (entry.size != 0 || entry.align != 1) {
                        error("uninhabited layout must be size 0 alignment 1");
                    }
                },
            },
            entry.shape);
    }

    void verify_struct(const CoreLayout &entry, const CoreLayoutStruct &shape) {
        if (shape.field_offsets.size() != shape.field_layouts.size()) {
            error("struct field offsets/layout ids are not parallel");
            return;
        }
        std::uint64_t size = 0;
        std::uint32_t align = 1;
        for (std::size_t i = 0; i < shape.field_layouts.size(); ++i) {
            const CoreLayoutId member_id = shape.field_layouts[i];
            if (!child(member_id, "struct field")) {
                continue;
            }
            const CoreLayout &member = table_.layouts[member_id.value];
            const auto offset = checked_align_up(size, member.align);
            const auto next = offset ? checked_add(*offset, member.size) : std::nullopt;
            if (!offset || !next) {
                error("struct layout arithmetic overflows");
                return;
            }
            if (shape.field_offsets[i] != *offset) {
                error("struct field offset is not canonical");
            }
            size = *next;
            align = std::max(align, member.align);
        }
        const auto padded = checked_align_up(size, align);
        if (!padded || entry.size != *padded || entry.align != align) {
            error("struct aggregate size/alignment is inconsistent");
        }
    }

    // A closure is ALWAYS the `(func_index:i32, env_ptr:i32)` word pair on
    // wasm32 (RFC 0026 P6-8a), and its environment — an INDIRECT edge — is a
    // struct aggregate whose field count is the capture count. The capture count
    // is not repeated on the layout (it is the env's field count), so the two
    // cannot disagree.
    void verify_closure(const CoreLayout &entry, const CoreLayoutClosure &shape) {
        if (entry.size != 8 || entry.align != 4) {
            error("closure layout must be size 8 alignment 4 on wasm32");
            return;
        }
        if (!shape.environment) {
            return; // a capture-free closure has no environment aggregate
        }
        if (!child(*shape.environment, "closure environment")) {
            return;
        }
        if (!std::holds_alternative<CoreLayoutStruct>(
                table_.layouts[shape.environment->value].shape)) {
            error("closure environment must be a struct aggregate");
        }
    }

    void verify_enum(const CoreLayout &entry, const CoreLayoutEnum &shape) {
        if (shape.tag_size != 4 ||
            shape.variant_payload_layouts.size() != shape.variant_payload_sizes.size()) {
            error("enum tag or parallel payload metadata is invalid");
            return;
        }
        std::uint64_t payload_size = 0;
        std::uint32_t payload_align = 1;
        for (std::size_t i = 0; i < shape.variant_payload_layouts.size(); ++i) {
            const CoreLayoutId payload_id = shape.variant_payload_layouts[i];
            if (!child(payload_id, "enum payload")) {
                continue;
            }
            const CoreLayout &payload = table_.layouts[payload_id.value];
            if (shape.variant_payload_sizes[i] != payload.size) {
                error("enum variant payload size disagrees with its aggregate");
            }
            payload_size = std::max(payload_size, payload.size);
            payload_align = std::max(payload_align, payload.align);
        }
        const auto payload_offset = checked_align_up(4, payload_align);
        const auto raw = payload_offset ? checked_add(*payload_offset, payload_size) : std::nullopt;
        const auto align = std::max<std::uint32_t>(4, payload_align);
        const auto size = raw ? checked_align_up(*raw, align) : std::nullopt;
        if (!payload_offset || !raw || !size || shape.payload_offset != *payload_offset ||
            entry.size != *size || entry.align != align) {
            error("enum aggregate size/alignment is inconsistent");
        }
    }

    void verify_container(const CoreLayout &entry, const CoreLayoutContainer &shape) {
        if (entry.size != 8 || entry.align != 4 || !child(shape.element, "container element")) {
            error("container header or element edge is invalid");
            return;
        }
        const CoreLayout &element = table_.layouts[shape.element.value];
        std::uint64_t raw_size = element.size;
        std::uint32_t align = element.align;
        std::uint64_t value_offset = 0;
        if (shape.value) {
            if (!child(*shape.value, "Map value")) {
                return;
            }
            const CoreLayout &value = table_.layouts[shape.value->value];
            const auto offset = checked_align_up(element.size, value.align);
            const auto raw = offset ? checked_add(*offset, value.size) : std::nullopt;
            if (!offset || !raw) {
                error("Map entry layout arithmetic overflows");
                return;
            }
            value_offset = *offset;
            raw_size = *raw;
            align = std::max(align, value.align);
        }
        const auto stride = checked_align_up(raw_size, align);
        const auto backing = stride ? checked_mul(*stride, shape.capacity) : std::nullopt;
        if (!stride || !backing || shape.value_offset != value_offset ||
            shape.stride != *stride || shape.backing_size != *backing) {
            error("container backing stride/capacity metadata is inconsistent");
        }
    }

    void verify_inline_acyclic() {
        std::vector<std::uint8_t> color(table_.layouts.size(), 0);
        const auto visit = [&](auto &&self, CoreLayoutId id) -> void {
            if (!in_range(id) || color[id.value] == 2) {
                return;
            }
            if (color[id.value] == 1) {
                error("layout graph contains a cycle made only of Inline edges",
                      layout::kInfiniteRecursion);
                return;
            }
            color[id.value] = 1;
            for (const CoreLayoutId child_id : inline_children(table_.layouts[id.value])) {
                self(self, child_id);
            }
            color[id.value] = 2;
        };
        for (std::uint32_t i = 0; i < table_.layouts.size(); ++i) {
            visit(visit, CoreLayoutId{i});
        }
    }

    void verify_reachability() {
        std::vector<bool> reached(table_.layouts.size(), false);
        const auto visit = [&](auto &&self, CoreLayoutId id) -> void {
            if (!in_range(id) || reached[id.value]) {
                return;
            }
            reached[id.value] = true;
            for (const CoreLayoutId child_id : all_children(table_.layouts[id.value])) {
                self(self, child_id);
            }
        };
        for (const CoreLayoutId root : table_.value_layouts) {
            visit(visit, root);
        }
        for (std::uint32_t i = 0; i < reached.size(); ++i) {
            if (!reached[i]) {
                error("layout arena contains an orphan node");
            }
        }
    }

    const CoreProgram &program_;
    const CoreLayoutTable &table_;
    std::vector<CoreLowerDiagnostic> diagnostics_;
};

enum class PairState : std::uint8_t { Visiting, Equivalent, Different };

} // namespace

std::vector<CoreLowerDiagnostic>
verify_core_layout_table(const CoreProgram &program, const CoreLayoutTable &table) {
    auto diagnostics = LayoutVerifier(program, table).run();
    if (!diagnostics.empty()) {
        return diagnostics;
    }

    // Bind every root and every private member-closure layout back to the
    // logical program, not merely to a self-consistent physical graph. The
    // projection is pure + deterministic, so recomputing with the SAME P4-C
    // template evaluator is a fail-closed consumption-boundary check, not a
    // second type/subtyping engine. Local graph verification above remains
    // independent and catches arithmetic/cycle bugs in the producer itself.
    LayoutBuilder expected_builder(program, table.target);
    const auto expected = expected_builder.run();
    auto expected_diagnostics = expected_builder.take_diagnostics();
    if (!expected) {
        diagnostics.insert(diagnostics.end(),
                           std::make_move_iterator(expected_diagnostics.begin()),
                           std::make_move_iterator(expected_diagnostics.end()));
        return diagnostics;
    }
    if (*expected != table) {
        diagnostics.push_back(CoreLowerDiagnostic{
            CoreDiagnosticSeverity::Error, std::string(layout::kInvalid),
            "layout table does not equal the deterministic projection of its CoreProgram",
            std::nullopt});
    }
    return diagnostics;
}

bool layouts_equivalent(const CoreLayoutTable &table, CoreLayoutId lhs, CoreLayoutId rhs) {
    std::map<std::pair<std::uint32_t, std::uint32_t>, PairState> states;
    const auto compare = [&](auto &&self, CoreLayoutId a, CoreLayoutId b) -> bool {
        if (a.value >= table.layouts.size() || b.value >= table.layouts.size()) {
            return false;
        }
        if (a == b) {
            return !std::holds_alternative<CoreLayoutPending>(table.layouts[a.value].shape);
        }
        const auto key = std::minmax(a.value, b.value);
        const auto pair = std::pair<std::uint32_t, std::uint32_t>{key.first, key.second};
        if (const auto it = states.find(pair); it != states.end()) {
            return it->second == PairState::Visiting || it->second == PairState::Equivalent;
        }
        states.emplace(pair, PairState::Visiting);
        const CoreLayout &left = table.layouts[a.value];
        const CoreLayout &right = table.layouts[b.value];
        bool equal = left.size == right.size && left.align == right.align &&
                     left.is_zero_sized == right.is_zero_sized &&
                     !left.shape.valueless_by_exception() &&
                     !right.shape.valueless_by_exception() &&
                     left.shape.index() == right.shape.index();
        if (equal) {
            equal = std::visit(
                Overloaded{
                    [&](const CoreLayoutPending &) { return false; },
                    [&](const CoreLayoutScalar &x) {
                        const auto *y = std::get_if<CoreLayoutScalar>(&right.shape);
                        return y != nullptr && x.repr == y->repr;
                    },
                    [&](const CoreLayoutBytes &x) {
                        const auto *y = std::get_if<CoreLayoutBytes>(&right.shape);
                        return y != nullptr && x.byte_count == y->byte_count;
                    },
                    [&](const CoreLayoutPtrLen &) {
                        return std::holds_alternative<CoreLayoutPtrLen>(right.shape);
                    },
                    [&](const CoreLayoutFnRef &) {
                        return std::holds_alternative<CoreLayoutFnRef>(right.shape);
                    },
                    [&](const CoreLayoutClosure &x) {
                        const auto *y = std::get_if<CoreLayoutClosure>(&right.shape);
                        if (y == nullptr ||
                            x.environment.has_value() != y->environment.has_value()) {
                            return false;
                        }
                        // The environment is an INDIRECT edge: two closures are
                        // physically equivalent iff the env aggregates are, which
                        // the recursion decides (a re-entered Visiting PAIR is
                        // provisionally equal, exactly as for a container).
                        return !x.environment || self(self, *x.environment, *y->environment);
                    },
                    [&](const CoreLayoutStruct &x) {
                        const auto *y = std::get_if<CoreLayoutStruct>(&right.shape);
                        if (y == nullptr || x.field_offsets != y->field_offsets ||
                            x.field_layouts.size() != y->field_layouts.size()) {
                            return false;
                        }
                        for (std::size_t i = 0; i < x.field_layouts.size(); ++i) {
                            if (!self(self, x.field_layouts[i], y->field_layouts[i])) {
                                return false;
                            }
                        }
                        return true;
                    },
                    [&](const CoreLayoutEnum &x) {
                        const auto *y = std::get_if<CoreLayoutEnum>(&right.shape);
                        if (y == nullptr || x.tag_size != y->tag_size ||
                            x.payload_offset != y->payload_offset ||
                            x.variant_payload_sizes != y->variant_payload_sizes ||
                            x.variant_payload_layouts.size() !=
                                y->variant_payload_layouts.size()) {
                            return false;
                        }
                        for (std::size_t i = 0; i < x.variant_payload_layouts.size(); ++i) {
                            if (!self(self, x.variant_payload_layouts[i],
                                      y->variant_payload_layouts[i])) {
                                return false;
                            }
                        }
                        return true;
                    },
                    [&](const CoreLayoutContainer &x) {
                        const auto *y = std::get_if<CoreLayoutContainer>(&right.shape);
                        if (y == nullptr || x.capacity != y->capacity || x.stride != y->stride ||
                            x.value_offset != y->value_offset ||
                            x.backing_size != y->backing_size ||
                            x.value.has_value() != y->value.has_value() ||
                            !self(self, x.element, y->element)) {
                            return false;
                        }
                        return !x.value || self(self, *x.value, *y->value);
                    },
                    [&](const CoreLayoutUninhabited &) {
                        return std::holds_alternative<CoreLayoutUninhabited>(right.shape);
                    },
                },
                left.shape);
        }
        states[pair] = equal ? PairState::Equivalent : PairState::Different;
        return equal;
    };
    return compare(compare, lhs, rhs);
}

bool value_layouts_equivalent(const CoreLayoutTable &table,
                              CoreValueTypeId lhs,
                              CoreValueTypeId rhs) {
    if (lhs.value >= table.value_layouts.size() || rhs.value >= table.value_layouts.size()) {
        return false;
    }
    const CoreLayoutId left = table.value_layouts[lhs.value];
    const CoreLayoutId right = table.value_layouts[rhs.value];
    if (left.value >= table.layouts.size() || right.value >= table.layouts.size() ||
        std::holds_alternative<CoreLayoutPending>(table.layouts[left.value].shape) ||
        std::holds_alternative<CoreLayoutPending>(table.layouts[right.value].shape)) {
        return false;
    }
    if (lhs == rhs) {
        return true;
    }
    return layouts_equivalent(table, left, right);
}

} // namespace ahfl::ir::core
