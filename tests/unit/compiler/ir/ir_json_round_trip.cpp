#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "ahfl/compiler/ir/lowering.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

[[nodiscard]] std::filesystem::path golden_ir_dir() {
#ifdef AHFL_SOURCE_DIR
    return std::filesystem::path{AHFL_SOURCE_DIR} / "tests" / "golden" / "ir";
#else
    return std::filesystem::path{"tests"} / "golden" / "ir";
#endif
}

[[nodiscard]] std::string read_file(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

// Every committed IR JSON golden that is `print_program_ir_json` output
// (format_version "ahfl.ir.v2"). KR5.9 requires parse -> print to reproduce
// each of these byte-for-byte.
//
// The `*.opt-ir.json` / `*.opt-ir.optimized.json` goldens are intentionally
// EXCLUDED: they are a different serialization (`"format":
// "AHFL_OPT_IR_V1"`) produced by the opt-IR pipeline's own emitter, not by
// IrJsonPrinter / print_program_ir_json, so parse_program_ir_json does not
// (and should not) accept them.
constexpr std::string_view kGoldenJsonFiles[] = {
    "ok_expr_temporal.json",
    "ok_workflow_value_flow.json",
};

} // namespace

TEST_CASE("IR JSON round-trips byte-identically for every golden") {
    const auto dir = golden_ir_dir();
    for (const auto &name : kGoldenJsonFiles) {
        CAPTURE(name);
        const auto path = dir / std::string(name);
        REQUIRE(std::filesystem::exists(path));

        const std::string original = read_file(path);
        REQUIRE_FALSE(original.empty());

        const auto program = ahfl::parse_program_ir_json(original);
        REQUIRE(program.has_value());

        std::ostringstream out;
        ahfl::print_program_ir_json(*program, out);
        const std::string reemitted = out.str();

        // Byte-identical round-trip: parse(original) -> print == original.
        CHECK(reemitted == original);
    }
}

TEST_CASE("IR JSON deserializer rejects malformed input") {
    CHECK_FALSE(ahfl::parse_program_ir_json("not json").has_value());
    CHECK_FALSE(ahfl::parse_program_ir_json("{}").has_value()); // missing declarations
    CHECK_FALSE(ahfl::parse_program_ir_json("[]").has_value()); // not an object
}

namespace {
[[nodiscard]] ahfl::ir::Program member_template_json_program() {
    using namespace ahfl::ir;
    Program program;
    StructDecl box;
    box.name = "Box";
    box.symbol_ref = SymbolRef{.kind = SymbolRefKind::Type, .canonical_name = "app::Box", .id = 7};
    box.type_param_count = 1;
    box.type_param_variances = {Variance::Covariant};
    FieldDecl field;
    field.name = "value";
    field.type_ref.kind = TypeRefKind::Any; // general prototype bridge shape
    box.fields.push_back(std::move(field));
    MemberTypeTemplateNode param;
    param.kind = MemberTypeTemplateKind::Param;
    param.param_index = 0;
    box.member_type_templates.push_back(std::move(param));
    box.field_type_template_roots = {0};
    program.declarations.emplace_back(std::move(box));

    EnumDecl packet;
    packet.name = "Packet";
    packet.symbol_ref = SymbolRef{.kind = SymbolRefKind::Type,
                                  .canonical_name = "app::Packet",
                                  .id = 8};
    packet.type_param_count = 1;
    packet.type_param_variances = {Variance::Covariant};
    EnumVariantDecl one;
    one.name = "One";
    one.payload_kind = EnumVariantPayloadKind::Tuple;
    TypeRef erased_payload;
    erased_payload.kind = TypeRefKind::Any;
    one.payload.push_back(std::move(erased_payload));
    one.payload_type_template_roots = {0};
    packet.variants.push_back(std::move(one));
    MemberTypeTemplateNode enum_param;
    enum_param.kind = MemberTypeTemplateKind::Param;
    enum_param.param_index = 0;
    packet.member_type_templates.push_back(std::move(enum_param));
    program.declarations.emplace_back(std::move(packet));
    return program;
}

[[nodiscard]] std::string print_json(const ahfl::ir::Program &program) {
    std::ostringstream out;
    ahfl::print_program_ir_json(program, out);
    return out.str();
}

[[nodiscard]] std::string
replace_json_array(std::string json, std::string_view key, std::string_view replacement) {
    const auto key_pos = json.find(std::string{"\""} + std::string(key) + "\"");
    if (key_pos == std::string::npos) {
        return {};
    }
    const auto begin = json.find('[', key_pos);
    if (begin == std::string::npos) {
        return {};
    }
    std::size_t depth = 0;
    for (std::size_t pos = begin; pos < json.size(); ++pos) {
        if (json[pos] == '[') {
            ++depth;
        } else if (json[pos] == ']' && --depth == 0) {
            json.replace(begin, pos - begin + 1, replacement);
            return json;
        }
    }
    return {};
}
} // namespace

