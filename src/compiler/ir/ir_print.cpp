#include "ahfl/compiler/ir/lowering.hpp"

#include "ahfl/base/support/overloaded.hpp"
#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/ir/analysis.hpp"
#include "ahfl/compiler/ir/identity.hpp"
#include "base/support/string_utils.hpp"

#include <cctype>
#include <cstddef>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace ahfl {

namespace {

[[nodiscard]] std::string_view capability_effect_kind_name(ir::CapabilityEffectKind kind) {
    switch (kind) {
    case ir::CapabilityEffectKind::Unknown:
        return "unknown";
    case ir::CapabilityEffectKind::Read:
        return "read";
    case ir::CapabilityEffectKind::ExternalSideEffect:
        return "external_side_effect";
    case ir::CapabilityEffectKind::DurableWrite:
        return "durable_write";
    case ir::CapabilityEffectKind::FinancialWrite:
        return "financial_write";
    }

    return "unknown";
}

[[nodiscard]] std::string_view capability_receipt_mode_name(ir::CapabilityReceiptMode mode) {
    switch (mode) {
    case ir::CapabilityReceiptMode::None:
        return "none";
    case ir::CapabilityReceiptMode::Optional:
        return "optional";
    case ir::CapabilityReceiptMode::Required:
        return "required";
    }

    return "none";
}

[[nodiscard]] std::string_view capability_retry_mode_name(ir::CapabilityRetryMode mode) {
    switch (mode) {
    case ir::CapabilityRetryMode::Unsafe:
        return "unsafe";
    case ir::CapabilityRetryMode::SafeIfIdempotent:
        return "safe_if_idempotent";
    case ir::CapabilityRetryMode::Safe:
        return "safe";
    }

    return "unsafe";
}

[[nodiscard]] std::string type_name(const ir::TypeRef &ref) {
    return std::string(ir::type_display_name(ref, "Any"));
}

[[nodiscard]] std::string symbol_name(const ir::SymbolRef &ref) {
    return std::string(ir::symbol_canonical_name(ref, "<unresolved-symbol>"));
}

[[nodiscard]] std::vector<std::string> symbol_names(const std::vector<ir::SymbolRef> &refs) {
    std::vector<std::string> names;
    names.reserve(refs.size());
    for (const auto &ref : refs) {
        const auto name = ir::symbol_canonical_name(ref);
        if (!name.empty()) {
            names.emplace_back(name);
        }
    }
    return names;
}

[[nodiscard]] std::string expr_binary_op_name(ir::ExprBinaryOp op) {
    switch (op) {
    case ir::ExprBinaryOp::Implies:
        return "=>";
    case ir::ExprBinaryOp::Or:
        return "||";
    case ir::ExprBinaryOp::And:
        return "&&";
    case ir::ExprBinaryOp::Equal:
        return "==";
    case ir::ExprBinaryOp::NotEqual:
        return "!=";
    case ir::ExprBinaryOp::Less:
        return "<";
    case ir::ExprBinaryOp::LessEqual:
        return "<=";
    case ir::ExprBinaryOp::Greater:
        return ">";
    case ir::ExprBinaryOp::GreaterEqual:
        return ">=";
    case ir::ExprBinaryOp::Add:
        return "+";
    case ir::ExprBinaryOp::Subtract:
        return "-";
    case ir::ExprBinaryOp::Multiply:
        return "*";
    case ir::ExprBinaryOp::Divide:
        return "/";
    case ir::ExprBinaryOp::Modulo:
        return "%";
    }

    return "<invalid-binary-op>";
}

[[nodiscard]] std::string expr_unary_op_name(ir::ExprUnaryOp op) {
    switch (op) {
    case ir::ExprUnaryOp::Not:
        return "!";
    case ir::ExprUnaryOp::Negate:
        return "-";
    case ir::ExprUnaryOp::Positive:
        return "+";
    }

    return "<invalid-unary-op>";
}

[[nodiscard]] std::string temporal_binary_op_name(ir::TemporalBinaryOp op) {
    switch (op) {
    case ir::TemporalBinaryOp::Implies:
        return "=>";
    case ir::TemporalBinaryOp::Or:
        return "||";
    case ir::TemporalBinaryOp::And:
        return "&&";
    case ir::TemporalBinaryOp::Until:
        return "U";
    }

    return "<invalid-temporal-binary-op>";
}

[[nodiscard]] std::string temporal_unary_op_name(ir::TemporalUnaryOp op) {
    switch (op) {
    case ir::TemporalUnaryOp::Always:
        return "G";
    case ir::TemporalUnaryOp::Eventually:
        return "F";
    case ir::TemporalUnaryOp::Next:
        return "X";
    case ir::TemporalUnaryOp::Not:
        return "!";
    }

    return "<invalid-temporal-unary-op>";
}

[[nodiscard]] std::string contract_clause_name(ir::ContractClauseKind kind) {
    switch (kind) {
    case ir::ContractClauseKind::Requires:
        return "requires";
    case ir::ContractClauseKind::Ensures:
        return "ensures";
    case ir::ContractClauseKind::Invariant:
        return "invariant";
    case ir::ContractClauseKind::Forbid:
        return "forbid";
    case ir::ContractClauseKind::Decreases:
        return "decreases";
    }

    return "<invalid-contract-clause>";
}

[[nodiscard]] std::string workflow_value_source_kind_name(ir::WorkflowValueSourceKind kind) {
    switch (kind) {
    case ir::WorkflowValueSourceKind::WorkflowInput:
        return "workflow_input";
    case ir::WorkflowValueSourceKind::WorkflowNodeOutput:
        return "workflow_node_output";
    }

    return "invalid";
}

[[nodiscard]] bool needs_grouping_for_suffix(const ir::Expr &expr) {
    return std::visit(Overloaded{
                          [](const ir::BoolLiteralExpr &) { return false; },
                          [](const ir::IntegerLiteralExpr &) { return false; },
                          [](const ir::FloatLiteralExpr &) { return false; },
                          [](const ir::DecimalLiteralExpr &) { return false; },
                          [](const ir::StringLiteralExpr &) { return false; },
                          [](const ir::DurationLiteralExpr &) { return false; },
                          [](const ir::PathExpr &) { return false; },
                          [](const ir::QualifiedValueExpr &) { return false; },
                          [](const ir::CallExpr &) { return false; },
                          [](const ir::LambdaExpr &) { return false; },
                          [](const ir::StructLiteralExpr &) { return false; },
	                          [](const ir::MemberAccessExpr &) { return false; },
	                          [](const ir::IndexAccessExpr &) { return false; },
	                          [](const ir::UnaryExpr &) { return true; },
	                          [](const ir::BinaryExpr &) { return true; },
	                          [](const ir::MatchExpr &) { return true; },
                          // P4-02: unwrap(...) is a compound expression.
                          [](const ir::UnwrapExpr &) { return true; },
	                      },
	                      expr.node);
}

class IrProgramPrinter final {
  public:
    explicit IrProgramPrinter(std::ostream &out) : out_(out) {}

