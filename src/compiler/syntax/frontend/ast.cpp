#include "ahfl/compiler/frontend/ast.hpp"

#include "ahfl/base/support/overloaded.hpp"

#include <sstream>
#include <utility>
#include <variant>

namespace ahfl::ast {

namespace {

std::string with_name(std::string_view prefix, const std::string &value) {
    std::string text(prefix);
    text += value;
    return text;
}

std::string with_count(std::string prefix, std::size_t count, std::string_view noun) {
    std::ostringstream builder;
    builder << prefix << " (" << count << " " << noun;
    builder << (count == 1 ? ")" : "s)");
    return builder.str();
}

std::string join_segments(const std::vector<std::string> &segments) {
    std::string text;

    for (std::size_t index = 0; index < segments.size(); ++index) {
        if (index != 0) {
            text += "::";
        }

        text += segments[index];
    }

    return text;
}

class AstInvariantValidator final {
  public:
    [[nodiscard]] std::vector<AstInvariantViolation> validate(const Program &program) {
        violations_.clear();

        for (const auto &declaration : program.declarations) {
            if (!declaration) {
                fail(program.range, "Program.declarations contains a null declaration");
                continue;
            }

            validate_declaration(*declaration);
        }

        return std::move(violations_);
    }

  private:
    std::vector<AstInvariantViolation> violations_;

    void fail(SourceRange range, std::string message) {
        violations_.push_back(AstInvariantViolation{
            .range = range,
            .message = std::move(message),
        });
    }

    void require(bool condition, SourceRange range, std::string_view message) {
        if (!condition) {
            fail(range, std::string(message));
        }
    }

    void validate_qualified_name(const QualifiedName *name,
                                 SourceRange range,
                                 std::string_view field_name) {
        require(name != nullptr, range, std::string(field_name) + " is missing");
        if (name != nullptr) {
            require(
                !name->segments.empty(), name->range, std::string(field_name) + " has no segments");
        }
    }

    void validate_path(const PathSyntax *path, SourceRange range, std::string_view field_name) {
        require(path != nullptr, range, std::string(field_name) + " is missing");
        if (path == nullptr) {
            return;
        }

        require(!path->root_name.empty(),
                path->range,
                std::string(field_name) + " is missing root_name");
        for (const auto &member : path->members) {
            require(!member.empty(), path->range, std::string(field_name) + " has empty member");
        }
    }

    void validate_type(const TypeSyntax &type) {
        std::visit(
            Overloaded{
                [&](const UnitType &) {},
                [&](const BoolType &) {},
                [&](const IntType &) {},
                [&](const FloatType &) {},
                [&](const StringType &) {},
                [&](const BoundedStringType &) {
                    // BoundedString bounds are stored as value types — presence is guaranteed by variant
                },
                [&](const UuidType &) {},
                [&](const TimestampType &) {},
                [&](const DurationType &) {},
                [&](const DecimalType &) {
                    // Decimal scale is stored as value type — presence is guaranteed by variant
                },
                [&](const NamedType &t) {
                    validate_qualified_name(t.name.get(), type.range, "NamedType.name");
                    for (const auto &arg : t.type_args) {
                        require(arg != nullptr, type.range, "NamedType is missing a type argument");
                        if (arg) {
                            validate_type(*arg);
                        }
                    }
                },
                [&](const FnType &t) {
                    for (const auto &param : t.params) {
                        require(param != nullptr, type.range, "FnType.params contains null");
                        if (param) {
                            validate_type(*param);
                        }
                    }
                    if (t.return_type) {
                        validate_type(*t.return_type);
                    }
                    if (t.has_effect_clause && t.effect_kind == EffectClauseKind::Capability) {
                        for (const auto &cap : t.effect_capabilities) {
                            require(cap != nullptr,
                                    type.range,
                                    "FnType.effect_capabilities contains null");
                            if (cap) {
                                validate_qualified_name(
                                    cap.get(), type.range, "fn type effect capability");
                            }
                        }
                    }
                },
                [&](const AppType &t) {
                    validate_qualified_name(t.name.get(), type.range, "AppType.name");
                    for (const auto &arg : t.arguments) {
                        require(arg != nullptr, type.range, "AppType.arguments contains null");
                        if (arg) {
                            validate_type(*arg);
                        }
                    }
                },
            },
            type.node);
    }

    void validate_expr(const ExprSyntax &expr) {
        std::visit(
            Overloaded{
                [&](const BoolLiteralExpr &) {
                    // Value type — presence guaranteed by variant
                },
                [&](const IntegerLiteralExpr &e) {
                    require(
                        e.literal != nullptr, expr.range, "IntegerLiteralExpr is missing literal");
                },
                [&](const FloatLiteralExpr &) {
                    // spelling is value type — presence guaranteed by variant
                },
                [&](const DecimalLiteralExpr &) {
                    // spelling is value type — presence guaranteed by variant
                },
                [&](const StringLiteralExpr &) {
                    // spelling is value type — presence guaranteed by variant
                },
                [&](const DurationLiteralExpr &e) {
                    require(
                        e.literal != nullptr, expr.range, "DurationLiteralExpr is missing literal");
                },
                [&](const PathExpr &e) {
                    require(e.path != nullptr, expr.range, "PathExpr is missing path");
                    if (e.path) {
                        require(!e.path->root_name.empty(),
                                e.path->range,
                                "PathSyntax is missing root_name");
                    }
                },
                [&](const QualifiedValueExpr &e) {
                    validate_qualified_name(e.name.get(), expr.range, "QualifiedValueExpr.name");
                },
                [&](const CallExpr &e) {
                    validate_qualified_name(e.callee.get(), expr.range, "CallExpr.callee");
                    for (const auto &type_arg : e.type_args) {
                        require(
                            type_arg != nullptr, expr.range, "CallExpr.type_args contains null");
                        if (type_arg) {
                            validate_type(*type_arg);
                        }
                    }
                    for (const auto &arg : e.arguments) {
                        require(arg != nullptr, expr.range, "CallExpr.arguments contains null");
                        if (arg) {
                            validate_expr(*arg);
                        }
                    }
                },
                [&](const MethodCallExpr &e) {
                    require(
                        e.receiver != nullptr, expr.range, "MethodCallExpr is missing receiver");
                    require(!e.method.empty(), expr.range, "MethodCallExpr is missing method");
                    if (e.receiver) {
                        validate_expr(*e.receiver);
                    }
                    for (const auto &type_arg : e.type_args) {
                        require(type_arg != nullptr,
                                expr.range,
                                "MethodCallExpr.type_args contains null");
                        if (type_arg) {
                            validate_type(*type_arg);
                        }
                    }
                    for (const auto &arg : e.arguments) {
                        require(
                            arg != nullptr, expr.range, "MethodCallExpr.arguments contains null");
                        if (arg) {
                            validate_expr(*arg);
                        }
                    }
                },
                [&](const StructLiteralExpr &e) {
                    validate_qualified_name(
                        e.type_name.get(), expr.range, "StructLiteralExpr.type_name");
                    for (const auto &field : e.fields) {
                        require(
                            field != nullptr, expr.range, "StructLiteralExpr.fields contains null");
                        if (!field) {
                            continue;
                        }
                        require(!field->field_name.empty(),
                                field->range,
                                "StructInitSyntax is missing field_name");
                        require(field->value != nullptr,
                                field->range,
                                "StructInitSyntax is missing value");
                        if (field->value) {
                            validate_expr(*field->value);
                        }
                    }
                },
                [&](const UnaryExpr &e) {
                    require(e.operand != nullptr, expr.range, "UnaryExpr is missing operand");
                    if (e.operand) {
                        validate_expr(*e.operand);
                    }
                },
                [&](const BinaryExpr &e) {
                    require(e.lhs != nullptr, expr.range, "BinaryExpr is missing lhs");
                    require(e.rhs != nullptr, expr.range, "BinaryExpr is missing rhs");
                    if (e.lhs) {
                        validate_expr(*e.lhs);
                    }
                    if (e.rhs) {
                        validate_expr(*e.rhs);
                    }
                },
                [&](const MemberAccessExpr &e) {
                    require(e.base != nullptr, expr.range, "MemberAccessExpr is missing base");
                    require(!e.member.empty(), expr.range, "MemberAccessExpr is missing member");
                    if (e.base) {
                        validate_expr(*e.base);
                    }
                },
                [&](const IndexAccessExpr &e) {
                    require(e.base != nullptr, expr.range, "IndexAccessExpr is missing base");
                    require(e.index != nullptr, expr.range, "IndexAccessExpr is missing index");
                    if (e.base) {
                        validate_expr(*e.base);
                    }
                    if (e.index) {
                        validate_expr(*e.index);
                    }
                },
                [&](const GroupExpr &e) {
                    require(e.inner != nullptr, expr.range, "GroupExpr is missing inner");
                    if (e.inner) {
                        validate_expr(*e.inner);
                    }
                },
                [&](const MatchExpr &e) {
                    require(e.scrutinee != nullptr, expr.range, "MatchExpr is missing scrutinee");
                    if (e.scrutinee) {
                        validate_expr(*e.scrutinee);
                    }
                    for (const auto &arm : e.arms) {
                        require(arm != nullptr, expr.range, "MatchExpr.arms contains null");
                        if (!arm) {
                            continue;
                        }
                        validate_match_arm(*arm);
                    }
                },
                [&](const LambdaExpr &e) {
                    // P2 (RFC §6): closure body is the only expression child.
                    for (const auto &param : e.params) {
                        require(param != nullptr, expr.range, "LambdaExpr.params contains null");
                        if (param && param->type) {
                            validate_type(*param->type);
                        }
                    }
                    require(e.body != nullptr, expr.range, "LambdaExpr is missing body");
                    if (e.body) {
                        validate_expr(*e.body);
                    }
                },
                // P4-02: unwrap(e) — identical operand invariants to the
                // statement-level unwrap, but expressed as an expression child.
                [&](const UnwrapExprSyntax &e) {
                    require(e.operand != nullptr, expr.range,
                            "UnwrapExpr is missing operand");
                    if (e.operand) {
                        validate_expr(*e.operand);
                    }
                },
            },
            expr.node);
    }

