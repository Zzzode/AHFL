#include "compiler/ir/opt/opt_lower.hpp"

#include <cassert>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace ahfl::ir::opt {

namespace {

struct RootTypeHints {
    bool has_input{false};
    bool has_context{false};
    bool has_output{false};
    TypeRef input;
    TypeRef context;
    TypeRef output;
    std::vector<std::pair<std::string, TypeRef>> extra_arguments;
};

[[nodiscard]] TypeRef clone_type_ref(const TypeRef &type) {
    TypeRef clone;
    clone.kind = type.kind;
    clone.display_name = type.display_name;
    clone.canonical_name = type.canonical_name;
    clone.variant_name = type.variant_name;
    clone.string_bounds = type.string_bounds;
    clone.decimal_scale = type.decimal_scale;
    clone.source_range = type.source_range;
    if (type.first) {
        clone.first = make_owned<TypeRef>(clone_type_ref(*type.first));
    }
    if (type.second) {
        clone.second = make_owned<TypeRef>(clone_type_ref(*type.second));
    }
    return clone;
}

[[nodiscard]] RootTypeHints clone_root_type_hints(const RootTypeHints &hints) {
    RootTypeHints clone;
    clone.has_input = hints.has_input;
    clone.has_context = hints.has_context;
    clone.has_output = hints.has_output;
    if (hints.has_input) {
        clone.input = clone_type_ref(hints.input);
    }
    if (hints.has_context) {
        clone.context = clone_type_ref(hints.context);
    }
    if (hints.has_output) {
        clone.output = clone_type_ref(hints.output);
    }
    clone.extra_arguments.reserve(hints.extra_arguments.size());
    for (const auto &argument : hints.extra_arguments) {
        clone.extra_arguments.emplace_back(argument.first, clone_type_ref(argument.second));
    }
    return clone;
}

[[nodiscard]] bool has_resolved_type(const TypeRef &type) noexcept {
    return type.kind != TypeRefKind::Unresolved;
}

[[nodiscard]] TypeRef type_or_unresolved(const ir::Expr *expr) {
    if (expr == nullptr) {
        return {};
    }
    return clone_type_ref(expr->resolved_type);
}

[[nodiscard]] TypeRef string_type_ref() {
    TypeRef type;
    type.kind = TypeRefKind::String;
    type.display_name = "String";
    return type;
}

/// Context for lowering a single StateHandler into an OptFunction.
class LoweringContext {
  public:
    explicit LoweringContext(std::string function_name,
                             SourceRangeOpt function_source_range = std::nullopt,
                             RootTypeHints root_type_hints = {})
        : function_name_(std::move(function_name)),
          function_source_range_(std::move(function_source_range)),
          root_type_hints_(std::move(root_type_hints)) {
        // Create the entry block.
        new_block("entry");
        seed_root_argument_locals();
    }

    [[nodiscard]] OptFunction finish(TypeRef return_type = {}) {
        OptFunction func;
        func.name = std::move(function_name_);
        func.locals = std::move(locals_);
        func.arg_count = arg_count_;
        func.blocks = std::move(blocks_);
        func.return_type = std::move(return_type);
        func.source_range = std::move(function_source_range_);
        return func;
    }

    // ---- Block management ----

    std::uint32_t new_block(std::string label) {
        auto id = static_cast<std::uint32_t>(blocks_.size());
        BasicBlock bb;
        bb.id = id;
        bb.label = std::move(label);
        bb.terminator.kind = Terminator::Kind::Unreachable;
        blocks_.push_back(std::move(bb));
        return id;
    }

    void set_current_block(std::uint32_t id) {
        current_block_ = id;
    }

    [[nodiscard]] std::uint32_t current_block_id() const {
        return current_block_;
    }

    BasicBlock &current_block() {
        assert(current_block_ < blocks_.size());
        return blocks_[current_block_];
    }

    // ---- Local management ----

    LocalRef new_local(std::string name,
                       TypeRef type = {},
                       bool is_temp = false,
                       SourceRangeOpt source_range = std::nullopt) {
        auto id = static_cast<std::uint32_t>(locals_.size());
        Local local;
        local.id = id;
        local.name = std::move(name);
        local.type = std::move(type);
        local.is_temp = is_temp;
        local.source_range = std::move(source_range);
        locals_.push_back(std::move(local));
        return id;
    }

    LocalRef new_temp(TypeRef type = {}, SourceRangeOpt source_range = std::nullopt) {
        auto name = "_tmp" + std::to_string(temp_counter_++);
        return new_local(
            std::move(name), std::move(type), /*is_temp=*/true, std::move(source_range));
    }

    void seed_root_argument_locals() {
        if (root_type_hints_.has_input) {
            new_argument_local("input", clone_type_ref(root_type_hints_.input));
        }
        if (root_type_hints_.has_context) {
            new_argument_local("ctx", clone_type_ref(root_type_hints_.context));
        }
        if (root_type_hints_.has_output) {
            new_argument_local("output", clone_type_ref(root_type_hints_.output));
        }
        for (const auto &[name, type] : root_type_hints_.extra_arguments) {
            if (find_local(name) == kNoLocal) {
                new_argument_local(name, clone_type_ref(type));
            }
        }
    }

