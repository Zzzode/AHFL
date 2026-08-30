#include "ahfl/compiler/ir/lowering.hpp"

#include "ahfl/compiler/ir/analysis.hpp"
#include "ahfl/compiler/ir/identity.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "ahfl/base/support/overloaded.hpp"
#include "ahfl/base/support/ownership.hpp"
#include "base/json/json_value.hpp"
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
    case ir::TypeRefKind::BoundedInt:
        return "bounded_int";
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

[[nodiscard]] std::string_view enum_variant_payload_kind_name(ir::EnumVariantPayloadKind kind) {
    switch (kind) {
    case ir::EnumVariantPayloadKind::Unit:
        return "unit";
    case ir::EnumVariantPayloadKind::Tuple:
        return "tuple";
    case ir::EnumVariantPayloadKind::Struct:
        return "struct";
    }
    return "invalid";
}

[[nodiscard]] std::string_view member_type_template_kind_name(ir::MemberTypeTemplateKind kind) {
    switch (kind) {
    case ir::MemberTypeTemplateKind::Concrete:
        return "concrete";
    case ir::MemberTypeTemplateKind::Param:
        return "param";
    case ir::MemberTypeTemplateKind::Nominal:
        return "nominal";
    case ir::MemberTypeTemplateKind::Fn:
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

[[nodiscard]] std::string_view expression_effect_name(ExprEffect effect) {
    switch (effect) {
    case ExprEffect::Pure:
        return "pure";
    case ExprEffect::ConstOnly:
        return "const_only";
    case ExprEffect::PredicateCall:
        return "predicate_call";
    case ExprEffect::Nondet:
        return "nondet";
    case ExprEffect::CapabilityCall:
        return "capability_call";
    case ExprEffect::ExternalEffect:
        return "external_effect";
    case ExprEffect::Unknown:
        return "unknown";
    }
    return "unknown";
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

    // RFC 0026 P4 (coercion): declaration-order variance array serialization.
    static std::string_view variance_json_name(ir::Variance v) {
        switch (v) {
        case ir::Variance::Invariant:
            return "invariant";
        case ir::Variance::Covariant:
            return "covariant";
        case ir::Variance::Contravariant:
            return "contravariant";
        }
        return "invariant";
    }

    void print_variances(const std::vector<ir::Variance> &values, int indent_level) {
        print_array(indent_level, [&](const auto &item) {
            for (const auto v : values) {
                item([&]() { write_string(variance_json_name(v)); });
            }
        });
    }

    void print_u32_array(const std::vector<std::uint32_t> &values, int indent_level) {
        print_array(indent_level, [&](const auto &item) {
            for (const auto value : values) {
                item([&]() { write_index(value); });
            }
        });
    }

    void print_member_type_templates(const std::vector<ir::MemberTypeTemplateNode> &nodes,
                                     int indent_level) {
        print_array(indent_level, [&](const auto &item) {
            for (const auto &node : nodes) {
                item([&]() {
                    print_object(indent_level + 1, [&](const auto &field) {
                        field("kind",
                              [&]() { write_string(member_type_template_kind_name(node.kind)); });
                        if (node.kind == ir::MemberTypeTemplateKind::Concrete ||
                            node.kind == ir::MemberTypeTemplateKind::Nominal) {
                            field("type_ref",
                                  [&]() { print_type_ref(node.type_ref, indent_level + 2); });
                        }
                        if (node.kind == ir::MemberTypeTemplateKind::Param) {
                            field("param_index", [&]() { write_index(node.param_index); });
                        }
                        if (node.kind == ir::MemberTypeTemplateKind::Nominal ||
                            node.kind == ir::MemberTypeTemplateKind::Fn) {
                            field("children",
                                  [&]() { print_u32_array(node.children, indent_level + 2); });
                        }
                        if (node.kind == ir::MemberTypeTemplateKind::Fn) {
                            field("return", [&]() { write_index(node.fn_return); });
                        }
                    });
                });
            }
        });
    }

    // RFC 0026 P4 (coercion): adjustment-plan op-kind wire names.
    static std::string_view adjustment_op_kind_name(ir::AdjustmentOpKind k) {
        switch (k) {
        case ir::AdjustmentOpKind::IntWiden:
            return "int_widen";
        case ir::AdjustmentOpKind::StringWiden:
            return "string_widen";
        case ir::AdjustmentOpKind::CapacityWiden:
            return "capacity_widen";
        case ir::AdjustmentOpKind::TypeArg:
            return "type_arg";
        case ir::AdjustmentOpKind::FnParam:
            return "fn_param";
        case ir::AdjustmentOpKind::FnReturn:
            return "fn_return";
        case ir::AdjustmentOpKind::VariantToEnum:
            return "variant_to_enum";
        case ir::AdjustmentOpKind::ToAny:
            return "to_any";
        case ir::AdjustmentOpKind::FromNever:
            return "from_never";
        }
        return "int_widen";
    }

    // RFC 0026 P4 (coercion): serialize a LetStatement adjustment plan. A public
    // IR field must round-trip exactly (no silent drop); the source/target and
    // per-node TypeRefs preserve identity.
    void print_adjustment_plan(const ir::AdjustmentPlan &plan, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("source", [&]() { print_type_ref(plan.source, indent_level + 1); });
            field("target", [&]() { print_type_ref(plan.target, indent_level + 1); });
            field("root", [&]() { write_index(plan.root); });
            field("nodes", [&]() {
                print_array(indent_level + 1, [&](const auto &item) {
                    for (const auto &node : plan.nodes) {
                        item([&]() {
                            print_object(indent_level + 2, [&](const auto &node_field) {
                                node_field("source",
                                           [&]() { print_type_ref(node.source, indent_level + 3); });
                                node_field("target",
                                           [&]() { print_type_ref(node.target, indent_level + 3); });
                                node_field("ops", [&]() {
                                    print_array(indent_level + 3, [&](const auto &op_item) {
                                        for (const auto &op : node.ops) {
                                            op_item([&]() {
                                                print_object(
                                                    indent_level + 4, [&](const auto &op_field) {
                                                        op_field("kind", [&]() {
                                                            write_string(
                                                                adjustment_op_kind_name(op.kind));
                                                        });
                                                        op_field("arg_index", [&]() {
                                                            write_index(op.arg_index);
                                                        });
                                                        op_field("child",
                                                                 [&]() { write_index(op.child); });
                                                    });
                                            });
                                        }
                                    });
                                });
                            });
                        });
                    }
                });
            });
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
               ref.int_bounds.has_value() || ref.string_bounds.has_value() ||
               ref.decimal_scale.has_value() || ref.collection_capacity.has_value() || ref.first ||
               ref.second || !ref.params.empty() || has_source_range(ref.source_range) ||
               ref.nominal_ref.kind != ir::SymbolRefKind::Unknown;
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
            if (ref.int_bounds.has_value()) {
                field("int_bounds", [&]() {
                    print_object(indent_level + 1, [&](const auto &bounds_field) {
                        bounds_field("minimum", [&]() { write_i64(ref.int_bounds->first); });
                        bounds_field("maximum", [&]() { write_i64(ref.int_bounds->second); });
                    });
                });
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
            // RFC 0025: bounded collection static capacity.
            if (ref.collection_capacity.has_value()) {
                field("collection_capacity",
                      [&]() { write_i64(static_cast<std::int64_t>(*ref.collection_capacity)); });
            }
            // RFC 0026 P4: resolved nominal identity of a Struct/Enum type ref.
            if (ref.nominal_ref.kind != ir::SymbolRefKind::Unknown) {
                field("nominal_ref", [&]() { print_symbol_ref(ref.nominal_ref, indent_level + 1); });
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
        field("effect", [&]() { write_string(expression_effect_name(expr.effect)); });
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

    // P3 (RFC §1.3): emit one interface method signature of a TraitDecl. Bodies
    // are never present on trait methods (interface-only); the effect object
    // mirrors the FnDecl effect emission so parsing is symmetric.
    void print_fn_effect_clause(const ir::FnEffectClause &effect, int indent_level) {
        print_object(indent_level, [&](const auto &entry) {
            entry("kind", [&]() {
                switch (effect.kind) {
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
            if (!effect.capabilities.empty()) {
                entry("capabilities", [&]() {
                    print_array(indent_level + 1, [&](const auto &item) {
                        for (const auto &capability : effect.capabilities) {
                            item([&]() { print_symbol_ref(capability, indent_level + 2); });
                        }
                    });
                });
            }
        });
    }

    void print_trait_method_sig(const ir::TraitMethodSig &method, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("name", [&]() { write_string(method.name); });
            if (!method.type_param_names.empty()) {
                field("type_params", [&]() {
                    print_array(indent_level + 1, [&](const auto &item) {
                        for (const auto &type_param : method.type_param_names) {
                            item([&]() { write_string(type_param); });
                        }
                    });
                });
            }
            field("params", [&]() { print_params(method.params, indent_level + 1); });
            if (method.has_return_type) {
                field("return_type",
                      [&]() { write_string(type_name(method.return_type_ref)); });
                if (has_type_ref(method.return_type_ref)) {
                    field("return_type_ref", [&]() {
                        print_type_ref(method.return_type_ref, indent_level + 1);
                    });
                }
            }
            field("effect", [&]() { print_fn_effect_clause(method.effect, indent_level + 1); });
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
        // RFC 0026 (3)-3b: matched-enum identity (present for every pattern
        // matched against a resolved enum). Emitted only when set so a
        // non-enum-matched pattern round-trips byte-exact.
        if (has_symbol_ref(pattern.matched_enum)) {
            field("matched_enum", [&]() { print_symbol_ref(pattern.matched_enum, indent_level); });
        }
        // RFC 0026 P4-B: the full resolved matched TypeRef (present when Sema had a
        // resolved type). Emitted only when set so a pattern with no resolved type
        // round-trips byte-exact.
        if (has_type_ref(pattern.matched_type_ref)) {
            field("matched_type_ref",
                  [&]() { print_type_ref(pattern.matched_type_ref, indent_level + 1); });
        }
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
                [&](const ir::IntRangePattern &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("int_range"); });
                        print_match_pattern_common(field, pattern, indent_level + 1);
                        field("start", [&]() { out_ << value.start; });
                        field("end", [&]() { out_ << value.end; });
                    });
                },
                [&](const ir::VariantPattern &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("variant"); });
                        print_match_pattern_common(field, pattern, indent_level + 1);
                        field("path", [&]() { write_string(value.path); });
                        // RFC 0026 (3)-3b: typed variant identity. owner_enum is
                        // emitted only when resolved so an unresolved-identity
                        // pattern round-trips byte-exact.
                        if (has_symbol_ref(value.owner_enum)) {
                            field("owner_enum",
                                  [&]() { print_symbol_ref(value.owner_enum, indent_level + 1); });
                        }
                        if (!value.variant_name.empty()) {
                            field("variant_name", [&]() { write_string(value.variant_name); });
                        }
                        field("payload_kind", [&]() {
                            switch (value.kind) {
                            case ir::VariantPatternKind::Unit:
                                write_string("unit");
                                break;
                            case ir::VariantPatternKind::Tuple:
                                write_string("tuple");
                                break;
                            case ir::VariantPatternKind::Struct:
                                write_string("struct");
                                break;
                            }
                        });
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
                        field("fields", [&]() {
                            print_array(indent_level + 1, [&](const auto &item) {
                                for (const auto &variant_field : value.fields) {
                                    item([&]() {
                                        print_object(indent_level + 2, [&](const auto &entry) {
                                            entry("name",
                                                  [&]() { write_string(variant_field.name); });
                                            entry("is_rest", [&]() {
                                                out_ << (variant_field.is_rest ? "true" : "false");
                                            });
                                            entry("pattern", [&]() {
                                                if (variant_field.pattern) {
                                                    print_match_pattern(*variant_field.pattern,
                                                                        indent_level + 3);
                                                } else {
                                                    out_ << "null";
                                                }
                                            });
                                        });
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
                        field("callee_ref", [&]() {
                            print_symbol_ref(value.callee_ref, indent_level + 1);
                        });
                        field("arguments", [&]() {
                            print_array(indent_level + 1, [&](const auto &item) {
                                for (const auto &argument : value.arguments) {
                                    item([&]() { print_expr(*argument, indent_level + 2); });
                                }
                            });
                        });
                    });
                },
                [&](const ir::MethodCallExpr &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("method_call"); });
                        print_expr_common_fields(field, expr, indent_level + 1);
                        field("receiver", [&]() {
                            if (value.receiver) {
                                print_expr(*value.receiver, indent_level + 1);
                            } else {
                                out_ << "null";
                            }
                        });
                        field("method", [&]() { write_string(value.method); });
                        field("method_ref", [&]() {
                            print_symbol_ref(value.method_ref, indent_level + 1);
                        });
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
                        // C-4 (Wave-24): explicit capture list.
                        if (!value.captures.empty()) {
                            field("captures",
                                  [&]() { write_string_array(value.captures, indent_level + 1); });
                        }
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
                        if (value.is_enum_variant) {
                            field("is_enum_variant", [&]() { out_ << "true"; });
                            field("enum_name", [&]() { write_string(value.enum_name); });
                            field("variant_name", [&]() { write_string(value.variant_name); });
                        }
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
                        field("scrutinee",
                              [&]() { print_expr(*value.scrutinee, indent_level + 1); });
                        field("arms", [&]() {
                            print_array(indent_level + 1, [&](const auto &item) {
                                for (const auto &arm : value.arms) {
                                    item([&]() {
                                        print_object(indent_level + 2, [&](const auto &arm_field) {
                                            arm_field("pattern", [&]() {
                                                print_match_pattern(arm.pattern, indent_level + 3);
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
                // P4-02: unwrap(operand) — operand mandatory; fallback_message is optional.
                [&](const ir::UnwrapExpr &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("unwrap"); });
                        print_expr_common_fields(field, expr, indent_level + 1);
                        field("operand", [&]() { print_expr(*value.operand, indent_level + 1); });
                        field("fallback_none_message", [&]() {
                            if (value.fallback_none_message) {
                                print_expr(*value.fallback_none_message, indent_level + 1);
                            } else {
                                write_null();
                            }
                        });
                    });
                },
                // RFC 0013 P3-gaps-B: the unit literal `{}` — kind + common fields only.
                [&](const ir::UnitLiteralExpr &) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("unit_literal"); });
                        print_expr_common_fields(field, expr, indent_level + 1);
                    });
                },
                // RFC 0024: bounded quantifier — quantifier kind, binder name(s),
                // collection operand, and body predicate.
                [&](const ir::QuantifierExpr &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("quantifier"); });
                        print_expr_common_fields(field, expr, indent_level + 1);
                        field("quantifier", [&]() {
                            write_string(value.kind == ir::QuantifierExpr::Kind::Exists ? "exists"
                                                                                        : "forall");
                        });
                        field("binder", [&]() { write_string(value.binder); });
                        field("value_binder", [&]() { write_string(value.value_binder); });
                        field("collection",
                              [&]() { print_expr(*value.collection, indent_level + 1); });
                        field("body", [&]() { print_expr(*value.body, indent_level + 1); });
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
                        // RFC 0026 P4 (coercion): additive; only emitted when a
                        // plan is present (inert until F2 populates it).
                        if (value.adjustment.has_value()) {
                            field("adjustment", [&]() {
                                print_adjustment_plan(*value.adjustment, indent_level + 1);
                            });
                        }
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
                [&](const ir::IfLetStatement &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("if_let"); });
                        print_source_range_field(field, statement.source_range, indent_level + 1);
                        field("pattern",
                              [&]() { print_match_pattern(value.pattern, indent_level + 1); });
                        field("scrutinee",
                              [&]() { print_expr(*value.scrutinee, indent_level + 1); });
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
                        if (value.message) {
                            field("message",
                                  [&]() { print_expr(*value.message, indent_level + 1); });
                        }
                    });
                },
                [&](const ir::UnwrapStatement &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("unwrap"); });
                        print_source_range_field(field, statement.source_range, indent_level + 1);
                        if (value.operand) {
                            field("operand",
                                  [&]() { print_expr(*value.operand, indent_level + 1); });
                        }
                    });
                },
                [&](const ir::RequiresStatement &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("requires"); });
                        print_source_range_field(field, statement.source_range, indent_level + 1);
                        if (value.condition) {
                            field("condition",
                                  [&]() { print_expr(*value.condition, indent_level + 1); });
                        }
                        if (value.message) {
                            field("message",
                                  [&]() { print_expr(*value.message, indent_level + 1); });
                        }
                    });
                },
                [&](const ir::UnreachableStatement &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("unreachable"); });
                        print_source_range_field(field, statement.source_range, indent_level + 1);
                        if (value.message) {
                            field("message",
                                  [&]() { print_expr(*value.message, indent_level + 1); });
                        }
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
            field("inferred_effect",
                  [&]() { write_string(expression_effect_name(summary.inferred_effect)); });
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
                        if (value.type_param_count != 0 || !value.type_param_variances.empty()) {
                            field("type_param_count",
                                  [&]() { write_index(value.type_param_count); });
                            field("type_param_variances", [&]() {
                                print_variances(value.type_param_variances, indent_level + 1);
                            });
                        }
                        if (!value.member_type_templates.empty() ||
                            !value.field_type_template_roots.empty()) {
                            field("member_type_templates", [&]() {
                                print_member_type_templates(
                                    value.member_type_templates, indent_level + 1);
                            });
                            field("field_type_template_roots", [&]() {
                                print_u32_array(
                                    value.field_type_template_roots, indent_level + 1);
                            });
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
                        if (value.type_param_count != 0 || !value.type_param_variances.empty()) {
                            field("type_param_count",
                                  [&]() { write_index(value.type_param_count); });
                            field("type_param_variances", [&]() {
                                print_variances(value.type_param_variances, indent_level + 1);
                            });
                        }
                        if (!value.member_type_templates.empty()) {
                            field("member_type_templates", [&]() {
                                print_member_type_templates(
                                    value.member_type_templates, indent_level + 1);
                            });
                        }
                        field("variants", [&]() {
                            print_array(indent_level + 1, [&](const auto &item) {
                                for (const auto &variant : value.variants) {
                                    item([&]() {
                                        print_object(indent_level + 2, [&](const auto &entry) {
                                            entry("name", [&]() { write_string(variant.name); });
                                            entry("payload_kind", [&]() {
                                                write_string(enum_variant_payload_kind_name(
                                                    variant.payload_kind));
                                            });
                                            print_source_range_field(
                                                entry, variant.source_range, indent_level + 3);
                                            if (!variant.payload_type_template_roots.empty()) {
                                                entry("payload_type_template_roots", [&]() {
                                                    print_u32_array(
                                                        variant.payload_type_template_roots,
                                                        indent_level + 3);
                                                });
                                            }
                                            entry("payload", [&]() {
                                                print_array(
                                                    indent_level + 3,
                                                    [&](const auto &payload_item) {
                                                        for (const auto &slot : variant.payload) {
                                                            payload_item([&]() {
                                                                print_object(
                                                                    indent_level + 4,
                                                                    [&](const auto &payload_entry) {
                                                                        payload_entry(
                                                                            "type", [&]() {
                                                                                write_string(
                                                                                    type_name(
                                                                                        slot));
                                                                            });
                                                                        if (has_type_ref(slot)) {
                                                                            payload_entry(
                                                                                "type_ref", [&]() {
                                                                                    print_type_ref(
                                                                                        slot,
                                                                                        indent_level +
                                                                                            5);
                                                                                });
                                                                        }
                                                                    });
                                                            });
                                                        }
                                                    });
                                            });
                                            entry("fields", [&]() {
                                                print_array(indent_level + 3, [&](const auto &field_item) {
                                                    for (const auto &variant_field :
                                                         variant.fields) {
                                                        field_item([&]() {
                                                            print_object(
                                                                indent_level + 4,
                                                                [&](const auto &field_entry) {
                                                                    field_entry("name", [&]() {
                                                                        write_string(
                                                                            variant_field.name);
                                                                    });
                                                                    field_entry("type", [&]() {
                                                                        write_string(type_name(
                                                                            variant_field
                                                                                .type_ref));
                                                                    });
                                                                    if (has_type_ref(
                                                                            variant_field
                                                                                .type_ref)) {
                                                                        field_entry(
                                                                            "type_ref", [&]() {
                                                                                print_type_ref(
                                                                                    variant_field
                                                                                        .type_ref,
                                                                                    indent_level +
                                                                                        5);
                                                                            });
                                                                    }
                                                                    print_source_range_field(
                                                                        field_entry,
                                                                        variant_field.source_range,
                                                                        indent_level + 5);
                                                                    field_entry("default_value", [&]() {
                                                                        if (variant_field
                                                                                .default_value) {
                                                                            print_expr(
                                                                                *variant_field
                                                                                     .default_value,
                                                                                indent_level + 5);
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
                                    });
                                }
                            });
                        });
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
                                                    print_object(
                                                        indent_level + 3, [&](const auto &dec) {
                                                            dec("wildcard", [&]() {
                                                                write_bool(
                                                                    clause.decreases_wildcard);
                                                            });
                                                            dec("terms", [&]() {
                                                                print_array(
                                                                    indent_level + 4,
                                                                    [&](const auto &term) {
                                                                        for (const auto &t :
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
                                // D-3 (Wave-24): effect-clause decreases measure.
                                entry("has_decreases",
                                      [&]() { write_bool(value.effect.has_decreases); });
                                if (value.effect.has_decreases ||
                                    !value.effect.decreases_terms.empty()) {
                                    entry("decreases_terms", [&]() {
                                        print_array(indent_level + 2, [&](const auto &item) {
                                            for (const auto &term : value.effect.decreases_terms) {
                                                item(
                                                    [&]() { print_expr(*term, indent_level + 3); });
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
                [&](const ir::TraitDecl &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("trait"); });
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
                        if (!value.super_traits.empty()) {
                            field("super_traits", [&]() {
                                print_array(indent_level + 1, [&](const auto &item) {
                                    for (const auto &super : value.super_traits) {
                                        item([&]() { print_symbol_ref(super, indent_level + 2); });
                                    }
                                });
                            });
                        }
                        field("methods", [&]() {
                            print_array(indent_level + 1, [&](const auto &item) {
                                for (const auto &method : value.methods) {
                                    item([&]() {
                                        print_trait_method_sig(method, indent_level + 2);
                                    });
                                }
                            });
                        });
                        if (has_symbol_ref(value.symbol_ref)) {
                            field("symbol_ref",
                                  [&]() { print_symbol_ref(value.symbol_ref, indent_level + 1); });
                        }
                    });
                },
                [&](const ir::ImplDecl &value) {
                    print_object(indent_level, [&](const auto &field) {
                        field("kind", [&]() { write_string("impl"); });
                        if (has_provenance(value.provenance)) {
                            field("provenance",
                                  [&]() { print_provenance(value.provenance, indent_level + 1); });
                        }
                        field("index", [&]() { write_index(value.index); });
                        field("is_inherent", [&]() { write_bool(value.is_inherent); });
                        field("target_type",
                              [&]() { write_string(type_name(value.target_type_ref)); });
                        if (has_type_ref(value.target_type_ref)) {
                            field("target_type_ref", [&]() {
                                print_type_ref(value.target_type_ref, indent_level + 1);
                            });
                        }
                        if (!value.is_inherent && has_symbol_ref(value.trait_ref)) {
                            field("trait_ref",
                                  [&]() { print_symbol_ref(value.trait_ref, indent_level + 1); });
                        }
                        if (!value.trait_type_args.empty()) {
                            field("trait_type_args", [&]() {
                                print_array(indent_level + 1, [&](const auto &item) {
                                    for (const auto &arg : value.trait_type_args) {
                                        item([&]() { print_type_ref(arg, indent_level + 2); });
                                    }
                                });
                            });
                        }
                        if (!value.type_param_names.empty()) {
                            field("type_params", [&]() {
                                print_array(indent_level + 1, [&](const auto &item) {
                                    for (const auto &type_param : value.type_param_names) {
                                        item([&]() { write_string(type_param); });
                                    }
                                });
                            });
                        }
                        if (!value.method_refs.empty()) {
                            field("method_refs", [&]() {
                                print_array(indent_level + 1, [&](const auto &item) {
                                    for (const auto &method_ref : value.method_refs) {
                                        item([&]() {
                                            print_symbol_ref(method_ref, indent_level + 2);
                                        });
                                    }
                                });
                            });
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
                            field("params",
                                  [&]() { print_params(value.params, indent_level + 1); });
                        }
                        if (has_type_ref(value.return_type_ref)) {
                            field("return_type",
                                  [&]() { write_string(type_name(value.return_type_ref)); });
                            field("return_type_ref", [&]() {
                                print_type_ref(value.return_type_ref, indent_level + 1);
                            });
                        }
                        if (has_type_ref(value.agent_input_type_ref)) {
                            field("agent_input_type",
                                  [&]() { write_string(type_name(value.agent_input_type_ref)); });
                            field("agent_input_type_ref", [&]() {
                                print_type_ref(value.agent_input_type_ref, indent_level + 1);
                            });
                        }
                        if (has_type_ref(value.agent_context_type_ref)) {
                            field("agent_context_type",
                                  [&]() { write_string(type_name(value.agent_context_type_ref)); });
                            field("agent_context_type_ref", [&]() {
                                print_type_ref(value.agent_context_type_ref, indent_level + 1);
                            });
                        }
                        if (has_type_ref(value.agent_output_type_ref)) {
                            field("agent_output_type",
                                  [&]() { write_string(type_name(value.agent_output_type_ref)); });
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

namespace {

// ----------------------------------------------------------------------------
// KR5.9: IR JSON deserializer. Mirrors IrJsonPrinter exactly (same schema,
// co-located so the two stay in sync). Every parse helper propagates failure
// through the shared `ok_` flag; the top-level parse returns nullopt if any
// required field is missing or malformed. Expressions are rebuilt bottom-up
// into program.expr_arena; derived analyses are recomputed after load.
// ----------------------------------------------------------------------------

using ahfl::json::JsonValue;

// Reverse enum lookups. Each returns false and leaves the out-param untouched
// when the string is unrecognized (caller flags the error).
[[nodiscard]] bool parse_path_root_kind(std::string_view s, ir::PathRootKind &out) {
    if (s == "identifier") { out = ir::PathRootKind::Identifier; return true; }
    if (s == "input") { out = ir::PathRootKind::Input; return true; }
    if (s == "context") { out = ir::PathRootKind::Context; return true; }
    if (s == "output") { out = ir::PathRootKind::Output; return true; }
    if (s == "state") { out = ir::PathRootKind::State; return true; }
    if (s == "local") { out = ir::PathRootKind::Local; return true; }
    return false;
}

[[nodiscard]] bool parse_expr_unary_op(std::string_view s, ir::ExprUnaryOp &out) {
    if (s == "not") { out = ir::ExprUnaryOp::Not; return true; }
    if (s == "negate") { out = ir::ExprUnaryOp::Negate; return true; }
    if (s == "positive") { out = ir::ExprUnaryOp::Positive; return true; }
    return false;
}

[[nodiscard]] bool parse_expr_binary_op(std::string_view s, ir::ExprBinaryOp &out) {
    if (s == "implies") { out = ir::ExprBinaryOp::Implies; return true; }
    if (s == "or") { out = ir::ExprBinaryOp::Or; return true; }
    if (s == "and") { out = ir::ExprBinaryOp::And; return true; }
    if (s == "equal") { out = ir::ExprBinaryOp::Equal; return true; }
    if (s == "not_equal") { out = ir::ExprBinaryOp::NotEqual; return true; }
    if (s == "less") { out = ir::ExprBinaryOp::Less; return true; }
    if (s == "less_equal") { out = ir::ExprBinaryOp::LessEqual; return true; }
    if (s == "greater") { out = ir::ExprBinaryOp::Greater; return true; }
    if (s == "greater_equal") { out = ir::ExprBinaryOp::GreaterEqual; return true; }
    if (s == "add") { out = ir::ExprBinaryOp::Add; return true; }
    if (s == "subtract") { out = ir::ExprBinaryOp::Subtract; return true; }
    if (s == "multiply") { out = ir::ExprBinaryOp::Multiply; return true; }
    if (s == "divide") { out = ir::ExprBinaryOp::Divide; return true; }
    if (s == "modulo") { out = ir::ExprBinaryOp::Modulo; return true; }
    return false;
}

[[nodiscard]] bool parse_temporal_unary_op(std::string_view s, ir::TemporalUnaryOp &out) {
    if (s == "always") { out = ir::TemporalUnaryOp::Always; return true; }
    if (s == "eventually") { out = ir::TemporalUnaryOp::Eventually; return true; }
    if (s == "next") { out = ir::TemporalUnaryOp::Next; return true; }
    if (s == "not") { out = ir::TemporalUnaryOp::Not; return true; }
    return false;
}

[[nodiscard]] bool parse_temporal_binary_op(std::string_view s, ir::TemporalBinaryOp &out) {
    if (s == "implies") { out = ir::TemporalBinaryOp::Implies; return true; }
    if (s == "or") { out = ir::TemporalBinaryOp::Or; return true; }
    if (s == "and") { out = ir::TemporalBinaryOp::And; return true; }
    if (s == "until") { out = ir::TemporalBinaryOp::Until; return true; }
    return false;
}

[[nodiscard]] bool parse_contract_clause_kind(std::string_view s, ir::ContractClauseKind &out) {
    if (s == "requires") { out = ir::ContractClauseKind::Requires; return true; }
    if (s == "ensures") { out = ir::ContractClauseKind::Ensures; return true; }
    if (s == "invariant") { out = ir::ContractClauseKind::Invariant; return true; }
    if (s == "forbid") { out = ir::ContractClauseKind::Forbid; return true; }
    if (s == "decreases") { out = ir::ContractClauseKind::Decreases; return true; }
    return false;
}

[[nodiscard]] bool parse_symbol_ref_kind(std::string_view s, ir::SymbolRefKind &out) {
    if (s == "unknown") { out = ir::SymbolRefKind::Unknown; return true; }
    if (s == "type") { out = ir::SymbolRefKind::Type; return true; }
    if (s == "const") { out = ir::SymbolRefKind::Const; return true; }
    if (s == "capability") { out = ir::SymbolRefKind::Capability; return true; }
    if (s == "predicate") { out = ir::SymbolRefKind::Predicate; return true; }
    if (s == "agent") { out = ir::SymbolRefKind::Agent; return true; }
    if (s == "workflow") { out = ir::SymbolRefKind::Workflow; return true; }
    if (s == "function") { out = ir::SymbolRefKind::Function; return true; }
    return false;
}

[[nodiscard]] bool parse_type_ref_kind(std::string_view s, ir::TypeRefKind &out) {
    if (s == "unresolved") { out = ir::TypeRefKind::Unresolved; return true; }
    if (s == "any") { out = ir::TypeRefKind::Any; return true; }
    if (s == "never") { out = ir::TypeRefKind::Never; return true; }
    if (s == "unit") { out = ir::TypeRefKind::Unit; return true; }
    if (s == "bool") { out = ir::TypeRefKind::Bool; return true; }
    if (s == "int") { out = ir::TypeRefKind::Int; return true; }
    if (s == "bounded_int") { out = ir::TypeRefKind::BoundedInt; return true; }
    if (s == "float") { out = ir::TypeRefKind::Float; return true; }
    if (s == "string") { out = ir::TypeRefKind::String; return true; }
    if (s == "bounded_string") { out = ir::TypeRefKind::BoundedString; return true; }
    if (s == "uuid") { out = ir::TypeRefKind::UUID; return true; }
    if (s == "timestamp") { out = ir::TypeRefKind::Timestamp; return true; }
    if (s == "duration") { out = ir::TypeRefKind::Duration; return true; }
    if (s == "decimal") { out = ir::TypeRefKind::Decimal; return true; }
    if (s == "struct") { out = ir::TypeRefKind::Struct; return true; }
    if (s == "enum") { out = ir::TypeRefKind::Enum; return true; }
    if (s == "fn") { out = ir::TypeRefKind::Fn; return true; }
    return false;
}

[[nodiscard]] bool parse_enum_variant_payload_kind(std::string_view s,
                                                   ir::EnumVariantPayloadKind &out) {
    if (s == "unit") { out = ir::EnumVariantPayloadKind::Unit; return true; }
    if (s == "tuple") { out = ir::EnumVariantPayloadKind::Tuple; return true; }
    if (s == "struct") { out = ir::EnumVariantPayloadKind::Struct; return true; }
    return false;
}

[[nodiscard]] bool parse_member_type_template_kind(std::string_view s,
                                                   ir::MemberTypeTemplateKind &out) {
    if (s == "concrete") {
        out = ir::MemberTypeTemplateKind::Concrete;
        return true;
    }
    if (s == "param") {
        out = ir::MemberTypeTemplateKind::Param;
        return true;
    }
    if (s == "nominal") {
        out = ir::MemberTypeTemplateKind::Nominal;
        return true;
    }
    if (s == "fn") {
        out = ir::MemberTypeTemplateKind::Fn;
        return true;
    }
    return false;
}

[[nodiscard]] bool parse_capability_effect_kind(std::string_view s, ir::CapabilityEffectKind &out) {
    if (s == "unknown") { out = ir::CapabilityEffectKind::Unknown; return true; }
    if (s == "read") { out = ir::CapabilityEffectKind::Read; return true; }
    if (s == "external_side_effect") { out = ir::CapabilityEffectKind::ExternalSideEffect; return true; }
    if (s == "durable_write") { out = ir::CapabilityEffectKind::DurableWrite; return true; }
    if (s == "financial_write") { out = ir::CapabilityEffectKind::FinancialWrite; return true; }
    return false;
}

[[nodiscard]] bool parse_capability_receipt_mode(std::string_view s,
                                                 ir::CapabilityReceiptMode &out) {
    if (s == "none") { out = ir::CapabilityReceiptMode::None; return true; }
    if (s == "optional") { out = ir::CapabilityReceiptMode::Optional; return true; }
    if (s == "required") { out = ir::CapabilityReceiptMode::Required; return true; }
    return false;
}

[[nodiscard]] bool parse_capability_retry_mode(std::string_view s, ir::CapabilityRetryMode &out) {
    if (s == "unsafe") { out = ir::CapabilityRetryMode::Unsafe; return true; }
    if (s == "safe_if_idempotent") { out = ir::CapabilityRetryMode::SafeIfIdempotent; return true; }
    if (s == "safe") { out = ir::CapabilityRetryMode::Safe; return true; }
    return false;
}

[[nodiscard]] bool parse_expr_effect(std::string_view s, ExprEffect &out) {
    if (s == "pure") { out = ExprEffect::Pure; return true; }
    if (s == "const_only") { out = ExprEffect::ConstOnly; return true; }
    if (s == "predicate_call") { out = ExprEffect::PredicateCall; return true; }
    if (s == "nondet") { out = ExprEffect::Nondet; return true; }
    if (s == "capability_call") { out = ExprEffect::CapabilityCall; return true; }
    if (s == "external_effect") { out = ExprEffect::ExternalEffect; return true; }
    if (s == "unknown") { out = ExprEffect::Unknown; return true; }
    return false;
}

// RFC 0026 P4 (coercion): variance wire name -> enum. Mirror of
// variance_json_name; an unknown spelling is a HARD parse failure (Codex P0-2),
// never a silent downgrade to Invariant (which could sneak a corrupt decl past
// the metadata-drift gate by matching the descriptor).
[[nodiscard]] bool parse_variance(std::string_view s, ir::Variance &out) {
    if (s == "invariant") { out = ir::Variance::Invariant; return true; }
    if (s == "covariant") { out = ir::Variance::Covariant; return true; }
    if (s == "contravariant") { out = ir::Variance::Contravariant; return true; }
    return false;
}

// RFC 0026 P4 (coercion): adjustment-plan op-kind wire name -> enum. Mirror of
// adjustment_op_kind_name; an unknown spelling is a HARD parse failure (Codex
// P0-2), never a silent downgrade to IntWiden.
[[nodiscard]] bool parse_adjustment_op_kind(std::string_view s, ir::AdjustmentOpKind &out) {
    if (s == "int_widen") { out = ir::AdjustmentOpKind::IntWiden; return true; }
    if (s == "string_widen") { out = ir::AdjustmentOpKind::StringWiden; return true; }
    if (s == "capacity_widen") { out = ir::AdjustmentOpKind::CapacityWiden; return true; }
    if (s == "type_arg") { out = ir::AdjustmentOpKind::TypeArg; return true; }
    if (s == "fn_param") { out = ir::AdjustmentOpKind::FnParam; return true; }
    if (s == "fn_return") { out = ir::AdjustmentOpKind::FnReturn; return true; }
    if (s == "variant_to_enum") { out = ir::AdjustmentOpKind::VariantToEnum; return true; }
    if (s == "to_any") { out = ir::AdjustmentOpKind::ToAny; return true; }
    if (s == "from_never") { out = ir::AdjustmentOpKind::FromNever; return true; }
    return false;
}

class IrJsonReader final {
  public:
    explicit IrJsonReader(ir::Program &program) : program_(&program) {}

    [[nodiscard]] bool ok() const noexcept { return ok_; }

    void fail() { ok_ = false; }

    // --- primitive field readers -------------------------------------------

    [[nodiscard]] std::string req_string(const JsonValue &obj, std::string_view key) {
        const auto *field = obj.get(key);
        if (field == nullptr) { fail(); return {}; }
        const auto value = field->as_string();
        if (!value.has_value()) { fail(); return {}; }
        return std::string(*value);
    }

    [[nodiscard]] std::string opt_string(const JsonValue &obj, std::string_view key,
                                         std::string fallback = {}) {
        const auto *field = obj.get(key);
        if (field == nullptr) { return fallback; }
        const auto value = field->as_string();
        if (!value.has_value()) { fail(); return {}; }
        return std::string(*value);
    }

    [[nodiscard]] bool opt_bool(const JsonValue &obj, std::string_view key, bool fallback = false) {
        const auto *field = obj.get(key);
        if (field == nullptr) { return fallback; }
        const auto value = field->as_bool();
        if (!value.has_value()) { fail(); return fallback; }
        return *value;
    }

    [[nodiscard]] std::int64_t req_i64(const JsonValue &obj, std::string_view key) {
        const auto *field = obj.get(key);
        if (field == nullptr) { fail(); return 0; }
        const auto value = field->as_int();
        if (!value.has_value()) { fail(); return 0; }
        return *value;
    }

    // RFC 0026 P4 (coercion): optional unsigned field (missing/null = fallback).
    // A present value must be an integer in the u32 range; a negative value or one
    // above UINT32_MAX is a HARD failure (Codex P0-2), never a silent signed cast.
    [[nodiscard]] std::uint32_t
    opt_u32(const JsonValue &obj, std::string_view key, std::uint32_t fallback) {
        const auto *field = obj.get(key);
        if (field == nullptr || field->is_null()) { return fallback; }
        const auto value = field->as_int();
        if (!value.has_value() || *value < 0 ||
            *value > static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max())) {
            fail();
            return fallback;
        }
        return static_cast<std::uint32_t>(*value);
    }

    // RFC 0026 P4 (coercion): declaration-order variance array (missing = empty).
    // Every element must be a known variance spelling; an unknown string or a
    // non-string element is a HARD failure (Codex P0-2), not a silent Invariant.
    [[nodiscard]] std::vector<ir::Variance>
    parse_variances(const JsonValue &obj, std::string_view key) {
        std::vector<ir::Variance> result;
        const auto *field = obj.get(key);
        if (field == nullptr) { return result; }
        if (!field->is_array()) { fail(); return result; }
        for (const auto &item : field->array_items) {
            const auto name = item->as_string();
            if (!name.has_value()) { fail(); return result; }
            ir::Variance variance{};
            if (!parse_variance(*name, variance)) { fail(); return result; }
            result.push_back(variance);
        }
        return result;
    }

    [[nodiscard]] std::vector<std::uint32_t> parse_u32_array(const JsonValue &obj,
                                                             std::string_view key) {
        std::vector<std::uint32_t> result;
        const auto *field = obj.get(key);
        if (field == nullptr) {
            return result;
        }
        if (!field->is_array()) {
            fail();
            return result;
        }
        result.reserve(field->array_items.size());
        for (const auto &item : field->array_items) {
            const auto value = item->as_int();
            if (!value.has_value() || *value < 0 ||
                *value > static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max())) {
                fail();
                return result;
            }
            result.push_back(static_cast<std::uint32_t>(*value));
        }
        return result;
    }

    [[nodiscard]] std::vector<ir::MemberTypeTemplateNode>
    parse_member_type_templates(const JsonValue &obj) {
        std::vector<ir::MemberTypeTemplateNode> result;
        const auto *field = obj.get("member_type_templates");
        if (field == nullptr) {
            return result;
        }
        if (!field->is_array()) {
            fail();
            return result;
        }
        result.reserve(field->array_items.size());
        for (const auto &item : field->array_items) {
            if (!item->is_object()) {
                fail();
                return result;
            }
            ir::MemberTypeTemplateNode node;
            if (!parse_member_type_template_kind(req_string(*item, "kind"), node.kind)) {
                fail();
                return result;
            }
            if (const auto *type_ref = item->get("type_ref"); type_ref != nullptr) {
                if (!type_ref->is_object()) {
                    fail();
                    return result;
                }
                node.type_ref = this->type_ref(*type_ref);
            }
            node.param_index = opt_u32(*item, "param_index", 0);
            node.children = parse_u32_array(*item, "children");
            node.fn_return = opt_u32(*item, "return", ir::kInvalidMemberTypeTemplateNode);
            result.push_back(std::move(node));
        }
        return result;
    }

    [[nodiscard]] std::vector<std::string> string_array(const JsonValue &obj,
                                                        std::string_view key) {
        std::vector<std::string> result;
        const auto *field = obj.get(key);
        if (field == nullptr) { return result; }
        if (!field->is_array()) { fail(); return result; }
        for (const auto &item : field->array_items) {
            const auto value = item->as_string();
            if (!value.has_value()) { fail(); return result; }
            result.emplace_back(*value);
        }
        return result;
    }

    // --- shared structural readers -----------------------------------------

    [[nodiscard]] ir::SourceRangeOpt source_range(const JsonValue &obj) {
        const auto *field = obj.get("source_range");
        if (field == nullptr || !field->is_object()) { return std::nullopt; }
        SourceRange range{};
        range.begin_offset = static_cast<std::size_t>(req_i64(*field, "begin_offset"));
        range.end_offset = static_cast<std::size_t>(req_i64(*field, "end_offset"));
        return range;
    }

    [[nodiscard]] ir::DeclarationProvenance provenance(const JsonValue &obj) {
        ir::DeclarationProvenance prov;
        const auto *field = obj.get("provenance");
        if (field == nullptr || !field->is_object()) { return prov; }
        prov.module_name = req_string(*field, "module_name");
        prov.source_path = req_string(*field, "source_path");
        prov.source_range = source_range(*field);
        return prov;
    }

    [[nodiscard]] ir::SymbolRef symbol_ref(const JsonValue &obj) {
        ir::SymbolRef ref;
        if (!obj.is_object()) { fail(); return ref; }
        const auto kind_str = req_string(obj, "kind");
        if (!parse_symbol_ref_kind(kind_str, ref.kind)) { fail(); }
        ref.canonical_name = req_string(obj, "canonical_name");
        ref.local_name = opt_string(obj, "local_name");
        ref.module_name = opt_string(obj, "module_name");
        if (const auto *id = obj.get("id"); id != nullptr) {
            // Symbol ids are size_t; synthetic impl-method ids set the high bit
            // and exceed INT64_MAX, so read the full unsigned magnitude.
            const auto value = id->as_uint();
            if (!value.has_value()) { fail(); } else {
                ref.id = static_cast<std::size_t>(*value);
            }
        }
        return ref;
    }

    // Optional symbol_ref field; returns default-constructed when absent.
    [[nodiscard]] ir::SymbolRef opt_symbol_ref(const JsonValue &obj, std::string_view key) {
        const auto *field = obj.get(key);
        if (field == nullptr) { return {}; }
        return symbol_ref(*field);
    }

    [[nodiscard]] ir::TypeRef type_ref(const JsonValue &obj) {
        ir::TypeRef ref;
        if (!obj.is_object()) { fail(); return ref; }
        const auto kind_str = req_string(obj, "kind");
        if (!parse_type_ref_kind(kind_str, ref.kind)) { fail(); }
        ref.display_name = req_string(obj, "display_name");
        ref.canonical_name = opt_string(obj, "canonical_name");
        ref.variant_name = opt_string(obj, "variant_name");
        if (const auto *bounds = obj.get("int_bounds"); bounds != nullptr) {
            ref.int_bounds = std::pair<std::int64_t, std::int64_t>{
                req_i64(*bounds, "minimum"), req_i64(*bounds, "maximum")};
        }
        if (const auto *bounds = obj.get("string_bounds"); bounds != nullptr) {
            ref.string_bounds = std::pair<std::int64_t, std::int64_t>{
                req_i64(*bounds, "minimum"), req_i64(*bounds, "maximum")};
        }
        if (const auto *scale = obj.get("decimal_scale"); scale != nullptr) {
            const auto value = scale->as_int();
            if (!value.has_value()) { fail(); } else { ref.decimal_scale = *value; }
        }
        if (const auto *cap = obj.get("collection_capacity"); cap != nullptr) {
            const auto value = cap->as_int();
            if (!value.has_value()) { fail(); } else {
                ref.collection_capacity = static_cast<std::uint64_t>(*value);
            }
        }
        // RFC 0026 P4: resolved nominal identity of a Struct/Enum type ref.
        ref.nominal_ref = opt_symbol_ref(obj, "nominal_ref");
        if (const auto *first = obj.get("element_type"); first != nullptr) {
            ref.first = make_owned<ir::TypeRef>(type_ref(*first));
        }
        if (const auto *second = obj.get("value_type"); second != nullptr) {
            ref.second = make_owned<ir::TypeRef>(type_ref(*second));
        }
        if (const auto *args = obj.get("type_args"); args != nullptr) {
            if (!args->is_array()) { fail(); return ref; }
            for (const auto &item : args->array_items) {
                if (item->is_null()) {
                    ref.params.push_back(nullptr);
                } else {
                    ref.params.push_back(make_owned<ir::TypeRef>(type_ref(*item)));
                }
            }
        }
        ref.source_range = source_range(obj);
        return ref;
    }

    // Optional type_ref field; default-constructed when absent (kind stays
    // Unresolved). Also seeds display_name from the paired display string so
    // re-emit of the "type"/"return_type"/... field reproduces byte-identically
    // even when the structured _ref object was omitted.
    [[nodiscard]] ir::TypeRef opt_type_ref(const JsonValue &obj, std::string_view ref_key,
                                           std::string_view display_key) {
        const auto *field = obj.get(ref_key);
        if (field != nullptr) { return type_ref(*field); }
        ir::TypeRef ref;
        if (!display_key.empty()) {
            const auto *display = obj.get(display_key);
            if (display != nullptr) {
                const auto value = display->as_string();
                if (value.has_value() && *value != "Any") {
                    // "Any" is the emitter's fallback for an empty ref; keep the
                    // ref empty in that case so has_type_ref() stays false.
                    ref.display_name = std::string(*value);
                }
            }
        }
        return ref;
    }

    // RFC 0026 P4 (coercion): parse a LetStatement adjustment plan (mirror of
    // print_adjustment_plan). An OPTIONAL field: only a missing key or JSON null
    // means "absent" (nullopt). A present value of the WRONG kind (not an object)
    // is a HARD failure (Codex P0-2) — it must never be silently dropped to
    // nullopt, which would erase a real plan on read. Inner arrays (nodes/ops) and
    // each op's `kind` are likewise strict.
    [[nodiscard]] std::optional<ir::AdjustmentPlan> opt_adjustment_plan(const JsonValue &obj) {
        const auto *field = obj.get("adjustment");
        if (field == nullptr || field->is_null()) { return std::nullopt; }
        if (!field->is_object()) { fail(); return std::nullopt; }
        ir::AdjustmentPlan plan;
        plan.source = opt_type_ref(*field, "source", "");
        plan.target = opt_type_ref(*field, "target", "");
        plan.root = opt_u32(*field, "root", 0);
        if (const auto *nodes = field->get("nodes"); nodes != nullptr) {
            if (!nodes->is_array()) { fail(); return std::nullopt; }
            for (const auto &node_item : nodes->array_items) {
                if (!node_item->is_object()) { fail(); return std::nullopt; }
                ir::AdjustmentNode node;
                node.source = opt_type_ref(*node_item, "source", "");
                node.target = opt_type_ref(*node_item, "target", "");
                if (const auto *ops = node_item->get("ops"); ops != nullptr) {
                    if (!ops->is_array()) { fail(); return std::nullopt; }
                    for (const auto &op_item : ops->array_items) {
                        if (!op_item->is_object()) { fail(); return std::nullopt; }
                        ir::AdjustmentOp op;
                        // `kind` is required and must be a known op-kind spelling;
                        // an unknown/absent/non-string kind fails closed.
                        const auto *kind = op_item->get("kind");
                        const auto kind_str = kind != nullptr ? kind->as_string() : std::nullopt;
                        if (!kind_str.has_value() ||
                            !parse_adjustment_op_kind(*kind_str, op.kind)) {
                            fail();
                            return std::nullopt;
                        }
                        op.arg_index = opt_u32(*op_item, "arg_index", 0);
                        op.child = opt_u32(*op_item, "child", 0xFFFFFFFFu);
                        node.ops.push_back(op);
                    }
                }
                plan.nodes.push_back(std::move(node));
            }
        }
        return plan;
    }

    [[nodiscard]] ir::Path path(const JsonValue &obj) {
        ir::Path result;
        if (!obj.is_object()) { fail(); return result; }
        if (!parse_path_root_kind(req_string(obj, "root_kind"), result.root_kind)) { fail(); }
        result.root_name = req_string(obj, "root_name");
        result.members = string_array(obj, "members");
        return result;
    }

    // --- expression readers ------------------------------------------------

    // Parse an expr JSON object into the arena, returning its handle.
    [[nodiscard]] ir::ExprRef expr(const JsonValue &obj) {
        if (!obj.is_object()) { fail(); return nullptr; }
        const auto kind = req_string(obj, "kind");
        ir::ExprNode node = build_expr_node(obj, kind);

        const auto id = static_cast<std::uint32_t>(req_i64(obj, "id"));
        ExprEffect effect = ExprEffect::Unknown;
        if (!parse_expr_effect(req_string(obj, "effect"), effect)) { fail(); }
        auto range = source_range(obj);
        ir::TypeRef resolved;
        if (const auto *rt = obj.get("resolved_type"); rt != nullptr) {
            resolved = type_ref(*rt);
        }
        return program_->expr_arena.make(std::move(node), std::move(range), std::move(resolved), id,
                                         effect);
    }

    // Optional expr field; returns null ExprRef when absent or JSON null.
    [[nodiscard]] ir::ExprRef opt_expr(const JsonValue &obj, std::string_view key) {
        const auto *field = obj.get(key);
        if (field == nullptr || field->is_null()) { return nullptr; }
        return expr(*field);
    }

    [[nodiscard]] ir::ExprNode build_expr_node(const JsonValue &obj, std::string_view kind) {
        if (kind == "bool_literal") {
            return ir::BoolLiteralExpr{.value = opt_bool(obj, "value")};
        }
        if (kind == "integer_literal") {
            return ir::IntegerLiteralExpr{.spelling = req_string(obj, "spelling")};
        }
        if (kind == "float_literal") {
            return ir::FloatLiteralExpr{.spelling = req_string(obj, "spelling")};
        }
        if (kind == "decimal_literal") {
            return ir::DecimalLiteralExpr{.spelling = req_string(obj, "spelling")};
        }
        if (kind == "string_literal") {
            return ir::StringLiteralExpr{.spelling = req_string(obj, "spelling")};
        }
        if (kind == "duration_literal") {
            return ir::DurationLiteralExpr{.spelling = req_string(obj, "spelling")};
        }
        if (kind == "path") {
            const auto *p = obj.get("path");
            if (p == nullptr) { fail(); return ir::UnitLiteralExpr{}; }
            return ir::PathExpr{.path = path(*p)};
        }
        if (kind == "qualified_value") {
            return ir::QualifiedValueExpr{.value = req_string(obj, "value")};
        }
        if (kind == "call") {
            ir::CallExpr call;
            call.callee = req_string(obj, "callee");
            if (const auto *cref = obj.get("callee_ref"); cref != nullptr) {
                call.callee_ref = symbol_ref(*cref);
            }
            const auto *args = obj.get("arguments");
            if (args == nullptr || !args->is_array()) { fail(); return call; }
            for (const auto &item : args->array_items) {
                call.arguments.push_back(expr(*item));
            }
            return call;
        }
        if (kind == "method_call") {
            ir::MethodCallExpr call;
            call.receiver = opt_expr(obj, "receiver");
            call.method = req_string(obj, "method");
            if (const auto *mref = obj.get("method_ref"); mref != nullptr) {
                call.method_ref = symbol_ref(*mref);
            }
            const auto *args = obj.get("arguments");
            if (args == nullptr || !args->is_array()) { fail(); return call; }
            for (const auto &item : args->array_items) {
                call.arguments.push_back(expr(*item));
            }
            return call;
        }
        if (kind == "lambda") {
            ir::LambdaExpr lambda;
            lambda.params = string_array(obj, "params");
            lambda.captures = string_array(obj, "captures");
            lambda.body = opt_expr(obj, "body");
            return lambda;
        }
        if (kind == "struct_literal") {
            ir::StructLiteralExpr lit;
            lit.type_name = req_string(obj, "type_name");
            lit.is_enum_variant = opt_bool(obj, "is_enum_variant");
            lit.enum_name = opt_string(obj, "enum_name");
            lit.variant_name = opt_string(obj, "variant_name");
            const auto *fields = obj.get("fields");
            if (fields == nullptr || !fields->is_array()) { fail(); return lit; }
            for (const auto &item : fields->array_items) {
                ir::StructFieldInit init;
                init.name = req_string(*item, "name");
                init.value = opt_expr(*item, "value");
                lit.fields.push_back(std::move(init));
            }
            return lit;
        }
        if (kind == "unary") {
            ir::UnaryExpr unary;
            if (!parse_expr_unary_op(req_string(obj, "op"), unary.op)) { fail(); }
            unary.operand = opt_expr(obj, "operand");
            return unary;
        }
        if (kind == "binary") {
            ir::BinaryExpr binary;
            if (!parse_expr_binary_op(req_string(obj, "op"), binary.op)) { fail(); }
            binary.lhs = opt_expr(obj, "lhs");
            binary.rhs = opt_expr(obj, "rhs");
            return binary;
        }
        if (kind == "member_access") {
            ir::MemberAccessExpr access;
            access.base = opt_expr(obj, "base");
            access.member = req_string(obj, "member");
            return access;
        }
        if (kind == "index_access") {
            ir::IndexAccessExpr access;
            access.base = opt_expr(obj, "base");
            access.index = opt_expr(obj, "index");
            return access;
        }
        if (kind == "match") {
            ir::MatchExpr match;
            match.scrutinee = opt_expr(obj, "scrutinee");
            const auto *arms = obj.get("arms");
            if (arms == nullptr || !arms->is_array()) { fail(); return match; }
            for (const auto &item : arms->array_items) {
                ir::MatchArmExpr arm;
                const auto *pattern = item->get("pattern");
                if (pattern == nullptr) { fail(); } else {
                    arm.pattern = match_pattern(*pattern);
                }
                arm.guard = opt_expr(*item, "guard");
                arm.body = opt_expr(*item, "body");
                match.arms.push_back(std::move(arm));
            }
            return match;
        }
        if (kind == "unwrap") {
            ir::UnwrapExpr unwrap;
            unwrap.operand = opt_expr(obj, "operand");
            unwrap.fallback_none_message = opt_expr(obj, "fallback_none_message");
            return unwrap;
        }
        if (kind == "unit_literal") {
            return ir::UnitLiteralExpr{};
        }
        if (kind == "quantifier") {
            ir::QuantifierExpr quant;
            quant.kind = req_string(obj, "quantifier") == "exists"
                             ? ir::QuantifierExpr::Kind::Exists
                             : ir::QuantifierExpr::Kind::ForAll;
            quant.binder = req_string(obj, "binder");
            quant.value_binder = req_string(obj, "value_binder");
            quant.collection = opt_expr(obj, "collection");
            quant.body = opt_expr(obj, "body");
            return quant;
        }
        fail();
        return ir::UnitLiteralExpr{};
    }

    // --- match patterns ----------------------------------------------------

    [[nodiscard]] ir::MatchPattern match_pattern(const JsonValue &obj) {
        ir::MatchPattern pattern;
        if (!obj.is_object()) { fail(); return pattern; }
        const auto kind = req_string(obj, "kind");
        pattern.text = req_string(obj, "text");
        pattern.source_range = source_range(obj);
        // RFC 0026 (3)-3b: matched-enum identity (absent => default empty ref).
        pattern.matched_enum = opt_symbol_ref(obj, "matched_enum");
        // RFC 0026 P4-B: full resolved matched TypeRef (absent => Unresolved).
        pattern.matched_type_ref = opt_type_ref(obj, "matched_type_ref", "matched_type");
        pattern.node = build_pattern_node(obj, kind);
        return pattern;
    }

    [[nodiscard]] Owned<ir::MatchPattern> opt_pattern_ptr(const JsonValue &item) {
        if (item.is_null()) { return nullptr; }
        return make_owned<ir::MatchPattern>(match_pattern(item));
    }

    [[nodiscard]] std::vector<Owned<ir::MatchPattern>> pattern_ptr_array(const JsonValue &obj,
                                                                         std::string_view key) {
        std::vector<Owned<ir::MatchPattern>> result;
        const auto *field = obj.get(key);
        if (field == nullptr || !field->is_array()) { return result; }
        for (const auto &item : field->array_items) {
            result.push_back(opt_pattern_ptr(*item));
        }
        return result;
    }

    [[nodiscard]] ir::MatchPatternNode build_pattern_node(const JsonValue &obj,
                                                          std::string_view kind) {
        if (kind == "literal") {
            return ir::LiteralPattern{.spelling = req_string(obj, "spelling")};
        }
        if (kind == "int_range") {
            return ir::IntRangePattern{.start = req_i64(obj, "start"), .end = req_i64(obj, "end")};
        }
        if (kind == "variant") {
            ir::VariantPattern variant;
            variant.path = req_string(obj, "path");
            // RFC 0026 (3)-3b: typed variant identity (absent => empty / "").
            variant.owner_enum = opt_symbol_ref(obj, "owner_enum");
            variant.variant_name = opt_string(obj, "variant_name");
            const auto payload_kind = req_string(obj, "payload_kind");
            if (payload_kind == "unit") { variant.kind = ir::VariantPatternKind::Unit; }
            else if (payload_kind == "tuple") { variant.kind = ir::VariantPatternKind::Tuple; }
            else if (payload_kind == "struct") { variant.kind = ir::VariantPatternKind::Struct; }
            else { fail(); }
            variant.subpatterns = pattern_ptr_array(obj, "subpatterns");
            const auto *fields = obj.get("fields");
            if (fields != nullptr && fields->is_array()) {
                for (const auto &item : fields->array_items) {
                    ir::VariantPatternField field;
                    field.name = req_string(*item, "name");
                    field.is_rest = opt_bool(*item, "is_rest");
                    const auto *pat = item->get("pattern");
                    if (pat != nullptr) { field.pattern = opt_pattern_ptr(*pat); }
                    variant.fields.push_back(std::move(field));
                }
            }
            return variant;
        }
        if (kind == "wildcard") {
            return ir::WildcardPattern{};
        }
        if (kind == "binding") {
            ir::BindingPattern binding;
            binding.name = req_string(obj, "name");
            binding.is_mut = opt_bool(obj, "is_mut");
            const auto *nested = obj.get("nested");
            if (nested != nullptr) { binding.nested = opt_pattern_ptr(*nested); }
            return binding;
        }
        if (kind == "tuple") {
            return ir::TuplePattern{.elements = pattern_ptr_array(obj, "elements")};
        }
        if (kind == "or") {
            return ir::OrPattern{.branches = pattern_ptr_array(obj, "branches")};
        }
        fail();
        return ir::WildcardPattern{};
    }

    // --- temporal expressions ----------------------------------------------

    [[nodiscard]] Owned<ir::TemporalExpr> temporal_expr(const JsonValue &obj) {
        auto result = make_owned<ir::TemporalExpr>();
        if (!obj.is_object()) { fail(); return result; }
        const auto kind = req_string(obj, "kind");
        result->source_range = source_range(obj);
        if (kind == "embedded_expr") {
            result->node = ir::EmbeddedTemporalExpr{.expr = opt_expr(obj, "expr")};
        } else if (kind == "called") {
            result->node = ir::CalledTemporalExpr{.capability = req_string(obj, "capability")};
        } else if (kind == "in_state") {
            result->node = ir::InStateTemporalExpr{.state = req_string(obj, "state")};
        } else if (kind == "running") {
            result->node = ir::RunningTemporalExpr{.node = req_string(obj, "node")};
        } else if (kind == "completed") {
            ir::CompletedTemporalExpr completed;
            completed.node = req_string(obj, "node");
            const auto *state = obj.get("state_name");
            if (state != nullptr && !state->is_null()) {
                const auto value = state->as_string();
                if (!value.has_value()) { fail(); } else { completed.state_name = std::string(*value); }
            }
            result->node = std::move(completed);
        } else if (kind == "unary") {
            ir::TemporalUnaryExpr unary;
            if (!parse_temporal_unary_op(req_string(obj, "op"), unary.op)) { fail(); }
            const auto *operand = obj.get("operand");
            if (operand == nullptr) { fail(); } else { unary.operand = temporal_expr(*operand); }
            result->node = std::move(unary);
        } else if (kind == "binary") {
            ir::TemporalBinaryExpr binary;
            if (!parse_temporal_binary_op(req_string(obj, "op"), binary.op)) { fail(); }
            const auto *lhs = obj.get("lhs");
            const auto *rhs = obj.get("rhs");
            if (lhs == nullptr || rhs == nullptr) { fail(); } else {
                binary.lhs = temporal_expr(*lhs);
                binary.rhs = temporal_expr(*rhs);
            }
            result->node = std::move(binary);
        } else {
            fail();
        }
        return result;
    }

    // --- statements & blocks -----------------------------------------------

    [[nodiscard]] Owned<ir::Statement> statement(const JsonValue &obj) {
        auto result = make_owned<ir::Statement>();
        if (!obj.is_object()) { fail(); return result; }
        const auto kind = req_string(obj, "kind");
        result->source_range = source_range(obj);
        if (kind == "let") {
            ir::LetStatement let;
            let.name = req_string(obj, "name");
            let.type_ref = opt_type_ref(obj, "type_ref", "type");
            let.initializer = opt_expr(obj, "initializer");
            let.adjustment = opt_adjustment_plan(obj);
            result->node = std::move(let);
        } else if (kind == "assign") {
            ir::AssignStatement assign;
            const auto *target = obj.get("target");
            if (target == nullptr) { fail(); } else { assign.target = path(*target); }
            assign.value = opt_expr(obj, "value");
            result->node = std::move(assign);
        } else if (kind == "if") {
            ir::IfStatement stmt;
            stmt.condition = opt_expr(obj, "condition");
            stmt.then_block = req_block_ptr(obj, "then_block");
            stmt.else_block = opt_block_ptr(obj, "else_block");
            result->node = std::move(stmt);
        } else if (kind == "if_let") {
            ir::IfLetStatement stmt;
            const auto *pattern = obj.get("pattern");
            if (pattern == nullptr) { fail(); } else { stmt.pattern = match_pattern(*pattern); }
            stmt.scrutinee = opt_expr(obj, "scrutinee");
            stmt.then_block = req_block_ptr(obj, "then_block");
            stmt.else_block = opt_block_ptr(obj, "else_block");
            result->node = std::move(stmt);
        } else if (kind == "goto") {
            result->node = ir::GotoStatement{.target_state = req_string(obj, "target_state")};
        } else if (kind == "return") {
            result->node = ir::ReturnStatement{.value = opt_expr(obj, "value")};
        } else if (kind == "assert") {
            ir::AssertStatement stmt;
            stmt.condition = opt_expr(obj, "condition");
            stmt.message = opt_expr(obj, "message");
            result->node = std::move(stmt);
        } else if (kind == "unwrap") {
            result->node = ir::UnwrapStatement{.operand = opt_expr(obj, "operand")};
        } else if (kind == "requires") {
            ir::RequiresStatement stmt;
            stmt.condition = opt_expr(obj, "condition");
            stmt.message = opt_expr(obj, "message");
            result->node = std::move(stmt);
        } else if (kind == "unreachable") {
            result->node = ir::UnreachableStatement{.message = opt_expr(obj, "message")};
        } else if (kind == "expr") {
            result->node = ir::ExprStatement{.expr = opt_expr(obj, "expr")};
        } else {
            fail();
        }
        return result;
    }

    [[nodiscard]] ir::Block block(const JsonValue &obj) {
        ir::Block result;
        if (!obj.is_object()) { fail(); return result; }
        result.source_range = source_range(obj);
        const auto *statements = obj.get("statements");
        if (statements == nullptr || !statements->is_array()) { fail(); return result; }
        for (const auto &item : statements->array_items) {
            result.statements.push_back(statement(*item));
        }
        return result;
    }

    [[nodiscard]] Owned<ir::Block> req_block_ptr(const JsonValue &obj, std::string_view key) {
        const auto *field = obj.get(key);
        if (field == nullptr) { fail(); return make_owned<ir::Block>(); }
        return make_owned<ir::Block>(block(*field));
    }

    [[nodiscard]] Owned<ir::Block> opt_block_ptr(const JsonValue &obj, std::string_view key) {
        const auto *field = obj.get(key);
        if (field == nullptr || field->is_null()) { return nullptr; }
        return make_owned<ir::Block>(block(*field));
    }

    // --- params -------------------------------------------------------------

    [[nodiscard]] std::vector<ir::ParamDecl> params(const JsonValue &obj, std::string_view key) {
        std::vector<ir::ParamDecl> result;
        const auto *field = obj.get(key);
        if (field == nullptr) { return result; }
        if (!field->is_array()) { fail(); return result; }
        for (const auto &item : field->array_items) {
            ir::ParamDecl param;
            param.name = req_string(*item, "name");
            param.type_ref = opt_type_ref(*item, "type_ref", "type");
            param.source_range = source_range(*item);
            result.push_back(std::move(param));
        }
        return result;
    }

    // --- declarations -------------------------------------------------------

    [[nodiscard]] std::optional<ir::Decl> decl(const JsonValue &obj) {
        if (!obj.is_object()) { fail(); return std::nullopt; }
        const auto kind = req_string(obj, "kind");
        if (kind == "module") {
            ir::ModuleDecl d;
            d.provenance = provenance(obj);
            d.name = req_string(obj, "name");
            return ir::Decl{std::move(d)};
        }
        if (kind == "import") {
            ir::ImportDecl d;
            d.provenance = provenance(obj);
            d.path = req_string(obj, "path");
            const auto *alias = obj.get("alias");
            if (alias != nullptr && !alias->is_null()) {
                const auto value = alias->as_string();
                if (!value.has_value()) { fail(); } else { d.alias = std::string(*value); }
            }
            return ir::Decl{std::move(d)};
        }
        if (kind == "const") {
            ir::ConstDecl d;
            d.provenance = provenance(obj);
            d.name = req_string(obj, "name");
            d.symbol_ref = opt_symbol_ref(obj, "symbol_ref");
            d.type_ref = opt_type_ref(obj, "type_ref", "type");
            d.value = opt_expr(obj, "value");
            return ir::Decl{std::move(d)};
        }
        if (kind == "type_alias") {
            ir::TypeAliasDecl d;
            d.provenance = provenance(obj);
            d.name = req_string(obj, "name");
            d.symbol_ref = opt_symbol_ref(obj, "symbol_ref");
            d.aliased_type_ref = opt_type_ref(obj, "aliased_type_ref", "aliased_type");
            return ir::Decl{std::move(d)};
        }
        if (kind == "struct") {
            ir::StructDecl d;
            d.provenance = provenance(obj);
            d.name = req_string(obj, "name");
            d.symbol_ref = opt_symbol_ref(obj, "symbol_ref");
            d.type_param_count = opt_u32(obj, "type_param_count", 0);
            d.type_param_variances = parse_variances(obj, "type_param_variances");
            d.member_type_templates = parse_member_type_templates(obj);
            d.field_type_template_roots =
                parse_u32_array(obj, "field_type_template_roots");
            const auto *fields = obj.get("fields");
            if (fields != nullptr && fields->is_array()) {
                for (const auto &item : fields->array_items) {
                    ir::FieldDecl field;
                    field.name = req_string(*item, "name");
                    field.type_ref = opt_type_ref(*item, "type_ref", "type");
                    field.source_range = source_range(*item);
                    field.default_value = opt_expr(*item, "default_value");
                    d.fields.push_back(std::move(field));
                }
            }
            return ir::Decl{std::move(d)};
        }
        if (kind == "enum") {
            return enum_decl(obj);
        }
        if (kind == "capability") {
            ir::CapabilityDecl d;
            d.provenance = provenance(obj);
            d.name = req_string(obj, "name");
            d.symbol_ref = opt_symbol_ref(obj, "symbol_ref");
            d.params = params(obj, "params");
            d.return_type_ref = opt_type_ref(obj, "return_type_ref", "return_type");
            if (const auto *effect = obj.get("effect"); effect != nullptr) {
                d.effect = capability_effect(*effect);
            }
            return ir::Decl{std::move(d)};
        }
        if (kind == "predicate") {
            ir::PredicateDecl d;
            d.provenance = provenance(obj);
            d.name = req_string(obj, "name");
            d.symbol_ref = opt_symbol_ref(obj, "symbol_ref");
            d.params = params(obj, "params");
            return ir::Decl{std::move(d)};
        }
        if (kind == "agent") {
            return agent_decl(obj);
        }
        if (kind == "contract") {
            return contract_decl(obj);
        }
        if (kind == "flow") {
            return flow_decl(obj);
        }
        if (kind == "workflow") {
            return workflow_decl(obj);
        }
        if (kind == "fn") {
            return fn_decl(obj);
        }
        if (kind == "trait") {
            return trait_decl(obj);
        }
        if (kind == "impl") {
            return impl_decl(obj);
        }
        if (kind == "instance") {
            return instance_decl(obj);
        }
        fail();
        return std::nullopt;
    }

    [[nodiscard]] ir::CapabilityEffectSpec capability_effect(const JsonValue &obj) {
        ir::CapabilityEffectSpec effect;
        effect.declared = opt_bool(obj, "declared");
        if (!parse_capability_effect_kind(req_string(obj, "kind"), effect.kind)) { fail(); }
        if (!parse_capability_receipt_mode(req_string(obj, "receipt_mode"), effect.receipt_mode)) {
            fail();
        }
        if (!parse_capability_retry_mode(req_string(obj, "retry_mode"), effect.retry_mode)) {
            fail();
        }
        effect.domain = opt_nullable_string(obj, "domain");
        effect.idempotency_key = opt_nullable_string(obj, "idempotency_key");
        effect.timeout = opt_nullable_string(obj, "timeout");
        effect.compensation = opt_nullable_string(obj, "compensation");
        effect.policies = string_array(obj, "policies");
        effect.source_range = source_range(obj);
        return effect;
    }

    [[nodiscard]] std::optional<std::string> opt_nullable_string(const JsonValue &obj,
                                                                std::string_view key) {
        const auto *field = obj.get(key);
        if (field == nullptr || field->is_null()) { return std::nullopt; }
        const auto value = field->as_string();
        if (!value.has_value()) { fail(); return std::nullopt; }
        return std::string(*value);
    }

    [[nodiscard]] std::optional<ir::Decl> enum_decl(const JsonValue &obj) {
        ir::EnumDecl d;
        d.provenance = provenance(obj);
        d.name = req_string(obj, "name");
        d.symbol_ref = opt_symbol_ref(obj, "symbol_ref");
        d.type_param_count = opt_u32(obj, "type_param_count", 0);
        d.type_param_variances = parse_variances(obj, "type_param_variances");
        d.member_type_templates = parse_member_type_templates(obj);
        const auto *variants = obj.get("variants");
        if (variants != nullptr && variants->is_array()) {
            for (const auto &item : variants->array_items) {
                ir::EnumVariantDecl variant;
                variant.name = req_string(*item, "name");
                if (!parse_enum_variant_payload_kind(req_string(*item, "payload_kind"),
                                                     variant.payload_kind)) {
                    fail();
                }
                variant.source_range = source_range(*item);
                variant.payload_type_template_roots =
                    parse_u32_array(*item, "payload_type_template_roots");
                const auto *payload = item->get("payload");
                if (payload != nullptr && payload->is_array()) {
                    for (const auto &slot : payload->array_items) {
                        variant.payload.push_back(opt_type_ref(*slot, "type_ref", "type"));
                    }
                }
                const auto *fields = item->get("fields");
                if (fields != nullptr && fields->is_array()) {
                    for (const auto &field_obj : fields->array_items) {
                        ir::EnumVariantFieldDecl field;
                        field.name = req_string(*field_obj, "name");
                        field.type_ref = opt_type_ref(*field_obj, "type_ref", "type");
                        field.source_range = source_range(*field_obj);
                        field.default_value = opt_expr(*field_obj, "default_value");
                        variant.fields.push_back(std::move(field));
                    }
                }
                d.variants.push_back(std::move(variant));
            }
        }
        return ir::Decl{std::move(d)};
    }

    [[nodiscard]] std::optional<ir::Decl> agent_decl(const JsonValue &obj) {
        ir::AgentDecl d;
        d.provenance = provenance(obj);
        d.name = req_string(obj, "name");
        d.symbol_ref = opt_symbol_ref(obj, "symbol_ref");
        d.input_type_ref = opt_type_ref(obj, "input_type_ref", "input_type");
        d.context_type_ref = opt_type_ref(obj, "context_type_ref", "context_type");
        d.output_type_ref = opt_type_ref(obj, "output_type_ref", "output_type");
        d.states = string_array(obj, "states");
        d.initial_state = req_string(obj, "initial_state");
        d.final_states = string_array(obj, "final_states");
        // The display-only "capabilities" string array is derived from
        // capability_refs on emit; the authoritative refs live in
        // "capability_refs" (omitted when empty).
        if (const auto *refs = obj.get("capability_refs"); refs != nullptr && refs->is_array()) {
            for (const auto &item : refs->array_items) {
                d.capability_refs.push_back(symbol_ref(*item));
            }
        }
        if (const auto *quota = obj.get("quota"); quota != nullptr && quota->is_array()) {
            for (const auto &item : quota->array_items) {
                d.quota.push_back(
                    ir::QuotaItem{.name = req_string(*item, "name"),
                                  .value = req_string(*item, "value")});
            }
        }
        if (const auto *trans = obj.get("transitions"); trans != nullptr && trans->is_array()) {
            for (const auto &item : trans->array_items) {
                d.transitions.push_back(
                    ir::TransitionDecl{.from_state = req_string(*item, "from_state"),
                                       .to_state = req_string(*item, "to_state")});
            }
        }
        return ir::Decl{std::move(d)};
    }

    [[nodiscard]] std::optional<ir::Decl> contract_decl(const JsonValue &obj) {
        ir::ContractDecl d;
        d.provenance = provenance(obj);
        d.target_ref = opt_symbol_ref(obj, "target_ref");
        const auto *clauses = obj.get("clauses");
        if (clauses != nullptr && clauses->is_array()) {
            for (const auto &item : clauses->array_items) {
                ir::ContractClause clause;
                if (!parse_contract_clause_kind(req_string(*item, "kind"), clause.kind)) { fail(); }
                clause.is_wildcard = opt_bool(*item, "wildcard");
                clause.source_range = source_range(*item);
                if (const auto *e = item->get("expr"); e != nullptr) {
                    clause.value = expr(*e);
                } else if (const auto *t = item->get("temporal"); t != nullptr) {
                    clause.value = temporal_expr(*t);
                }
                if (const auto *dec = item->get("decreases"); dec != nullptr) {
                    clause.decreases_wildcard = opt_bool(*dec, "wildcard");
                    const auto *terms = dec->get("terms");
                    if (terms != nullptr && terms->is_array()) {
                        for (const auto &term : terms->array_items) {
                            clause.decreases_terms.push_back(expr(*term));
                        }
                    }
                }
                d.clauses.push_back(std::move(clause));
            }
        }
        return ir::Decl{std::move(d)};
    }

    [[nodiscard]] ir::StatePolicyItem state_policy_item(const JsonValue &obj) {
        const auto kind = req_string(obj, "kind");
        if (kind == "retry") {
            return ir::RetryPolicy{.limit = req_string(obj, "limit")};
        }
        if (kind == "retry_on") {
            return ir::RetryOnPolicy{.targets = string_array(obj, "targets")};
        }
        if (kind == "timeout") {
            return ir::TimeoutPolicy{.duration = req_string(obj, "duration")};
        }
        fail();
        return ir::RetryPolicy{};
    }

    [[nodiscard]] std::optional<ir::Decl> flow_decl(const JsonValue &obj) {
        ir::FlowDecl d;
        d.provenance = provenance(obj);
        d.target_ref = opt_symbol_ref(obj, "target_ref");
        const auto *handlers = obj.get("state_handlers");
        if (handlers != nullptr && handlers->is_array()) {
            for (const auto &item : handlers->array_items) {
                ir::StateHandler handler;
                handler.state_name = req_string(*item, "state_name");
                handler.source_range = source_range(*item);
                if (const auto *policy = item->get("policy");
                    policy != nullptr && policy->is_array()) {
                    for (const auto &p : policy->array_items) {
                        handler.policy.push_back(state_policy_item(*p));
                    }
                }
                // "summary" is a derived analysis; recomputed after load.
                const auto *body = item->get("body");
                if (body == nullptr) { fail(); } else { handler.body = block(*body); }
                d.state_handlers.push_back(std::move(handler));
            }
        }
        return ir::Decl{std::move(d)};
    }

    [[nodiscard]] std::optional<ir::Decl> workflow_decl(const JsonValue &obj) {
        ir::WorkflowDecl d;
        d.provenance = provenance(obj);
        d.name = req_string(obj, "name");
        d.symbol_ref = opt_symbol_ref(obj, "symbol_ref");
        d.input_type_ref = opt_type_ref(obj, "input_type_ref", "input_type");
        d.output_type_ref = opt_type_ref(obj, "output_type_ref", "output_type");
        const auto *nodes = obj.get("nodes");
        if (nodes != nullptr && nodes->is_array()) {
            for (const auto &item : nodes->array_items) {
                ir::WorkflowNode node;
                node.name = req_string(*item, "name");
                node.source_range = source_range(*item);
                node.target_ref = opt_symbol_ref(*item, "target_ref");
                node.input = opt_expr(*item, "input");
                // "input_summary" is derived; recomputed after load.
                node.after = string_array(*item, "after");
                d.nodes.push_back(std::move(node));
            }
        }
        if (const auto *safety = obj.get("safety"); safety != nullptr && safety->is_array()) {
            for (const auto &item : safety->array_items) {
                d.safety.push_back(temporal_expr(*item));
            }
        }
        if (const auto *liveness = obj.get("liveness"); liveness != nullptr && liveness->is_array()) {
            for (const auto &item : liveness->array_items) {
                d.liveness.push_back(temporal_expr(*item));
            }
        }
        // "return_summary" is derived; recomputed after load.
        d.return_value = opt_expr(obj, "return_value");
        return ir::Decl{std::move(d)};
    }

    [[nodiscard]] std::optional<ir::Decl> fn_decl(const JsonValue &obj) {
        ir::FnDecl d;
        d.provenance = provenance(obj);
        d.name = req_string(obj, "name");
        d.type_param_names = string_array(obj, "type_params");
        d.params = params(obj, "params");
        if (const auto *rt = obj.get("return_type"); rt != nullptr) {
            d.has_return_type = true;
            d.return_type_ref = opt_type_ref(obj, "return_type_ref", "return_type");
        }
        d.has_body = opt_bool(obj, "has_body");
        if (const auto *effect = obj.get("effect"); effect != nullptr) {
            d.effect = fn_effect(*effect);
        }
        if (const auto *body = obj.get("body"); body != nullptr && !body->is_null()) {
            d.body = make_owned<ir::Block>(block(*body));
        }
        d.symbol_ref = opt_symbol_ref(obj, "symbol_ref");
        return ir::Decl{std::move(d)};
    }

    [[nodiscard]] ir::FnEffectClause fn_effect(const JsonValue &obj) {
        ir::FnEffectClause effect;
        const auto kind = req_string(obj, "kind");
        if (kind == "Pure") { effect.kind = ir::FnEffectKind::Pure; }
        else if (kind == "Nondet") { effect.kind = ir::FnEffectKind::Nondet; }
        else if (kind == "Capability") { effect.kind = ir::FnEffectKind::Capability; }
        else { fail(); }
        if (const auto *caps = obj.get("capabilities"); caps != nullptr && caps->is_array()) {
            for (const auto &item : caps->array_items) {
                effect.capabilities.push_back(symbol_ref(*item));
            }
        }
        effect.has_decreases = opt_bool(obj, "has_decreases");
        if (const auto *terms = obj.get("decreases_terms");
            terms != nullptr && terms->is_array()) {
            for (const auto &item : terms->array_items) {
                effect.decreases_terms.push_back(expr(*item));
            }
        }
        return effect;
    }

    [[nodiscard]] ir::TraitMethodSig trait_method_sig(const JsonValue &obj) {
        ir::TraitMethodSig method;
        method.name = req_string(obj, "name");
        method.type_param_names = string_array(obj, "type_params");
        method.params = params(obj, "params");
        if (const auto *rt = obj.get("return_type"); rt != nullptr) {
            method.has_return_type = true;
            method.return_type_ref = opt_type_ref(obj, "return_type_ref", "return_type");
        }
        if (const auto *effect = obj.get("effect"); effect != nullptr) {
            method.effect = fn_effect(*effect);
        }
        return method;
    }

    [[nodiscard]] std::optional<ir::Decl> trait_decl(const JsonValue &obj) {
        ir::TraitDecl d;
        d.provenance = provenance(obj);
        d.name = req_string(obj, "name");
        d.type_param_names = string_array(obj, "type_params");
        if (const auto *supers = obj.get("super_traits");
            supers != nullptr && supers->is_array()) {
            for (const auto &item : supers->array_items) {
                d.super_traits.push_back(symbol_ref(*item));
            }
        }
        if (const auto *methods = obj.get("methods"); methods != nullptr && methods->is_array()) {
            for (const auto &item : methods->array_items) {
                d.methods.push_back(trait_method_sig(*item));
            }
        }
        d.symbol_ref = opt_symbol_ref(obj, "symbol_ref");
        return ir::Decl{std::move(d)};
    }

    [[nodiscard]] std::optional<ir::Decl> impl_decl(const JsonValue &obj) {
        ir::ImplDecl d;
        d.provenance = provenance(obj);
        d.index = static_cast<std::size_t>(req_i64(obj, "index"));
        d.is_inherent = opt_bool(obj, "is_inherent");
        d.target_type_ref = opt_type_ref(obj, "target_type_ref", "target_type");
        d.trait_ref = opt_symbol_ref(obj, "trait_ref");
        if (const auto *args = obj.get("trait_type_args"); args != nullptr && args->is_array()) {
            for (const auto &item : args->array_items) {
                d.trait_type_args.push_back(type_ref(*item));
            }
        }
        d.type_param_names = string_array(obj, "type_params");
        if (const auto *refs = obj.get("method_refs"); refs != nullptr && refs->is_array()) {
            for (const auto &item : refs->array_items) {
                d.method_refs.push_back(symbol_ref(*item));
            }
        }
        return ir::Decl{std::move(d)};
    }

    [[nodiscard]] std::optional<ir::Decl> instance_decl(const JsonValue &obj) {
        ir::InstanceDecl d;
        d.provenance = provenance(obj);
        d.name = req_string(obj, "name");
        const auto kind = req_string(obj, "instance_kind");
        if (kind == "capability") { d.kind = ir::InstanceKind::Capability; }
        else if (kind == "predicate") { d.kind = ir::InstanceKind::Predicate; }
        else if (kind == "agent") { d.kind = ir::InstanceKind::Agent; }
        else if (kind == "workflow") { d.kind = ir::InstanceKind::Workflow; }
        else if (kind == "fn") { d.kind = ir::InstanceKind::Fn; }
        else if (kind == "unknown") { d.kind = ir::InstanceKind::Unknown; }
        else { fail(); }
        d.symbol_ref = opt_symbol_ref(obj, "symbol_ref");
        if (const auto *args = obj.get("type_args"); args != nullptr && args->is_array()) {
            for (const auto &item : args->array_items) {
                d.type_args.push_back(type_ref(*item));
            }
        }
        d.params = params(obj, "params");
        d.return_type_ref = opt_type_ref(obj, "return_type_ref", "return_type");
        d.agent_input_type_ref = opt_type_ref(obj, "agent_input_type_ref", "agent_input_type");
        d.agent_context_type_ref =
            opt_type_ref(obj, "agent_context_type_ref", "agent_context_type");
        d.agent_output_type_ref = opt_type_ref(obj, "agent_output_type_ref", "agent_output_type");
        d.workflow_input_type_ref =
            opt_type_ref(obj, "workflow_input_type_ref", "workflow_input_type");
        d.workflow_output_type_ref =
            opt_type_ref(obj, "workflow_output_type_ref", "workflow_output_type");
        return ir::Decl{std::move(d)};
    }

    // --- top level ----------------------------------------------------------

    [[nodiscard]] bool read_program(const JsonValue &root) {
        if (!root.is_object()) { fail(); return false; }
        program_->format_version = req_string(root, "format_version");
        // "formal_observations" is a derived analysis recomputed after load.
        const auto *declarations = root.get("declarations");
        if (declarations == nullptr || !declarations->is_array()) { fail(); return false; }
        for (const auto &item : declarations->array_items) {
            auto parsed = decl(*item);
            if (!ok_ || !parsed.has_value()) { fail(); return false; }
            program_->declarations.push_back(std::move(*parsed));
        }
        return ok_;
    }

  private:
    ir::Program *program_{nullptr};
    bool ok_{true};
};

} // namespace

std::optional<ir::Program> parse_program_ir_json(std::string_view json) {
    auto parsed = ahfl::json::parse_json(json);
    if (!parsed.has_value() || *parsed == nullptr) {
        return std::nullopt;
    }
    ir::Program program;
    // The default-constructed Program carries format_version = kFormatVersion;
    // read_program overwrites it from JSON.
    IrJsonReader reader(program);
    if (!reader.read_program(**parsed)) {
        return std::nullopt;
    }
    // Derived analyses (formal observations, flow/workflow summaries) are not
    // stored in JSON — recompute them from the parsed declarations so a
    // subsequent print_program_ir_json reproduces the input byte-for-byte.
    ir::recompute_derived_analyses(program, ir::ProgramPhase::Analyzed);
    return program;
}

} // namespace ahfl