    /// Validate a match arm (P1 ADT, RFC §1.6).
    void validate_match_arm(const MatchArmSyntax &arm) {
        require(arm.pattern != nullptr, arm.range, "MatchArmSyntax is missing pattern");
        if (arm.pattern) {
            validate_pattern(*arm.pattern);
        }
        if (arm.guard) {
            validate_expr(*arm.guard);
        }
        require(arm.body != nullptr, arm.range, "MatchArmSyntax is missing body");
        if (arm.body) {
            validate_expr(*arm.body);
        }
    }

    /// Validate a pattern (P1 ADT, RFC §1.6).
    void validate_pattern(const PatternSyntax &pattern) {
        std::visit(
            Overloaded{
                [&](const LiteralPattern &) {
                    require(
                        !pattern.text.empty(), pattern.range, "LiteralPattern is missing spelling");
                },
                [&](const VariantPattern &p) {
                    validate_qualified_name(p.path.get(), pattern.range, "VariantPattern.path");
                    for (const auto &sub : p.subpatterns) {
                        require(sub != nullptr, pattern.range, "VariantPattern.subpatterns null");
                        if (sub) {
                            validate_pattern(*sub);
                        }
                    }
                },
                [&](const WildcardPattern &) {},
                [&](const BindingPattern &p) {
                    require(!p.name.empty(), pattern.range, "BindingPattern is missing name");
                    if (p.nested) {
                        validate_pattern(*p.nested);
                    }
                },
                [&](const TuplePattern &p) {
                    for (const auto &elem : p.elements) {
                        require(elem != nullptr, pattern.range, "TuplePattern.elements null");
                        if (elem) {
                            validate_pattern(*elem);
                        }
                    }
                },
                [&](const OrPattern &p) {
                    require(!p.branches.empty(), pattern.range, "OrPattern has no branches");
                    for (const auto &branch : p.branches) {
                        require(branch != nullptr, pattern.range, "OrPattern.branches null");
                        if (branch) {
                            validate_pattern(*branch);
                        }
                    }
                },
            },
            pattern.node);
    }