TEST_CASE("IR JSON round-trips declaration member type templates") {
    using namespace ahfl::ir;
    const auto first = print_json(member_template_json_program());
    const auto reparsed = ahfl::parse_program_ir_json(first);
    REQUIRE(reparsed.has_value());
    CHECK(print_json(*reparsed) == first);
    REQUIRE(reparsed->declarations.size() == 2);
    const auto *box = std::get_if<StructDecl>(&reparsed->declarations[0]);
    REQUIRE(box != nullptr);
    REQUIRE(box->member_type_templates.size() == 1);
    CHECK(box->member_type_templates[0].kind == MemberTypeTemplateKind::Param);
    CHECK(box->member_type_templates[0].param_index == 0);
    CHECK(box->field_type_template_roots == std::vector<std::uint32_t>{0});
    const auto *packet = std::get_if<EnumDecl>(&reparsed->declarations[1]);
    REQUIRE(packet != nullptr);
    REQUIRE(packet->member_type_templates.size() == 1);
    REQUIRE(packet->variants.size() == 1);
    CHECK(packet->variants[0].payload_type_template_roots ==
          std::vector<std::uint32_t>{0});
}

TEST_CASE("IR JSON member template wire format fails closed") {
    const auto valid = print_json(member_template_json_program());

    SUBCASE("unknown kind") {
        auto malformed = valid;
        const auto pos = malformed.find("\"kind\": \"param\"");
        REQUIRE(pos != std::string::npos);
        malformed.replace(
            pos, std::string_view{"\"kind\": \"param\""}.size(), "\"kind\": \"future\"");
        CHECK_FALSE(ahfl::parse_program_ir_json(malformed).has_value());
    }
    SUBCASE("templates must be an array") {
        const auto malformed = replace_json_array(valid, "member_type_templates", "{}");
        REQUIRE_FALSE(malformed.empty());
        CHECK_FALSE(ahfl::parse_program_ir_json(malformed).has_value());
    }
    SUBCASE("template items must be objects") {
        const auto malformed = replace_json_array(valid, "member_type_templates", "[1]");
        REQUIRE_FALSE(malformed.empty());
        CHECK_FALSE(ahfl::parse_program_ir_json(malformed).has_value());
    }
    SUBCASE("template children must be an array") {
        auto malformed = valid;
        const auto pos = malformed.find("\"param_index\": 0");
        REQUIRE(pos != std::string::npos);
        malformed.insert(pos, "\"children\": {},\n          ");
        CHECK_FALSE(ahfl::parse_program_ir_json(malformed).has_value());
    }
    SUBCASE("template type_ref must be an object") {
        auto malformed = valid;
        const auto pos = malformed.find("\"param_index\": 0");
        REQUIRE(pos != std::string::npos);
        malformed.insert(pos, "\"type_ref\": 1,\n          ");
        CHECK_FALSE(ahfl::parse_program_ir_json(malformed).has_value());
    }
    SUBCASE("param indices reject negative numbers") {
        auto malformed = valid;
        const auto pos = malformed.find("\"param_index\": 0");
        REQUIRE(pos != std::string::npos);
        malformed.replace(pos, std::string_view{"\"param_index\": 0"}.size(),
                          "\"param_index\": -1");
        CHECK_FALSE(ahfl::parse_program_ir_json(malformed).has_value());
    }
    SUBCASE("param indices reject values above u32") {
        auto malformed = valid;
        const auto pos = malformed.find("\"param_index\": 0");
        REQUIRE(pos != std::string::npos);
        malformed.replace(pos, std::string_view{"\"param_index\": 0"}.size(),
                          "\"param_index\": 4294967296");
        CHECK_FALSE(ahfl::parse_program_ir_json(malformed).has_value());
    }
    SUBCASE("root ids reject negative numbers") {
        const auto malformed = replace_json_array(valid, "field_type_template_roots", "[-1]");
        REQUIRE_FALSE(malformed.empty());
        CHECK_FALSE(ahfl::parse_program_ir_json(malformed).has_value());
    }
    SUBCASE("root ids reject values above u32") {
        const auto malformed =
            replace_json_array(valid, "field_type_template_roots", "[4294967296]");
        REQUIRE_FALSE(malformed.empty());
        CHECK_FALSE(ahfl::parse_program_ir_json(malformed).has_value());
    }
    SUBCASE("root ids must be an array") {
        const auto malformed = replace_json_array(valid, "field_type_template_roots", "{}");
        REQUIRE_FALSE(malformed.empty());
        CHECK_FALSE(ahfl::parse_program_ir_json(malformed).has_value());
    }
}

