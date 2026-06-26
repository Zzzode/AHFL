#include "ahfl/compiler/ir/lowering.hpp"

#include "ahfl/compiler/ir/analysis.hpp"
#include "ahfl/compiler/ir/identity.hpp"

#include <cstddef>
#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "ahfl/base/support/overloaded.hpp"
#include "base/support/json.hpp"

namespace ahfl {

namespace {

[[nodiscard]] std::string_view path_root_kind_name(ir::PathRootKind kind) {
    switch (kind) {
    case ir::PathRootKind::Identifier:
        return "identifier";
    case ir::PathRootKind::Input:
        return "input";
    case ir::PathRootKind::Context:
        return "context";
    case ir::PathRootKind::Output:
        return "output";
    case ir::PathRootKind::State:
        return "state";
    case ir::PathRootKind::Local:
        return "local";
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
    case ir::ContractClauseKind::Decreases:
        return "decreases";
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
    case ir::SymbolRefKind::Function:
        return "function";
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
    case ir::TypeRefKind::Fn:
        return "fn";
    }

    return "invalid";
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
        program_ = &program;
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(program.format_version); });
            field("formal_observations", [&]() {
                print_array(1, [&](const auto &item) {
                    for (const auto &observation : ir::formal_observations(program)) {
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
    const ir::Program *program_{nullptr};

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

    void write_bool(bool value) {
        out_ << (value ? "true" : "false");
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

    void write_index(std::size_t value) {
        out_ << value;
    }

    void write_i64(std::int64_t value) {
        out_ << value;
    }

    [[nodiscard]] bool has_symbol_ref(const ir::SymbolRef &ref) const {
        return ref.kind != ir::SymbolRefKind::Unknown || !ref.canonical_name.empty() ||
               !ref.local_name.empty() || !ref.module_name.empty() || ref.id.has_value();
    }

    [[nodiscard]] bool has_type_ref(const ir::TypeRef &ref) const {
        return ref.kind != ir::TypeRefKind::Unresolved || !ref.display_name.empty() ||
               !ref.canonical_name.empty() || !ref.variant_name.empty() ||
               ref.string_bounds.has_value() || ref.decimal_scale.has_value() || ref.first ||
               ref.second || !ref.params.empty() || has_source_range(ref.source_range);
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
            if (ref.id.has_value()) {
                field("id", [&]() { write_index(*ref.id); });
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
            if (!ref.variant_name.empty()) {
                field("variant_name", [&]() { write_string(ref.variant_name); });
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
                const auto first_name = "element_type";
                field(first_name, [&]() { print_type_ref(*ref.first, indent_level + 1); });
            }
            if (ref.second) {
                field("value_type", [&]() { print_type_ref(*ref.second, indent_level + 1); });
            }
            if (!ref.params.empty()) {
                field("type_args", [&]() {
                    print_array(indent_level + 1, [&](const auto &item) {
                        for (const auto &param : ref.params) {
                            item([&]() {
                                if (param) {
                                    print_type_ref(*param, indent_level + 2);
                                } else {
                                    write_null();
                                }
                            });
                        }
                    });
                });
            }
            print_source_range_field(field, ref.source_range, indent_level + 1);
        });
    }

    template <typename Field>
    void print_expr_common_fields(const Field &field, const ir::Expr &expr, int indent_level) {
        field("id", [&]() { write_index(expr.id); });
        print_source_range_field(field, expr.source_range, indent_level);
        if (has_type_ref(expr.resolved_type)) {
            field("resolved_type", [&]() { print_type_ref(expr.resolved_type, indent_level); });
        }
    }

    void print_param(const ir::ParamDecl &param, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("name", [&]() { write_string(param.name); });
            field("type", [&]() { write_string(type_name(param.type_ref)); });
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

	    void print_match_pattern_common(const auto &field,
	                                    const ir::MatchPattern &pattern,
	                                    int indent_level) {
	        field("text", [&]() { write_string(pattern.text); });
	        print_source_range_field(field, pattern.source_range, indent_level);
	    }

	    void print_match_pattern(const ir::MatchPattern &pattern, int indent_level) {
	        std::visit(
	            Overloaded{
	                [&](const ir::LiteralPattern &value) {
	                    print_object(indent_level, [&](const auto &field) {
	                        field("kind", [&]() { write_string("literal"); });
	                        print_match_pattern_common(field, pattern, indent_level + 1);
	                        field("spelling", [&]() { write_string(value.spelling); });
	                    });
	                },
	                [&](const ir::VariantPattern &value) {
	                    print_object(indent_level, [&](const auto &field) {
	                        field("kind", [&]() { write_string("variant"); });
	                        print_match_pattern_common(field, pattern, indent_level + 1);
	                        field("path", [&]() { write_string(value.path); });
	                        field("subpatterns", [&]() {
	                            print_array(indent_level + 1, [&](const auto &item) {
	                                for (const auto &subpattern : value.subpatterns) {
	                                    item([&]() {
	                                        if (subpattern) {
	                                            print_match_pattern(*subpattern, indent_level + 2);
	                                        } else {
	                                            write_null();
	                                        }
	                                    });
	                                }
	                            });
	                        });
	                    });
	                },
	                [&](const ir::WildcardPattern &) {
	                    print_object(indent_level, [&](const auto &field) {
	                        field("kind", [&]() { write_string("wildcard"); });
	                        print_match_pattern_common(field, pattern, indent_level + 1);
	                    });
	                },
	                [&](const ir::BindingPattern &value) {
	                    print_object(indent_level, [&](const auto &field) {
	                        field("kind", [&]() { write_string("binding"); });
	                        print_match_pattern_common(field, pattern, indent_level + 1);
	                        field("name", [&]() { write_string(value.name); });
	                        field("is_mut", [&]() { write_bool(value.is_mut); });
	                        field("nested", [&]() {
	                            if (value.nested) {
	                                print_match_pattern(*value.nested, indent_level + 1);
	                            } else {
	                                write_null();
	                            }
	                        });
	                    });
	                },
	                [&](const ir::TuplePattern &value) {
	                    print_object(indent_level, [&](const auto &field) {
	                        field("kind", [&]() { write_string("tuple"); });
	                        print_match_pattern_common(field, pattern, indent_level + 1);
	                        field("elements", [&]() {
	                            print_array(indent_level + 1, [&](const auto &item) {
	                                for (const auto &element : value.elements) {
	                                    item([&]() {
	                                        if (element) {
	                                            print_match_pattern(*element, indent_level + 2);
	                                        } else {
	                                            write_null();
	                                        }
	                                    });
	                                }
	                            });
	                        });
	                    });
	                },
	                [&](const ir::OrPattern &value) {
	                    print_object(indent_level, [&](const auto &field) {
	                        field("kind", [&]() { write_string("or"); });
	                        print_match_pattern_common(field, pattern, indent_level + 1);
	                        field("branches", [&]() {
	                            print_array(indent_level + 1, [&](const auto &item) {
	                                for (const auto &branch : value.branches) {
	                                    item([&]() {
	                                        if (branch) {
	                                            print_match_pattern(*branch, indent_level + 2);
	                                        } else {
	                                            write_null();
	                                        }
	                                    });
	                                }
	                            });
	                        });
	                    });
	                },
	            },
	            pattern.node);
	    }

	    void print_expr(const ir::Expr &expr, int indent_level) {
	        std::visit(
            Overloaded{
                [&](const ir::BoolLiteralExpr &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("bool_literal"); });
                        print_expr_common_fields(field, expr, indent_level + 1);
                        field("value", [&]() { write_bool(value.value); });
                    });
                },
                [&](const ir::IntegerLiteralExpr &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("integer_literal"); });
                        print_expr_common_fields(field, expr, indent_level + 1);
                        field("spelling", [&]() { write_string(value.spelling); });
                    });
                },
                [&](const ir::FloatLiteralExpr &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("float_literal"); });
                        print_expr_common_fields(field, expr, indent_level + 1);
                        field("spelling", [&]() { write_string(value.spelling); });
                    });
                },
                [&](const ir::DecimalLiteralExpr &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("decimal_literal"); });
                        print_expr_common_fields(field, expr, indent_level + 1);
                        field("spelling", [&]() { write_string(value.spelling); });
                    });
                },
                [&](const ir::StringLiteralExpr &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("string_literal"); });
                        print_expr_common_fields(field, expr, indent_level + 1);
                        field("spelling", [&]() { write_string(value.spelling); });
                    });
                },
                [&](const ir::DurationLiteralExpr &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("duration_literal"); });
                        print_expr_common_fields(field, expr, indent_level + 1);
                        field("spelling", [&]() { write_string(value.spelling); });
                    });
                },
                [&](const ir::PathExpr &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("path"); });
                        print_expr_common_fields(field, expr, indent_level + 1);
                        field("path", [&]() { print_path(value.path, indent_level + 1); });
                    });
                },
                [&](const ir::QualifiedValueExpr &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("qualified_value"); });
                        print_expr_common_fields(field, expr, indent_level + 1);
                        field("value", [&]() { write_string(value.value); });
                    });
                },
                [&](const ir::CallExpr &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("call"); });
                        print_expr_common_fields(field, expr, indent_level + 1);
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
                [&](const ir::LambdaExpr &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("lambda"); });
                        print_expr_common_fields(field, expr, indent_level + 1);
                        field("params",
                              [&]() { write_string_array(value.params, indent_level + 1); });
                        field("body", [&]() {
                            if (value.body) {
                                print_expr(*value.body, indent_level + 1);
                            } else {
                                out_ << "null";
                            }
                        });
                    });
                },
                [&](const ir::StructLiteralExpr &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("struct_literal"); });
                        print_expr_common_fields(field, expr, indent_level + 1);
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
                [&](const ir::UnaryExpr &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("unary"); });
                        print_expr_common_fields(field, expr, indent_level + 1);
                        field("op", [&]() { write_string(expr_unary_op_name(value.op)); });
                        field("operand", [&]() { print_expr(*value.operand, indent_level + 1); });
                    });
                },
                [&](const ir::BinaryExpr &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("binary"); });
                        print_expr_common_fields(field, expr, indent_level + 1);
                        field("op", [&]() { write_string(expr_binary_op_name(value.op)); });
                        field("lhs", [&]() { print_expr(*value.lhs, indent_level + 1); });
                        field("rhs", [&]() { print_expr(*value.rhs, indent_level + 1); });
                    });
                },
                [&](const ir::MemberAccessExpr &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("member_access"); });
                        print_expr_common_fields(field, expr, indent_level + 1);
                        field("base", [&]() { print_expr(*value.base, indent_level + 1); });
                        field("member", [&]() { write_string(value.member); });
                    });
                },
	                [&](const ir::IndexAccessExpr &value) {
	                    print_object(indent_level, [&](const auto &field) {
	                        field("kind", [&]() { write_string("index_access"); });
	                        print_expr_common_fields(field, expr, indent_level + 1);
	                        field("base", [&]() { print_expr(*value.base, indent_level + 1); });
	                        field("index", [&]() { print_expr(*value.index, indent_level + 1); });
	                    });
	                },
	                [&](const ir::MatchExpr &value) {
	                    print_object(indent_level, [&](const auto &field) {
	                        field("kind", [&]() { write_string("match"); });
	                        print_expr_common_fields(field, expr, indent_level + 1);
	                        field("scrutinee", [&]() { print_expr(*value.scrutinee, indent_level + 1); });
	                        field("arms", [&]() {
	                            print_array(indent_level + 1, [&](const auto &item) {
	                                for (const auto &arm : value.arms) {
	                                    item([&]() {
	                                        print_object(
	                                            indent_level + 2, [&](const auto &arm_field) {
	                                                arm_field("pattern", [&]() {
	                                                    print_match_pattern(arm.pattern,
	                                                                        indent_level + 3);
	                                                });
	                                                arm_field("guard", [&]() {
	                                                    if (arm.guard) {
	                                                        print_expr(*arm.guard, indent_level + 3);
	                                                    } else {
	                                                        write_null();
	                                                    }
	                                                });
	                                                arm_field("body", [&]() {
	                                                    print_expr(*arm.body, indent_level + 3);
	                                                });
	                                            });
	                                    });
	                                }
	                            });
	                        });
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
                        field("type", [&]() { write_string(type_name(value.type_ref)); });
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
                        field("type", [&]() { write_string(type_name(value.type_ref)); });
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
                        field("aliased_type",
                              [&]() { write_string(type_name(value.aliased_type_ref)); });
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
                                            entry("type", [&]() {
                                                write_string(type_name(struct_field.type_ref));
                                            });
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
                        field("return_type",
                              [&]() { write_string(type_name(value.return_type_ref)); });
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
                        field("input_type",
                              [&]() { write_string(type_name(value.input_type_ref)); });
                        if (has_type_ref(value.input_type_ref)) {
                            field("input_type_ref", [&]() {
                                print_type_ref(value.input_type_ref, indent_level + 1);
                            });
                        }
                        field("context_type",
                              [&]() { write_string(type_name(value.context_type_ref)); });
                        if (has_type_ref(value.context_type_ref)) {
                            field("context_type_ref", [&]() {
                                print_type_ref(value.context_type_ref, indent_level + 1);
                            });
                        }
                        field("output_type",
                              [&]() { write_string(type_name(value.output_type_ref)); });
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
                        field("capabilities", [&]() {
                            write_string_array(symbol_names(value.capability_refs),
                                               indent_level + 1);
                        });
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
                        field("target", [&]() { write_string(symbol_name(value.target_ref)); });
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
                                            if (clause.is_wildcard) {
                                                entry("wildcard", [&]() { write_bool(true); });
                                            }
                                            print_source_range_field(
                                                entry, clause.source_range, indent_level + 3);
                                            std::visit(Overloaded{
                                                           [&](const ir::ExprRef &expr) {
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
                                            // P4.S6: decreases clause fields.
                                            // Omits the entire block when no
                                            // decreases is declared so JSON
                                            // size stays compact for legacy
                                            // programs; a from-json consumer
                                            // treats missing fields as `false`
                                            // / empty.
                                            if (clause.decreases_wildcard ||
                                                !clause.decreases_terms.empty()) {
                                                entry("decreases", [&]() {
                                                    print_object(indent_level + 3,
                                                                 [&](const auto &dec) {
                                                                     dec("wildcard",
                                                                         [&]() {
                                                                             write_bool(
                                                                                 clause
                                                                                     .decreases_wildcard);
                                                                         });
                                                                     dec("terms", [&]() {
                                                                         print_array(
                                                                             indent_level + 4,
                                                                             [&](const auto &term) {
                                                                                 for (const auto
                                                                                          &t :
                                                                                      clause
                                                                                          .decreases_terms) {
                                                                                     term([&]() {
                                                                                         print_expr(
                                                                                             *t,
                                                                                             indent_level +
                                                                                                 5);
                                                                                     });
                                                                                 }
                                                                             });
                                                                     });
                                                                 });
                                                });
                                            }
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
                        field("target", [&]() { write_string(symbol_name(value.target_ref)); });
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
                                                static const ir::StateHandler::Summary
                                                    empty_summary{};
                                                const auto *summary =
                                                    program_ == nullptr
                                                        ? nullptr
                                                        : ir::find_state_handler_summary(
                                                              *program_, value, handler);
                                                print_flow_summary(
                                                    summary == nullptr ? empty_summary : *summary,
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
                        field("input_type",
                              [&]() { write_string(type_name(value.input_type_ref)); });
                        if (has_type_ref(value.input_type_ref)) {
                            field("input_type_ref", [&]() {
                                print_type_ref(value.input_type_ref, indent_level + 1);
                            });
                        }
                        field("output_type",
                              [&]() { write_string(type_name(value.output_type_ref)); });
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
                                            entry("target", [&]() {
                                                write_string(symbol_name(node.target_ref));
                                            });
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
                                                static const ir::WorkflowExprSummary
                                                    empty_summary{};
                                                const auto *summary =
                                                    program_ == nullptr
                                                        ? nullptr
                                                        : ir::find_workflow_node_input_summary(
                                                              *program_, value, node);
                                                print_workflow_expr_summary(
                                                    summary == nullptr ? empty_summary : *summary,
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
                            static const ir::WorkflowExprSummary empty_summary{};
                            const auto *summary =
                                program_ == nullptr
                                    ? nullptr
                                    : ir::find_workflow_return_summary(*program_, value);
                            print_workflow_expr_summary(
                                summary == nullptr ? empty_summary : *summary, indent_level + 1);
                        });
                        field("return_value",
                              [&]() { print_expr(*value.return_value, indent_level + 1); });
                    });
                },
                // P2c (RFC §3.2.2): top-level fn declaration. Mirrors the
                // capability signature surface plus generic type-parameter
                // names and the three-state effect clause.
                [&](const ir::FnDecl &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("fn"); });
                        if (has_provenance(value.provenance)) {
                            field("provenance",
                                  [&]() { print_provenance(value.provenance, indent_level + 1); });
                        }
                        field("name", [&]() { write_string(value.name); });
                        if (!value.type_param_names.empty()) {
                            field("type_params", [&]() {
                                print_array(indent_level + 1, [&](const auto &item) {
                                    for (const auto &type_param : value.type_param_names) {
                                        item([&]() { write_string(type_param); });
                                    }
                                });
                            });
                        }
                        field("params", [&]() { print_params(value.params, indent_level + 1); });
                        if (value.has_return_type) {
                            field("return_type",
                                  [&]() { write_string(type_name(value.return_type_ref)); });
                            if (has_type_ref(value.return_type_ref)) {
                                field("return_type_ref", [&]() {
                                    print_type_ref(value.return_type_ref, indent_level + 1);
                                });
                            }
                        }
                        field("has_body", [&]() { write_bool(value.has_body); });
                        field("effect", [&]() {
                            print_object(indent_level + 1, [&](const auto &entry) {
                                entry("kind", [&]() {
                                    switch (value.effect.kind) {
                                    case ir::FnEffectKind::Pure:
                                        write_string("Pure");
                                        break;
                                    case ir::FnEffectKind::Nondet:
                                        write_string("Nondet");
                                        break;
                                    case ir::FnEffectKind::Capability:
                                        write_string("Capability");
                                        break;
                                    }
                                });
                                if (!value.effect.capabilities.empty()) {
                                    entry("capabilities", [&]() {
                                        print_array(indent_level + 2, [&](const auto &item) {
                                            for (const auto &capability :
                                                 value.effect.capabilities) {
                                                item([&]() {
                                                    print_symbol_ref(capability, indent_level + 3);
                                                });
                                            }
                                        });
                                    });
                                }
                            });
                        });
                        if (value.body) {
                            field("body", [&]() { print_block(*value.body, indent_level + 1); });
                        }
                        if (has_symbol_ref(value.symbol_ref)) {
                            field("symbol_ref",
                                  [&]() { print_symbol_ref(value.symbol_ref, indent_level + 1); });
                        }
                    });
                },
                [&](const ir::InstanceDecl &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("instance"); });
                        if (has_provenance(value.provenance)) {
                            field("provenance",
                                  [&]() { print_provenance(value.provenance, indent_level + 1); });
                        }
                        field("name", [&]() { write_string(value.name); });
                        field("instance_kind", [&]() {
                            switch (value.kind) {
                            case ir::InstanceKind::Capability:
                                write_string("capability");
                                break;
                            case ir::InstanceKind::Predicate:
                                write_string("predicate");
                                break;
                            case ir::InstanceKind::Agent:
                                write_string("agent");
                                break;
                            case ir::InstanceKind::Workflow:
                                write_string("workflow");
                                break;
                            case ir::InstanceKind::Fn:
                                write_string("fn");
                                break;
                            case ir::InstanceKind::Unknown:
                                write_string("unknown");
                                break;
                            }
                        });
                        if (has_symbol_ref(value.symbol_ref)) {
                            field("symbol_ref",
                                  [&]() { print_symbol_ref(value.symbol_ref, indent_level + 1); });
                        }
                        if (!value.type_args.empty()) {
                            field("type_args", [&]() {
                                print_array(indent_level + 1, [&](const auto &item) {
                                    for (const auto &tref : value.type_args) {
                                        item([&]() { print_type_ref(tref, indent_level + 2); });
                                    }
                                });
                            });
                        }
                        if (!value.params.empty()) {
                            field("params", [&]() { print_params(value.params, indent_level + 1); });
                        }
                        if (has_type_ref(value.return_type_ref)) {
                            field("return_type",
                                  [&]() { write_string(type_name(value.return_type_ref)); });
                            field("return_type_ref", [&]() {
                                print_type_ref(value.return_type_ref, indent_level + 1);
                            });
                        }
                        if (has_type_ref(value.agent_input_type_ref)) {
                            field("agent_input_type", [&]() {
                                write_string(type_name(value.agent_input_type_ref));
                            });
                            field("agent_input_type_ref", [&]() {
                                print_type_ref(value.agent_input_type_ref, indent_level + 1);
                            });
                        }
                        if (has_type_ref(value.agent_context_type_ref)) {
                            field("agent_context_type", [&]() {
                                write_string(type_name(value.agent_context_type_ref));
                            });
                            field("agent_context_type_ref", [&]() {
                                print_type_ref(value.agent_context_type_ref, indent_level + 1);
                            });
                        }
                        if (has_type_ref(value.agent_output_type_ref)) {
                            field("agent_output_type", [&]() {
                                write_string(type_name(value.agent_output_type_ref));
                            });
                            field("agent_output_type_ref", [&]() {
                                print_type_ref(value.agent_output_type_ref, indent_level + 1);
                            });
                        }
                        if (has_type_ref(value.workflow_input_type_ref)) {
                            field("workflow_input_type", [&]() {
                                write_string(type_name(value.workflow_input_type_ref));
                            });
                            field("workflow_input_type_ref", [&]() {
                                print_type_ref(value.workflow_input_type_ref, indent_level + 1);
                            });
                        }
                        if (has_type_ref(value.workflow_output_type_ref)) {
                            field("workflow_output_type", [&]() {
                                write_string(type_name(value.workflow_output_type_ref));
                            });
                            field("workflow_output_type_ref", [&]() {
                                print_type_ref(value.workflow_output_type_ref, indent_level + 1);
                            });
                        }
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