    void validate_statement(const StatementSyntax &statement) {
        int active_count = 0;
        active_count += statement.let_stmt != nullptr;
        active_count += statement.assign_stmt != nullptr;
        active_count += statement.if_stmt != nullptr;
        active_count += statement.if_let_stmt != nullptr;
        active_count += statement.goto_stmt != nullptr;
        active_count += statement.return_stmt != nullptr;
        active_count += statement.assert_stmt != nullptr;
        active_count += statement.unwrap_stmt != nullptr;
        active_count += statement.requires_stmt != nullptr;
        active_count += statement.unreachable_stmt != nullptr;
        active_count += statement.expr_stmt != nullptr;
        require(active_count == 1,
                statement.range,
                "StatementSyntax must have exactly one active payload");

        switch (statement.kind) {
        case StatementSyntaxKind::Let:
            require(statement.let_stmt != nullptr,
                    statement.range,
                    "Let StatementSyntax is missing let_stmt");
            if (statement.let_stmt) {
                require(!statement.let_stmt->name.empty(),
                        statement.let_stmt->range,
                        "LetStmtSyntax is missing name");
                if (statement.let_stmt->type) {
                    validate_type(*statement.let_stmt->type);
                }
                require(statement.let_stmt->initializer != nullptr,
                        statement.let_stmt->range,
                        "LetStmtSyntax is missing initializer");
                if (statement.let_stmt->initializer) {
                    validate_expr(*statement.let_stmt->initializer);
                }
            }
            break;
        case StatementSyntaxKind::Assign:
            require(statement.assign_stmt != nullptr,
                    statement.range,
                    "Assign StatementSyntax is missing assign_stmt");
            if (statement.assign_stmt) {
                require(statement.assign_stmt->target != nullptr,
                        statement.assign_stmt->range,
                        "AssignStmtSyntax is missing target");
                require(statement.assign_stmt->value != nullptr,
                        statement.assign_stmt->range,
                        "AssignStmtSyntax is missing value");
                if (statement.assign_stmt->value) {
                    validate_expr(*statement.assign_stmt->value);
                }
            }
            break;
        case StatementSyntaxKind::If:
            require(statement.if_stmt != nullptr,
                    statement.range,
                    "If StatementSyntax is missing if_stmt");
            if (statement.if_stmt) {
                require(statement.if_stmt->condition != nullptr,
                        statement.if_stmt->range,
                        "IfStmtSyntax is missing condition");
                require(statement.if_stmt->then_block != nullptr,
                        statement.if_stmt->range,
                        "IfStmtSyntax is missing then_block");
                if (statement.if_stmt->condition) {
                    validate_expr(*statement.if_stmt->condition);
                }
                if (statement.if_stmt->then_block) {
                    validate_block(*statement.if_stmt->then_block);
                }
                if (statement.if_stmt->else_block) {
                    validate_block(*statement.if_stmt->else_block);
                }
            }
            break;
        case StatementSyntaxKind::IfLet:
            require(statement.if_let_stmt != nullptr,
                    statement.range,
                    "IfLet StatementSyntax is missing if_let_stmt");
            if (statement.if_let_stmt) {
                require(statement.if_let_stmt->pattern != nullptr,
                        statement.if_let_stmt->range,
                        "IfLetStmtSyntax is missing pattern");
                require(statement.if_let_stmt->scrutinee != nullptr,
                        statement.if_let_stmt->range,
                        "IfLetStmtSyntax is missing scrutinee");
                require(statement.if_let_stmt->then_block != nullptr,
                        statement.if_let_stmt->range,
                        "IfLetStmtSyntax is missing then_block");
                if (statement.if_let_stmt->pattern) {
                    require(!statement.if_let_stmt->pattern->variant_name.empty(),
                            statement.if_let_stmt->pattern->range,
                            "IfLetPatternSyntax is missing variant_name");
                }
                if (statement.if_let_stmt->scrutinee) {
                    validate_expr(*statement.if_let_stmt->scrutinee);
                }
                if (statement.if_let_stmt->then_block) {
                    validate_block(*statement.if_let_stmt->then_block);
                }
                if (statement.if_let_stmt->else_block) {
                    validate_block(*statement.if_let_stmt->else_block);
                }
            }
            break;
        case StatementSyntaxKind::Goto:
            require(statement.goto_stmt != nullptr,
                    statement.range,
                    "Goto StatementSyntax is missing goto_stmt");
            if (statement.goto_stmt) {
                require(!statement.goto_stmt->target_state.empty(),
                        statement.goto_stmt->range,
                        "GotoStmtSyntax is missing target_state");
            }
            break;
        case StatementSyntaxKind::Return:
            require(statement.return_stmt != nullptr,
                    statement.range,
                    "Return StatementSyntax is missing return_stmt");
            if (statement.return_stmt && statement.return_stmt->value) {
                validate_expr(*statement.return_stmt->value);
            }
            break;
        case StatementSyntaxKind::Assert:
            require(statement.assert_stmt != nullptr,
                    statement.range,
                    "Assert StatementSyntax is missing assert_stmt");
            if (statement.assert_stmt) {
                // Arity-0 `assert()` is intentionally allowed through the AST
                // layer so the typechecker can emit a single stable
                // `typecheck.WRONG_ARITY` diagnostic instead of a low-level
                // parse error.  Only recurse into child expressions when they
                // are actually present.
                if (statement.assert_stmt->condition) {
                    validate_expr(*statement.assert_stmt->condition);
                }
                if (statement.assert_stmt->message) {
                    validate_expr(*statement.assert_stmt->message);
                }
            }
            break;
        case StatementSyntaxKind::Unwrap:
            require(statement.unwrap_stmt != nullptr,
                    statement.range,
                    "Unwrap StatementSyntax is missing unwrap_stmt");
            if (statement.unwrap_stmt) {
                // Mirrors assert: arity-0 `unwrap()` is deferred to the
                // typechecker so the stable WRONG_ARITY code is produced.
                if (statement.unwrap_stmt->operand) {
                    validate_expr(*statement.unwrap_stmt->operand);
                }
            }
            break;
        case StatementSyntaxKind::Requires:
            require(statement.requires_stmt != nullptr,
                    statement.range,
                    "Requires StatementSyntax is missing requires_stmt");
            if (statement.requires_stmt) {
                // Mirrors assert: arity-0 `requires()` is deferred to the
                // typechecker so the stable WRONG_ARITY code is produced.
                if (statement.requires_stmt->condition) {
                    validate_expr(*statement.requires_stmt->condition);
                }
                if (statement.requires_stmt->message) {
                    validate_expr(*statement.requires_stmt->message);
                }
            }
            break;
        case StatementSyntaxKind::Unreachable:
            require(statement.unreachable_stmt != nullptr,
                    statement.range,
                    "Unreachable StatementSyntax is missing unreachable_stmt");
            if (statement.unreachable_stmt && statement.unreachable_stmt->message) {
                validate_expr(*statement.unreachable_stmt->message);
            }
            break;
        case StatementSyntaxKind::Expr:
            require(statement.expr_stmt != nullptr,
                    statement.range,
                    "Expr StatementSyntax is missing expr_stmt");
            if (statement.expr_stmt) {
                require(statement.expr_stmt->expr != nullptr,
                        statement.expr_stmt->range,
                        "ExprStmtSyntax is missing expr");
                if (statement.expr_stmt->expr) {
                    validate_expr(*statement.expr_stmt->expr);
                }
            }
            break;
        }
    }

    void validate_block(const BlockSyntax &block) {
        for (const auto &statement : block.statements) {
            require(statement != nullptr, block.range, "BlockSyntax.statements contains null");
            if (statement) {
                validate_statement(*statement);
            }
        }
    }

    void validate_temporal(const TemporalExprSyntax &expr) {
        std::visit(
            Overloaded{
                [&](const EmbeddedTemporalExpr &e) {
                    require(e.expr != nullptr,
                            expr.range,
                            "EmbeddedExpr TemporalExprSyntax is missing expr");
                    if (e.expr) {
                        validate_expr(*e.expr);
                    }
                },
                [&](const CalledTemporalExpr &e) {
                    require(!e.name.empty(),
                            expr.range,
                            std::string("Called") + " TemporalExprSyntax is missing name");
                },
                [&](const InStateTemporalExpr &e) {
                    require(!e.name.empty(),
                            expr.range,
                            std::string("InState") + " TemporalExprSyntax is missing name");
                },
                [&](const RunningTemporalExpr &e) {
                    require(!e.name.empty(),
                            expr.range,
                            std::string("Running") + " TemporalExprSyntax is missing name");
                },
                [&](const CompletedTemporalExpr &e) {
                    require(!e.name.empty(),
                            expr.range,
                            std::string("Completed") + " TemporalExprSyntax is missing name");
                },
                [&](const UnaryTemporalExpr &e) {
                    require(e.operand != nullptr,
                            expr.range,
                            "Unary TemporalExprSyntax is missing operand");
                    if (e.operand) {
                        validate_temporal(*e.operand);
                    }
                },
                [&](const BinaryTemporalExpr &e) {
                    require(
                        e.lhs != nullptr, expr.range, "Binary TemporalExprSyntax is missing lhs");
                    require(
                        e.rhs != nullptr, expr.range, "Binary TemporalExprSyntax is missing rhs");
                    if (e.lhs) {
                        validate_temporal(*e.lhs);
                    }
                    if (e.rhs) {
                        validate_temporal(*e.rhs);
                    }
                },
            },
            expr.node);
    }

    void validate_params(const std::vector<Owned<ParamDeclSyntax>> &params, SourceRange range) {
        for (const auto &param : params) {
            require(param != nullptr, range, "parameter list contains null");
            if (!param) {
                continue;
            }
            require(!param->name.empty(), param->range, "ParamDeclSyntax is missing name");
            // Self param (`self` / `mut self`) omits the type annotation at the
            // grammar level; the receiver type is inferred from the enclosing
            // impl/trait target during semantic analysis (P3b). Other
            // parameters must carry an explicit type.
            if (!param->is_self) {
                require(param->type != nullptr,
                        param->range,
                        "ParamDeclSyntax is missing type");
            }
            if (param->type) {
                validate_type(*param->type);
            }
        }
    }

    void validate_capability_effect(const CapabilityEffectSyntax &effect) {
        if (effect.domain) {
            validate_qualified_name(effect.domain.get(), effect.range, "CapabilityEffect.domain");
        }
        if (effect.idempotency_key) {
            validate_path(
                effect.idempotency_key.get(), effect.range, "CapabilityEffect.idempotency_key");
        }
        if (effect.timeout) {
            require(!effect.timeout->spelling.empty(),
                    effect.timeout->range,
                    "CapabilityEffect.timeout is missing spelling");
        }
        if (effect.compensation) {
            validate_qualified_name(
                effect.compensation.get(), effect.range, "CapabilityEffect.compensation");
        }
        for (const auto &policy : effect.policies) {
            require(policy != nullptr, effect.range, "CapabilityEffect.policies contains null");
            if (policy) {
                validate_qualified_name(policy.get(), effect.range, "CapabilityEffect.policy");
            }
        }
    }