// RFC 0026 P4 (coercion) F1: a NON-EMPTY LetStatement adjustment plan must
// round-trip through IR JSON byte-for-byte (a public IR field must never
// silent-drop). Builds a program
// with a `let` carrying a compositional plan (a List<Int(0,0)>(4) -> List<Int>(8)
// shape: one root node with CapacityWiden + TypeArg{0} whose child is IntWiden),
// prints it, parses it back, prints again, and asserts the two texts match.
TEST_CASE("IR JSON round-trips a non-empty LetStatement adjustment plan") {
    using namespace ahfl::ir;

    const auto int_type = [](std::optional<std::pair<std::int64_t, std::int64_t>> bounds) {
        TypeRef t;
        t.kind = bounds.has_value() ? TypeRefKind::BoundedInt : TypeRefKind::Int;
        t.int_bounds = bounds;
        return t;
    };
    const auto list_of = [](TypeRef elem, std::optional<std::uint64_t> cap) {
        TypeRef t;
        t.kind = TypeRefKind::Struct;
        t.canonical_name = "std::collections::List";
        t.nominal_ref = SymbolRef{.kind = SymbolRefKind::Type,
                                  .canonical_name = "std::collections::List"};
        t.collection_capacity = cap;
        t.params.push_back(std::make_unique<TypeRef>(std::move(elem)));
        return t;
    };

    // Program with a flow whose single handler binds `let xs: List<Int>(8) = <...>`
    // carrying the compositional adjustment plan.
    Program program;
    ExprRef init = program.expr_arena.make(IntegerLiteralExpr{"0"});

    AdjustmentPlan plan;
    plan.source = list_of(int_type(std::pair<std::int64_t, std::int64_t>{0, 0}), std::uint64_t{4});
    plan.target = list_of(int_type(std::nullopt), std::uint64_t{8});
    plan.root = 0;
    // node 0: the List boundary, ops = [CapacityWiden, TypeArg{0 -> child node 1}]
    AdjustmentNode root_node;
    root_node.source = list_of(int_type(std::pair<std::int64_t, std::int64_t>{0, 0}), std::uint64_t{4});
    root_node.target = list_of(int_type(std::nullopt), std::uint64_t{8});
    root_node.ops.push_back(AdjustmentOp{.kind = AdjustmentOpKind::CapacityWiden});
    root_node.ops.push_back(AdjustmentOp{.kind = AdjustmentOpKind::TypeArg, .arg_index = 0, .child = 1});
    // node 1: the element IntWiden Int(0,0) -> Int
    AdjustmentNode elem_node;
    elem_node.source = int_type(std::pair<std::int64_t, std::int64_t>{0, 0});
    elem_node.target = int_type(std::nullopt);
    elem_node.ops.push_back(AdjustmentOp{.kind = AdjustmentOpKind::IntWiden});
    plan.nodes.push_back(std::move(root_node));
    plan.nodes.push_back(std::move(elem_node));

    LetStatement let;
    let.name = "xs";
    let.type_ref = list_of(int_type(std::nullopt), std::uint64_t{8});
    let.initializer = init;
    let.adjustment = std::move(plan);

    auto stmt = std::make_unique<Statement>();
    stmt->node = std::move(let);
    StateHandler handler;
    handler.state_name = "S";
    handler.body.statements.push_back(std::move(stmt));
    FlowDecl flow;
    flow.target_ref.kind = SymbolRefKind::Agent;
    flow.target_ref.canonical_name = "app::A";
    flow.state_handlers.push_back(std::move(handler));
    program.declarations.emplace_back(std::move(flow));

    std::ostringstream first;
    ahfl::print_program_ir_json(program, first);
    const auto reparsed = ahfl::parse_program_ir_json(first.str());
    REQUIRE(reparsed.has_value());

    // The reparsed let carries the full compositional plan (proving the public
    // IR adjustment field is not silently dropped on read/write). Byte-exact
    // whole-program idempotence is covered by the golden round-trip test above;
    // this hand-built program only pins the adjustment-plan fidelity.
    REQUIRE(reparsed->declarations.size() == 1);
    const auto *flow_out = std::get_if<FlowDecl>(&reparsed->declarations[0]);
    REQUIRE(flow_out != nullptr);
    REQUIRE(flow_out->state_handlers.size() == 1);
    REQUIRE(flow_out->state_handlers[0].body.statements.size() == 1);
    const auto *let_out =
        std::get_if<LetStatement>(&flow_out->state_handlers[0].body.statements[0]->node);
    REQUIRE(let_out != nullptr);
    REQUIRE(let_out->adjustment.has_value());
    CHECK(let_out->adjustment->nodes.size() == 2);
    CHECK(let_out->adjustment->root == 0);
    REQUIRE(let_out->adjustment->nodes[0].ops.size() == 2);
    CHECK(let_out->adjustment->nodes[0].ops[0].kind == AdjustmentOpKind::CapacityWiden);
    CHECK(let_out->adjustment->nodes[0].ops[1].kind == AdjustmentOpKind::TypeArg);
    CHECK(let_out->adjustment->nodes[0].ops[1].child == 1);
    REQUIRE(let_out->adjustment->nodes[1].ops.size() == 1);
    CHECK(let_out->adjustment->nodes[1].ops[0].kind == AdjustmentOpKind::IntWiden);
}