    void print(const ir::Program &program) {
        program_ = &program;
        out_ << program.format_version << '\n';

        bool emitted_section = false;
        if (!ir::formal_observations(program).empty()) {
            print_formal_observations(ir::formal_observations(program));
            emitted_section = true;
        }

        for (const auto &declaration : program.declarations) {
            if (emitted_section) {
                out_ << '\n';
            }

            std::visit(
                [this](const auto &typed_decl) {
                    print_provenance(typed_decl.provenance);
                    print_decl(typed_decl);
                },
                declaration);
            emitted_section = true;
        }
    }

  private:
    std::ostream &out_;
    const ir::Program *program_{nullptr};

    void line(int indent_level, std::string_view text) {
        out_ << std::string(static_cast<std::size_t>(indent_level) * 2, ' ') << text << '\n';
    }

    [[nodiscard]] std::string
    render_formal_observation(const ir::FormalObservation &observation) const {
        return std::visit(Overloaded{
                              [&](const ir::CalledCapabilityObservation &value) {
                                  return "observation " + observation.symbol +
                                         ": called_capability(agent=" + value.agent +
                                         ", capability=" + value.capability + ")";
                              },
                              [&](const ir::EmbeddedBoolObservation &value) {
                                  const auto scope_kind = [&]() {
                                      switch (value.scope.kind) {
                                      case ir::FormalObservationScopeKind::ContractClause:
                                          return std::string("contract_clause");
                                      case ir::FormalObservationScopeKind::WorkflowSafetyClause:
                                          return std::string("workflow_safety_clause");
                                      case ir::FormalObservationScopeKind::WorkflowLivenessClause:
                                          return std::string("workflow_liveness_clause");
                                      }

                                      return std::string("invalid");
                                  }();

                                  return "observation " + observation.symbol +
                                         ": embedded_bool_expr(scope=" + scope_kind +
                                         ", owner=" + value.scope.owner +
                                         ", clause=" + std::to_string(value.scope.clause_index) +
                                         ", atom=" + std::to_string(value.scope.atom_index) + ")";
                              },
                          },
                          observation.node);
    }