    void new_argument_local(std::string name, TypeRef type) {
        static_cast<void>(new_local(std::move(name), std::move(type)));
        ++arg_count_;
    }

    /// Look up a local by name. Returns kNoLocal if not found.
    [[nodiscard]] LocalRef find_local(const std::string &name) const {
        for (std::uint32_t i = 0; i < locals_.size(); ++i) {
            if (locals_[i].name == name) {
                return i;
            }
        }
        return kNoLocal;
    }

    void refine_local(LocalRef ref,
                      const TypeRef &type,
                      const SourceRangeOpt &source_range = std::nullopt) {
        if (ref == kNoLocal || ref >= locals_.size()) {
            return;
        }
        auto &local = locals_[ref];
        if (!has_resolved_type(local.type) && has_resolved_type(type)) {
            local.type = clone_type_ref(type);
        }
        if (!local.source_range.has_value() && source_range.has_value()) {
            local.source_range = source_range;
        }
    }

    [[nodiscard]] TypeRef root_type_hint(const ir::Path &path) const {
        if (path.root_kind == PathRootKind::Input && root_type_hints_.has_input) {
            return clone_type_ref(root_type_hints_.input);
        }
        if (path.root_kind == PathRootKind::Output && root_type_hints_.has_output) {
            return clone_type_ref(root_type_hints_.output);
        }
        if (path.root_kind == PathRootKind::Context && root_type_hints_.has_context) {
            return clone_type_ref(root_type_hints_.context);
        }
        return {};
    }

    // ---- Statement emission ----

    void emit_assign(LocalRef dest, Rvalue rvalue, SourceRangeOpt source_range = std::nullopt) {
        refine_local(dest, rvalue.result_type, source_range);
        Statement stmt;
        stmt.kind = Statement::Kind::Assign;
        stmt.dest = dest;
        stmt.rvalue = std::move(rvalue);
        stmt.source_range = std::move(source_range);
        current_block().statements.push_back(std::move(stmt));
    }

    void emit_nop(SourceRangeOpt source_range = std::nullopt) {
        Statement stmt;
        stmt.kind = Statement::Kind::Nop;
        stmt.source_range = std::move(source_range);
        current_block().statements.push_back(std::move(stmt));
    }

    void set_terminator(Terminator term) {
        current_block().terminator = std::move(term);
    }

    // ---- Expression lowering ----

    /// Lower an expression to an operand. Complex expressions produce
    /// intermediate temporaries.
    [[nodiscard]] Operand lower_expr(const ir::Expr *expr) {
        if (expr == nullptr) {
            return make_constant(std::monostate{});
        }
        return std::visit([this, expr](const auto &node) { return lower_expr_node(node, *expr); },
                          expr->node);
    }

  private:
    // ---- Expression node visitors ----

    [[nodiscard]] Operand lower_expr_node(const ir::NoneLiteralExpr & /*e*/,
                                          const ir::Expr & /*expr*/) {
        return make_constant(std::monostate{});
    }

    [[nodiscard]] Operand lower_expr_node(const ir::BoolLiteralExpr &e, const ir::Expr & /*expr*/) {
        return make_constant(e.value);
    }

    [[nodiscard]] Operand lower_expr_node(const ir::IntegerLiteralExpr &e,
                                          const ir::Expr & /*expr*/) {
        // Try to parse to int64, fall back to string constant.
        try {
            auto val = std::stoll(e.spelling);
            return make_constant(val);
        } catch (...) {
            return make_constant(e.spelling);
        }
    }

    [[nodiscard]] Operand lower_expr_node(const ir::FloatLiteralExpr &e,
                                          const ir::Expr & /*expr*/) {
        try {
            auto val = std::stod(e.spelling);
            return make_constant(val);
        } catch (...) {
            return make_constant(e.spelling);
        }
    }

    [[nodiscard]] Operand lower_expr_node(const ir::DecimalLiteralExpr &e,
                                          const ir::Expr & /*expr*/) {
        return make_constant(e.spelling);
    }

    [[nodiscard]] Operand lower_expr_node(const ir::StringLiteralExpr &e,
                                          const ir::Expr & /*expr*/) {
        return make_constant(e.spelling);
    }

    [[nodiscard]] Operand lower_expr_node(const ir::DurationLiteralExpr &e,
                                          const ir::Expr & /*expr*/) {
        return make_constant(e.spelling);
    }

    [[nodiscard]] Operand lower_expr_node(const ir::SomeExpr &e, const ir::Expr &expr) {
        // Lower inner value and wrap in a temp.
        auto inner = lower_expr(e.value.get());
        auto tmp = new_temp(clone_type_ref(expr.resolved_type), expr.source_range);
        Rvalue rv;
        rv.kind = Rvalue::Kind::Use;
        rv.result_type = clone_type_ref(expr.resolved_type);
        rv.operands.push_back(inner);
        emit_assign(tmp, std::move(rv), expr.source_range);
        return make_local(tmp);
    }