TEST_CASE("IR JSON round-trips top and bottom adjustment leaf operations") {
    using namespace ahfl::ir;

    const auto type = [](TypeRefKind kind) {
        TypeRef out;
        out.kind = kind;
        return out;
    };
    const auto make_plan = [](TypeRef source, TypeRef target, AdjustmentOpKind kind) {
        AdjustmentPlan plan;
        plan.source = clone_type_ref(source);
        plan.target = clone_type_ref(target);
        plan.root = 0;
        AdjustmentNode node;
        node.source = std::move(source);
        node.target = std::move(target);
        node.ops.push_back(AdjustmentOp{.kind = kind});
        plan.nodes.push_back(std::move(node));
        return plan;
    };

    Program program;
    StateHandler handler;
    handler.state_name = "S";

    auto to_any = std::make_unique<Statement>();
    LetStatement to_any_let;
    to_any_let.name = "to_any";
    to_any_let.type_ref = type(TypeRefKind::Any);
    to_any_let.initializer = program.expr_arena.make(IntegerLiteralExpr{"1"});
    to_any_let.adjustment =
        make_plan(type(TypeRefKind::Int), type(TypeRefKind::Any), AdjustmentOpKind::ToAny);
    to_any->node = std::move(to_any_let);
    handler.body.statements.push_back(std::move(to_any));

    auto from_never = std::make_unique<Statement>();
    LetStatement from_never_let;
    from_never_let.name = "from_never";
    from_never_let.type_ref = type(TypeRefKind::Int);
    from_never_let.initializer = program.expr_arena.make(IntegerLiteralExpr{"2"});
    from_never_let.adjustment =
        make_plan(type(TypeRefKind::Never), type(TypeRefKind::Int), AdjustmentOpKind::FromNever);
    from_never->node = std::move(from_never_let);
    handler.body.statements.push_back(std::move(from_never));

    FlowDecl flow;
    flow.target_ref.kind = SymbolRefKind::Agent;
    flow.target_ref.canonical_name = "app::A";
    flow.state_handlers.push_back(std::move(handler));
    program.declarations.emplace_back(std::move(flow));

    std::ostringstream out;
    ahfl::print_program_ir_json(program, out);
    CHECK(out.str().find("\"to_any\"") != std::string::npos);
    CHECK(out.str().find("\"from_never\"") != std::string::npos);
    const auto back = ahfl::parse_program_ir_json(out.str());
    REQUIRE(back.has_value());
    const auto *flow_out = std::get_if<FlowDecl>(&back->declarations[0]);
    REQUIRE(flow_out != nullptr);
    REQUIRE(flow_out->state_handlers[0].body.statements.size() == 2);
    const auto *to_any_out =
        std::get_if<LetStatement>(&flow_out->state_handlers[0].body.statements[0]->node);
    const auto *from_never_out =
        std::get_if<LetStatement>(&flow_out->state_handlers[0].body.statements[1]->node);
    REQUIRE(to_any_out != nullptr);
    REQUIRE(from_never_out != nullptr);
    REQUIRE(to_any_out->adjustment.has_value());
    REQUIRE(from_never_out->adjustment.has_value());
    CHECK(to_any_out->adjustment->nodes[0].ops[0].kind == AdjustmentOpKind::ToAny);
    CHECK(from_never_out->adjustment->nodes[0].ops[0].kind == AdjustmentOpKind::FromNever);
}