    void print_formal_observations(const std::vector<ir::FormalObservation> &observations) {
        line(0, "formal_observations {");
        for (const auto &observation : observations) {
            line(1, render_formal_observation(observation));
        }
        line(0, "}");
    }

    [[nodiscard]] bool has_provenance(const ir::DeclarationProvenance &provenance) const {
        return !provenance.module_name.empty() || !provenance.source_path.empty();
    }

    void print_provenance(const ir::DeclarationProvenance &provenance) {
        if (!has_provenance(provenance)) {
            return;
        }

        line(0,
             "@provenance(module=" + provenance.module_name + ", source=" + provenance.source_path +
                 ")");
    }

    [[nodiscard]] std::string render_path(const ir::Path &path) const {
        std::ostringstream builder;

        switch (path.root_kind) {
        case ir::PathRootKind::Identifier:
            builder << path.root_name;
            break;
        case ir::PathRootKind::Input:
            builder << "input";
            break;
        case ir::PathRootKind::Context:
            builder << path.root_name;
            break;
        case ir::PathRootKind::Output:
            builder << "output";
            break;
        case ir::PathRootKind::State:
        case ir::PathRootKind::Local:
            builder << path.root_name;
            break;
        }

        for (const auto &member : path.members) {
            builder << '.' << member;
        }

        return builder.str();
    }

    [[nodiscard]] std::string render_bool(bool value) const {
        return value ? "true" : "false";
    }

    [[nodiscard]] std::string render_workflow_value_read(const ir::WorkflowValueRead &read) const {
        std::string rendered = workflow_value_source_kind_name(read.kind) + "(" + read.root_name;
        for (const auto &member : read.members) {
            rendered += "." + member;
        }
        rendered += ")";
        return rendered;
    }

    void print_flow_summary(const ir::StateHandler::Summary &summary, int indent_level) {
        std::vector<std::string> assigned_paths;
        assigned_paths.reserve(summary.assigned_paths.size());
        for (const auto &assigned_path : summary.assigned_paths) {
            assigned_paths.push_back(render_path(assigned_path));
        }

        line(indent_level, "summary {");
        line(indent_level + 1, "goto_targets: [" + join(summary.goto_targets, ", ") + "]");
        line(indent_level + 1, "may_return: " + render_bool(summary.may_return));
        line(indent_level + 1, "may_fallthrough: " + render_bool(summary.may_fallthrough));
        line(indent_level + 1, "assigned_paths: [" + join(assigned_paths, ", ") + "]");
        line(indent_level + 1, "called_targets: [" + join(summary.called_targets, ", ") + "]");
        line(indent_level + 1, "assert_count: " + std::to_string(summary.assert_count));
        line(indent_level, "}");
    }

    void print_workflow_expr_summary(std::string_view label,
                                     const ir::WorkflowExprSummary &summary,
                                     int indent_level) {
        std::vector<std::string> reads;
        reads.reserve(summary.reads.size());
        for (const auto &read : summary.reads) {
            reads.push_back(render_workflow_value_read(read));
        }

        line(indent_level, std::string(label) + " {");
        line(indent_level + 1, "reads: [" + join(reads, ", ") + "]");
        line(indent_level, "}");
    }