    [[nodiscard]] Operand lower_expr_node(const ir::PathExpr &e, const ir::Expr &expr) {
        // Try to resolve the path to a local variable.
        auto ref = find_local(e.path.root_name);
        if (ref == kNoLocal) {
            auto root_type = e.path.members.empty() ? clone_type_ref(expr.resolved_type)
                                                    : root_type_hint(e.path);
            // Create a placeholder local for unresolved paths.
            ref = new_local(e.path.root_name,
                            std::move(root_type),
                            /*is_temp=*/false,
                            e.path.members.empty() ? expr.source_range : std::nullopt);
        } else if (e.path.members.empty()) {
            refine_local(ref, expr.resolved_type, expr.source_range);
        }

        // If there are member accesses, chain Field rvalues.
        LocalRef current = ref;
        for (std::size_t index = 0; index < e.path.members.size(); ++index) {
            const auto &member = e.path.members[index];
            const bool is_final_member = index + 1 == e.path.members.size();
            auto member_type = is_final_member ? clone_type_ref(expr.resolved_type) : TypeRef{};
            auto tmp = new_temp(clone_type_ref(member_type),
                                is_final_member ? expr.source_range : std::nullopt);
            Rvalue rv;
            rv.kind = Rvalue::Kind::Field;
            rv.field_name = member;
            rv.result_type = std::move(member_type);
            rv.operands.push_back(make_local(current));
            emit_assign(tmp, std::move(rv), is_final_member ? expr.source_range : std::nullopt);
            current = tmp;
        }
        return make_local(current);
    }

    [[nodiscard]] Operand lower_expr_node(const ir::QualifiedValueExpr &e,
                                          const ir::Expr & /*expr*/) {
        return make_constant(e.value);
    }

    [[nodiscard]] Operand lower_expr_node(const ir::CallExpr &e, const ir::Expr &expr) {
        // Lower arguments.
        std::vector<Operand> args;
        args.reserve(e.arguments.size());
        for (const auto &arg : e.arguments) {
            args.push_back(lower_expr(arg.get()));
        }

        // Emit as a Call rvalue (not a Call terminator for simplicity in this
        // initial lowering — Call terminators are used for control flow splits).
        auto dest = new_temp(clone_type_ref(expr.resolved_type), expr.source_range);
        Rvalue rv;
        rv.kind = Rvalue::Kind::Call;
        rv.callee = e.callee;
        rv.operands = std::move(args);
        rv.result_type = clone_type_ref(expr.resolved_type);
        emit_assign(dest, std::move(rv), expr.source_range);
        return make_local(dest);
    }

    [[nodiscard]] Operand lower_expr_node(const ir::StructLiteralExpr &e, const ir::Expr &expr) {
        std::vector<Operand> field_values;
        field_values.reserve(e.fields.size());
        for (const auto &field : e.fields) {
            field_values.push_back(lower_expr(field.value.get()));
        }
        auto dest = new_temp(clone_type_ref(expr.resolved_type), expr.source_range);
        Rvalue rv;
        rv.kind = Rvalue::Kind::Aggregate;
        rv.operands = std::move(field_values);
        rv.result_type = clone_type_ref(expr.resolved_type);
        emit_assign(dest, std::move(rv), expr.source_range);
        return make_local(dest);
    }

    [[nodiscard]] Operand lower_expr_node(const ir::ListLiteralExpr &e, const ir::Expr &expr) {
        std::vector<Operand> items;
        items.reserve(e.items.size());
        for (const auto &item : e.items) {
            items.push_back(lower_expr(item.get()));
        }
        auto dest = new_temp(clone_type_ref(expr.resolved_type), expr.source_range);
        Rvalue rv;
        rv.kind = Rvalue::Kind::Aggregate;
        rv.operands = std::move(items);
        rv.result_type = clone_type_ref(expr.resolved_type);
        emit_assign(dest, std::move(rv), expr.source_range);
        return make_local(dest);
    }

    [[nodiscard]] Operand lower_expr_node(const ir::SetLiteralExpr &e, const ir::Expr &expr) {
        std::vector<Operand> items;
        items.reserve(e.items.size());
        for (const auto &item : e.items) {
            items.push_back(lower_expr(item.get()));
        }
        auto dest = new_temp(clone_type_ref(expr.resolved_type), expr.source_range);
        Rvalue rv;
        rv.kind = Rvalue::Kind::Aggregate;
        rv.operands = std::move(items);
        rv.result_type = clone_type_ref(expr.resolved_type);
        emit_assign(dest, std::move(rv), expr.source_range);
        return make_local(dest);
    }