    void validate_declaration(const Decl &declaration) {
        switch (declaration.kind) {
        case NodeKind::Program:
            fail(declaration.range, "Program node cannot appear in Program.declarations");
            break;
        case NodeKind::ModuleDecl: {
            const auto &node = static_cast<const ModuleDecl &>(declaration);
            validate_qualified_name(node.name.get(), node.range, "ModuleDecl.name");
            break;
        }
        case NodeKind::ImportDecl: {
            const auto &node = static_cast<const ImportDecl &>(declaration);
            validate_qualified_name(node.path.get(), node.range, "ImportDecl.path");
            break;
        }
        case NodeKind::ConstDecl: {
            const auto &node = static_cast<const ConstDecl &>(declaration);
            require(!node.name.empty(), node.range, "ConstDecl is missing name");
            require(node.type != nullptr, node.range, "ConstDecl is missing type");
            require(node.value != nullptr, node.range, "ConstDecl is missing value");
            if (node.type) {
                validate_type(*node.type);
            }
            if (node.value) {
                validate_expr(*node.value);
            }
            break;
        }
        case NodeKind::TypeAliasDecl: {
            const auto &node = static_cast<const TypeAliasDecl &>(declaration);
            require(!node.name.empty(), node.range, "TypeAliasDecl is missing name");
            require(
                node.aliased_type != nullptr, node.range, "TypeAliasDecl is missing aliased_type");
            for (const auto &param : node.type_params) {
                require(param != nullptr, node.range, "TypeAliasDecl.type_params contains null");
                if (param) {
                    require(!param->name.empty(), param->range, "TypeParamSyntax is missing name");
                    for (const auto &bound : param->bounds) {
                        require(
                            bound != nullptr, param->range, "TypeParamSyntax.bounds contains null");
                        if (bound) {
                            validate_type(*bound);
                        }
                    }
                }
            }
            if (node.aliased_type) {
                validate_type(*node.aliased_type);
            }
            break;
        }
        case NodeKind::StructDecl: {
            const auto &node = static_cast<const StructDecl &>(declaration);
            require(!node.name.empty(), node.range, "StructDecl is missing name");
            for (const auto &param : node.type_params) {
                require(param != nullptr, node.range, "StructDecl.type_params contains null");
                if (param) {
                    require(!param->name.empty(), param->range, "TypeParamSyntax is missing name");
                    for (const auto &bound : param->bounds) {
                        require(
                            bound != nullptr, param->range, "TypeParamSyntax.bounds contains null");
                        if (bound) {
                            validate_type(*bound);
                        }
                    }
                }
            }
            for (const auto &field : node.fields) {
                require(field != nullptr, node.range, "StructDecl.fields contains null");
                if (!field) {
                    continue;
                }
                require(
                    !field->name.empty(), field->range, "StructFieldDeclSyntax is missing name");
                require(
                    field->type != nullptr, field->range, "StructFieldDeclSyntax is missing type");
                if (field->type) {
                    validate_type(*field->type);
                }
                if (field->default_value) {
                    validate_expr(*field->default_value);
                }
            }
            break;
        }
        case NodeKind::EnumDecl: {
            const auto &node = static_cast<const EnumDecl &>(declaration);
            require(!node.name.empty(), node.range, "EnumDecl is missing name");
            for (const auto &param : node.type_params) {
                require(param != nullptr, node.range, "EnumDecl.type_params contains null");
                if (param) {
                    require(!param->name.empty(), param->range, "TypeParamSyntax is missing name");
                    for (const auto &bound : param->bounds) {
                        require(
                            bound != nullptr, param->range, "TypeParamSyntax.bounds contains null");
                        if (bound) {
                            validate_type(*bound);
                        }
                    }
                }
            }
            for (const auto &variant : node.variants) {
                require(variant != nullptr, node.range, "EnumDecl.variants contains null");
                if (variant) {
                    require(!variant->name.empty(),
                            variant->range,
                            "EnumVariantDeclSyntax is missing name");
                    require(variant->payload.empty() || variant->named_fields.empty(),
                            variant->range,
                            "EnumVariantDeclSyntax cannot carry both tuple and struct payloads");
                    // P1 (ADT): validate the optional positional payload types.
                    for (const auto &payload_type : variant->payload) {
                        require(payload_type != nullptr,
                                variant->range,
                                "EnumVariantDeclSyntax.payload contains null");
                        if (payload_type) {
                            validate_type(*payload_type);
                        }
                    }
                    // RFC d-1 POC: validate struct (named-field) payload.
                    for (const auto &field : variant->named_fields) {
                        require(field != nullptr,
                                variant->range,
                                "EnumVariantDeclSyntax.named_fields contains null");
                        if (field) {
                            require(!field->name.empty(),
                                    field->range,
                                    "EnumVariantFieldSyntax is missing name");
                            require(field->type != nullptr,
                                    field->range,
                                    "EnumVariantFieldSyntax is missing type");
                            if (field->type) {
                                validate_type(*field->type);
                            }
                            if (field->default_value) {
                                validate_expr(*field->default_value);
                            }
                        }
                    }
                }
            }
            break;
        }
        case NodeKind::CapabilityDecl: {
            const auto &node = static_cast<const CapabilityDecl &>(declaration);
            require(!node.name.empty(), node.range, "CapabilityDecl is missing name");
            validate_params(node.params, node.range);
            require(
                node.return_type != nullptr, node.range, "CapabilityDecl is missing return_type");
            if (node.return_type) {
                validate_type(*node.return_type);
            }
            if (node.effect) {
                validate_capability_effect(*node.effect);
            }
            break;
        }
        case NodeKind::PredicateDecl: {
            const auto &node = static_cast<const PredicateDecl &>(declaration);
            require(!node.name.empty(), node.range, "PredicateDecl is missing name");
            validate_params(node.params, node.range);
            break;
        }
        case NodeKind::AgentDecl: {
            const auto &node = static_cast<const AgentDecl &>(declaration);
            require(!node.name.empty(), node.range, "AgentDecl is missing name");
            require(node.input_type != nullptr, node.range, "AgentDecl is missing input_type");
            require(node.context_type != nullptr, node.range, "AgentDecl is missing context_type");
            require(node.output_type != nullptr, node.range, "AgentDecl is missing output_type");
            if (node.input_type) {
                validate_type(*node.input_type);
            }
            if (node.context_type) {
                validate_type(*node.context_type);
            }
            if (node.output_type) {
                validate_type(*node.output_type);
            }
            if (node.quota) {
                for (const auto &item : node.quota->items) {
                    require(
                        item != nullptr, node.quota->range, "AgentQuotaSyntax.items contains null");
                    if (!item) {
                        continue;
                    }
                    const auto has_integer = item->integer_value != nullptr;
                    const auto has_duration = item->duration_value != nullptr;
                    require(has_integer != has_duration,
                            item->range,
                            "AgentQuotaItemSyntax must have exactly one value payload");
                }
            }
            for (const auto &transition : node.transitions) {
                require(transition != nullptr, node.range, "AgentDecl.transitions contains null");
                if (transition) {
                    require(!transition->from_state.empty(),
                            transition->range,
                            "TransitionSyntax is missing from_state");
                    require(!transition->to_state.empty(),
                            transition->range,
                            "TransitionSyntax is missing to_state");
                }
            }
            break;
        }
        case NodeKind::ContractDecl: {
            const auto &node = static_cast<const ContractDecl &>(declaration);
            validate_qualified_name(node.target.get(), node.range, "ContractDecl.target");
            for (const auto &clause : node.clauses) {
                require(clause != nullptr, node.range, "ContractDecl.clauses contains null");
                if (!clause) {
                    continue;
                }
                const auto has_expr = clause->expr != nullptr;
                const auto has_temporal = clause->temporal_expr != nullptr;
                // Wildcard decreases carries no expression payload; that's a
                // deliberate grammatical choice ("decreases: *;").
                const bool allows_no_payload =
                    (clause->kind == ContractClauseKind::Decreases && clause->is_wildcard);
                require(allows_no_payload || has_expr != has_temporal,
                        clause->range,
                        "ContractClauseSyntax must have exactly one expression payload");
                if (clause->expr) {
                    validate_expr(*clause->expr);
                }
                if (clause->temporal_expr) {
                    validate_temporal(*clause->temporal_expr);
                }
            }
            break;
        }
        case NodeKind::FlowDecl: {
            const auto &node = static_cast<const FlowDecl &>(declaration);
            validate_qualified_name(node.target.get(), node.range, "FlowDecl.target");
            for (const auto &handler : node.state_handlers) {
                require(handler != nullptr, node.range, "FlowDecl.state_handlers contains null");
                if (!handler) {
                    continue;
                }
                require(!handler->state_name.empty(),
                        handler->range,
                        "StateHandlerSyntax is missing state_name");
                require(
                    handler->body != nullptr, handler->range, "StateHandlerSyntax is missing body");
                if (handler->body) {
                    validate_block(*handler->body);
                }
            }
            break;
        }
        case NodeKind::WorkflowDecl: {
            const auto &node = static_cast<const WorkflowDecl &>(declaration);
            require(!node.name.empty(), node.range, "WorkflowDecl is missing name");
            require(node.input_type != nullptr, node.range, "WorkflowDecl is missing input_type");
            require(node.output_type != nullptr, node.range, "WorkflowDecl is missing output_type");
            require(
                node.return_value != nullptr, node.range, "WorkflowDecl is missing return_value");
            if (node.input_type) {
                validate_type(*node.input_type);
            }
            if (node.output_type) {
                validate_type(*node.output_type);
            }
            for (const auto &workflow_node : node.nodes) {
                require(workflow_node != nullptr, node.range, "WorkflowDecl.nodes contains null");
                if (!workflow_node) {
                    continue;
                }
                require(!workflow_node->name.empty(),
                        workflow_node->range,
                        "WorkflowNodeDeclSyntax is missing name");
                validate_qualified_name(workflow_node->target.get(),
                                        workflow_node->range,
                                        "WorkflowNodeDeclSyntax.target");
                require(workflow_node->input != nullptr,
                        workflow_node->range,
                        "WorkflowNodeDeclSyntax is missing input");
                if (workflow_node->input) {
                    validate_expr(*workflow_node->input);
                }
            }
            for (const auto &formula : node.safety) {
                require(formula != nullptr, node.range, "WorkflowDecl.safety contains null");
                if (formula) {
                    validate_temporal(*formula);
                }
            }
            for (const auto &formula : node.liveness) {
                require(formula != nullptr, node.range, "WorkflowDecl.liveness contains null");
                if (formula) {
                    validate_temporal(*formula);
                }
            }
            if (node.return_value) {
                validate_expr(*node.return_value);
            }
            break;
        }
        case NodeKind::FnDecl: {
            const auto &node = static_cast<const FnDecl &>(declaration);
            require(!node.name.empty(), node.range, "FnDecl is missing name");
            for (const auto &type_param : node.type_params) {
                require(type_param != nullptr, node.range, "FnDecl.type_params contains null");
                if (type_param) {
                    require(!type_param->name.empty(),
                            type_param->range,
                            "TypeParamSyntax is missing name");
                    for (const auto &bound : type_param->bounds) {
                        if (bound) {
                            validate_type(*bound);
                        }
                    }
                }
            }
            for (const auto &param : node.params) {
                require(param != nullptr, node.range, "FnDecl.params contains null");
                if (param) {
                    require(!param->name.empty(), param->range, "ParamDeclSyntax is missing name");
                    // Bare `self` / `mut self` params carry no explicit type at the
                    // grammar level; the receiver type is inferred from the enclosing
                    // impl/trait target during semantic analysis (P3b). Other
                    // parameters must carry an explicit type.
                    if (!param->is_self) {
                        require(param->type != nullptr,
                                param->range,
                                "ParamDeclSyntax is missing type");
                    }
                    if (param->type) {
                        validate_type(*param->type);
                    }
                }
            }
            if (node.return_type) {
                validate_type(*node.return_type);
            }
            if (node.effect_clause) {
                validate_effect_clause(*node.effect_clause);
            }
            if (node.where_clause) {
                validate_where_clause(*node.where_clause);
            }
            if (node.body) {
                validate_block(*node.body);
            }
            break;
        }
        case NodeKind::TraitDecl: {
            const auto &node = static_cast<const TraitDecl &>(declaration);
            require(!node.name.empty(), node.range, "TraitDecl is missing name");
            for (const auto &type_param : node.type_params) {
                require(type_param != nullptr, node.range, "TraitDecl.type_params contains null");
                if (type_param) {
                    require(!type_param->name.empty(),
                            type_param->range,
                            "TypeParamSyntax is missing name");
                    for (const auto &bound : type_param->bounds) {
                        if (bound) {
                            validate_type(*bound);
                        }
                    }
                }
            }
            for (const auto &super_trait : node.super_traits) {
                require(super_trait != nullptr, node.range, "TraitDecl.super_traits contains null");
                if (super_trait) {
                    validate_type(*super_trait);
                }
            }
            for (const auto &item : node.items) {
                require(item != nullptr, node.range, "TraitDecl.items contains null");
                if (!item) {
                    continue;
                }
                require(!item->name.empty(), item->range, "TraitItemSyntax is missing name");
                if (item->kind == TraitItemKind::Fn) {
                    for (const auto &param : item->params) {
                        require(
                            param != nullptr, item->range, "trait fn item params contains null");
                        if (param) {
                            require(!param->name.empty(),
                                    param->range,
                                    "ParamDeclSyntax is missing name");
                            // Bare `self` / `mut self` params carry no explicit type at the
                            // grammar level; defer to P3b for receiver inference.
                            if (!param->is_self) {
                                require(param->type != nullptr,
                                        param->range,
                                        "ParamDeclSyntax is missing type");
                            }
                            if (param->type) {
                                validate_type(*param->type);
                            }
                        }
                    }
                    if (item->return_type) {
                        validate_type(*item->return_type);
                    }
                    if (item->effect_clause) {
                        validate_effect_clause(*item->effect_clause);
                    }
                    if (item->where_clause) {
                        validate_where_clause(*item->where_clause);
                    }
                } else if (item->kind == TraitItemKind::AssocType) {
                    require(item->assoc_type != nullptr,
                            item->range,
                            "TraitItemSyntax assoc type is missing");
                    if (item->assoc_type) {
                        require(!item->assoc_type->name.empty(),
                                item->assoc_type->range,
                                "assoc type is missing name");
                        for (const auto &bound : item->assoc_type->bounds) {
                            if (bound) {
                                validate_type(*bound);
                            }
                        }
                        if (item->assoc_type->default_type) {
                            validate_type(*item->assoc_type->default_type);
                        }
                    }
                } else if (item->kind == TraitItemKind::AssocConst) {
                    require(item->assoc_const != nullptr,
                            item->range,
                            "TraitItemSyntax assoc const is missing");
                    if (item->assoc_const) {
                        require(!item->assoc_const->name.empty(),
                                item->assoc_const->range,
                                "assoc const is missing name");
                        require(item->assoc_const->type != nullptr,
                                item->assoc_const->range,
                                "assoc const is missing type");
                        if (item->assoc_const->type) {
                            validate_type(*item->assoc_const->type);
                        }
                        if (item->assoc_const->default_value) {
                            validate_expr(*item->assoc_const->default_value);
                        }
                    }
                }
            }
            break;
        }
        case NodeKind::ImplDecl: {
            const auto &node = static_cast<const ImplDecl &>(declaration);
            require(node.target_type != nullptr, node.range, "ImplDecl is missing target_type");
            if (node.target_type) {
                validate_type(*node.target_type);
            }
            for (const auto &type_param : node.type_params) {
                require(type_param != nullptr, node.range, "ImplDecl.type_params contains null");
                if (type_param) {
                    require(!type_param->name.empty(),
                            type_param->range,
                            "TypeParamSyntax is missing name");
                    for (const auto &bound : type_param->bounds) {
                        if (bound) {
                            validate_type(*bound);
                        }
                    }
                }
            }
            if (node.trait_ref) {
                validate_type(*node.trait_ref);
            }
            if (node.where_clause) {
                validate_where_clause(*node.where_clause);
            }
            for (const auto &method : node.methods) {
                require(method != nullptr, node.range, "ImplDecl.methods contains null");
                if (method) {
                    // Body is optional for @builtin facade impl methods: the
                    // compiler lowers them directly to the named builtin hook
                    // (matching module-level `@builtin fn name(...);`).
                    require(method->body != nullptr || method->builtin_name.has_value(),
                            method->range,
                            "impl method definition must carry a body (unless @builtin prototype)");
                    require(!method->name.empty(), method->range, "FnDecl is missing name");
                    if (method->return_type) {
                        validate_type(*method->return_type);
                    }
                    if (method->body) {
                        validate_block(*method->body);
                    }
                }
            }
            for (const auto &assoc : node.assoc_items) {
                require(assoc != nullptr, node.range, "ImplDecl.assoc_items contains null");
                if (assoc) {
                    require(!assoc->name.empty(), assoc->range, "assoc item is missing name");
                    require(assoc->type != nullptr, assoc->range, "assoc item is missing type");
                    if (assoc->type) {
                        validate_type(*assoc->type);
                    }
                }
            }
            for (const auto &assoc_const : node.const_items) {
                require(assoc_const != nullptr,
                        node.range,
                        "ImplDecl.const_items contains null");
                if (assoc_const) {
                    require(!assoc_const->name.empty(),
                            assoc_const->range,
                            "impl assoc const is missing name");
                    require(assoc_const->type != nullptr,
                            assoc_const->range,
                            "impl assoc const is missing type");
                    if (assoc_const->type) {
                        validate_type(*assoc_const->type);
                    }
                    require(assoc_const->value != nullptr,
                            assoc_const->range,
                            "impl assoc const is missing value");
                    if (assoc_const->value) {
                        validate_expr(*assoc_const->value);
                    }
                }
            }
            break;
        }
        }
    }