    [[nodiscard]] std::string render_suffix_base(const ir::Expr &expr) const {
        const auto rendered = render_expr(expr);
        if (needs_grouping_for_suffix(expr)) {
            return "(" + rendered + ")";
        }

        return rendered;
    }

    [[nodiscard]] std::string render_pattern(const ir::MatchPattern &pattern) const {
        return std::visit(
            Overloaded{
                [](const ir::LiteralPattern &value) { return value.spelling; },
                [this](const ir::VariantPattern &value) {
                    std::vector<std::string> subpatterns;
                    subpatterns.reserve(value.subpatterns.size());
                    for (const auto &subpattern : value.subpatterns) {
                        subpatterns.push_back(subpattern ? render_pattern(*subpattern)
                                                          : std::string{"_"});
                    }
                    if (subpatterns.empty()) {
                        return value.path;
                    }
                    return value.path + "(" + join(subpatterns, ", ") + ")";
                },
                [](const ir::WildcardPattern &) { return std::string{"_"}; },
                [this](const ir::BindingPattern &value) {
                    std::string rendered = value.is_mut ? "mut " + value.name : value.name;
                    if (value.nested) {
                        rendered += " @ " + render_pattern(*value.nested);
                    }
                    return rendered;
                },
                [this](const ir::TuplePattern &value) {
                    std::vector<std::string> elements;
                    elements.reserve(value.elements.size());
                    for (const auto &element : value.elements) {
                        elements.push_back(element ? render_pattern(*element) : std::string{"_"});
                    }
                    return "(" + join(elements, ", ") + ")";
                },
                [this](const ir::OrPattern &value) {
                    std::vector<std::string> branches;
                    branches.reserve(value.branches.size());
                    for (const auto &branch : value.branches) {
                        branches.push_back(branch ? render_pattern(*branch) : std::string{"_"});
                    }
                    return join(branches, " | ");
                },
            },
            pattern.node);
    }

    [[nodiscard]] std::string render_expr(const ir::Expr &expr) const {
        return std::visit(
            Overloaded{
                [](const ir::BoolLiteralExpr &value) {
                    return std::string(value.value ? "true" : "false");
                },
                [](const ir::IntegerLiteralExpr &value) { return value.spelling; },
                [](const ir::FloatLiteralExpr &value) { return value.spelling; },
                [](const ir::DecimalLiteralExpr &value) { return value.spelling; },
                [](const ir::StringLiteralExpr &value) { return value.spelling; },
                [](const ir::DurationLiteralExpr &value) { return value.spelling; },
                [this](const ir::PathExpr &value) { return render_path(value.path); },
                [](const ir::QualifiedValueExpr &value) { return value.value; },
                [this](const ir::CallExpr &value) {
                    std::vector<std::string> arguments;
                    arguments.reserve(value.arguments.size());
                    for (const auto &argument : value.arguments) {
                        arguments.push_back(render_expr(*argument));
                    }

                    return value.callee + "(" + join(arguments, ", ") + ")";
                },
                [this](const ir::LambdaExpr &value) {
                    const auto body = value.body ? render_expr(*value.body) : std::string{"none"};
                    return "\\" + join(value.params, ", ") + " -> " + body;
                },
                [this](const ir::StructLiteralExpr &value) {
                    std::vector<std::string> fields;
                    fields.reserve(value.fields.size());
                    for (const auto &field : value.fields) {
                        fields.push_back(field.name + ": " + render_expr(*field.value));
                    }

                    return value.type_name + " { " + join(fields, ", ") + " }";
                },
                [this](const ir::UnaryExpr &value) {
                    return "(" + expr_unary_op_name(value.op) + render_expr(*value.operand) + ")";
                },
                [this](const ir::BinaryExpr &value) {
                    return "(" + render_expr(*value.lhs) + " " + expr_binary_op_name(value.op) +
                           " " + render_expr(*value.rhs) + ")";
                },
                [this](const ir::MemberAccessExpr &value) {
                    return render_suffix_base(*value.base) + "." + value.member;
                },
                [this](const ir::IndexAccessExpr &value) {
                    return render_suffix_base(*value.base) + "[" + render_expr(*value.index) + "]";
                },
                [this](const ir::MatchExpr &value) {
                    std::vector<std::string> arms;
                    arms.reserve(value.arms.size());
                    for (const auto &arm : value.arms) {
                        std::string rendered = render_pattern(arm.pattern);
                        if (arm.guard) {
                            rendered += " if " + render_expr(*arm.guard);
                        }
                        rendered += " => " +
                                    (arm.body ? render_expr(*arm.body) : std::string{"none"});
                        arms.push_back(std::move(rendered));
                    }
                    return "match " +
                           (value.scrutinee ? render_expr(*value.scrutinee)
                                            : std::string{"none"}) +
                           " { " + join(arms, ", ") + " }";
                },
                // P4-02: unwrap(operand) — mirrors the statement-level keyword
                // so diffs stay readable; fall back to "<none>" for malformed
                // ASTs (e.g. typed-tree rewrites that temporarily null the operand).
                [this](const ir::UnwrapExpr &value) {
                    return "unwrap(" +
                           (value.operand ? render_expr(*value.operand)
                                          : std::string{"<none>"}) +
                           ")";
                },
            },
            expr.node);
    }