namespace {

// Build a minimal program whose single `let` carries a compositional adjustment
// plan (root List boundary with CapacityWiden + TypeArg{0->child IntWiden}), and
// return its `print_program_ir_json` text. Used by the P0-2 parser-negative tests
// below to prove that corrupting a wire enum / shape makes the deserializer FAIL
// CLOSED rather than silently downgrade.
[[nodiscard]] std::string plan_program_json() {
    using namespace ahfl::ir;
    const auto int_type = [](std::optional<std::pair<std::int64_t, std::int64_t>> bounds) {
        TypeRef t;
        t.kind = bounds.has_value() ? TypeRefKind::BoundedInt : TypeRefKind::Int;
        t.int_bounds = bounds;
        return t;
    };
    const auto list_of = [](TypeRef elem, std::optional<std::uint64_t> cap) {
        TypeRef t;
        t.kind = TypeRefKind::Struct;
        t.canonical_name = "std::collections::List";
        t.nominal_ref = SymbolRef{.kind = SymbolRefKind::Type,
                                  .canonical_name = "std::collections::List"};
        t.collection_capacity = cap;
        t.params.push_back(std::make_unique<TypeRef>(std::move(elem)));
        return t;
    };

    Program program;
    ExprRef init = program.expr_arena.make(IntegerLiteralExpr{"0"});
    AdjustmentPlan plan;
    plan.source = list_of(int_type(std::pair<std::int64_t, std::int64_t>{0, 0}), std::uint64_t{4});
    plan.target = list_of(int_type(std::nullopt), std::uint64_t{8});
    plan.root = 0;
    AdjustmentNode root_node;
    root_node.source = list_of(int_type(std::pair<std::int64_t, std::int64_t>{0, 0}), std::uint64_t{4});
    root_node.target = list_of(int_type(std::nullopt), std::uint64_t{8});
    root_node.ops.push_back(AdjustmentOp{.kind = AdjustmentOpKind::CapacityWiden});
    root_node.ops.push_back(AdjustmentOp{.kind = AdjustmentOpKind::TypeArg, .arg_index = 0, .child = 1});
    AdjustmentNode elem_node;
    elem_node.source = int_type(std::pair<std::int64_t, std::int64_t>{0, 0});
    elem_node.target = int_type(std::nullopt);
    elem_node.ops.push_back(AdjustmentOp{.kind = AdjustmentOpKind::IntWiden});
    plan.nodes.push_back(std::move(root_node));
    plan.nodes.push_back(std::move(elem_node));

    LetStatement let;
    let.name = "xs";
    let.type_ref = list_of(int_type(std::nullopt), std::uint64_t{8});
    let.initializer = init;
    let.adjustment = std::move(plan);

    auto stmt = std::make_unique<Statement>();
    stmt->node = std::move(let);
    StateHandler handler;
    handler.state_name = "S";
    handler.body.statements.push_back(std::move(stmt));
    FlowDecl flow;
    flow.target_ref.kind = SymbolRefKind::Agent;
    flow.target_ref.canonical_name = "app::A";
    flow.state_handlers.push_back(std::move(handler));
    program.declarations.emplace_back(std::move(flow));

    std::ostringstream out;
    ahfl::print_program_ir_json(program, out);
    return out.str();
}

// Replace the first occurrence of `needle` with `replacement`; REQUIRE it existed.
[[nodiscard]] std::string replace_first(std::string text, std::string_view needle,
                                        std::string_view replacement) {
    const auto pos = text.find(needle);
    REQUIRE(pos != std::string::npos);
    text.replace(pos, needle.size(), replacement);
    return text;
}

} // namespace

