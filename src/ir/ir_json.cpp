#include "ahfl/ir/ir.hpp"

#include <cstddef>
#include <cstdint>
#include <ostream>
#include <string_view>
#include <utility>
#include <variant>

#include "ahfl/support/json.hpp"
#include "ahfl/support/overloaded.hpp"

namespace ahfl {

namespace {


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

[[nodiscard]] std::string_view symbol_ref_kind_name(ir::SymbolRefKind kind) {
    switch (kind) {
    case ir::SymbolRefKind::Unknown:
        return "unknown";
    case ir::SymbolRefKind::Type:
        return "type";
    case ir::SymbolRefKind::Const:
        return "const";
    case ir::SymbolRefKind::Capability:
        return "capability";
    case ir::SymbolRefKind::Predicate:
        return "predicate";
    case ir::SymbolRefKind::Agent:
        return "agent";
    case ir::SymbolRefKind::Workflow:
        return "workflow";
    }

    return "invalid";
}

[[nodiscard]] std::string_view type_ref_kind_name(ir::TypeRefKind kind) {
    switch (kind) {
    case ir::TypeRefKind::Unresolved:
        return "unresolved";
    case ir::TypeRefKind::Any:
        return "any";
    case ir::TypeRefKind::Never:
        return "never";
    case ir::TypeRefKind::Unit:
        return "unit";
    case ir::TypeRefKind::Bool:
        return "bool";
    case ir::TypeRefKind::Int:
        return "int";
    case ir::TypeRefKind::Float:
        return "float";
    case ir::TypeRefKind::String:
        return "string";
    case ir::TypeRefKind::BoundedString:
        return "bounded_string";
    case ir::TypeRefKind::UUID:
        return "uuid";
    case ir::TypeRefKind::Timestamp:
        return "timestamp";
    case ir::TypeRefKind::Duration:
        return "duration";
    case ir::TypeRefKind::Decimal:
        return "decimal";
    case ir::TypeRefKind::Struct:
        return "struct";
    case ir::TypeRefKind::Enum:
        return "enum";
    case ir::TypeRefKind::Optional:
        return "optional";
    case ir::TypeRefKind::List:
        return "list";
    case ir::TypeRefKind::Set:
        return "set";
    case ir::TypeRefKind::Map:
        return "map";
    }

    return "invalid";
}

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

    [[nodiscard]] bool has_source_range(const ir::SourceRangeOpt &range) const {
        return range.has_value();
    }