    [[nodiscard]] std::string render_temporal(const ir::TemporalExpr &expr) const {
        return std::visit(
            Overloaded{
                [this](const ir::EmbeddedTemporalExpr &value) { return render_expr(*value.expr); },
                [](const ir::CalledTemporalExpr &value) {
                    return "called(" + value.capability + ")";
                },
                [](const ir::InStateTemporalExpr &value) {
                    return "in_state(" + value.state + ")";
                },
                [](const ir::RunningTemporalExpr &value) { return "running(" + value.node + ")"; },
                [](const ir::CompletedTemporalExpr &value) {
                    if (value.state_name.has_value()) {
                        return "completed(" + value.node + ", " + *value.state_name + ")";
                    }

                    return "completed(" + value.node + ")";
                },
                [this](const ir::TemporalUnaryExpr &value) {
                    return temporal_unary_op_name(value.op) + "(" +
                           render_temporal(*value.operand) + ")";
                },
                [this](const ir::TemporalBinaryExpr &value) {
                    return "(" + render_temporal(*value.lhs) + " " +
                           temporal_binary_op_name(value.op) + " " + render_temporal(*value.rhs) +
                           ")";
                },
            },
            expr.node);
    }

    void print_block(const ir::Block &block, int indent_level) {
        for (const auto &statement : block.statements) {
            print_statement(*statement, indent_level);
        }
    }

    void print_statement(const ir::Statement &statement, int indent_level) {
        std::visit(Overloaded{
                       [this, indent_level](const ir::LetStatement &value) {
                           line(indent_level,
                                "let " + value.name + ": " + type_name(value.type_ref) + " = " +
                                    render_expr(*value.initializer));
                       },
                       [this, indent_level](const ir::AssignStatement &value) {
                           line(indent_level,
                                render_path(value.target) + " = " + render_expr(*value.value));
                       },
                       [this, indent_level](const ir::IfStatement &value) {
                           line(indent_level, "if " + render_expr(*value.condition) + " {");
                           print_block(*value.then_block, indent_level + 1);
                           if (value.else_block) {
                               line(indent_level, "} else {");
                               print_block(*value.else_block, indent_level + 1);
                           }
                           line(indent_level, "}");
                       },
                       [this, indent_level](const ir::GotoStatement &value) {
                           line(indent_level, "goto " + value.target_state);
                       },
                       [this, indent_level](const ir::ReturnStatement &value) {
                           if (value.value) {
                               line(indent_level, "return " + render_expr(*value.value));
                           } else {
                               line(indent_level, "return");
                           }
                       },
                       [this, indent_level](const ir::AssertStatement &value) {
                           std::string text = "assert " + render_expr(*value.condition);
                           if (value.message) {
                               text += ", " + render_expr(*value.message);
                           }
                           line(indent_level, text);
                       },
                       [this, indent_level](const ir::UnwrapStatement &value) {
                           std::string text = "unwrap";
                           if (value.operand) {
                               text += " " + render_expr(*value.operand);
                           }
                           line(indent_level, text);
                       },
                       [this, indent_level](const ir::RequiresStatement &value) {
                           std::string text = "requires";
                           if (value.condition) {
                               text += " " + render_expr(*value.condition);
                           }
                           if (value.message) {
                               text += ", " + render_expr(*value.message);
                           }
                           line(indent_level, text);
                       },
                       [this, indent_level](const ir::UnreachableStatement &value) {
                           std::string text = "unreachable";
                           if (value.message) {
                               text += " " + render_expr(*value.message);
                           }
                           line(indent_level, text);
                       },
                       [this, indent_level](const ir::ExprStatement &value) {
                           line(indent_level, render_expr(*value.expr));
                       },
                   },
                   statement.node);
    }