    /// Validate an effect clause (P2, RFC §2; P4a RFC §3.1 decreases measure).
    void validate_effect_clause(const EffectClauseSyntax &clause) {
        if (clause.kind == EffectClauseKind::Capability) {
            for (const auto &capability : clause.capabilities) {
                require(capability != nullptr,
                        clause.range,
                        "EffectClauseSyntax.capabilities contains null");
                if (capability) {
                    validate_qualified_name(capability.get(), clause.range, "effect capability");
                }
            }
        }
        if (clause.decreases_expr) {
            validate_expr(*clause.decreases_expr);
        }
    }

    /// Validate a where-clause (P2, RFC §6).
    void validate_where_clause(const WhereClauseSyntax &clause) {
        for (const auto &constraint : clause.constraints) {
            require(
                constraint != nullptr, clause.range, "WhereClauseSyntax.constraints contains null");
            if (!constraint) {
                continue;
            }
            require(constraint->subject != nullptr,
                    constraint->range,
                    "WhereConstraintSyntax is missing subject");
            if (constraint->subject) {
                validate_type(*constraint->subject);
            }
            for (const auto &argument : constraint->arguments) {
                if (argument) {
                    validate_type(*argument);
                }
            }
            for (const auto &bound : constraint->bounds) {
                if (bound) {
                    validate_type(*bound);
                }
            }
        }
    }
};

} // namespace

std::string_view to_string(NodeKind kind) noexcept {
    switch (kind) {
    case NodeKind::Program:
        return "Program";
    case NodeKind::ModuleDecl:
        return "ModuleDecl";
    case NodeKind::ImportDecl:
        return "ImportDecl";
    case NodeKind::ConstDecl:
        return "ConstDecl";
    case NodeKind::TypeAliasDecl:
        return "TypeAliasDecl";
    case NodeKind::StructDecl:
        return "StructDecl";
    case NodeKind::EnumDecl:
        return "EnumDecl";
    case NodeKind::CapabilityDecl:
        return "CapabilityDecl";
    case NodeKind::PredicateDecl:
        return "PredicateDecl";
    case NodeKind::AgentDecl:
        return "AgentDecl";
    case NodeKind::ContractDecl:
        return "ContractDecl";
    case NodeKind::FlowDecl:
        return "FlowDecl";
    case NodeKind::WorkflowDecl:
        return "WorkflowDecl";
    case NodeKind::FnDecl:
        return "FnDecl";
    case NodeKind::TraitDecl:
        return "TraitDecl";
    case NodeKind::ImplDecl:
        return "ImplDecl";
    }

    return "Unknown";
}

std::string_view to_string(ImplItemKind kind) noexcept {
    switch (kind) {
    case ImplItemKind::Fn:
        return "Fn";
    case ImplItemKind::AssocType:
        return "AssocType";
    case ImplItemKind::AssocConst:
        return "AssocConst";
    }

    return "Unknown";
}

std::string_view to_string(ContractClauseKind kind) noexcept {
    switch (kind) {
    case ContractClauseKind::Requires:
        return "requires";
    case ContractClauseKind::Ensures:
        return "ensures";
    case ContractClauseKind::Invariant:
        return "invariant";
    case ContractClauseKind::Forbid:
        return "forbid";
    case ContractClauseKind::Decreases:
        return "decreases";
    }

    return "unknown";
}

std::string_view to_string(CapabilityEffectKind kind) noexcept {
    switch (kind) {
    case CapabilityEffectKind::Unknown:
        return "unknown";
    case CapabilityEffectKind::Read:
        return "read";
    case CapabilityEffectKind::ExternalSideEffect:
        return "external_side_effect";
    case CapabilityEffectKind::DurableWrite:
        return "durable_write";
    case CapabilityEffectKind::FinancialWrite:
        return "financial_write";
    }

    return "unknown";
}

std::string_view to_string(CapabilityReceiptMode mode) noexcept {
    switch (mode) {
    case CapabilityReceiptMode::None:
        return "none";
    case CapabilityReceiptMode::Optional:
        return "optional";
    case CapabilityReceiptMode::Required:
        return "required";
    }

    return "none";
}

std::string_view to_string(CapabilityRetryMode mode) noexcept {
    switch (mode) {
    case CapabilityRetryMode::Unsafe:
        return "unsafe";
    case CapabilityRetryMode::SafeIfIdempotent:
        return "safe_if_idempotent";
    case CapabilityRetryMode::Safe:
        return "safe";
    }

    return "unsafe";
}

std::string QualifiedName::spelling() const {
    return join_segments(segments);
}

std::string PathSyntax::spelling() const {
    std::string text = root_name;

    for (const auto &member : members) {
        text += ".";
        text += member;
    }

    return text;
}

namespace {

struct TypeSyntaxSpellingVisitor {
    std::string operator()(const UnitType &) const {
        return "Unit";
    }
    std::string operator()(const BoolType &) const {
        return "Bool";
    }
    std::string operator()(const IntType &) const {
        return "Int";
    }
    std::string operator()(const FloatType &) const {
        return "Float";
    }
    std::string operator()(const StringType &) const {
        return "String";
    }
    std::string operator()(const BoundedStringType &t) const {
        std::ostringstream builder;
        builder << "String(" << t.min_length << ", " << t.max_length << ")";
        return builder.str();
    }
    std::string operator()(const UuidType &) const {
        return "UUID";
    }
    std::string operator()(const TimestampType &) const {
        return "Timestamp";
    }
    std::string operator()(const DurationType &) const {
        return "Duration";
    }
    std::string operator()(const DecimalType &t) const {
        std::ostringstream builder;
        builder << "Decimal(" << int(t.scale) << ")";
        return builder.str();
    }
    std::string operator()(const NamedType &t) const {
        std::string result = t.name->spelling();
        if (!t.type_args.empty()) {
            result.push_back('<');
            for (std::size_t i = 0; i < t.type_args.size(); ++i) {
                if (i != 0) {
                    result.append(", ");
                }
                result.append(t.type_args[i]->spelling());
            }
            result.push_back('>');
        }
        return result;
    }
    std::string operator()(const FnType &t) const {
        std::ostringstream builder;
        builder << "Fn(";
        for (std::size_t i = 0; i < t.params.size(); ++i) {
            if (i > 0) {
                builder << ", ";
            }
            builder << t.params[i]->spelling();
        }
        builder << ")";
        if (t.return_type) {
            builder << " -> " << t.return_type->spelling();
        }
        if (t.has_effect_clause) {
            builder << " effect ";
            switch (t.effect_kind) {
            case EffectClauseKind::Pure:
                builder << "Pure";
                break;
            case EffectClauseKind::Nondet:
                builder << "Nondet";
                break;
            case EffectClauseKind::Capability:
                for (std::size_t i = 0; i < t.effect_capabilities.size(); ++i) {
                    if (i > 0) {
                        builder << ", ";
                    }
                    builder << t.effect_capabilities[i]->spelling();
                }
                break;
            }
        }
        return builder.str();
    }
    std::string operator()(const AppType &t) const {
        std::ostringstream builder;
        builder << t.name->spelling() << "<";
        for (std::size_t i = 0; i < t.arguments.size(); ++i) {
            if (i > 0) {
                builder << ", ";
            }
            builder << t.arguments[i]->spelling();
        }
        builder << ">";
        return builder.str();
    }
};

} // namespace

std::string TypeSyntax::spelling() const {
    return std::visit(TypeSyntaxSpellingVisitor{}, node);
}

std::vector<AstInvariantViolation> validate_program_invariants(const Program &program) {
    return AstInvariantValidator{}.validate(program);
}

Node::Node(NodeKind kind, ahfl::SourceRange range) : kind(kind), range(range) {}

Decl::Decl(NodeKind kind, ahfl::SourceRange range) : Node(kind, range) {}

Program::Program(std::string source_name, ahfl::SourceRange range)
    : Node(NodeKind::Program, range), source_name(std::move(source_name)) {}

void Program::accept(Visitor &visitor) {
    visitor.visit(*this);
}

ModuleDecl::ModuleDecl(Owned<QualifiedName> name, ahfl::SourceRange range)
    : Decl(NodeKind::ModuleDecl, range), name(std::move(name)) {}

void ModuleDecl::accept(Visitor &visitor) {
    visitor.visit(*this);
}

std::string ModuleDecl::headline() const {
    return with_name("module ", name->spelling());
}

ImportDecl::ImportDecl(Owned<QualifiedName> path, std::string alias, ahfl::SourceRange range)
    : Decl(NodeKind::ImportDecl, range), path(std::move(path)), alias(std::move(alias)) {}

void ImportDecl::accept(Visitor &visitor) {
    visitor.visit(*this);
}

std::string ImportDecl::headline() const {
    if (alias.empty()) {
        return with_name("import ", path->spelling());
    }

    return "import " + path->spelling() + " as " + alias;
}

ConstDecl::ConstDecl(std::string name, ahfl::SourceRange range)
    : Decl(NodeKind::ConstDecl, range), name(std::move(name)) {}

void ConstDecl::accept(Visitor &visitor) {
    visitor.visit(*this);
}

std::string ConstDecl::headline() const {
    if (!type) {
        return with_name("const ", name);
    }

    return "const " + name + " : " + type->spelling();
}

TypeAliasDecl::TypeAliasDecl(std::string name, ahfl::SourceRange range)
    : Decl(NodeKind::TypeAliasDecl, range), name(std::move(name)) {}

void TypeAliasDecl::accept(Visitor &visitor) {
    visitor.visit(*this);
}

std::string TypeAliasDecl::headline() const {
    if (!aliased_type) {
        return with_name("type ", name);
    }

    std::ostringstream builder;
    builder << "type " << name;
    if (!type_params.empty()) {
        builder << "<" << type_params.size() << " type param";
        builder << (type_params.size() == 1 ? "" : "s") << ">";
    }
    builder << " = " << aliased_type->spelling();
    return builder.str();
}

StructDecl::StructDecl(std::string name, ahfl::SourceRange range)
    : Decl(NodeKind::StructDecl, range), name(std::move(name)) {}

void StructDecl::accept(Visitor &visitor) {
    visitor.visit(*this);
}

std::string StructDecl::headline() const {
    std::ostringstream builder;
    builder << "struct " << name;
    if (!type_params.empty()) {
        builder << "<" << type_params.size() << " type param";
        builder << (type_params.size() == 1 ? "" : "s") << ">";
    }
    builder << " (" << fields.size() << " field" << (fields.size() == 1 ? "" : "s") << ")";
    return builder.str();
}

EnumDecl::EnumDecl(std::string name, ahfl::SourceRange range)
    : Decl(NodeKind::EnumDecl, range), name(std::move(name)) {}

void EnumDecl::accept(Visitor &visitor) {
    visitor.visit(*this);
}

std::string EnumDecl::headline() const {
    std::ostringstream builder;
    builder << "enum " << name;
    if (!type_params.empty()) {
        builder << "<" << type_params.size() << " type param";
        builder << (type_params.size() == 1 ? "" : "s") << ">";
    }
    builder << " (" << variants.size() << " variant" << (variants.size() == 1 ? "" : "s") << ")";
    return builder.str();
}

CapabilityDecl::CapabilityDecl(std::string name, ahfl::SourceRange range)
    : Decl(NodeKind::CapabilityDecl, range), name(std::move(name)) {}

void CapabilityDecl::accept(Visitor &visitor) {
    visitor.visit(*this);
}

std::string CapabilityDecl::headline() const {
    std::ostringstream builder;
    builder << "capability " << name << " (" << params.size() << " param";
    builder << (params.size() == 1 ? "" : "s") << ")";

    if (return_type) {
        builder << " -> " << return_type->spelling();
    }

    return builder.str();
}

PredicateDecl::PredicateDecl(std::string name, ahfl::SourceRange range)
    : Decl(NodeKind::PredicateDecl, range), name(std::move(name)) {}

void PredicateDecl::accept(Visitor &visitor) {
    visitor.visit(*this);
}

std::string PredicateDecl::headline() const {
    return with_count("predicate " + name, params.size(), "param");
}

AgentDecl::AgentDecl(std::string name, ahfl::SourceRange range)
    : Decl(NodeKind::AgentDecl, range), name(std::move(name)) {}

void AgentDecl::accept(Visitor &visitor) {
    visitor.visit(*this);
}

std::string AgentDecl::headline() const {
    std::ostringstream builder;
    builder << "agent " << name << " (" << states.size() << " states, " << transitions.size()
            << " transitions)";
    return builder.str();
}

ContractDecl::ContractDecl(Owned<QualifiedName> target, ahfl::SourceRange range)
    : Decl(NodeKind::ContractDecl, range), target(std::move(target)) {}

void ContractDecl::accept(Visitor &visitor) {
    visitor.visit(*this);
}

std::string ContractDecl::headline() const {
    return with_count("contract for " + target->spelling(), clauses.size(), "clause");
}

FlowDecl::FlowDecl(Owned<QualifiedName> target, ahfl::SourceRange range)
    : Decl(NodeKind::FlowDecl, range), target(std::move(target)) {}

void FlowDecl::accept(Visitor &visitor) {
    visitor.visit(*this);
}

std::string FlowDecl::headline() const {
    return with_count("flow for " + target->spelling(), state_handlers.size(), "handler");
}

WorkflowDecl::WorkflowDecl(std::string name, ahfl::SourceRange range)
    : Decl(NodeKind::WorkflowDecl, range), name(std::move(name)) {}

void WorkflowDecl::accept(Visitor &visitor) {
    visitor.visit(*this);
}

std::string WorkflowDecl::headline() const {
    return with_count("workflow " + name, nodes.size(), "node");
}

FnDecl::FnDecl(std::string name, ahfl::SourceRange range)
    : Decl(NodeKind::FnDecl, range), name(std::move(name)) {}

void FnDecl::accept(Visitor &visitor) {
    visitor.visit(*this);
}

std::string FnDecl::headline() const {
    std::ostringstream builder;
    builder << "fn " << name;
    if (!type_params.empty()) {
        builder << "<" << type_params.size() << " type param";
        builder << (type_params.size() == 1 ? "" : "s") << ">";
    }
    builder << " (" << params.size() << " param" << (params.size() == 1 ? "" : "s") << ")";
    if (return_type) {
        builder << " -> " << return_type->spelling();
    }
    if (!body) {
        builder << " ;";
    }
    return builder.str();
}

void RecursiveVisitor::visit(FnDecl &) {}

TraitDecl::TraitDecl(std::string name, ahfl::SourceRange range)
    : Decl(NodeKind::TraitDecl, range), name(std::move(name)) {}

void TraitDecl::accept(Visitor &visitor) {
    visitor.visit(*this);
}

std::string TraitDecl::headline() const {
    std::ostringstream builder;
    builder << "trait " << name;
    if (!type_params.empty()) {
        builder << "<" << type_params.size() << " type param";
        builder << (type_params.size() == 1 ? "" : "s") << ">";
    }
    builder << " (" << items.size() << " item" << (items.size() == 1 ? "" : "s") << ")";
    return builder.str();
}

ImplDecl::ImplDecl(ahfl::SourceRange range) : Decl(NodeKind::ImplDecl, range) {}

void ImplDecl::accept(Visitor &visitor) {
    visitor.visit(*this);
}

std::string ImplDecl::headline() const {
    std::ostringstream builder;
    builder << "impl";
    if (!type_params.empty()) {
        builder << "<" << type_params.size() << " type param";
        builder << (type_params.size() == 1 ? "" : "s") << ">";
    }
    if (trait_ref) {
        builder << " " << trait_ref->spelling() << " for";
    }
    if (target_type) {
        builder << " " << target_type->spelling();
    }
    builder << " (" << methods.size() << " method" << (methods.size() == 1 ? "" : "s");
    if (!assoc_items.empty()) {
        builder << ", " << assoc_items.size() << " assoc type"
                << (assoc_items.size() == 1 ? "" : "s");
    }
    if (!const_items.empty()) {
        builder << ", " << const_items.size() << " assoc const"
                << (const_items.size() == 1 ? "" : "s");
    }
    builder << ")";
    return builder.str();
}

void RecursiveVisitor::visit(TraitDecl &) {}
void RecursiveVisitor::visit(ImplDecl &) {}

std::string_view to_string(EffectClauseKind kind) noexcept {
    switch (kind) {
    case EffectClauseKind::Pure:
        return "Pure";
    case EffectClauseKind::Nondet:
        return "Nondet";
    case EffectClauseKind::Capability:
        return "Capability";
    }
    return "Unknown";
}

void RecursiveVisitor::visit(Program &node) {
    for (auto &declaration : node.declarations) {
        declaration->accept(*this);
    }
}

void RecursiveVisitor::visit(ModuleDecl &) {}

void RecursiveVisitor::visit(ImportDecl &) {}

void RecursiveVisitor::visit(ConstDecl &) {}

void RecursiveVisitor::visit(TypeAliasDecl &) {}

void RecursiveVisitor::visit(StructDecl &) {}

void RecursiveVisitor::visit(EnumDecl &) {}

void RecursiveVisitor::visit(CapabilityDecl &) {}

void RecursiveVisitor::visit(PredicateDecl &) {}

void RecursiveVisitor::visit(AgentDecl &) {}

void RecursiveVisitor::visit(ContractDecl &) {}

void RecursiveVisitor::visit(FlowDecl &) {}

void RecursiveVisitor::visit(WorkflowDecl &) {}

} // namespace ahfl::ast