    void print_source_range(SourceRange range, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("begin_offset", [&]() { out_ << range.begin_offset; });
            field("end_offset", [&]() { out_ << range.end_offset; });
        });
    }

    template <typename Field>
    void print_source_range_field(const Field &field,
                                  const ir::SourceRangeOpt &range,
                                  int indent_level) {
        if (has_source_range(range)) {
            field("source_range", [&]() { print_source_range(*range, indent_level); });
        }
    }

    [[nodiscard]] bool has_provenance(const ir::DeclarationProvenance &provenance) const {
        return !provenance.module_name.empty() || !provenance.source_path.empty() ||
               has_source_range(provenance.source_range);
    }

    void print_provenance(const ir::DeclarationProvenance &provenance, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("module_name", [&]() { write_string(provenance.module_name); });
            field("source_path", [&]() { write_string(provenance.source_path); });
            print_source_range_field(field, provenance.source_range, indent_level + 1);
        });
    }

    void write_bool(bool value) {
        out_ << (value ? "true" : "false");
    }

    void write_index(std::size_t value) {
        out_ << value;
    }

    void write_i64(std::int64_t value) {
        out_ << value;
    }

    [[nodiscard]] bool has_symbol_ref(const ir::SymbolRef &ref) const {
        return ref.kind != ir::SymbolRefKind::Unknown || !ref.canonical_name.empty() ||
               !ref.local_name.empty() || !ref.module_name.empty();
    }

    [[nodiscard]] bool has_type_ref(const ir::TypeRef &ref) const {
        return ref.kind != ir::TypeRefKind::Unresolved || !ref.display_name.empty() ||
               !ref.canonical_name.empty() || ref.string_bounds.has_value() ||
               ref.decimal_scale.has_value() || ref.first || ref.second;
    }

    void print_symbol_ref(const ir::SymbolRef &ref, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("kind", [&]() { write_string(symbol_ref_kind_name(ref.kind)); });
            field("canonical_name", [&]() { write_string(ref.canonical_name); });
            if (!ref.local_name.empty()) {
                field("local_name", [&]() { write_string(ref.local_name); });
            }
            if (!ref.module_name.empty()) {
                field("module_name", [&]() { write_string(ref.module_name); });
            }
        });
    }

    void print_type_ref(const ir::TypeRef &ref, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("kind", [&]() { write_string(type_ref_kind_name(ref.kind)); });
            field("display_name", [&]() { write_string(ref.display_name); });
            if (!ref.canonical_name.empty()) {
                field("canonical_name", [&]() { write_string(ref.canonical_name); });
            }
            if (ref.string_bounds.has_value()) {
                field("string_bounds", [&]() {
                    print_object(indent_level + 1, [&](const auto &bounds_field) {
                        bounds_field("minimum", [&]() { write_i64(ref.string_bounds->first); });
                        bounds_field("maximum", [&]() { write_i64(ref.string_bounds->second); });
                    });
                });
            }
            if (ref.decimal_scale.has_value()) {
                field("decimal_scale", [&]() { write_i64(*ref.decimal_scale); });
            }
            if (ref.first) {
                const auto first_name =
                    ref.kind == ir::TypeRefKind::Map ? "key_type" : "element_type";
                field(first_name, [&]() { print_type_ref(*ref.first, indent_level + 1); });
            }
            if (ref.second) {
                field("value_type", [&]() { print_type_ref(*ref.second, indent_level + 1); });
            }
        });
    }

    void print_param(const ir::ParamDecl &param, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("name", [&]() { write_string(param.name); });
            field("type", [&]() { write_string(param.type); });
            if (has_type_ref(param.type_ref)) {
                field("type_ref", [&]() { print_type_ref(param.type_ref, indent_level + 1); });
            }
            print_source_range_field(field, param.source_range, indent_level + 1);
        });
    }

    void print_params(const std::vector<ir::ParamDecl> &params, int indent_level) {
        print_array(indent_level, [&](const auto &item) {
            for (const auto &param : params) {
                item([&]() { print_param(param, indent_level + 1); });
            }
        });
    }

    void print_capability_effect(const ir::CapabilityEffectSpec &effect, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("declared", [&]() { write_bool(effect.declared); });
            field("kind", [&]() { write_string(capability_effect_kind_name(effect.kind)); });
            field("receipt_mode",
                  [&]() { write_string(capability_receipt_mode_name(effect.receipt_mode)); });
            field("retry_mode",
                  [&]() { write_string(capability_retry_mode_name(effect.retry_mode)); });
            field("domain", [&]() {
                if (effect.domain.has_value()) {
                    write_string(*effect.domain);
                } else {
                    write_null();
                }
            });
            field("idempotency_key", [&]() {
                if (effect.idempotency_key.has_value()) {
                    write_string(*effect.idempotency_key);
                } else {
                    write_null();
                }
            });
            field("timeout", [&]() {
                if (effect.timeout.has_value()) {
                    write_string(*effect.timeout);
                } else {
                    write_null();
                }
            });
            field("compensation", [&]() {
                if (effect.compensation.has_value()) {
                    write_string(*effect.compensation);
                } else {
                    write_null();
                }
            });
            field("policies", [&]() { write_string_array(effect.policies, indent_level + 1); });
            print_source_range_field(field, effect.source_range, indent_level + 1);
        });
    }

    void print_symbol_ref_array(const std::vector<ir::SymbolRef> &refs, int indent_level) {
        print_array(indent_level, [&](const auto &item) {
            for (const auto &ref : refs) {
                item([&]() { print_symbol_ref(ref, indent_level + 1); });
            }
        });
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
                        print_source_range_field(field, expr.source_range, indent_level + 1);
                    });
                },
                [&](const ir::BoolLiteralExpr &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("bool_literal"); });
                        print_source_range_field(field, expr.source_range, indent_level + 1);
                        field("value", [&]() { write_bool(value.value); });
                    });
                },
                [&](const ir::IntegerLiteralExpr &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("integer_literal"); });
                        print_source_range_field(field, expr.source_range, indent_level + 1);
                        field("spelling", [&]() { write_string(value.spelling); });
                    });
                },
                [&](const ir::FloatLiteralExpr &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("float_literal"); });
                        print_source_range_field(field, expr.source_range, indent_level + 1);
                        field("spelling", [&]() { write_string(value.spelling); });
                    });
                },
                [&](const ir::DecimalLiteralExpr &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("decimal_literal"); });
                        print_source_range_field(field, expr.source_range, indent_level + 1);
                        field("spelling", [&]() { write_string(value.spelling); });
                    });
                },
                [&](const ir::StringLiteralExpr &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("string_literal"); });
                        print_source_range_field(field, expr.source_range, indent_level + 1);
                        field("spelling", [&]() { write_string(value.spelling); });
                    });
                },
                [&](const ir::DurationLiteralExpr &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("duration_literal"); });
                        print_source_range_field(field, expr.source_range, indent_level + 1);
                        field("spelling", [&]() { write_string(value.spelling); });
                    });
                },
                [&](const ir::SomeExpr &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("some"); });
                        print_source_range_field(field, expr.source_range, indent_level + 1);
                        field("value", [&]() { print_expr(*value.value, indent_level + 1); });
                    });
                },
                [&](const ir::PathExpr &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("path"); });
                        print_source_range_field(field, expr.source_range, indent_level + 1);
                        field("path", [&]() { print_path(value.path, indent_level + 1); });
                    });
                },
                [&](const ir::QualifiedValueExpr &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("qualified_value"); });
                        print_source_range_field(field, expr.source_range, indent_level + 1);
                        field("value", [&]() { write_string(value.value); });
                    });
                },
                [&](const ir::CallExpr &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("call"); });
                        print_source_range_field(field, expr.source_range, indent_level + 1);
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
                        print_source_range_field(field, expr.source_range, indent_level + 1);
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
                        print_source_range_field(field, expr.source_range, indent_level + 1);
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
                        print_source_range_field(field, expr.source_range, indent_level + 1);
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
                        print_source_range_field(field, expr.source_range, indent_level + 1);
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
                        print_source_range_field(field, expr.source_range, indent_level + 1);
                        field("op", [&]() { write_string(expr_unary_op_name(value.op)); });
                        field("operand", [&]() { print_expr(*value.operand, indent_level + 1); });
                    });
                },
                [&](const ir::BinaryExpr &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("binary"); });
                        print_source_range_field(field, expr.source_range, indent_level + 1);
                        field("op", [&]() { write_string(expr_binary_op_name(value.op)); });
                        field("lhs", [&]() { print_expr(*value.lhs, indent_level + 1); });
                        field("rhs", [&]() { print_expr(*value.rhs, indent_level + 1); });
                    });
                },
                [&](const ir::MemberAccessExpr &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("member_access"); });
                        print_source_range_field(field, expr.source_range, indent_level + 1);
                        field("base", [&]() { print_expr(*value.base, indent_level + 1); });
                        field("member", [&]() { write_string(value.member); });
                    });
                },
                [&](const ir::IndexAccessExpr &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("index_access"); });
                        print_source_range_field(field, expr.source_range, indent_level + 1);
                        field("base", [&]() { print_expr(*value.base, indent_level + 1); });
                        field("index", [&]() { print_expr(*value.index, indent_level + 1); });
                    });
                },
                [&](const ir::GroupExpr &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("group"); });
                        print_source_range_field(field, expr.source_range, indent_level + 1);
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
                        print_source_range_field(field, expr.source_range, indent_level + 1);
                        field("expr", [&]() { print_expr(*value.expr, indent_level + 1); });
                    });
                },
                [&](const ir::CalledTemporalExpr &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("called"); });
                        print_source_range_field(field, expr.source_range, indent_level + 1);
                        field("capability", [&]() { write_string(value.capability); });
                    });
                },
                [&](const ir::InStateTemporalExpr &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("in_state"); });
                        print_source_range_field(field, expr.source_range, indent_level + 1);
                        field("state", [&]() { write_string(value.state); });
                    });
                },
                [&](const ir::RunningTemporalExpr &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("running"); });
                        print_source_range_field(field, expr.source_range, indent_level + 1);
                        field("node", [&]() { write_string(value.node); });
                    });
                },
                [&](const ir::CompletedTemporalExpr &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("completed"); });
                        print_source_range_field(field, expr.source_range, indent_level + 1);
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
                        print_source_range_field(field, expr.source_range, indent_level + 1);
                        field("op", [&]() { write_string(temporal_unary_op_name(value.op)); });
                        field("operand",
                              [&]() { print_temporal_expr(*value.operand, indent_level + 1); });
                    });
                },
                [&](const ir::TemporalBinaryExpr &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("binary"); });
                        print_source_range_field(field, expr.source_range, indent_level + 1);
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
                        print_source_range_field(field, statement.source_range, indent_level + 1);
                        field("name", [&]() { write_string(value.name); });
                        field("type", [&]() { write_string(value.type); });
                        field("initializer",
                              [&]() { print_expr(*value.initializer, indent_level + 1); });
                    });
                },
                [&](const ir::AssignStatement &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("assign"); });
                        print_source_range_field(field, statement.source_range, indent_level + 1);
                        field("target", [&]() { print_path(value.target, indent_level + 1); });
                        field("value", [&]() { print_expr(*value.value, indent_level + 1); });
                    });
                },
                [&](const ir::IfStatement &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("if"); });
                        print_source_range_field(field, statement.source_range, indent_level + 1);
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
                        print_source_range_field(field, statement.source_range, indent_level + 1);
                        field("target_state", [&]() { write_string(value.target_state); });
                    });
                },
                [&](const ir::ReturnStatement &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("return"); });
                        print_source_range_field(field, statement.source_range, indent_level + 1);
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
                        print_source_range_field(field, statement.source_range, indent_level + 1);
                        field("condition",
                              [&]() { print_expr(*value.condition, indent_level + 1); });
                    });
                },
                [&](const ir::ExprStatement &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("expr"); });
                        print_source_range_field(field, statement.source_range, indent_level + 1);
                        field("expr", [&]() { print_expr(*value.expr, indent_level + 1); });
                    });
                },
            },
            statement.node);
    }

    void print_block(const ir::Block &block, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            print_source_range_field(field, block.source_range, indent_level + 1);
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
                        if (has_symbol_ref(value.symbol_ref)) {
                            field("symbol_ref",
                                  [&]() { print_symbol_ref(value.symbol_ref, indent_level + 1); });
                        }
                        field("type", [&]() { write_string(value.type); });
                        if (has_type_ref(value.type_ref)) {
                            field("type_ref",
                                  [&]() { print_type_ref(value.type_ref, indent_level + 1); });
                        }
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
                        if (has_symbol_ref(value.symbol_ref)) {
                            field("symbol_ref",
                                  [&]() { print_symbol_ref(value.symbol_ref, indent_level + 1); });
                        }
                        field("aliased_type", [&]() { write_string(value.aliased_type); });
                        if (has_type_ref(value.aliased_type_ref)) {
                            field("aliased_type_ref", [&]() {
                                print_type_ref(value.aliased_type_ref, indent_level + 1);
                            });
                        }
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
                        if (has_symbol_ref(value.symbol_ref)) {
                            field("symbol_ref",
                                  [&]() { print_symbol_ref(value.symbol_ref, indent_level + 1); });
                        }
                        field("fields", [&]() {
                            print_array(indent_level + 1, [&](const auto &item) {
                                for (const auto &struct_field : value.fields) {
                                    item([&]() {
                                        print_object(indent_level + 2, [&](const auto &entry) {
                                            entry("name",
                                                  [&]() { write_string(struct_field.name); });
                                            entry("type",
                                                  [&]() { write_string(struct_field.type); });
                                            if (has_type_ref(struct_field.type_ref)) {
                                                entry("type_ref", [&]() {
                                                    print_type_ref(struct_field.type_ref,
                                                                   indent_level + 3);
                                                });
                                            }
                                            print_source_range_field(
                                                entry, struct_field.source_range, indent_level + 3);
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
                        if (has_symbol_ref(value.symbol_ref)) {
                            field("symbol_ref",
                                  [&]() { print_symbol_ref(value.symbol_ref, indent_level + 1); });
                        }
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
                        if (has_symbol_ref(value.symbol_ref)) {
                            field("symbol_ref",
                                  [&]() { print_symbol_ref(value.symbol_ref, indent_level + 1); });
                        }
                        field("params", [&]() { print_params(value.params, indent_level + 1); });
                        field("return_type", [&]() { write_string(value.return_type); });
                        if (has_type_ref(value.return_type_ref)) {
                            field("return_type_ref", [&]() {
                                print_type_ref(value.return_type_ref, indent_level + 1);
                            });
                        }
                        if (value.effect.declared) {
                            field("effect", [&]() {
                                print_capability_effect(value.effect, indent_level + 1);
                            });
                        }
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
                        if (has_symbol_ref(value.symbol_ref)) {
                            field("symbol_ref",
                                  [&]() { print_symbol_ref(value.symbol_ref, indent_level + 1); });
                        }
                        field("params", [&]() { print_params(value.params, indent_level + 1); });
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
                        if (has_symbol_ref(value.symbol_ref)) {
                            field("symbol_ref",
                                  [&]() { print_symbol_ref(value.symbol_ref, indent_level + 1); });
                        }
                        field("input_type", [&]() { write_string(value.input_type); });
                        if (has_type_ref(value.input_type_ref)) {
                            field("input_type_ref", [&]() {
                                print_type_ref(value.input_type_ref, indent_level + 1);
                            });
                        }
                        field("context_type", [&]() { write_string(value.context_type); });
                        if (has_type_ref(value.context_type_ref)) {
                            field("context_type_ref", [&]() {
                                print_type_ref(value.context_type_ref, indent_level + 1);
                            });
                        }
                        field("output_type", [&]() { write_string(value.output_type); });
                        if (has_type_ref(value.output_type_ref)) {
                            field("output_type_ref", [&]() {
                                print_type_ref(value.output_type_ref, indent_level + 1);
                            });
                        }
                        field("states",
                              [&]() { write_string_array(value.states, indent_level + 1); });
                        field("initial_state", [&]() { write_string(value.initial_state); });
                        field("final_states",
                              [&]() { write_string_array(value.final_states, indent_level + 1); });
                        field("capabilities",
                              [&]() { write_string_array(value.capabilities, indent_level + 1); });
                        if (!value.capability_refs.empty()) {
                            field("capability_refs", [&]() {
                                print_symbol_ref_array(value.capability_refs, indent_level + 1);
                            });
                        }
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
                        if (has_symbol_ref(value.target_ref)) {
                            field("target_ref",
                                  [&]() { print_symbol_ref(value.target_ref, indent_level + 1); });
                        }
                        field("clauses", [&]() {
                            print_array(indent_level + 1, [&](const auto &item) {
                                for (const auto &clause : value.clauses) {
                                    item([&]() {
                                        print_object(indent_level + 2, [&](const auto &entry) {
                                            entry("kind", [&]() {
                                                write_string(contract_clause_name(clause.kind));
                                            });
                                            print_source_range_field(
                                                entry, clause.source_range, indent_level + 3);
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
                        if (has_symbol_ref(value.target_ref)) {
                            field("target_ref",
                                  [&]() { print_symbol_ref(value.target_ref, indent_level + 1); });
                        }
                        field("state_handlers", [&]() {
                            print_array(indent_level + 1, [&](const auto &item) {
                                for (const auto &handler : value.state_handlers) {
                                    item([&]() {
                                        print_object(indent_level + 2, [&](const auto &entry) {
                                            entry("state_name",
                                                  [&]() { write_string(handler.state_name); });
                                            print_source_range_field(
                                                entry, handler.source_range, indent_level + 3);
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
                        if (has_symbol_ref(value.symbol_ref)) {
                            field("symbol_ref",
                                  [&]() { print_symbol_ref(value.symbol_ref, indent_level + 1); });
                        }
                        field("input_type", [&]() { write_string(value.input_type); });
                        if (has_type_ref(value.input_type_ref)) {
                            field("input_type_ref", [&]() {
                                print_type_ref(value.input_type_ref, indent_level + 1);
                            });
                        }
                        field("output_type", [&]() { write_string(value.output_type); });
                        if (has_type_ref(value.output_type_ref)) {
                            field("output_type_ref", [&]() {
                                print_type_ref(value.output_type_ref, indent_level + 1);
                            });
                        }
                        field("nodes", [&]() {
                            print_array(indent_level + 1, [&](const auto &item) {
                                for (const auto &node : value.nodes) {
                                    item([&]() {
                                        print_object(indent_level + 2, [&](const auto &entry) {
                                            entry("name", [&]() { write_string(node.name); });
                                            print_source_range_field(
                                                entry, node.source_range, indent_level + 3);
                                            entry("target", [&]() { write_string(node.target); });
                                            if (has_symbol_ref(node.target_ref)) {
                                                entry("target_ref", [&]() {
                                                    print_symbol_ref(node.target_ref,
                                                                     indent_level + 3);
                                                });
                                            }
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
