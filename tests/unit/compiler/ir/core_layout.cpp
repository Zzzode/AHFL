#include <doctest.h>

#include "ahfl/compiler/ir/core_layout.hpp"

#include <cstdint>
#include <limits>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

using namespace ahfl::ir::core;
using ahfl::SourceRange;

[[nodiscard]] bool has_code(const CoreLayoutBuildResult &result, std::string_view code) {
    for (const auto &diagnostic : result.diagnostics) {
        if (diagnostic.code == code) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool has_code(const std::vector<CoreLowerDiagnostic> &diagnostics,
                            std::string_view code) {
    for (const auto &diagnostic : diagnostics) {
        if (diagnostic.code == code) {
            return true;
        }
    }
    return false;
}

CoreMemberTypeTemplateNode concrete(CoreValueTypeId value) {
    CoreMemberTypeTemplateNode node;
    node.kind = CoreMemberTypeTemplateKind::Concrete;
    node.concrete = value;
    return node;
}

CoreMemberTypeTemplateNode param(std::uint32_t index) {
    CoreMemberTypeTemplateNode node;
    node.kind = CoreMemberTypeTemplateKind::Param;
    node.param_index = index;
    return node;
}

CoreMemberTypeTemplateNode nominal(CoreTypeId type,
                                   std::vector<CoreMemberTypeTemplateNodeId> args = {},
                                   std::optional<std::uint64_t> capacity = std::nullopt) {
    CoreMemberTypeTemplateNode node;
    node.kind = CoreMemberTypeTemplateKind::Nominal;
    node.nominal = type;
    node.children = std::move(args);
    node.capacity = capacity;
    return node;
}

CoreTypeDecl collection(std::string name,
                        CoreNominalRole role,
                        std::uint32_t arity) {
    CoreTypeDecl decl;
    decl.kind = CoreTypeDecl::Kind::Struct;
    decl.name = std::move(name);
    decl.role = role;
    decl.type_param_count = arity;
    decl.variances.assign(arity, CoreVariance::Covariant);
    return decl;
}

TEST_CASE("P4-D wasm32 scalar layouts are deterministic side artifacts") {
    CoreProgram program;
    program.value_types = {
        CoreValueType{CoreVtUnit{}},       CoreValueType{CoreVtBool{}},
        CoreValueType{CoreVtInt{}},
        CoreValueType{CoreVtInt{std::pair<std::int64_t, std::int64_t>{0, 255}}},
        CoreValueType{CoreVtFloat{}},      CoreValueType{CoreVtDecimal{2}},
        CoreValueType{CoreVtDuration{}},   CoreValueType{CoreVtTimestamp{}},
        CoreValueType{CoreVtUuid{}},       CoreValueType{CoreVtString{}},
        CoreValueType{CoreVtFn{{}, CoreValueTypeId{0}}},
        CoreValueType{CoreVtNever{}},
    };

    const auto first = compute_core_layouts(program);
    REQUIRE(first.ok());
    REQUIRE(first.table.has_value());
    const auto second = compute_core_layouts(program);
    REQUIRE(second.ok());
    CHECK(*first.table == *second.table);
    CHECK(program.value_types.size() == 12);

    const auto &table = *first.table;
    CHECK(table.value_layouts.size() == program.value_types.size());
    CHECK(table.layouts[table.value_layouts[0].value].size == 0);
    CHECK(table.layouts[table.value_layouts[1].value].size == 4);
    CHECK(table.layouts[table.value_layouts[2].value].size == 8);
    CHECK(table.layouts[table.value_layouts[3].value].size == 4);
    CHECK(table.layouts[table.value_layouts[4].value].size == 8);
    CHECK(table.layouts[table.value_layouts[5].value].size == 8);
    CHECK(table.layouts[table.value_layouts[6].value].size == 8);
    CHECK(table.layouts[table.value_layouts[7].value].size == 8);
    CHECK(table.layouts[table.value_layouts[8].value].size == 16);
    CHECK(table.layouts[table.value_layouts[8].value].align == 1);
    CHECK(table.layouts[table.value_layouts[9].value].size == 8);
    CHECK(table.layouts[table.value_layouts[10].value].size == 4);
    CHECK(std::get<CoreLayoutScalar>(table.layouts[table.value_layouts[1].value].shape).repr ==
          CoreScalarRepr::I32);
    CHECK(std::get<CoreLayoutScalar>(table.layouts[table.value_layouts[2].value].shape).repr ==
          CoreScalarRepr::I64);
    CHECK(std::get<CoreLayoutScalar>(table.layouts[table.value_layouts[3].value].shape).repr ==
          CoreScalarRepr::I32);
    CHECK(std::get<CoreLayoutScalar>(table.layouts[table.value_layouts[4].value].shape).repr ==
          CoreScalarRepr::F64);
    CHECK(std::holds_alternative<CoreLayoutPtrLen>(
        table.layouts[table.value_layouts[9].value].shape));
    CHECK(std::holds_alternative<CoreLayoutFnRef>(
        table.layouts[table.value_layouts[10].value].shape));
    CHECK(std::holds_alternative<CoreLayoutUninhabited>(
        table.layouts[table.value_layouts[11].value].shape));
    CHECK(value_layouts_equivalent(table, CoreValueTypeId{1}, CoreValueTypeId{3}));
    CHECK(value_layouts_equivalent(table, CoreValueTypeId{5}, CoreValueTypeId{6}));
    CHECK_FALSE(value_layouts_equivalent(table, CoreValueTypeId{8}, CoreValueTypeId{9}));
}

TEST_CASE("P4-D computes declaration-order struct tuple and enum aggregates") {
    CoreProgram program;
    program.value_types = {
        CoreValueType{CoreVtBool{}},
        CoreValueType{CoreVtInt{}},
    };

    CoreTypeDecl pair;
    pair.kind = CoreTypeDecl::Kind::Struct;
    pair.name = "app::Pair";
    pair.fields = {"small", "wide"};
    pair.field_nominal_types = {CoreTypeId{}, CoreTypeId{}};
    pair.field_has_default = {false, false};
    pair.member_type_templates = {concrete(CoreValueTypeId{0}),
                                  concrete(CoreValueTypeId{1})};
    pair.field_type_template_roots = {CoreMemberTypeTemplateNodeId{0},
                                      CoreMemberTypeTemplateNodeId{1}};
    program.types.push_back(std::move(pair));

    CoreTypeDecl maybe;
    maybe.kind = CoreTypeDecl::Kind::Enum;
    maybe.name = "app::MaybeBool";
    maybe.variants = {"None", "Some"};
    maybe.member_type_templates = {concrete(CoreValueTypeId{0})};
    CoreTypeDecl::VariantPayload none;
    CoreTypeDecl::VariantPayload some;
    some.kind = CoreTypeDecl::VariantPayload::Kind::Tuple;
    some.slot_type_template_roots = {CoreMemberTypeTemplateNodeId{0}};
    maybe.variant_payloads = {std::move(none), std::move(some)};
    program.types.push_back(std::move(maybe));

    program.value_types.push_back(
        CoreValueType{CoreVtTuple{{CoreValueTypeId{0}, CoreValueTypeId{1}}}});
    program.value_types.push_back(
        CoreValueType{CoreVtNominal{CoreTypeId{0}, {}, std::nullopt}});
    program.value_types.push_back(
        CoreValueType{CoreVtNominal{CoreTypeId{1}, {}, std::nullopt}});

    const auto result = compute_core_layouts(program);
    REQUIRE(result.ok());
    const auto &table = *result.table;
    const auto &tuple = table.layouts[table.value_layouts[2].value];
    const auto &record = table.layouts[table.value_layouts[3].value];
    REQUIRE(std::holds_alternative<CoreLayoutStruct>(tuple.shape));
    REQUIRE(std::holds_alternative<CoreLayoutStruct>(record.shape));
    CHECK(std::get<CoreLayoutStruct>(tuple.shape).field_offsets ==
          std::vector<std::uint64_t>{0, 8});
    CHECK(std::get<CoreLayoutStruct>(record.shape).field_offsets ==
          std::vector<std::uint64_t>{0, 8});
    CHECK(record.size == 16);

    const auto &tagged = table.layouts[table.value_layouts[4].value];
    REQUIRE(std::holds_alternative<CoreLayoutEnum>(tagged.shape));
    const auto &enum_shape = std::get<CoreLayoutEnum>(tagged.shape);
    CHECK(enum_shape.tag_size == 4);
    CHECK(enum_shape.payload_offset == 4);
    CHECK(enum_shape.variant_payload_sizes == std::vector<std::uint64_t>{0, 4});
    CHECK(tagged.size == 8);
    CHECK(tagged.align == 4);
}

TEST_CASE("P4-D bounded containers carry backing layout and capacity contracts") {
    CoreProgram program;
    program.types.push_back(collection("std::collections::List", CoreNominalRole::List, 1));
    program.types.push_back(collection("std::collections::Set", CoreNominalRole::Set, 1));
    auto map = collection("std::collections::Map", CoreNominalRole::Map, 2);
    map.variances[0] = CoreVariance::Invariant;
    program.types.push_back(std::move(map));
    program.value_types = {
        CoreValueType{CoreVtBool{}},
        CoreValueType{CoreVtUuid{}},
        CoreValueType{CoreVtNominal{CoreTypeId{0}, {CoreValueTypeId{1}}, 4}},
        CoreValueType{CoreVtNominal{CoreTypeId{1}, {CoreValueTypeId{0}}, 3}},
        CoreValueType{
            CoreVtNominal{CoreTypeId{2}, {CoreValueTypeId{0}, CoreValueTypeId{1}}, 2}},
        CoreValueType{CoreVtNominal{CoreTypeId{0}, {CoreValueTypeId{1}}, 8}},
    };

    const auto result = compute_core_layouts(program);
    REQUIRE(result.ok());
    const auto &table = *result.table;
    const auto &list = std::get<CoreLayoutContainer>(
        table.layouts[table.value_layouts[2].value].shape);
    CHECK(list.capacity == 4);
    CHECK(list.stride == 16);
    CHECK(list.backing_size == 64);
    CHECK_FALSE(list.value.has_value());

    const auto &set = std::get<CoreLayoutContainer>(
        table.layouts[table.value_layouts[3].value].shape);
    CHECK(set.stride == 4);
    CHECK(set.backing_size == 12);

    const auto &map_layout = std::get<CoreLayoutContainer>(
        table.layouts[table.value_layouts[4].value].shape);
    REQUIRE(map_layout.value.has_value());
    CHECK(map_layout.value_offset == 4);
    CHECK(map_layout.stride == 20);
    CHECK(map_layout.backing_size == 40);
    CHECK_FALSE(value_layouts_equivalent(table, CoreValueTypeId{2}, CoreValueTypeId{5}));
}

TEST_CASE("P4-D reuses the P4-C evaluator in a private generic member closure") {
    CoreProgram program;

    CoreTypeDecl box;
    box.kind = CoreTypeDecl::Kind::Struct;
    box.name = "app::Box";
    box.fields = {"value"};
    box.field_nominal_types = {CoreTypeId{}};
    box.field_has_default = {false};
    box.type_param_count = 1;
    box.variances = {CoreVariance::Covariant};
    // T, List<T>(4), Option<List<T>(4)>.
    box.member_type_templates = {
        param(0), nominal(CoreTypeId{1}, {CoreMemberTypeTemplateNodeId{0}}, 4),
        nominal(CoreTypeId{2}, {CoreMemberTypeTemplateNodeId{1}})};
    box.field_type_template_roots = {CoreMemberTypeTemplateNodeId{2}};
    program.types.push_back(std::move(box));
    program.types.push_back(collection("std::collections::List", CoreNominalRole::List, 1));

    CoreTypeDecl option;
    option.kind = CoreTypeDecl::Kind::Enum;
    option.name = "std::option::Option";
    option.role = CoreNominalRole::Option;
    option.type_param_count = 1;
    option.variances = {CoreVariance::Covariant};
    option.variants = {"Some", "None"};
    option.member_type_templates = {param(0)};
    CoreTypeDecl::VariantPayload some;
    some.kind = CoreTypeDecl::VariantPayload::Kind::Tuple;
    some.slot_type_template_roots = {CoreMemberTypeTemplateNodeId{0}};
    option.variant_payloads = {std::move(some), CoreTypeDecl::VariantPayload{}};
    program.types.push_back(std::move(option));

    program.value_types = {
        CoreValueType{CoreVtInt{}},
        CoreValueType{CoreVtNominal{CoreTypeId{0}, {CoreValueTypeId{0}}, std::nullopt}},
    };
    const auto original = program.value_types;
    const auto first = compute_core_layouts(program);
    REQUIRE(first.ok());
    CHECK(program.value_types == original);
    REQUIRE(first.table.has_value());
    CHECK(first.table->value_layouts.size() == original.size());
    CHECK(first.table->layouts.size() > first.table->value_layouts.size());
    const auto &box_layout = first.table->layouts[first.table->value_layouts[1].value];
    CHECK(box_layout.size == 12);
    CHECK(box_layout.align == 4);
    const auto second = compute_core_layouts(program);
    REQUIRE(second.ok());
    CHECK(*first.table == *second.table);
}

TEST_CASE("P4-D permits only collection-indirect recursive layout cycles") {
    SUBCASE("Node through bounded List is finite") {
        CoreProgram program;
        CoreTypeDecl node;
        node.kind = CoreTypeDecl::Kind::Struct;
        node.name = "app::Node";
        node.fields = {"children"};
        node.field_nominal_types = {CoreTypeId{1}};
        node.field_has_default = {false};
        node.member_type_templates = {
            nominal(CoreTypeId{0}),
            nominal(CoreTypeId{1}, {CoreMemberTypeTemplateNodeId{0}}, 4),
        };
        node.field_type_template_roots = {CoreMemberTypeTemplateNodeId{1}};
        program.types.push_back(std::move(node));
        program.types.push_back(
            collection("std::collections::List", CoreNominalRole::List, 1));
        program.value_types = {
            CoreValueType{CoreVtNominal{CoreTypeId{0}, {}, std::nullopt}},
        };
        const auto before = program.value_types;
        const auto result = compute_core_layouts(program);
        REQUIRE(result.ok());
        CHECK(program.value_types == before);
        CHECK(result.table->value_layouts.size() == 1);
        const CoreLayoutId node_layout = result.table->value_layouts[0];
        const auto &node_shape =
            std::get<CoreLayoutStruct>(result.table->layouts[node_layout.value].shape);
        REQUIRE(node_shape.field_layouts.size() == 1);
        const auto &list_shape = std::get<CoreLayoutContainer>(
            result.table->layouts[node_shape.field_layouts[0].value].shape);
        CHECK(list_shape.element == node_layout);
        CHECK(list_shape.stride == 8);
        CHECK(list_shape.backing_size == 32);
    }

    SUBCASE("direct struct recursion fails closed") {
        CoreProgram program;
        CoreTypeDecl node;
        node.kind = CoreTypeDecl::Kind::Struct;
        node.name = "app::BadNode";
        node.source_range = SourceRange{11, 29};
        node.fields = {"next"};
        node.field_nominal_types = {CoreTypeId{0}};
        node.field_has_default = {false};
        node.member_type_templates = {nominal(CoreTypeId{0})};
        node.field_type_template_roots = {CoreMemberTypeTemplateNodeId{0}};
        program.types.push_back(std::move(node));
        program.value_types = {
            CoreValueType{CoreVtNominal{CoreTypeId{0}, {}, std::nullopt}},
        };
        const auto result = compute_core_layouts(program);
        CHECK_FALSE(result.ok());
        CHECK_FALSE(result.table.has_value());
        CHECK(has_code(result, layout::kInfiniteRecursion));
        REQUIRE(result.diagnostics.size() == 1);
        REQUIRE(result.diagnostics[0].source_range.has_value());
        CHECK(*result.diagnostics[0].source_range == (SourceRange{11, 29}));
    }

    SUBCASE("direct enum payload recursion fails closed") {
        CoreProgram program;
        CoreTypeDecl loop;
        loop.kind = CoreTypeDecl::Kind::Enum;
        loop.name = "app::Loop";
        loop.variants = {"Again"};
        loop.member_type_templates = {nominal(CoreTypeId{0})};
        CoreTypeDecl::VariantPayload payload;
        payload.kind = CoreTypeDecl::VariantPayload::Kind::Tuple;
        payload.slot_type_template_roots = {CoreMemberTypeTemplateNodeId{0}};
        loop.variant_payloads = {std::move(payload)};
        program.types.push_back(std::move(loop));
        program.value_types = {
            CoreValueType{CoreVtNominal{CoreTypeId{0}, {}, std::nullopt}},
        };
        const auto result = compute_core_layouts(program);
        CHECK_FALSE(result.ok());
        CHECK(has_code(result, layout::kInfiniteRecursion));
    }
}

TEST_CASE("P4-D fail-closes unsupported unbounded overflow and target cases") {
    SUBCASE("closure") {
        CoreProgram program;
        program.value_types = {
            CoreValueType{CoreVtFn{{}, CoreValueTypeId{0}}},
            CoreValueType{CoreVtClosure{CoreValueTypeId{0}, {}}},
        };
        const auto result = compute_core_layouts(program);
        CHECK_FALSE(result.ok());
        CHECK(has_code(result, layout::kUnsupported));
    }

    SUBCASE("unbounded collection") {
        CoreProgram program;
        program.types.push_back(
            collection("std::collections::List", CoreNominalRole::List, 1));
        program.value_types = {
            CoreValueType{CoreVtBool{}},
            CoreValueType{CoreVtNominal{CoreTypeId{0}, {CoreValueTypeId{0}}, std::nullopt}},
        };
        const auto result = compute_core_layouts(program);
        CHECK_FALSE(result.ok());
        CHECK(has_code(result, layout::kUnbounded));
    }

    SUBCASE("backing size overflow") {
        CoreProgram program;
        program.types.push_back(
            collection("std::collections::List", CoreNominalRole::List, 1));
        program.types[0].source_range = SourceRange{31, 47};
        program.value_types = {
            CoreValueType{CoreVtUuid{}},
            CoreValueType{CoreVtNominal{CoreTypeId{0}, {CoreValueTypeId{0}},
                                       std::numeric_limits<std::uint64_t>::max()}},
        };
        const auto result = compute_core_layouts(program);
        CHECK_FALSE(result.ok());
        CHECK(has_code(result, layout::kOverflow));
        REQUIRE(result.diagnostics.size() == 1);
        REQUIRE(result.diagnostics[0].source_range.has_value());
        CHECK(*result.diagnostics[0].source_range == (SourceRange{31, 47}));
    }

    SUBCASE("non-canonical target") {
        CoreProgram program;
        program.value_types = {CoreValueType{CoreVtBool{}}};
        TargetDataLayout target;
        target.pointer_align = 8;
        const auto result = compute_core_layouts(program, target);
        CHECK_FALSE(result.ok());
        CHECK(has_code(result, layout::kUnsupported));
    }
}

TEST_CASE("P4-D layout verifier rejects placeholders inline cycles and orphans") {
    CoreProgram program;
    program.value_types = {CoreValueType{CoreVtUnit{}}};

    SUBCASE("unfinished placeholder") {
        CoreLayoutTable table;
        table.layouts = {CoreLayout{}};
        table.value_layouts = {CoreLayoutId{0}};
        const auto diagnostics = verify_core_layout_table(program, table);
        CHECK(has_code(diagnostics, layout::kInvalid));
    }

    SUBCASE("inline cycle") {
        CoreLayoutTable table;
        CoreLayoutStruct first;
        first.field_offsets = {0};
        first.field_layouts = {CoreLayoutId{1}};
        CoreLayoutStruct second;
        second.field_offsets = {0};
        second.field_layouts = {CoreLayoutId{0}};
        table.layouts = {CoreLayout{0, 1, true, std::move(first)},
                         CoreLayout{0, 1, true, std::move(second)}};
        table.value_layouts = {CoreLayoutId{0}};
        const auto diagnostics = verify_core_layout_table(program, table);
        CHECK(has_code(diagnostics, layout::kInfiniteRecursion));
    }

    SUBCASE("orphan") {
        CoreLayoutTable table;
        table.layouts = {CoreLayout{0, 1, true, CoreLayoutStruct{}},
                         CoreLayout{4, 4, false,
                                    CoreLayoutScalar{CoreScalarRepr::I32}}};
        table.value_layouts = {CoreLayoutId{0}};
        const auto diagnostics = verify_core_layout_table(program, table);
        CHECK(has_code(diagnostics, layout::kInvalid));
    }

    SUBCASE("self-consistent but wrong logical root projection") {
        CoreLayoutTable table;
        table.layouts = {
            CoreLayout{8, 8, false, CoreLayoutScalar{CoreScalarRepr::I64}}};
        table.value_layouts = {CoreLayoutId{0}};
        const auto diagnostics = verify_core_layout_table(program, table);
        CHECK(has_code(diagnostics, layout::kInvalid));
    }
}

TEST_CASE("P4-D layout equivalence is cycle-safe and compares backing contracts") {
    CoreLayoutTable table;
    CoreLayoutStruct node_a;
    node_a.field_offsets = {0};
    node_a.field_layouts = {CoreLayoutId{1}};
    CoreLayoutContainer list_a;
    list_a.element = CoreLayoutId{0};
    list_a.capacity = 4;
    list_a.stride = 8;
    list_a.backing_size = 32;
    CoreLayoutStruct node_b;
    node_b.field_offsets = {0};
    node_b.field_layouts = {CoreLayoutId{3}};
    CoreLayoutContainer list_b = list_a;
    list_b.element = CoreLayoutId{2};
    table.layouts = {
        CoreLayout{8, 4, false, std::move(node_a)},
        CoreLayout{8, 4, false, std::move(list_a)},
        CoreLayout{8, 4, false, std::move(node_b)},
        CoreLayout{8, 4, false, std::move(list_b)},
    };
    table.value_layouts = {CoreLayoutId{0}, CoreLayoutId{2}};

    CHECK(layouts_equivalent(table, CoreLayoutId{0}, CoreLayoutId{2}));
    CHECK(value_layouts_equivalent(table, CoreValueTypeId{0}, CoreValueTypeId{1}));
    auto &changed = std::get<CoreLayoutContainer>(table.layouts[3].shape);
    changed.capacity = 5;
    changed.backing_size = 40;
    CHECK_FALSE(layouts_equivalent(table, CoreLayoutId{0}, CoreLayoutId{2}));
}

} // namespace