    void print_policy_item(const ir::StatePolicyItem &policy_item, int indent_level) {
        std::visit(Overloaded{
                       [this, indent_level](const ir::RetryPolicy &value) {
                           line(indent_level, "retry: " + value.limit);
                       },
                       [this, indent_level](const ir::RetryOnPolicy &value) {
                           line(indent_level, "retry_on: [" + join(value.targets, ", ") + "]");
                       },
                       [this, indent_level](const ir::TimeoutPolicy &value) {
                           line(indent_level, "timeout: " + value.duration);
                       },
                   },
                   policy_item);
    }

    [[nodiscard]] std::string print_params(const std::vector<ir::ParamDecl> &params) const {
        std::vector<std::string> rendered;
        rendered.reserve(params.size());

        for (const auto &param : params) {
            rendered.push_back(param.name + ": " + type_name(param.type_ref));
        }

        return join(rendered, ", ");
    }

    void print_decl(const ir::ModuleDecl &declaration) {
        line(0, "module " + declaration.name);
    }

    void print_decl(const ir::ImportDecl &declaration) {
        if (declaration.alias.has_value()) {
            line(0, "import " + declaration.path + " as " + *declaration.alias);
            return;
        }

        line(0, "import " + declaration.path);
    }

    void print_decl(const ir::ConstDecl &declaration) {
        line(0,
             "const " + declaration.name + ": " + type_name(declaration.type_ref) + " = " +
                 render_expr(*declaration.value));
    }

    void print_decl(const ir::TypeAliasDecl &declaration) {
        line(0, "typealias " + declaration.name + " = " + type_name(declaration.aliased_type_ref));
    }

    void print_decl(const ir::StructDecl &declaration) {
        line(0, "struct " + declaration.name + " {");

        for (const auto &field : declaration.fields) {
            std::string text = "field " + field.name + ": " + type_name(field.type_ref);
            if (field.default_value) {
                text += " = " + render_expr(*field.default_value);
            }

            line(1, text);
        }

        line(0, "}");
    }

    void print_decl(const ir::EnumDecl &declaration) {
        line(0, "enum " + declaration.name + " {");
        for (const auto &variant : declaration.variants) {
            line(1, "variant " + variant);
        }
        line(0, "}");
    }

    void print_decl(const ir::CapabilityDecl &declaration) {
        line(0,
             "capability " + declaration.name + "(" + print_params(declaration.params) + ") -> " +
                 type_name(declaration.return_type_ref));
        if (declaration.effect.declared) {
            line(1, "effect: " + std::string(capability_effect_kind_name(declaration.effect.kind)));
            line(1,
                 "receipt: " +
                     std::string(capability_receipt_mode_name(declaration.effect.receipt_mode)));
            line(1,
                 "retry: " +
                     std::string(capability_retry_mode_name(declaration.effect.retry_mode)));
            if (declaration.effect.domain.has_value()) {
                line(1, "domain: " + *declaration.effect.domain);
            }
            if (declaration.effect.idempotency_key.has_value()) {
                line(1, "idempotency: " + *declaration.effect.idempotency_key);
            }
            if (declaration.effect.timeout.has_value()) {
                line(1, "timeout: " + *declaration.effect.timeout);
            }
            if (declaration.effect.compensation.has_value()) {
                line(1, "compensation: " + *declaration.effect.compensation);
            }
            if (!declaration.effect.policies.empty()) {
                line(1, "policy: [" + join(declaration.effect.policies, ", ") + "]");
            }
        }
    }

