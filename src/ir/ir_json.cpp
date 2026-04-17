#include "ahfl/ir/ir.hpp"

#include <cstddef>
#include <ostream>
#include <string_view>
#include <utility>
#include <variant>

#include "ahfl/support/json.hpp"

namespace ahfl {

namespace {

template <typename... Ts> struct Overloaded : Ts... {
    using Ts::operator()...;
};

template <typename... Ts> Overloaded(Ts...) -> Overloaded<Ts...>;

[[nodiscard]] std::string_view path_root_kind_name(ir::PathRootKind kind) {
    switch (kind) {
    case ir::PathRootKind::Identifier:
        return "identifier";
    case ir::PathRootKind::Input:
        return "input";
    case ir::PathRootKind::Output:
        return "output";
    }

    return "invalid";
}

[[nodiscard]] std::string_view expr_unary_op_name(ir::ExprUnaryOp op) {
    switch (op) {
    case ir::ExprUnaryOp::Not:
        return "not";
    case ir::ExprUnaryOp::Negate:
        return "negate";
    case ir::ExprUnaryOp::Positive:
        return "positive";
    }

    return "invalid";
}

[[nodiscard]] std::string_view expr_binary_op_name(ir::ExprBinaryOp op) {
    switch (op) {
    case ir::ExprBinaryOp::Implies:
        return "implies";
    case ir::ExprBinaryOp::Or:
        return "or";
    case ir::ExprBinaryOp::And:
        return "and";
    case ir::ExprBinaryOp::Equal:
        return "equal";
    case ir::ExprBinaryOp::NotEqual:
        return "not_equal";
    case ir::ExprBinaryOp::Less:
        return "less";
    case ir::ExprBinaryOp::LessEqual:
        return "less_equal";
    case ir::ExprBinaryOp::Greater:
        return "greater";
    case ir::ExprBinaryOp::GreaterEqual:
        return "greater_equal";
    case ir::ExprBinaryOp::Add:
        return "add";
    case ir::ExprBinaryOp::Subtract:
        return "subtract";
    case ir::ExprBinaryOp::Multiply:
        return "multiply";
    case ir::ExprBinaryOp::Divide:
        return "divide";
    case ir::ExprBinaryOp::Modulo:
        return "modulo";
    }

    return "invalid";
}

[[nodiscard]] std::string_view temporal_unary_op_name(ir::TemporalUnaryOp op) {
    switch (op) {
    case ir::TemporalUnaryOp::Always:
        return "always";
    case ir::TemporalUnaryOp::Eventually:
        return "eventually";
    case ir::TemporalUnaryOp::Next:
        return "next";
    case ir::TemporalUnaryOp::Not:
        return "not";
    }

    return "invalid";
}

[[nodiscard]] std::string_view temporal_binary_op_name(ir::TemporalBinaryOp op) {
    switch (op) {
    case ir::TemporalBinaryOp::Implies:
        return "implies";
    case ir::TemporalBinaryOp::Or:
        return "or";
    case ir::TemporalBinaryOp::And:
        return "and";
    case ir::TemporalBinaryOp::Until:
        return "until";
    }

    return "invalid";
}

[[nodiscard]] std::string_view contract_clause_name(ir::ContractClauseKind kind) {
    switch (kind) {
    case ir::ContractClauseKind::Requires:
        return "requires";
    case ir::ContractClauseKind::Ensures:
        return "ensures";
    case ir::ContractClauseKind::Invariant:
        return "invariant";
    case ir::ContractClauseKind::Forbid:
        return "forbid";
    }

    return "invalid";
}

[[nodiscard]] std::string_view workflow_value_source_kind_name(ir::WorkflowValueSourceKind kind) {
    switch (kind) {
    case ir::WorkflowValueSourceKind::WorkflowInput:
        return "workflow_input";
    case ir::WorkflowValueSourceKind::WorkflowNodeOutput:
        return "workflow_node_output";
    }

    return "invalid";
}

[[nodiscard]] std::string_view
formal_observation_scope_kind_name(ir::FormalObservationScopeKind kind) {
    switch (kind) {
    case ir::FormalObservationScopeKind::ContractClause:
        return "contract_clause";
    case ir::FormalObservationScopeKind::WorkflowSafetyClause:
        return "workflow_safety_clause";
    case ir::FormalObservationScopeKind::WorkflowLivenessClause:
        return "workflow_liveness_clause";
    }

    return "invalid";
}

class IrJsonPrinter final {
  public:
    explicit IrJsonPrinter(std::ostream &out) : out_(out) {}