    [[nodiscard]] Operand lower_expr_node(const ir::MapLiteralExpr &e, const ir::Expr &expr) {
        std::vector<Operand> items;
        items.reserve(e.entries.size() * 2);
        for (const auto &entry : e.entries) {
            items.push_back(lower_expr(entry.key.get()));
            items.push_back(lower_expr(entry.value.get()));
        }
        auto dest = new_temp(clone_type_ref(expr.resolved_type), expr.source_range);
        Rvalue rv;
        rv.kind = Rvalue::Kind::Aggregate;
        rv.operands = std::move(items);
        rv.result_type = clone_type_ref(expr.resolved_type);
        emit_assign(dest, std::move(rv), expr.source_range);
        return make_local(dest);
    }

    [[nodiscard]] Operand lower_expr_node(const ir::UnaryExpr &e, const ir::Expr &expr) {
        auto operand = lower_expr(e.operand.get());
        auto dest = new_temp(clone_type_ref(expr.resolved_type), expr.source_range);
        Rvalue rv;
        rv.kind = Rvalue::Kind::UnaryOp;
        rv.op = unary_op_name(e.op);
        rv.result_type = clone_type_ref(expr.resolved_type);
        rv.operands.push_back(operand);
        emit_assign(dest, std::move(rv), expr.source_range);
        return make_local(dest);
    }

    [[nodiscard]] Operand lower_expr_node(const ir::BinaryExpr &e, const ir::Expr &expr) {
        auto lhs = lower_expr(e.lhs.get());
        auto rhs = lower_expr(e.rhs.get());
        auto dest = new_temp(clone_type_ref(expr.resolved_type), expr.source_range);
        Rvalue rv;
        rv.kind = Rvalue::Kind::BinaryOp;
        rv.op = binary_op_name(e.op);
        rv.result_type = clone_type_ref(expr.resolved_type);
        rv.operands.push_back(lhs);
        rv.operands.push_back(rhs);
        emit_assign(dest, std::move(rv), expr.source_range);
        return make_local(dest);
    }

    [[nodiscard]] Operand lower_expr_node(const ir::MemberAccessExpr &e, const ir::Expr &expr) {
        auto base = lower_expr(e.base.get());
        auto dest = new_temp(clone_type_ref(expr.resolved_type), expr.source_range);
        Rvalue rv;
        rv.kind = Rvalue::Kind::Field;
        rv.field_name = e.member;
        rv.result_type = clone_type_ref(expr.resolved_type);
        rv.operands.push_back(base);
        emit_assign(dest, std::move(rv), expr.source_range);
        return make_local(dest);
    }

    [[nodiscard]] Operand lower_expr_node(const ir::IndexAccessExpr &e, const ir::Expr &expr) {
        auto base = lower_expr(e.base.get());
        auto index = lower_expr(e.index.get());
        auto dest = new_temp(clone_type_ref(expr.resolved_type), expr.source_range);
        Rvalue rv;
        rv.kind = Rvalue::Kind::Index;
        rv.result_type = clone_type_ref(expr.resolved_type);
        rv.operands.push_back(base);
        rv.operands.push_back(index);
        emit_assign(dest, std::move(rv), expr.source_range);
        return make_local(dest);
    }

    // ---- Helpers ----

    static Operand make_constant(Constant c) {
        Operand op;
        op.kind = Operand::Kind::Constant;
        op.constant = std::move(c);
        return op;
    }

    static Operand make_local(LocalRef ref) {
        Operand op;
        op.kind = Operand::Kind::Local;
        op.local = ref;
        return op;
    }

    static std::string unary_op_name(ir::ExprUnaryOp op) {
        switch (op) {
        case ir::ExprUnaryOp::Not:
            return "not";
        case ir::ExprUnaryOp::Negate:
            return "neg";
        case ir::ExprUnaryOp::Positive:
            return "pos";
        }
        return "unknown_unary";
    }

    static std::string binary_op_name(ir::ExprBinaryOp op) {
        switch (op) {
        case ir::ExprBinaryOp::Implies:
            return "implies";
        case ir::ExprBinaryOp::Or:
            return "or";
        case ir::ExprBinaryOp::And:
            return "and";
        case ir::ExprBinaryOp::Equal:
            return "eq";
        case ir::ExprBinaryOp::NotEqual:
            return "ne";
        case ir::ExprBinaryOp::Less:
            return "lt";
        case ir::ExprBinaryOp::LessEqual:
            return "le";
        case ir::ExprBinaryOp::Greater:
            return "gt";
        case ir::ExprBinaryOp::GreaterEqual:
            return "ge";
        case ir::ExprBinaryOp::Add:
            return "add";
        case ir::ExprBinaryOp::Subtract:
            return "sub";
        case ir::ExprBinaryOp::Multiply:
            return "mul";
        case ir::ExprBinaryOp::Divide:
            return "div";
        case ir::ExprBinaryOp::Modulo:
            return "mod";
        }
        return "unknown_binary";
    }