    void print_decl(const ir::PredicateDecl &declaration) {
        line(0,
             "predicate " + declaration.name + "(" + print_params(declaration.params) +
                 ") -> Bool");
    }

    void print_decl(const ir::AgentDecl &declaration) {
        line(0, "agent " + declaration.name + " {");
        line(1, "input: " + type_name(declaration.input_type_ref));
        line(1, "context: " + type_name(declaration.context_type_ref));
        line(1, "output: " + type_name(declaration.output_type_ref));
        line(1, "states: [" + join(declaration.states, ", ") + "]");
        line(1, "initial: " + declaration.initial_state);
        line(1, "final: [" + join(declaration.final_states, ", ") + "]");
        line(1, "capabilities: [" + join(symbol_names(declaration.capability_refs), ", ") + "]");

        if (!declaration.quota.empty()) {
            line(1, "quota {");
            for (const auto &item : declaration.quota) {
                line(2, item.name + ": " + item.value);
            }
            line(1, "}");
        }

        for (const auto &transition : declaration.transitions) {
            line(1, "transition " + transition.from_state + " -> " + transition.to_state);
        }

        line(0, "}");
    }

    void print_decl(const ir::ContractDecl &declaration) {
        line(0, "contract " + symbol_name(declaration.target_ref) + " {");
        for (const auto &clause : declaration.clauses) {
            const auto value = std::visit(
                Overloaded{
                    [this](const ir::ExprRef &expr) { return render_expr(*expr); },
                    [this](const ir::TemporalExprPtr &expr) { return render_temporal(*expr); },
                },
                clause.value);
            line(1, contract_clause_name(clause.kind) + ": " + value);
        }
        line(0, "}");
    }

    void print_decl(const ir::FlowDecl &declaration) {
        line(0, "flow " + symbol_name(declaration.target_ref) + " {");

        for (const auto &handler : declaration.state_handlers) {
            line(1, "state " + handler.state_name + " {");
            for (const auto &policy_item : handler.policy) {
                print_policy_item(policy_item, 2);
            }
            static const ir::StateHandler::Summary empty_summary{};
            const auto *summary =
                program_ == nullptr
                    ? nullptr
                    : ir::find_state_handler_summary(*program_, declaration, handler);
            print_flow_summary(summary == nullptr ? empty_summary : *summary, 2);
            print_block(handler.body, 2);
            line(1, "}");
        }

        line(0, "}");
    }

    void print_decl(const ir::WorkflowDecl &declaration) {
        line(0, "workflow " + declaration.name + " {");
        line(1, "input: " + type_name(declaration.input_type_ref));
        line(1, "output: " + type_name(declaration.output_type_ref));

        for (const auto &node : declaration.nodes) {
            std::string text = "node " + node.name + ": " + symbol_name(node.target_ref) + "(" +
                               render_expr(*node.input) + ")";
            if (!node.after.empty()) {
                text += " after [" + join(node.after, ", ") + "]";
            }

            line(1, text);
            static const ir::WorkflowExprSummary empty_summary{};
            const auto *summary =
                program_ == nullptr
                    ? nullptr
                    : ir::find_workflow_node_input_summary(*program_, declaration, node);
            print_workflow_expr_summary(
                "input_summary", summary == nullptr ? empty_summary : *summary, 2);
        }

        for (const auto &formula : declaration.safety) {
            line(1, "safety: " + render_temporal(*formula));
        }
        for (const auto &formula : declaration.liveness) {
            line(1, "liveness: " + render_temporal(*formula));
        }

        static const ir::WorkflowExprSummary empty_summary{};
        const auto *summary = program_ == nullptr
                                  ? nullptr
                                  : ir::find_workflow_return_summary(*program_, declaration);
        print_workflow_expr_summary(
            "return_summary", summary == nullptr ? empty_summary : *summary, 1);
        line(1, "return: " + render_expr(*declaration.return_value));
        line(0, "}");
    }