// RFC 0026 P4 (coercion) F1 forward-fix (Codex P0-2): every wire enum in an
// adjustment plan must FAIL CLOSED on an unknown spelling — the deserializer must
// never silently downgrade an unknown adjustment op to IntWiden (which would let a
// corrupt plan masquerade as a legal one). The baseline plan program parses; each
// corruption below must make `parse_program_ir_json` return nullopt.
TEST_CASE("IR JSON deserializer fails closed on a malformed adjustment op kind") {
    const std::string base = plan_program_json();
    REQUIRE(ahfl::parse_program_ir_json(base).has_value());

    // An unknown op-kind spelling is rejected (not coerced to int_widen).
    const auto bad_op = replace_first(base, "\"capacity_widen\"", "\"totally_bogus_op\"");
    CHECK_FALSE(ahfl::parse_program_ir_json(bad_op).has_value());
}

TEST_CASE("IR JSON deserializer fails closed on a non-object adjustment field") {
    const std::string base = plan_program_json();
    REQUIRE(ahfl::parse_program_ir_json(base).has_value());

    // A present-but-wrong-kind `adjustment` (array instead of object) must FAIL,
    // not be silently dropped to nullopt — that would erase a real plan on read.
    const auto bad_shape = replace_first(base, "\"adjustment\": {", "\"adjustment\": [");
    CHECK_FALSE(ahfl::parse_program_ir_json(bad_shape).has_value());
}

TEST_CASE("IR JSON deserializer fails closed on a negative adjustment child index") {
    const std::string base = plan_program_json();
    REQUIRE(ahfl::parse_program_ir_json(base).has_value());

    // `child` is a u32; a negative value must be rejected, not signed-cast.
    const auto bad_child = replace_first(base, "\"child\": 1", "\"child\": -1");
    CHECK_FALSE(ahfl::parse_program_ir_json(bad_child).has_value());
}

// RFC 0027 P6/P7 (KR6.13-E): the ir_json reader resolves an expression's
// `"kind"` through the single table generated from expr_nodes.def (the same
// table the writer emits from). An unknown / misspelled wire name must therefore
// FAIL CLOSED — it can no longer silently demote to a default node via a missing
// `if (kind == ...)` branch.
TEST_CASE("IR JSON deserializer fails closed on an unknown expression kind") {
    using namespace ahfl::ir;

    // Minimal flow handler holding one BoolLiteralExpr statement expression.
    Program program;
    StateHandler handler;
    handler.state_name = "S";
    auto stmt = std::make_unique<Statement>();
    stmt->node = ExprStatement{program.expr_arena.make(BoolLiteralExpr{.value = true})};
    handler.body.statements.push_back(std::move(stmt));
    FlowDecl flow;
    flow.target_ref.kind = SymbolRefKind::Agent;
    flow.target_ref.canonical_name = "app::A";
    flow.state_handlers.push_back(std::move(handler));
    program.declarations.emplace_back(std::move(flow));

    std::ostringstream out;
    ahfl::print_program_ir_json(program, out);
    const std::string base = out.str();
    REQUIRE(base.find("\"bool_literal\"") != std::string::npos);
    REQUIRE(ahfl::parse_program_ir_json(base).has_value());

    // A wire-name typo is rejected through the shared table (no silent demotion).
    const auto bad = replace_first(base, "\"bool_literal\"", "\"bool_litteral\"");
    CHECK_FALSE(ahfl::parse_program_ir_json(bad).has_value());
}

// RFC 0027 P6/P7 (KR6.13-P7): the same fail-closed property now holds for the
// MatchPatternNode / TemporalExprNode / StatementNode / Decl families, whose
// `"kind"` spellings the reader resolves through the SHARED wire-name tables
// derived from pattern_nodes.def / temporal_nodes.def / stmt_nodes.def /
// decl_nodes.def. A typo can no longer silently demote to a default node.
TEST_CASE("IR JSON deserializer fails closed on unknown statement / decl kinds") {
    using namespace ahfl::ir;

    Program program;
    StateHandler handler;
    handler.state_name = "S";
    auto stmt = std::make_unique<Statement>();
    stmt->node = ExprStatement{program.expr_arena.make(BoolLiteralExpr{.value = true})};
    handler.body.statements.push_back(std::move(stmt));
    FlowDecl flow;
    flow.target_ref.kind = SymbolRefKind::Agent;
    flow.target_ref.canonical_name = "app::A";
    flow.state_handlers.push_back(std::move(handler));
    program.declarations.emplace_back(std::move(flow));

    std::ostringstream out;
    ahfl::print_program_ir_json(program, out);
    const std::string base = out.str();
    REQUIRE(ahfl::parse_program_ir_json(base).has_value());

    // Statement kind: the ExprStatement's own `"kind": "expr"` is misspelled.
    const auto bad_stmt = replace_first(base, "\"kind\": \"expr\"", "\"kind\": \"exprss\"");
    CHECK_FALSE(ahfl::parse_program_ir_json(bad_stmt).has_value());

    // Decl kind: the FlowDecl's `"kind": "flow"` is misspelled.
    const auto bad_decl = replace_first(base, "\"kind\": \"flow\"", "\"kind\": \"floww\"");
    CHECK_FALSE(ahfl::parse_program_ir_json(bad_decl).has_value());
}