    void print(const ir::Program &program) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(program.format_version); });
            field("formal_observations", [&]() {
                print_array(1, [&](const auto &item) {
                    for (const auto &observation : program.formal_observations) {
                        item([&]() { print_formal_observation(observation, 2); });
                    }
                });
            });
            field("declarations", [&]() {
                print_array(1, [&](const auto &item) {
                    for (const auto &declaration : program.declarations) {
                        item([&]() { print_decl(declaration, 2); });
                    }
                });
            });
        });
        out_ << '\n';
    }

  private:
    std::ostream &out_;

    void write_indent(int indent_level) {
        out_ << std::string(static_cast<std::size_t>(indent_level) * 2, ' ');
    }

    void newline_and_indent(int indent_level) {
        out_ << '\n';
        write_indent(indent_level);
    }

    void write_string(std::string_view value) {
        write_escaped_json_string(out_, value);
    }

    template <typename WriteFields> void print_object(int indent_level, WriteFields write_fields) {
        out_ << '{';
        bool wrote_any_field = false;

        const auto field = [&](std::string_view name, const auto &write_value) {
            if (wrote_any_field) {
                out_ << ',';
            }
            newline_and_indent(indent_level + 1);
            write_string(name);
            out_ << ": ";
            write_value();
            wrote_any_field = true;
        };

        write_fields(field);

        if (wrote_any_field) {
            newline_and_indent(indent_level);
        }
        out_ << '}';
    }

    template <typename WriteItems> void print_array(int indent_level, WriteItems write_items) {
        out_ << '[';
        bool wrote_any_item = false;

        const auto item = [&](const auto &write_value) {
            if (wrote_any_item) {
                out_ << ',';
            }
            newline_and_indent(indent_level + 1);
            write_value();
            wrote_any_item = true;
        };

        write_items(item);

        if (wrote_any_item) {
            newline_and_indent(indent_level);
        }
        out_ << ']';
    }

    void write_string_array(const std::vector<std::string> &values, int indent_level) {
        print_array(indent_level, [&](const auto &item) {
            for (const auto &value : values) {
                item([&]() { write_string(value); });
            }
        });
    }

    void write_null() {
        out_ << "null";
    }

    [[nodiscard]] bool has_provenance(const ir::DeclarationProvenance &provenance) const {
        return !provenance.module_name.empty() || !provenance.source_path.empty();
    }

    void print_provenance(const ir::DeclarationProvenance &provenance, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("module_name", [&]() { write_string(provenance.module_name); });
            field("source_path", [&]() { write_string(provenance.source_path); });
        });
    }

    void write_bool(bool value) {
        out_ << (value ? "true" : "false");
    }

    void write_index(std::size_t value) {
        out_ << value;
    }

    void print_formal_observation_scope(const ir::FormalObservationScope &scope, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("kind", [&]() { write_string(formal_observation_scope_kind_name(scope.kind)); });
            field("owner", [&]() { write_string(scope.owner); });
            field("clause_index", [&]() { write_index(scope.clause_index); });
            field("atom_index", [&]() { write_index(scope.atom_index); });
        });
    }

    void print_formal_observation(const ir::FormalObservation &observation, int indent_level) {
        std::visit(Overloaded{
                       [&](const ir::CalledCapabilityObservation &value) {
                           print_object(indent_level, [&](const auto &field) {
                               field("symbol", [&]() { write_string(observation.symbol); });
                               field("kind", [&]() { write_string("called_capability"); });
                               field("agent", [&]() { write_string(value.agent); });
                               field("capability", [&]() { write_string(value.capability); });
                           });
                       },
                       [&](const ir::EmbeddedBoolObservation &value) {
                           print_object(indent_level, [&](const auto &field) {
                               field("symbol", [&]() { write_string(observation.symbol); });
                               field("kind", [&]() { write_string("embedded_bool_expr"); });
                               field("scope", [&]() {
                                   print_formal_observation_scope(value.scope, indent_level + 1);
                               });
                           });
                       },
                   },
                   observation.node);
    }

    void print_path(const ir::Path &path, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("root_kind", [&]() { write_string(path_root_kind_name(path.root_kind)); });
            field("root_name", [&]() { write_string(path.root_name); });
            field("members", [&]() { write_string_array(path.members, indent_level + 1); });
        });
    }

    void print_expr(const ir::Expr &expr, int indent_level) {
        std::visit(
            Overloaded{
                [&](const ir::NoneLiteralExpr &) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("none_literal"); });
                    });
                },
                [&](const ir::BoolLiteralExpr &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("bool_literal"); });
                        field("value", [&]() { write_bool(value.value); });
                    });
                },
                [&](const ir::IntegerLiteralExpr &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("integer_literal"); });
                        field("spelling", [&]() { write_string(value.spelling); });
                    });
                },
                [&](const ir::FloatLiteralExpr &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("float_literal"); });
                        field("spelling", [&]() { write_string(value.spelling); });
                    });
                },
                [&](const ir::DecimalLiteralExpr &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("decimal_literal"); });
                        field("spelling", [&]() { write_string(value.spelling); });
                    });
                },
                [&](const ir::StringLiteralExpr &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("string_literal"); });
                        field("spelling", [&]() { write_string(value.spelling); });
                    });
                },
                [&](const ir::DurationLiteralExpr &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("duration_literal"); });
                        field("spelling", [&]() { write_string(value.spelling); });
                    });
                },
                [&](const ir::SomeExpr &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("some"); });
                        field("value", [&]() { print_expr(*value.value, indent_level + 1); });
                    });
                },
                [&](const ir::PathExpr &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("path"); });
                        field("path", [&]() { print_path(value.path, indent_level + 1); });
                    });
                },
                [&](const ir::QualifiedValueExpr &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("qualified_value"); });
                        field("value", [&]() { write_string(value.value); });
                    });
                },
                [&](const ir::CallExpr &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("call"); });
                        field("callee", [&]() { write_string(value.callee); });
                        field("arguments", [&]() {
                            print_array(indent_level + 1, [&](const auto &item) {
                                for (const auto &argument : value.arguments) {
                                    item([&]() { print_expr(*argument, indent_level + 2); });
                                }
                            });
                        });
                    });
                },
                [&](const ir::StructLiteralExpr &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("struct_literal"); });
                        field("type_name", [&]() { write_string(value.type_name); });
                        field("fields", [&]() {
                            print_array(indent_level + 1, [&](const auto &item) {
                                for (const auto &struct_field : value.fields) {
                                    item([&]() {
                                        print_object(indent_level + 2, [&](const auto &entry) {
                                            entry("name",
                                                  [&]() { write_string(struct_field.name); });
                                            entry("value", [&]() {
                                                print_expr(*struct_field.value, indent_level + 3);
                                            });
                                        });
                                    });
                                }
                            });
                        });
                    });
                },
                [&](const ir::ListLiteralExpr &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("list_literal"); });
                        field("items", [&]() {
                            print_array(indent_level + 1, [&](const auto &item) {
                                for (const auto &list_item : value.items) {
                                    item([&]() { print_expr(*list_item, indent_level + 2); });
                                }
                            });
                        });
                    });
                },
                [&](const ir::SetLiteralExpr &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("set_literal"); });
                        field("items", [&]() {
                            print_array(indent_level + 1, [&](const auto &item) {
                                for (const auto &set_item : value.items) {
                                    item([&]() { print_expr(*set_item, indent_level + 2); });
                                }
                            });
                        });
                    });
                },
                [&](const ir::MapLiteralExpr &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("map_literal"); });
                        field("entries", [&]() {
                            print_array(indent_level + 1, [&](const auto &item) {
                                for (const auto &entry : value.entries) {
                                    item([&]() {
                                        print_object(
                                            indent_level + 2, [&](const auto &entry_field) {
                                                entry_field("key", [&]() {
                                                    print_expr(*entry.key, indent_level + 3);
                                                });
                                                entry_field("value", [&]() {
                                                    print_expr(*entry.value, indent_level + 3);
                                                });
                                            });
                                    });
                                }
                            });
                        });
                    });
                },
                [&](const ir::UnaryExpr &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("unary"); });
                        field("op", [&]() { write_string(expr_unary_op_name(value.op)); });
                        field("operand", [&]() { print_expr(*value.operand, indent_level + 1); });
                    });
                },
                [&](const ir::BinaryExpr &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("binary"); });
                        field("op", [&]() { write_string(expr_binary_op_name(value.op)); });
                        field("lhs", [&]() { print_expr(*value.lhs, indent_level + 1); });
                        field("rhs", [&]() { print_expr(*value.rhs, indent_level + 1); });
                    });
                },
                [&](const ir::MemberAccessExpr &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("member_access"); });
                        field("base", [&]() { print_expr(*value.base, indent_level + 1); });
                        field("member", [&]() { write_string(value.member); });
                    });
                },
                [&](const ir::IndexAccessExpr &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("index_access"); });
                        field("base", [&]() { print_expr(*value.base, indent_level + 1); });
                        field("index", [&]() { print_expr(*value.index, indent_level + 1); });
                    });
                },
                [&](const ir::GroupExpr &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("group"); });
                        field("expr", [&]() { print_expr(*value.expr, indent_level + 1); });
                    });
                },
            },
            expr.node);
    }

    void print_temporal_expr(const ir::TemporalExpr &expr, int indent_level) {
        std::visit(
            Overloaded{
                [&](const ir::EmbeddedTemporalExpr &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("embedded_expr"); });
                        field("expr", [&]() { print_expr(*value.expr, indent_level + 1); });
                    });
                },
                [&](const ir::CalledTemporalExpr &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("called"); });
                        field("capability", [&]() { write_string(value.capability); });
                    });
                },
                [&](const ir::InStateTemporalExpr &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("in_state"); });
                        field("state", [&]() { write_string(value.state); });
                    });
                },
                [&](const ir::RunningTemporalExpr &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("running"); });
                        field("node", [&]() { write_string(value.node); });
                    });
                },
                [&](const ir::CompletedTemporalExpr &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("completed"); });
                        field("node", [&]() { write_string(value.node); });
                        field("state_name", [&]() {
                            if (value.state_name.has_value()) {
                                write_string(*value.state_name);
                            } else {
                                write_null();
                            }
                        });
                    });
                },
                [&](const ir::TemporalUnaryExpr &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("unary"); });
                        field("op", [&]() { write_string(temporal_unary_op_name(value.op)); });
                        field("operand",
                              [&]() { print_temporal_expr(*value.operand, indent_level + 1); });
                    });
                },
                [&](const ir::TemporalBinaryExpr &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("binary"); });
                        field("op", [&]() { write_string(temporal_binary_op_name(value.op)); });
                        field("lhs", [&]() { print_temporal_expr(*value.lhs, indent_level + 1); });
                        field("rhs", [&]() { print_temporal_expr(*value.rhs, indent_level + 1); });
                    });
                },
            },
            expr.node);
    }

    void print_statement(const ir::Statement &statement, int indent_level) {
        std::visit(
            Overloaded{
                [&](const ir::LetStatement &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("let"); });
                        field("name", [&]() { write_string(value.name); });
                        field("type", [&]() { write_string(value.type); });
                        field("initializer",
                              [&]() { print_expr(*value.initializer, indent_level + 1); });
                    });
                },
                [&](const ir::AssignStatement &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("assign"); });
                        field("target", [&]() { print_path(value.target, indent_level + 1); });
                        field("value", [&]() { print_expr(*value.value, indent_level + 1); });
                    });
                },
                [&](const ir::IfStatement &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("if"); });
                        field("condition",
                              [&]() { print_expr(*value.condition, indent_level + 1); });
                        field("then_block",
                              [&]() { print_block(*value.then_block, indent_level + 1); });
                        field("else_block", [&]() {
                            if (value.else_block) {
                                print_block(*value.else_block, indent_level + 1);
                            } else {
                                write_null();
                            }
                        });
                    });
                },
                [&](const ir::GotoStatement &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("goto"); });
                        field("target_state", [&]() { write_string(value.target_state); });
                    });
                },
                [&](const ir::ReturnStatement &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("return"); });
                        field("value", [&]() {
                            if (value.value) {
                                print_expr(*value.value, indent_level + 1);
                            } else {
                                write_null();
                            }
                        });
                    });
                },
                [&](const ir::AssertStatement &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("assert"); });
                        field("condition",
                              [&]() { print_expr(*value.condition, indent_level + 1); });
                    });
                },
                [&](const ir::ExprStatement &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("expr"); });
                        field("expr", [&]() { print_expr(*value.expr, indent_level + 1); });
                    });
                },
            },
            statement.node);
    }

    void print_block(const ir::Block &block, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("statements", [&]() {
                print_array(indent_level + 1, [&](const auto &item) {
                    for (const auto &statement : block.statements) {
                        item([&]() { print_statement(*statement, indent_level + 2); });
                    }
                });
            });
        });
    }

    void print_flow_summary(const ir::StateHandler::Summary &summary, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("goto_targets",
                  [&]() { write_string_array(summary.goto_targets, indent_level + 1); });
            field("may_return", [&]() { write_bool(summary.may_return); });
            field("may_fallthrough", [&]() { write_bool(summary.may_fallthrough); });
            field("assigned_paths", [&]() {
                print_array(indent_level + 1, [&](const auto &item) {
                    for (const auto &path : summary.assigned_paths) {
                        item([&]() { print_path(path, indent_level + 2); });
                    }
                });
            });
            field("called_targets",
                  [&]() { write_string_array(summary.called_targets, indent_level + 1); });
            field("assert_count", [&]() { write_index(summary.assert_count); });
        });
    }

    void print_workflow_value_read(const ir::WorkflowValueRead &read, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("kind", [&]() { write_string(workflow_value_source_kind_name(read.kind)); });
            field("root_name", [&]() { write_string(read.root_name); });
            field("members", [&]() { write_string_array(read.members, indent_level + 1); });
        });
    }

    void print_workflow_expr_summary(const ir::WorkflowExprSummary &summary, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("reads", [&]() {
                print_array(indent_level + 1, [&](const auto &item) {
                    for (const auto &read : summary.reads) {
                        item([&]() { print_workflow_value_read(read, indent_level + 2); });
                    }
                });
            });
        });
    }

    void print_state_policy_item(const ir::StatePolicyItem &item, int indent_level) {
        std::visit(Overloaded{
                       [&](const ir::RetryPolicy &value) {
                           print_object(indent_level, [&](const auto &field) {
                               field("kind", [&]() { write_string("retry"); });
                               field("limit", [&]() { write_string(value.limit); });
                           });
                       },
                       [&](const ir::RetryOnPolicy &value) {
                           print_object(indent_level, [&](const auto &field) {
                               field("kind", [&]() { write_string("retry_on"); });
                               field("targets", [&]() {
                                   write_string_array(value.targets, indent_level + 1);
                               });
                           });
                       },
                       [&](const ir::TimeoutPolicy &value) {
                           print_object(indent_level, [&](const auto &field) {
                               field("kind", [&]() { write_string("timeout"); });
                               field("duration", [&]() { write_string(value.duration); });
                           });
                       },
                   },
                   item);
    }

    void print_decl(const ir::Decl &declaration, int indent_level) {
        std::visit(
            Overloaded{
                [&](const ir::ModuleDecl &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("module"); });
                        if (has_provenance(value.provenance)) {
                            field("provenance",
                                  [&]() { print_provenance(value.provenance, indent_level + 1); });
                        }
                        field("name", [&]() { write_string(value.name); });
                    });
                },
                [&](const ir::ImportDecl &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("import"); });
                        if (has_provenance(value.provenance)) {
                            field("provenance",
                                  [&]() { print_provenance(value.provenance, indent_level + 1); });
                        }
                        field("path", [&]() { write_string(value.path); });
                        field("alias", [&]() {
                            if (value.alias.has_value()) {
                                write_string(*value.alias);
                            } else {
                                write_null();
                            }
                        });
                    });
                },
                [&](const ir::ConstDecl &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("const"); });
                        if (has_provenance(value.provenance)) {
                            field("provenance",
                                  [&]() { print_provenance(value.provenance, indent_level + 1); });
                        }
                        field("name", [&]() { write_string(value.name); });
                        field("type", [&]() { write_string(value.type); });
                        field("value", [&]() { print_expr(*value.value, indent_level + 1); });
                    });
                },
                [&](const ir::TypeAliasDecl &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("type_alias"); });
                        if (has_provenance(value.provenance)) {
                            field("provenance",
                                  [&]() { print_provenance(value.provenance, indent_level + 1); });
                        }
                        field("name", [&]() { write_string(value.name); });
                        field("aliased_type", [&]() { write_string(value.aliased_type); });
                    });
                },
                [&](const ir::StructDecl &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("struct"); });
                        if (has_provenance(value.provenance)) {
                            field("provenance",
                                  [&]() { print_provenance(value.provenance, indent_level + 1); });
                        }
                        field("name", [&]() { write_string(value.name); });
                        field("fields", [&]() {
                            print_array(indent_level + 1, [&](const auto &item) {
                                for (const auto &struct_field : value.fields) {
                                    item([&]() {
                                        print_object(indent_level + 2, [&](const auto &entry) {
                                            entry("name",
                                                  [&]() { write_string(struct_field.name); });
                                            entry("type",
                                                  [&]() { write_string(struct_field.type); });
                                            entry("default_value", [&]() {
                                                if (struct_field.default_value) {
                                                    print_expr(*struct_field.default_value,
                                                               indent_level + 3);
                                                } else {
                                                    write_null();
                                                }
                                            });
                                        });
                                    });
                                }
                            });
                        });
                    });
                },
                [&](const ir::EnumDecl &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("enum"); });
                        if (has_provenance(value.provenance)) {
                            field("provenance",
                                  [&]() { print_provenance(value.provenance, indent_level + 1); });
                        }
                        field("name", [&]() { write_string(value.name); });
                        field("variants",
                              [&]() { write_string_array(value.variants, indent_level + 1); });
                    });
                },
                [&](const ir::CapabilityDecl &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("capability"); });
                        if (has_provenance(value.provenance)) {
                            field("provenance",
                                  [&]() { print_provenance(value.provenance, indent_level + 1); });
                        }
                        field("name", [&]() { write_string(value.name); });
                        field("params", [&]() {
                            print_array(indent_level + 1, [&](const auto &item) {
                                for (const auto &param : value.params) {
                                    item([&]() {
                                        print_object(indent_level + 2, [&](const auto &entry) {
                                            entry("name", [&]() { write_string(param.name); });
                                            entry("type", [&]() { write_string(param.type); });
                                        });
                                    });
                                }
                            });
                        });
                        field("return_type", [&]() { write_string(value.return_type); });
                    });
                },
                [&](const ir::PredicateDecl &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("predicate"); });
                        if (has_provenance(value.provenance)) {
                            field("provenance",
                                  [&]() { print_provenance(value.provenance, indent_level + 1); });
                        }
                        field("name", [&]() { write_string(value.name); });
                        field("params", [&]() {
                            print_array(indent_level + 1, [&](const auto &item) {
                                for (const auto &param : value.params) {
                                    item([&]() {
                                        print_object(indent_level + 2, [&](const auto &entry) {
                                            entry("name", [&]() { write_string(param.name); });
                                            entry("type", [&]() { write_string(param.type); });
                                        });
                                    });
                                }
                            });
                        });
                        field("return_type", [&]() { write_string("Bool"); });
                    });
                },
                [&](const ir::AgentDecl &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("agent"); });
                        if (has_provenance(value.provenance)) {
                            field("provenance",
                                  [&]() { print_provenance(value.provenance, indent_level + 1); });
                        }
                        field("name", [&]() { write_string(value.name); });
                        field("input_type", [&]() { write_string(value.input_type); });
                        field("context_type", [&]() { write_string(value.context_type); });
                        field("output_type", [&]() { write_string(value.output_type); });
                        field("states",
                              [&]() { write_string_array(value.states, indent_level + 1); });
                        field("initial_state", [&]() { write_string(value.initial_state); });
                        field("final_states",
                              [&]() { write_string_array(value.final_states, indent_level + 1); });
                        field("capabilities",
                              [&]() { write_string_array(value.capabilities, indent_level + 1); });
                        field("quota", [&]() {
                            print_array(indent_level + 1, [&](const auto &item) {
                                for (const auto &quota_item : value.quota) {
                                    item([&]() {
                                        print_object(indent_level + 2, [&](const auto &entry) {
                                            entry("name", [&]() { write_string(quota_item.name); });
                                            entry("value",
                                                  [&]() { write_string(quota_item.value); });
                                        });
                                    });
                                }
                            });
                        });
                        field("transitions", [&]() {
                            print_array(indent_level + 1, [&](const auto &item) {
                                for (const auto &transition : value.transitions) {
                                    item([&]() {
                                        print_object(indent_level + 2, [&](const auto &entry) {
                                            entry("from_state",
                                                  [&]() { write_string(transition.from_state); });
                                            entry("to_state",
                                                  [&]() { write_string(transition.to_state); });
                                        });
                                    });
                                }
                            });
                        });
                    });
                },
                [&](const ir::ContractDecl &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("contract"); });
                        if (has_provenance(value.provenance)) {
                            field("provenance",
                                  [&]() { print_provenance(value.provenance, indent_level + 1); });
                        }
                        field("target", [&]() { write_string(value.target); });
                        field("clauses", [&]() {
                            print_array(indent_level + 1, [&](const auto &item) {
                                for (const auto &clause : value.clauses) {
                                    item([&]() {
                                        print_object(indent_level + 2, [&](const auto &entry) {
                                            entry("kind", [&]() {
                                                write_string(contract_clause_name(clause.kind));
                                            });
                                            std::visit(Overloaded{
                                                           [&](const ir::ExprPtr &expr) {
                                                               entry("expr", [&]() {
                                                                   print_expr(*expr,
                                                                              indent_level + 3);
                                                               });
                                                           },
                                                           [&](const ir::TemporalExprPtr &expr) {
                                                               entry("temporal", [&]() {
                                                                   print_temporal_expr(
                                                                       *expr, indent_level + 3);
                                                               });
                                                           },
                                                       },
                                                       clause.value);
                                        });
                                    });
                                }
                            });
                        });
                    });
                },
                [&](const ir::FlowDecl &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("flow"); });
                        if (has_provenance(value.provenance)) {
                            field("provenance",
                                  [&]() { print_provenance(value.provenance, indent_level + 1); });
                        }
                        field("target", [&]() { write_string(value.target); });
                        field("state_handlers", [&]() {
                            print_array(indent_level + 1, [&](const auto &item) {
                                for (const auto &handler : value.state_handlers) {
                                    item([&]() {
                                        print_object(indent_level + 2, [&](const auto &entry) {
                                            entry("state_name",
                                                  [&]() { write_string(handler.state_name); });
                                            entry("policy", [&]() {
                                                print_array(
                                                    indent_level + 3, [&](const auto &policy_item) {
                                                        for (const auto &policy : handler.policy) {
                                                            policy_item([&]() {
                                                                print_state_policy_item(
                                                                    policy, indent_level + 4);
                                                            });
                                                        }
                                                    });
                                            });
                                            entry("summary", [&]() {
                                                print_flow_summary(handler.summary,
                                                                   indent_level + 3);
                                            });
                                            entry("body", [&]() {
                                                print_block(handler.body, indent_level + 3);
                                            });
                                        });
                                    });
                                }
                            });
                        });
                    });
                },
                [&](const ir::WorkflowDecl &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("workflow"); });
                        if (has_provenance(value.provenance)) {
                            field("provenance",
                                  [&]() { print_provenance(value.provenance, indent_level + 1); });
                        }
                        field("name", [&]() { write_string(value.name); });
                        field("input_type", [&]() { write_string(value.input_type); });
                        field("output_type", [&]() { write_string(value.output_type); });
                        field("nodes", [&]() {
                            print_array(indent_level + 1, [&](const auto &item) {
                                for (const auto &node : value.nodes) {
                                    item([&]() {
                                        print_object(indent_level + 2, [&](const auto &entry) {
                                            entry("name", [&]() { write_string(node.name); });
                                            entry("target", [&]() { write_string(node.target); });
                                            entry("input", [&]() {
                                                print_expr(*node.input, indent_level + 3);
                                            });
                                            entry("input_summary", [&]() {
                                                print_workflow_expr_summary(node.input_summary,
                                                                            indent_level + 3);
                                            });
                                            entry("after", [&]() {
                                                write_string_array(node.after, indent_level + 3);
                                            });
                                        });
                                    });
                                }
                            });
                        });
                        field("safety", [&]() {
                            print_array(indent_level + 1, [&](const auto &item) {
                                for (const auto &formula : value.safety) {
                                    item(
                                        [&]() { print_temporal_expr(*formula, indent_level + 2); });
                                }
                            });
                        });
                        field("liveness", [&]() {
                            print_array(indent_level + 1, [&](const auto &item) {
                                for (const auto &formula : value.liveness) {
                                    item(
                                        [&]() { print_temporal_expr(*formula, indent_level + 2); });
                                }
                            });
                        });
                        field("return_summary", [&]() {
                            print_workflow_expr_summary(value.return_summary, indent_level + 1);
                        });
                        field("return_value",
                              [&]() { print_expr(*value.return_value, indent_level + 1); });
                    });
                },
            },
            declaration);
    }
};

} // namespace

void print_program_ir_json(const ir::Program &program, std::ostream &out) {
    IrJsonPrinter printer(out);
    printer.print(program);
}

void emit_program_ir_json(const ast::Program &program,
                          const ResolveResult &resolve_result,
                          const TypeCheckResult &type_check_result,
                          std::ostream &out) {
    print_program_ir_json(lower_program_ir(program, resolve_result, type_check_result), out);
}

void emit_program_ir_json(const SourceGraph &graph,
                          const ResolveResult &resolve_result,
                          const TypeCheckResult &type_check_result,
                          std::ostream &out) {
    print_program_ir_json(lower_program_ir(graph, resolve_result, type_check_result), out);
}

} // namespace ahfl