    // P2c (RFC §3.2.2): human-readable IR dump for a top-level `fn`. Mirrors
    // the capability/predicate signature surface plus the generic
    // type-parameter names and the three-state effect clause.
    void print_decl(const ir::FnDecl &declaration) {
        std::string signature = "fn " + declaration.name;
        if (!declaration.type_param_names.empty()) {
            signature += "<" + join(declaration.type_param_names, ", ") + ">";
        }
        signature += "(" + print_params(declaration.params) + ")";
        if (declaration.has_return_type) {
            signature += " -> " + type_name(declaration.return_type_ref);
        }
        if (!declaration.has_body) {
            signature += ";";
        }
        line(0, signature);

        const auto effect_name = [](ir::FnEffectKind kind) -> std::string_view {
            switch (kind) {
            case ir::FnEffectKind::Pure:
                return "Pure";
            case ir::FnEffectKind::Nondet:
                return "Nondet";
            case ir::FnEffectKind::Capability:
                return "Capability";
            }
            return "Unknown";
        };
        line(1, std::string("effect: ") + std::string(effect_name(declaration.effect.kind)));
        if (!declaration.effect.capabilities.empty()) {
            line(1,
                 "capabilities: [" + join(symbol_names(declaration.effect.capabilities), ", ") +
                     "]");
        }
        if (declaration.body) {
            line(1, "body {");
            print_block(*declaration.body, 2);
            line(1, "}");
        }
    }

    void print_decl(const ir::InstanceDecl &declaration) {
        std::string_view kind_name = "unknown";
        switch (declaration.kind) {
        case ir::InstanceKind::Capability: kind_name = "capability"; break;
        case ir::InstanceKind::Predicate: kind_name = "predicate"; break;
        case ir::InstanceKind::Agent: kind_name = "agent"; break;
        case ir::InstanceKind::Workflow: kind_name = "workflow"; break;
        case ir::InstanceKind::Fn: kind_name = "fn"; break;
        case ir::InstanceKind::Unknown: kind_name = "unknown"; break;
        }
        line(0, std::string("instance ") + std::string(kind_name) + " " + declaration.name + " {");
        line(1, "symbol: " + symbol_name(declaration.symbol_ref));
        if (!declaration.type_args.empty()) {
            std::vector<std::string> names;
            names.reserve(declaration.type_args.size());
            for (const auto &t : declaration.type_args) names.push_back(type_name(t));
            line(1, "type_args: [" + join(names, ", ") + "]");
        }
        if (!declaration.params.empty()) {
            line(1, "params: (" + print_params(declaration.params) + ")");
        }
        if (!type_name(declaration.return_type_ref).empty()) {
            line(1, "return: " + type_name(declaration.return_type_ref));
        }
        if (!type_name(declaration.agent_input_type_ref).empty()) {
            line(1, "agent_input: " + type_name(declaration.agent_input_type_ref));
        }
        if (!type_name(declaration.agent_context_type_ref).empty()) {
            line(1, "agent_context: " + type_name(declaration.agent_context_type_ref));
        }
        if (!type_name(declaration.agent_output_type_ref).empty()) {
            line(1, "agent_output: " + type_name(declaration.agent_output_type_ref));
        }
        if (!type_name(declaration.workflow_input_type_ref).empty()) {
            line(1, "workflow_input: " + type_name(declaration.workflow_input_type_ref));
        }
        if (!type_name(declaration.workflow_output_type_ref).empty()) {
            line(1, "workflow_output: " + type_name(declaration.workflow_output_type_ref));
        }
        line(0, "}");
    }
};

} // namespace

void print_program_ir(const ir::Program &program, std::ostream &out) {
    IrProgramPrinter printer(out);
    printer.print(program);
}

void emit_program_ir(const ast::Program &program,
                     const ResolveResult &resolve_result,
                     const TypeCheckResult &type_check_result,
                     std::ostream &out) {
    print_program_ir(lower_program_ir(program, resolve_result, type_check_result), out);
}

void emit_program_ir(const SourceGraph &graph,
                     const ResolveResult &resolve_result,
                     const TypeCheckResult &type_check_result,
                     std::ostream &out) {
    print_program_ir(lower_program_ir(graph, resolve_result, type_check_result), out);
}

} // namespace ahfl