    std::string function_name_;
    std::vector<Local> locals_;
    std::vector<BasicBlock> blocks_;
    SourceRangeOpt function_source_range_;
    RootTypeHints root_type_hints_;
    std::uint32_t current_block_{0};
    std::uint32_t temp_counter_{0};
    std::uint32_t arg_count_{0};
};

// ---- Statement lowering (free functions operating on context) ----

void lower_block(LoweringContext &ctx, const ir::Block &block);
void lower_stmt_node(LoweringContext &ctx, const ir::LetStatement &s, const ir::Statement &stmt);
void lower_stmt_node(LoweringContext &ctx, const ir::AssignStatement &s, const ir::Statement &stmt);
void lower_stmt_node(LoweringContext &ctx, const ir::IfStatement &s, const ir::Statement &stmt);
void lower_stmt_node(LoweringContext &ctx, const ir::GotoStatement &s, const ir::Statement &stmt);
void lower_stmt_node(LoweringContext &ctx, const ir::ReturnStatement &s, const ir::Statement &stmt);
void lower_stmt_node(LoweringContext &ctx, const ir::AssertStatement &s, const ir::Statement &stmt);
void lower_stmt_node(LoweringContext &ctx, const ir::ExprStatement &s, const ir::Statement &stmt);

void lower_statement(LoweringContext &ctx, const ir::Statement &stmt) {
    std::visit([&ctx, &stmt](const auto &node) { lower_stmt_node(ctx, node, stmt); }, stmt.node);
}

[[nodiscard]] std::string path_storage_name(const ir::Path &path) {
    std::string name = path.root_name;
    for (const auto &member : path.members) {
        name += '.';
        name += member;
    }
    return name;
}

void lower_stmt_node(LoweringContext &ctx, const ir::LetStatement &s, const ir::Statement &stmt) {
    auto init_operand = ctx.lower_expr(s.initializer.get());
    auto binding_type = has_resolved_type(s.type_ref) ? clone_type_ref(s.type_ref)
                                                      : type_or_unresolved(s.initializer.get());
    auto local =
        ctx.new_local(s.name, clone_type_ref(binding_type), /*is_temp=*/false, stmt.source_range);
    Rvalue rv;
    rv.kind = Rvalue::Kind::Use;
    rv.result_type = std::move(binding_type);
    rv.operands.push_back(init_operand);
    ctx.emit_assign(local, std::move(rv), stmt.source_range);
}

void lower_stmt_node(LoweringContext &ctx,
                     const ir::AssignStatement &s,
                     const ir::Statement &stmt) {
    auto value_operand = ctx.lower_expr(s.value.get());
    auto value_type = type_or_unresolved(s.value.get());
    // Find or create the target local.
    auto target_name = path_storage_name(s.target);
    auto target = ctx.find_local(target_name);
    if (target == kNoLocal) {
        target = ctx.new_local(std::move(target_name),
                               clone_type_ref(value_type),
                               /*is_temp=*/false,
                               stmt.source_range);
    } else {
        ctx.refine_local(target, value_type, stmt.source_range);
    }
    Rvalue rv;
    rv.kind = Rvalue::Kind::Use;
    rv.result_type = std::move(value_type);
    rv.operands.push_back(value_operand);
    ctx.emit_assign(target, std::move(rv), stmt.source_range);
}

void lower_stmt_node(LoweringContext &ctx, const ir::IfStatement &s, const ir::Statement &stmt) {
    auto cond_operand = ctx.lower_expr(s.condition.get());
    auto cond_type = type_or_unresolved(s.condition.get());
    auto cond_range = s.condition ? s.condition->source_range : stmt.source_range;
    auto cond_local = ctx.new_temp(clone_type_ref(cond_type), cond_range);
    {
        Rvalue rv;
        rv.kind = Rvalue::Kind::Use;
        rv.result_type = std::move(cond_type);
        rv.operands.push_back(cond_operand);
        ctx.emit_assign(cond_local, std::move(rv), cond_range);
    }

    auto then_block = ctx.new_block("if.then");
    auto else_block = ctx.new_block("if.else");
    auto merge_block = ctx.new_block("if.merge");

    // Set SwitchBool terminator on current block.
    Terminator term;
    term.kind = Terminator::Kind::SwitchBool;
    term.condition = cond_local;
    term.then_block = then_block;
    term.else_block = else_block;
    term.source_range = stmt.source_range;
    ctx.set_terminator(std::move(term));

    // Lower then branch.
    ctx.set_current_block(then_block);
    if (s.then_block) {
        lower_block(ctx, *s.then_block);
    }
    // If the then branch didn't set a terminator (still Unreachable), jump to merge.
    if (ctx.current_block().terminator.kind == Terminator::Kind::Unreachable) {
        Terminator goto_merge;
        goto_merge.kind = Terminator::Kind::Goto;
        goto_merge.target = merge_block;
        goto_merge.source_range = stmt.source_range;
        ctx.set_terminator(std::move(goto_merge));
    }

    // Lower else branch.
    ctx.set_current_block(else_block);
    if (s.else_block) {
        lower_block(ctx, *s.else_block);
    }
    if (ctx.current_block().terminator.kind == Terminator::Kind::Unreachable) {
        Terminator goto_merge;
        goto_merge.kind = Terminator::Kind::Goto;
        goto_merge.target = merge_block;
        goto_merge.source_range = stmt.source_range;
        ctx.set_terminator(std::move(goto_merge));
    }

    // Continue in merge block.
    ctx.set_current_block(merge_block);
}

void lower_stmt_node(LoweringContext &ctx, const ir::GotoStatement &s, const ir::Statement &stmt) {
    // Goto translates to a Return terminator carrying the state name as a
    // string constant in a temporary (the runtime uses this to dispatch).
    auto state_type = string_type_ref();
    auto state_local = ctx.new_temp(clone_type_ref(state_type), stmt.source_range);
    Rvalue rv;
    rv.kind = Rvalue::Kind::Use;
    rv.result_type = std::move(state_type);
    rv.operands.push_back([&] {
        Operand op;
        op.kind = Operand::Kind::Constant;
        op.constant = s.target_state;
        return op;
    }());
    ctx.emit_assign(state_local, std::move(rv), stmt.source_range);

    Terminator term;
    term.kind = Terminator::Kind::Return;
    term.return_value = state_local;
    term.source_range = stmt.source_range;
    ctx.set_terminator(std::move(term));

    // Create a new block for any dead code after goto.
    auto next = ctx.new_block("after.goto");
    ctx.set_current_block(next);
}

void lower_stmt_node(LoweringContext &ctx,
                     const ir::ReturnStatement &s,
                     const ir::Statement &stmt) {
    auto value_operand = ctx.lower_expr(s.value.get());
    auto return_type = type_or_unresolved(s.value.get());
    auto ret_local = ctx.new_temp(clone_type_ref(return_type), stmt.source_range);
    Rvalue rv;
    rv.kind = Rvalue::Kind::Use;
    rv.result_type = std::move(return_type);
    rv.operands.push_back(value_operand);
    ctx.emit_assign(ret_local, std::move(rv), stmt.source_range);

    Terminator term;
    term.kind = Terminator::Kind::Return;
    term.return_value = ret_local;
    term.source_range = stmt.source_range;
    ctx.set_terminator(std::move(term));

    // Dead code after return gets a new block.
    auto next = ctx.new_block("after.return");
    ctx.set_current_block(next);
}

void lower_stmt_node(LoweringContext &ctx,
                     const ir::AssertStatement &s,
                     const ir::Statement &stmt) {
    auto cond_operand = ctx.lower_expr(s.condition.get());
    auto cond_type = type_or_unresolved(s.condition.get());
    auto cond_range = s.condition ? s.condition->source_range : stmt.source_range;
    auto cond_local = ctx.new_temp(clone_type_ref(cond_type), cond_range);
    {
        Rvalue rv;
        rv.kind = Rvalue::Kind::Use;
        rv.result_type = std::move(cond_type);
        rv.operands.push_back(cond_operand);
        ctx.emit_assign(cond_local, std::move(rv), cond_range);
    }

    auto continue_block = ctx.new_block("after.assert");

    Terminator term;
    term.kind = Terminator::Kind::Assert;
    term.condition = cond_local;
    term.target = continue_block; // resume here if assertion passes
    term.source_range = stmt.source_range;
    ctx.set_terminator(std::move(term));

    ctx.set_current_block(continue_block);
}

void lower_stmt_node(LoweringContext &ctx, const ir::ExprStatement &s, const ir::Statement &stmt) {
    if (s.expr) {
        // Lower expression for its side-effects; discard result.
        (void)ctx.lower_expr(s.expr.get());
    } else {
        ctx.emit_nop(stmt.source_range);
    }
}

void lower_block(LoweringContext &ctx, const ir::Block &block) {
    for (const auto &stmt : block.statements) {
        if (stmt) {
            lower_statement(ctx, *stmt);
        }
    }
}

/// Resolve the agent name that a FlowDecl targets.
std::string resolve_flow_agent_name(const ir::FlowDecl &flow) {
    if (!flow.target_ref.canonical_name.empty()) {
        return flow.target_ref.canonical_name;
    }
    if (!flow.target_ref.local_name.empty()) {
        return flow.target_ref.local_name;
    }
    return "unknown_agent";
}

[[nodiscard]] bool symbol_refs_match(const SymbolRef &lhs, const SymbolRef &rhs) {
    if (lhs.id.has_value() && rhs.id.has_value()) {
        return lhs.id == rhs.id;
    }
    if (!lhs.canonical_name.empty() && !rhs.canonical_name.empty()) {
        return lhs.canonical_name == rhs.canonical_name;
    }
    return !lhs.local_name.empty() && !rhs.local_name.empty() && lhs.local_name == rhs.local_name;
}

[[nodiscard]] const AgentDecl *find_agent_by_ref(const Program &program,
                                                 const SymbolRef &agent_ref) {
    for (const auto &decl : program.declarations) {
        const auto *agent = std::get_if<AgentDecl>(&decl);
        if (agent == nullptr) {
            continue;
        }
        if (symbol_refs_match(agent->symbol_ref, agent_ref)) {
            return agent;
        }
    }
    return nullptr;
}

[[nodiscard]] RootTypeHints root_type_hints_for_agent(const AgentDecl *agent) {
    RootTypeHints hints;
    if (agent == nullptr) {
        return hints;
    }
    hints.has_input = has_resolved_type(agent->input_type_ref);
    hints.has_context = has_resolved_type(agent->context_type_ref);
    hints.has_output = has_resolved_type(agent->output_type_ref);
    if (hints.has_input) {
        hints.input = clone_type_ref(agent->input_type_ref);
    }
    if (hints.has_context) {
        hints.context = clone_type_ref(agent->context_type_ref);
    }
    if (hints.has_output) {
        hints.output = clone_type_ref(agent->output_type_ref);
    }
    return hints;
}

[[nodiscard]] RootTypeHints root_type_hints_for_flow(const Program &program, const FlowDecl &flow) {
    return root_type_hints_for_agent(find_agent_by_ref(program, flow.target_ref));
}

[[nodiscard]] RootTypeHints root_type_hints_for_contract(const Program &program,
                                                         const ContractDecl &contract) {
    return root_type_hints_for_agent(find_agent_by_ref(program, contract.target_ref));
}

[[nodiscard]] TypeRef output_type_for_workflow_node(const Program &program,
                                                    const WorkflowNode &node) {
    const auto *agent = find_agent_by_ref(program, node.target_ref);
    if (agent == nullptr || !has_resolved_type(agent->output_type_ref)) {
        return {};
    }
    return clone_type_ref(agent->output_type_ref);
}

[[nodiscard]] RootTypeHints root_type_hints_for_workflow_expr(const Program &program,
                                                              const WorkflowDecl &workflow) {
    RootTypeHints hints;
    if (has_resolved_type(workflow.input_type_ref)) {
        hints.has_input = true;
        hints.input = clone_type_ref(workflow.input_type_ref);
    } else {
        hints.extra_arguments.emplace_back("input", TypeRef{});
    }
    for (const auto &node : workflow.nodes) {
        if (node.name.empty()) {
            continue;
        }
        hints.extra_arguments.emplace_back(node.name, output_type_for_workflow_node(program, node));
    }
    return hints;
}

void set_return_terminator_for_operand(LoweringContext &ctx,
                                       Operand operand,
                                       TypeRef return_type,
                                       SourceRangeOpt source_range) {
    LocalRef return_local = kNoLocal;
    if (operand.kind == Operand::Kind::Local) {
        return_local = operand.local;
    } else {
        return_local = ctx.new_temp(clone_type_ref(return_type), source_range);
        Rvalue rv;
        rv.kind = Rvalue::Kind::Use;
        rv.result_type = clone_type_ref(return_type);
        rv.operands.push_back(std::move(operand));
        ctx.emit_assign(return_local, std::move(rv), source_range);
    }

    Terminator term;
    term.kind = Terminator::Kind::Return;
    term.return_value = return_local;
    term.source_range = source_range;
    ctx.set_terminator(std::move(term));
}

[[nodiscard]] OptFunction lower_expr_function(std::string function_name,
                                              ExprRef expr,
                                              SourceRangeOpt source_range,
                                              RootTypeHints root_type_hints) {
    LoweringContext ctx(
        std::move(function_name), std::move(source_range), std::move(root_type_hints));
    auto return_type = type_or_unresolved(expr.get());
    auto operand = ctx.lower_expr(expr.get());
    set_return_terminator_for_operand(ctx,
                                      std::move(operand),
                                      clone_type_ref(return_type),
                                      expr ? expr->source_range : SourceRangeOpt{});
    return ctx.finish(std::move(return_type));
}

void record_skipped_temporal_fragment(OptProgram &program,
                                      const std::string &function_prefix,
                                      std::string kind,
                                      const SourceRangeOpt &source_range) {
    program.skipped_temporal_fragments.push_back(SkippedTemporalFragment{
        .scope = function_prefix,
        .kind = std::move(kind),
        .reason = "temporal atom is not a pure expression fragment",
        .source_range = source_range,
    });
}

void lower_temporal_embedded_exprs(OptProgram &program,
                                   const TemporalExprPtr &temporal,
                                   const std::string &function_prefix,
                                   const RootTypeHints &root_type_hints,
                                   std::uint32_t &embedded_index) {
    if (!temporal) {
        return;
    }

    std::visit(
        [&](const auto &node) {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, EmbeddedTemporalExpr>) {
                if (node.expr) {
                    program.functions.push_back(lower_expr_function(
                        function_prefix + "::embedded::" + std::to_string(embedded_index++),
                        node.expr,
                        node.expr->source_range,
                        clone_root_type_hints(root_type_hints)));
                }
            } else if constexpr (std::is_same_v<T, CalledTemporalExpr>) {
                record_skipped_temporal_fragment(
                    program, function_prefix, "called", temporal->source_range);
            } else if constexpr (std::is_same_v<T, InStateTemporalExpr>) {
                record_skipped_temporal_fragment(
                    program, function_prefix, "in_state", temporal->source_range);
            } else if constexpr (std::is_same_v<T, RunningTemporalExpr>) {
                record_skipped_temporal_fragment(
                    program, function_prefix, "running", temporal->source_range);
            } else if constexpr (std::is_same_v<T, CompletedTemporalExpr>) {
                record_skipped_temporal_fragment(
                    program, function_prefix, "completed", temporal->source_range);
            } else if constexpr (std::is_same_v<T, TemporalUnaryExpr>) {
                lower_temporal_embedded_exprs(
                    program, node.operand, function_prefix, root_type_hints, embedded_index);
            } else if constexpr (std::is_same_v<T, TemporalBinaryExpr>) {
                lower_temporal_embedded_exprs(
                    program, node.lhs, function_prefix, root_type_hints, embedded_index);
                lower_temporal_embedded_exprs(
                    program, node.rhs, function_prefix, root_type_hints, embedded_index);
            }
        },
        temporal->node);
}

} // anonymous namespace