namespace ahfl {

// ---------------------------------------------------------------------------
// DecreasesClauseSyntax – standalone canonicaliser (R-09: no DeclKind dispatch).
// ---------------------------------------------------------------------------

namespace {

std::string expr_spelling_for_dedup(const ast::ExprSyntax &expr) {
    if (!expr.text.empty()) {
        return expr.text;
    }

    // Fallback spelling: use the node kind index so two expressions built the
    // same way produce the same key.  We don't pull in the full AST printer
    // here to keep the desugar pass cheap.
    return std::to_string(static_cast<std::uint32_t>(ast::expr_syntax_kind(expr)));
}

} // namespace

bool desugar_decreases_clause(ast::DecreasesClauseSyntax &clause) {
    bool changed = false;

    if (clause.is_wildcard) {
        // Wildcard form is canonical on its own.  We still drop any nullptr
        // / stray entries in `terms` so downstream consumers don't have to
        // defensive-check around an "unused" vector.
        if (!clause.terms.empty()) {
            clause.terms.clear();
            changed = true;
        }
        return changed;
    }

    // Pass 1: drop nullptr entries (parser or programmatic builder corner
    // case).  We walk in place; size can only shrink so an out-of-place
    // rebuild is both simpler and cache-friendlier for the typical sizes
    // (1..4 terms).
    {
        std::vector<ahfl::Owned<ast::ExprSyntax>> compact;
        compact.reserve(clause.terms.size());
        for (auto &term : clause.terms) {
            if (term) {
                compact.push_back(std::move(term));
            } else {
                changed = true;
            }
        }
        clause.terms.swap(compact);
    }

    // Pass 2: drop *consecutive* duplicate spellings.  We only collapse
    // consecutive duplicates because the tuple is lexicographically ordered
    // by definition, and non-adjacent duplicates (e.g. x, y, x) are
    // meaningful: they represent a genuine two-step measure.
    {
        std::vector<ahfl::Owned<ast::ExprSyntax>> dedup;
        dedup.reserve(clause.terms.size());
        std::string last_spelling;
        for (auto &term : clause.terms) {
            const auto spelling = expr_spelling_for_dedup(*term);
            if (!dedup.empty() && spelling == last_spelling) {
                changed = true;
                continue;
            }
            last_spelling = spelling;
            dedup.push_back(std::move(term));
        }
        clause.terms.swap(dedup);
    }

    return changed;
}

} // namespace ahfl