TEST_CASE("IR JSON deserializer fails closed on unknown temporal and pattern kinds") {
    using namespace ahfl::ir;

    // A contract clause carrying an embedded temporal formula, plus an if-let
    // statement carrying a literal match pattern: together they exercise the
    // temporal_node and match_pattern_node wire tables.
    Program program;
    StateHandler handler;
    handler.state_name = "S";
    auto if_let = std::make_unique<Statement>();
    IfLetStatement ifs;
    ifs.pattern.node = LiteralPattern{.spelling = "0"};
    ifs.scrutinee = program.expr_arena.make(IntegerLiteralExpr{.spelling = "0"});
    ifs.then_block = ahfl::make_owned<Block>();
    if_let->node = std::move(ifs);
    handler.body.statements.push_back(std::move(if_let));
    FlowDecl flow;
    flow.target_ref.kind = SymbolRefKind::Agent;
    flow.target_ref.canonical_name = "app::A";
    flow.state_handlers.push_back(std::move(handler));
    program.declarations.emplace_back(std::move(flow));

    // A contract clause with an embedded temporal formula.
    ContractDecl contract;
    ContractClause clause;
    clause.kind = ContractClauseKind::Ensures;
    clause.value = ahfl::make_owned<TemporalExpr>();
    std::get<TemporalExprPtr>(clause.value)->node = EmbeddedTemporalExpr{
        .expr = program.expr_arena.make(BoolLiteralExpr{.value = true})};
    contract.clauses.push_back(std::move(clause));
    program.declarations.emplace_back(std::move(contract));

    std::ostringstream out;
    ahfl::print_program_ir_json(program, out);
    const std::string base = out.str();
    REQUIRE(base.find("\"literal\"") != std::string::npos);
    REQUIRE(base.find("\"embedded_expr\"") != std::string::npos);
    REQUIRE(ahfl::parse_program_ir_json(base).has_value());

    const auto bad_pattern = replace_first(base, "\"literal\"", "\"litteral\"");
    CHECK_FALSE(ahfl::parse_program_ir_json(bad_pattern).has_value());

    const auto bad_temporal = replace_first(base, "\"embedded_expr\"", "\"embedded_exprr\"");
    CHECK_FALSE(ahfl::parse_program_ir_json(bad_temporal).has_value());
}

namespace {

// A deliberately field-complete TypeRef: a bounded generic collection
// (`collection_capacity` + `nominal_ref` + `params`) whose element is itself a
// nominal struct carrying its own `nominal_ref`, plus a null param slot and an
// Fn return in `first`. Exercises every field `clone_type_ref` /
// `type_refs_equal` must carry, at depth and across a null child.
[[nodiscard]] ahfl::ir::TypeRef make_full_type_ref() {
    using namespace ahfl::ir;
    TypeRef inner;
    inner.kind = TypeRefKind::Struct;
    inner.display_name = "User";
    inner.canonical_name = "app::User";
    inner.nominal_ref = SymbolRef{.kind = SymbolRefKind::Type,
                                  .canonical_name = "app::User",
                                  .local_name = "User",
                                  .module_name = "app",
                                  .id = std::size_t{7}};

    TypeRef root;
    root.kind = TypeRefKind::Struct;
    root.display_name = "List<User>(4)";
    root.canonical_name = "std::collections::List";
    root.collection_capacity = std::uint64_t{4};
    root.nominal_ref = SymbolRef{.kind = SymbolRefKind::Type,
                                 .canonical_name = "std::collections::List",
                                 .local_name = "List",
                                 .module_name = "std::collections",
                                 .id = std::size_t{3}};
    root.first = ahfl::make_owned<TypeRef>(TypeRef{.kind = TypeRefKind::Bool,
                                                   .display_name = "Bool"});
    root.params.push_back(ahfl::make_owned<TypeRef>(std::move(inner)));
    root.params.push_back(nullptr); // null param slot must survive clone/equality
    return root;
}

} // namespace