OptProgram lower_to_opt(const Program &program) {
    OptProgram result;
    result.source_program_version = program.format_version;

    for (const auto &decl : program.declarations) {
        const auto *flow = std::get_if<FlowDecl>(&decl);
        if (flow == nullptr) {
            continue;
        }

        auto agent_name = resolve_flow_agent_name(*flow);
        auto root_type_hints = root_type_hints_for_flow(program, *flow);

        for (const auto &handler : flow->state_handlers) {
            auto func_name = "flow::" + agent_name + "::" + handler.state_name;

            LoweringContext ctx(
                func_name, handler.source_range, clone_root_type_hints(root_type_hints));
            lower_block(ctx, handler.body);

            // If the entry block's terminator is still Unreachable after
            // lowering, set a default Return terminator.
            auto &final_block = ctx.current_block();
            if (final_block.terminator.kind == Terminator::Kind::Unreachable) {
                Terminator term;
                term.kind = Terminator::Kind::Return;
                term.return_value = kNoLocal;
                term.source_range = handler.source_range;
                ctx.set_terminator(std::move(term));
            }

            result.functions.push_back(ctx.finish());
        }
    }

    for (const auto &decl : program.declarations) {
        const auto *contract = std::get_if<ContractDecl>(&decl);
        if (contract == nullptr) {
            continue;
        }

        auto root_type_hints = root_type_hints_for_contract(program, *contract);
        const auto owner = !contract->target_ref.canonical_name.empty()
                               ? contract->target_ref.canonical_name
                               : contract->target_ref.local_name;
        for (std::uint32_t index = 0; index < contract->clauses.size(); ++index) {
            const auto &clause = contract->clauses[index];
            const auto prefix = "contract::" + owner + "::clause::" + std::to_string(index);
            if (const auto *expr = std::get_if<ExprRef>(&clause.value); expr != nullptr) {
                if (*expr) {
                    result.functions.push_back(
                        lower_expr_function(prefix,
                                            *expr,
                                            clause.source_range,
                                            clone_root_type_hints(root_type_hints)));
                }
                continue;
            }

            const auto *temporal = std::get_if<TemporalExprPtr>(&clause.value);
            if (temporal != nullptr) {
                std::uint32_t embedded_index = 0;
                lower_temporal_embedded_exprs(
                    result, *temporal, prefix, root_type_hints, embedded_index);
            }
        }
    }

    for (const auto &decl : program.declarations) {
        const auto *workflow = std::get_if<WorkflowDecl>(&decl);
        if (workflow == nullptr) {
            continue;
        }

        auto root_type_hints = root_type_hints_for_workflow_expr(program, *workflow);
        const auto workflow_name =
            workflow->name.empty() ? std::string{"unknown_workflow"} : workflow->name;
        for (const auto &node : workflow->nodes) {
            if (!node.input) {
                continue;
            }
            result.functions.push_back(lower_expr_function("workflow::" + workflow_name +
                                                               "::node::" + node.name + "::input",
                                                           node.input,
                                                           node.source_range,
                                                           clone_root_type_hints(root_type_hints)));
        }
        if (workflow->return_value) {
            result.functions.push_back(
                lower_expr_function("workflow::" + workflow_name + "::return",
                                    workflow->return_value,
                                    workflow->return_value->source_range,
                                    clone_root_type_hints(root_type_hints)));
        }

        for (std::uint32_t index = 0; index < workflow->safety.size(); ++index) {
            std::uint32_t embedded_index = 0;
            lower_temporal_embedded_exprs(result,
                                          workflow->safety[index],
                                          "workflow::" + workflow_name +
                                              "::safety::" + std::to_string(index),
                                          root_type_hints,
                                          embedded_index);
        }
        for (std::uint32_t index = 0; index < workflow->liveness.size(); ++index) {
            std::uint32_t embedded_index = 0;
            lower_temporal_embedded_exprs(result,
                                          workflow->liveness[index],
                                          "workflow::" + workflow_name +
                                              "::liveness::" + std::to_string(index),
                                          root_type_hints,
                                          embedded_index);
        }
    }

    return result;
}

} // namespace ahfl::ir::opt