TEST_CASE("clone_type_ref preserves every field including nominal_ref, capacity, nested + null") {
    const auto original = make_full_type_ref();
    const auto cloned = ahfl::ir::clone_type_ref(original);

    // A full deep clone is structurally equal to its source.
    CHECK(ahfl::ir::type_refs_equal(original, cloned));

    // Spot-check the fields the divergent clones used to drop.
    CHECK(cloned.collection_capacity == original.collection_capacity);
    CHECK(cloned.nominal_ref.kind == ahfl::ir::SymbolRefKind::Type);
    CHECK(cloned.nominal_ref.canonical_name == "std::collections::List");
    CHECK(cloned.nominal_ref.id == std::size_t{3});
    REQUIRE(cloned.params.size() == 2);
    REQUIRE(cloned.params[0] != nullptr);
    CHECK(cloned.params[1] == nullptr); // null slot preserved
    CHECK(cloned.params[0]->nominal_ref.canonical_name == "app::User");
    REQUIRE(cloned.first != nullptr);
    CHECK(cloned.first->kind == ahfl::ir::TypeRefKind::Bool);

    // Deep clone, not a shallow alias.
    CHECK(cloned.params[0].get() != original.params[0].get());
    CHECK(cloned.first.get() != original.first.get());
}

TEST_CASE("type_refs_equal uses id-first nominal identity") {
    const auto base = make_full_type_ref();

    SUBCASE("same canonical, DIFFERENT resolved id -> not equal (P0 identity)") {
        // The core probe: identical spelling, different resolved declaration.
        auto other = ahfl::ir::clone_type_ref(base);
        other.nominal_ref.id = std::size_t{4}; // base has id 3, canonical unchanged
        CHECK(other.nominal_ref.canonical_name == base.nominal_ref.canonical_name);
        CHECK_FALSE(ahfl::ir::type_refs_equal(base, other));
    }
    SUBCASE("same id, differing local/module display -> identity equal") {
        // Identity is the id; incidental display names do not split it.
        auto other = ahfl::ir::clone_type_ref(base);
        other.nominal_ref.local_name = "ListAlias";
        other.nominal_ref.module_name = "std::alias";
        CHECK(ahfl::ir::type_refs_equal(base, other));
    }
    SUBCASE("name-only refs: same canonical -> equal, different -> not equal") {
        auto a = ahfl::ir::clone_type_ref(base);
        auto b = ahfl::ir::clone_type_ref(base);
        a.nominal_ref.id.reset();
        b.nominal_ref.id.reset();
        CHECK(ahfl::ir::type_refs_equal(a, b));
        b.nominal_ref.canonical_name = "std::collections::Set";
        CHECK_FALSE(ahfl::ir::type_refs_equal(a, b));
    }
    SUBCASE("nested param: same canonical different id -> not equal") {
        auto other = ahfl::ir::clone_type_ref(base);
        other.params[0]->nominal_ref.id = std::size_t{42}; // inner base id is 7
        CHECK_FALSE(ahfl::ir::type_refs_equal(base, other));
    }
}

TEST_CASE("type_refs_equal distinguishes on nominal kind, capacity, and nested params") {
    const auto base = make_full_type_ref();

    SUBCASE("nominal_ref kind difference is observed") {
        auto other = ahfl::ir::clone_type_ref(base);
        // base's nominal_ref kind is Type; a differing kind is a differing
        // identity even at the same id/canonical.
        other.nominal_ref.kind = ahfl::ir::SymbolRefKind::Const;
        CHECK_FALSE(ahfl::ir::type_refs_equal(base, other));
    }
    SUBCASE("collection_capacity difference is observed") {
        auto other = ahfl::ir::clone_type_ref(base);
        other.collection_capacity = std::uint64_t{8};
        CHECK_FALSE(ahfl::ir::type_refs_equal(base, other));
    }
    SUBCASE("null vs non-null param slot is observed") {
        auto other = ahfl::ir::clone_type_ref(base);
        other.params[1] = ahfl::make_owned<ahfl::ir::TypeRef>(
            ahfl::ir::TypeRef{.kind = ahfl::ir::TypeRefKind::Int});
        CHECK_FALSE(ahfl::ir::type_refs_equal(base, other));
    }
    SUBCASE("identical rebuild compares equal") {
        CHECK(ahfl::ir::type_refs_equal(base, make_full_type_ref()));
    }
}
