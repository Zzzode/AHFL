#include "ahfl/compiler/ir/typed_hir_lower.hpp"

#include "ahfl/base/support/overloaded.hpp"
#include "ahfl/compiler/frontend/ast.hpp"
#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/ir/analysis.hpp"
#include "ahfl/compiler/ir/identity.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

// ---------------------------------------------------------------------------
// Typed HIR -> Semantic IR canonical boundary
//
// Public overloads accepting ast::Program / SourceGraph are API adapters only.
// Semantic IR facts are read from TypedProgram: declarations, expressions,
// statements, temporal expressions, resolver snapshots, module/import payloads,
// and source provenance all come from the typed store.
// ---------------------------------------------------------------------------

namespace ahfl {

namespace {

[[nodiscard]] bool paths_equal(const ir::Path &lhs, const ir::Path &rhs) {
    return lhs.root_kind == rhs.root_kind && lhs.root_name == rhs.root_name &&
           lhs.members == rhs.members;
}

[[nodiscard]] bool workflow_value_reads_equal(const ir::WorkflowValueRead &lhs,
                                              const ir::WorkflowValueRead &rhs) {
    return lhs.kind == rhs.kind && lhs.root_name == rhs.root_name && lhs.members == rhs.members;
}

[[nodiscard]] ir::TypeRef make_type_ref_value(ir::TypeRefKind kind, std::string display_name) {
    return ir::TypeRef{
        .kind = kind,
        .display_name = std::move(display_name),
        .canonical_name = {},
        .variant_name = {},
        .string_bounds = std::nullopt,
        .decimal_scale = std::nullopt,
        .source_range = std::nullopt,
        .first = nullptr,
        .second = nullptr,
    };
}

[[nodiscard]] std::string logical_source_path(std::string_view module_name) {
    if (module_name.empty())
        return {};
    std::string path;
    path.reserve(module_name.size() + 5);
    for (std::size_t index = 0; index < module_name.size(); ++index) {
        if (index + 1 < module_name.size() && module_name[index] == ':' &&
            module_name[index + 1] == ':') {
            path += '/';
            ++index;
            continue;
        }
        path += module_name[index];
    }
    path += ".ahfl";
    return path;
}

[[nodiscard]] ir::CapabilityEffectKind
lower_capability_effect_kind(ast::CapabilityEffectKind kind) {
    switch (kind) {
    case ast::CapabilityEffectKind::Unknown:
        return ir::CapabilityEffectKind::Unknown;
    case ast::CapabilityEffectKind::Read:
        return ir::CapabilityEffectKind::Read;
    case ast::CapabilityEffectKind::ExternalSideEffect:
        return ir::CapabilityEffectKind::ExternalSideEffect;
    case ast::CapabilityEffectKind::DurableWrite:
        return ir::CapabilityEffectKind::DurableWrite;
    case ast::CapabilityEffectKind::FinancialWrite:
        return ir::CapabilityEffectKind::FinancialWrite;
    }
    return ir::CapabilityEffectKind::Unknown;
}

[[nodiscard]] ir::CapabilityReceiptMode
lower_capability_receipt_mode(ast::CapabilityReceiptMode mode) {
    switch (mode) {
    case ast::CapabilityReceiptMode::None:
        return ir::CapabilityReceiptMode::None;
    case ast::CapabilityReceiptMode::Optional:
        return ir::CapabilityReceiptMode::Optional;
    case ast::CapabilityReceiptMode::Required:
        return ir::CapabilityReceiptMode::Required;
    }
    return ir::CapabilityReceiptMode::None;
}

[[nodiscard]] ir::CapabilityRetryMode lower_capability_retry_mode(ast::CapabilityRetryMode mode) {
    switch (mode) {
    case ast::CapabilityRetryMode::Unsafe:
        return ir::CapabilityRetryMode::Unsafe;
    case ast::CapabilityRetryMode::SafeIfIdempotent:
        return ir::CapabilityRetryMode::SafeIfIdempotent;
    case ast::CapabilityRetryMode::Safe:
        return ir::CapabilityRetryMode::Safe;
    }
    return ir::CapabilityRetryMode::Unsafe;
}

/// Build a CapabilityEffectSpec from TypeEnvironment's CapabilityEffectTypeInfo
/// (T1.8 Phase 4: replaces the AST-based lower_capability_effect for the info path).
[[nodiscard]] ir::CapabilityEffectSpec
lower_capability_effect_from_info(const CapabilityEffectTypeInfo &info) {
    ir::CapabilityEffectSpec effect;
    if (!info.declared)
        return effect;
    effect.declared = true;
    effect.kind =
        lower_capability_effect_kind(static_cast<ast::CapabilityEffectKind>(info.effect_kind));
    effect.receipt_mode =
        lower_capability_receipt_mode(static_cast<ast::CapabilityReceiptMode>(info.receipt_mode));
    effect.retry_mode =
        lower_capability_retry_mode(static_cast<ast::CapabilityRetryMode>(info.retry_mode));
    effect.source_range = info.source_range;
    if (!info.domain.empty())
        effect.domain = info.domain;
    if (!info.idempotency_key.empty())
        effect.idempotency_key = info.idempotency_key;
    if (!info.timeout.empty())
        effect.timeout = info.timeout;
    if (!info.compensation.empty())
        effect.compensation = info.compensation;
    effect.policies = info.policies;
    return effect;
}

[[nodiscard]] ir::ExprUnaryOp lower_expr_unary_op(ast::ExprUnaryOp op) {
    switch (op) {
    case ast::ExprUnaryOp::Not:
        return ir::ExprUnaryOp::Not;
    case ast::ExprUnaryOp::Negate:
        return ir::ExprUnaryOp::Negate;
    case ast::ExprUnaryOp::Positive:
        return ir::ExprUnaryOp::Positive;
    }
    return ir::ExprUnaryOp::Not;
}

[[nodiscard]] ir::ExprBinaryOp lower_expr_binary_op(ast::ExprBinaryOp op) {
    switch (op) {
    case ast::ExprBinaryOp::Implies:
        return ir::ExprBinaryOp::Implies;
    case ast::ExprBinaryOp::Or:
        return ir::ExprBinaryOp::Or;
    case ast::ExprBinaryOp::And:
        return ir::ExprBinaryOp::And;
    case ast::ExprBinaryOp::Equal:
        return ir::ExprBinaryOp::Equal;
    case ast::ExprBinaryOp::NotEqual:
        return ir::ExprBinaryOp::NotEqual;
    case ast::ExprBinaryOp::Less:
        return ir::ExprBinaryOp::Less;
    case ast::ExprBinaryOp::LessEqual:
        return ir::ExprBinaryOp::LessEqual;
    case ast::ExprBinaryOp::Greater:
        return ir::ExprBinaryOp::Greater;
    case ast::ExprBinaryOp::GreaterEqual:
        return ir::ExprBinaryOp::GreaterEqual;
    case ast::ExprBinaryOp::Add:
        return ir::ExprBinaryOp::Add;
    case ast::ExprBinaryOp::Subtract:
        return ir::ExprBinaryOp::Subtract;
    case ast::ExprBinaryOp::Multiply:
        return ir::ExprBinaryOp::Multiply;
    case ast::ExprBinaryOp::Divide:
        return ir::ExprBinaryOp::Divide;
    case ast::ExprBinaryOp::Modulo:
        return ir::ExprBinaryOp::Modulo;
    }
    return ir::ExprBinaryOp::Implies;
}

[[nodiscard]] ir::ContractClauseKind lower_contract_clause_kind(ast::ContractClauseKind kind) {
    switch (kind) {
    case ast::ContractClauseKind::Requires:
        return ir::ContractClauseKind::Requires;
    case ast::ContractClauseKind::Ensures:
        return ir::ContractClauseKind::Ensures;
    case ast::ContractClauseKind::Invariant:
        return ir::ContractClauseKind::Invariant;
    case ast::ContractClauseKind::Forbid:
        return ir::ContractClauseKind::Forbid;
    }
    return ir::ContractClauseKind::Requires;
}

[[nodiscard]] ir::SymbolRefKind lower_symbol_ref_kind(SymbolKind kind) {
    switch (kind) {
    case SymbolKind::Struct:
    case SymbolKind::Enum:
    case SymbolKind::TypeAlias:
        return ir::SymbolRefKind::Type;
    case SymbolKind::Const:
        return ir::SymbolRefKind::Const;
    case SymbolKind::Capability:
        return ir::SymbolRefKind::Capability;
    case SymbolKind::Predicate:
        return ir::SymbolRefKind::Predicate;
    case SymbolKind::Agent:
        return ir::SymbolRefKind::Agent;
    case SymbolKind::Workflow:
        return ir::SymbolRefKind::Workflow;
    case SymbolKind::Function:
        // P2c (RFC §3.2.2): a top-level fn symbol lowers as Function.
        return ir::SymbolRefKind::Function;
    case SymbolKind::Trait:
        // P3 (RFC §3.2.2 / type-system §1.3): a trait symbol lowers as a Type
        // ref — it occupies type positions at bound/impl sites. No method-call
        // lowering today (P3c).
        return ir::SymbolRefKind::Type;
    }
    return ir::SymbolRefKind::Unknown;
}

template <typename T> void push_unique_value(std::vector<T> &values, const T &value) {
    for (const auto &existing : values)
        if (existing == value)
            return;
    values.push_back(value);
}

void push_unique_path(std::vector<ir::Path> &values, const ir::Path &value) {
    for (const auto &existing : values)
        if (paths_equal(existing, value))
            return;
    values.push_back(value);
}

void push_unique_workflow_value_read(std::vector<ir::WorkflowValueRead> &values,
                                     const ir::WorkflowValueRead &value) {
    for (const auto &existing : values)
        if (workflow_value_reads_equal(existing, value))
            return;
    values.push_back(value);
}

// ---------------------------------------------------------------------------
// Typed-tree based lowerer. TypedProgram is the canonical structural source for
// backend-ready Semantic IR.
// ---------------------------------------------------------------------------

class TypedIrLowerer final {
  public:
    explicit TypedIrLowerer(const TypedProgram &typed_program) : typed_program_(&typed_program) {}

    [[nodiscard]] ir::Program lower() const {
        ir::Program program_ir;
        arena_ = &program_ir.expr_arena;
        program_ir.declarations.reserve(typed_program_->declarations.size());
        for (const TypedDecl *typed_decl : ordered_typed_declarations()) {
            if (typed_decl == nullptr)
                continue;
            current_source_id_ = typed_decl->source_id;
            current_module_name_ = module_name_for(*typed_decl);
            program_ir.declarations.push_back(lower_typed_declaration(*typed_decl));
        }
        current_source_id_.reset();
        current_module_name_.clear();
        return program_ir;
    }

  private:
    const TypedProgram *typed_program_{nullptr};
    mutable std::optional<SourceId> current_source_id_;
    mutable std::string current_module_name_;
    mutable ir::ExprArena *arena_{nullptr};
    mutable std::uint32_t next_expr_id_{0};
    mutable std::uint32_t next_statement_id_{0};
    mutable std::uint32_t next_decl_id_{0};

    [[nodiscard]] std::vector<const TypedDecl *> ordered_typed_declarations() const {
        std::vector<const TypedDecl *> declarations;
        declarations.reserve(typed_program_->declarations.size());
        for (const auto &decl : typed_program_->declarations) {
            declarations.push_back(&decl);
        }
        std::stable_sort(
            declarations.begin(), declarations.end(), [](const auto *lhs, const auto *rhs) {
                const auto lhs_source = lhs->source_id.has_value() ? lhs->source_id->value : 0;
                const auto rhs_source = rhs->source_id.has_value() ? rhs->source_id->value : 0;
                if (lhs_source != rhs_source) {
                    return lhs_source < rhs_source;
                }
                if (lhs->range.begin_offset != rhs->range.begin_offset) {
                    return lhs->range.begin_offset < rhs->range.begin_offset;
                }
                return lhs->range.end_offset < rhs->range.end_offset;
            });
        return declarations;
    }

    [[nodiscard]] std::string module_name_for(const TypedDecl &decl) const {
        if (const auto *module = payload_as<ModuleDeclInfo>(&decl); module != nullptr) {
            return module->name;
        }
        for (const auto &candidate : typed_program_->declarations) {
            if (candidate.kind != ast::NodeKind::ModuleDecl ||
                candidate.source_id != decl.source_id) {
                continue;
            }
            if (const auto *module = payload_as<ModuleDeclInfo>(&candidate); module != nullptr) {
                return module->name;
            }
        }
        if (decl.symbol.value != 0) {
            if (const auto symbol = typed_program_->find_symbol(decl.symbol); symbol.has_value()) {
                return symbol->get().module_name;
            }
        }
        return current_module_name_;
    }

    // =====================================================================
    // Small factory helpers
    // =====================================================================

    template <typename T>
    [[nodiscard]] ir::ExprRef
    make_expr(T node, std::optional<SourceRange> source_range = std::nullopt) const {
        assert(arena_ != nullptr);
        return arena_->make(
            ir::ExprNode{std::move(node)}, std::move(source_range), {}, next_expr_id_++);
    }

    template <typename T>
    [[nodiscard]] ir::TemporalExprPtr
    make_temporal(T node, std::optional<SourceRange> source_range = std::nullopt) const {
        auto expr = make_owned<ir::TemporalExpr>();
        expr->node = std::move(node);
        expr->source_range = source_range;
        return expr;
    }

    template <typename T>
    [[nodiscard]] ir::StatementPtr
    make_statement(T node, std::optional<SourceRange> source_range = std::nullopt) const {
        auto statement = make_owned<ir::Statement>();
        statement->node = std::move(node);
        statement->source_range = source_range;
        statement->id = next_statement_id_++;
        return statement;
    }

    template <typename InfoT>
    [[nodiscard]] static const InfoT *payload_as(const TypedDecl *decl) noexcept {
        if (decl == nullptr)
            return nullptr;
        return std::get_if<InfoT>(&decl->payload);
    }

    // =====================================================================
    // Source / provenance helpers
    // =====================================================================

    void enter_source(const SourceUnit &source) const {
        current_source_id_ = source.id;
        current_module_name_ = source.module_name;
    }

    void leave_source() const {
        current_source_id_.reset();
        current_module_name_.clear();
    }

    [[nodiscard]] ir::DeclarationProvenance
    current_provenance(std::optional<SourceRange> source_range = std::nullopt) const {
        if (!current_source_id_.has_value() || current_module_name_.empty()) {
            return ir::DeclarationProvenance{
                .module_name = {},
                .source_path = {},
                .source_range = source_range,
            };
        }
        return ir::DeclarationProvenance{
            .module_name = current_module_name_,
            .source_path = logical_source_path(current_module_name_),
            .source_range = source_range,
        };
    }

    template <typename DeclT>
    [[nodiscard]] DeclT
    with_provenance(DeclT declaration,
                    std::optional<SourceRange> source_range = std::nullopt) const {
        declaration.provenance = current_provenance(source_range);
        declaration.provenance.id = next_decl_id_++;
        return declaration;
    }

    // =====================================================================
    // Symbol / reference helpers
    // =====================================================================

    [[nodiscard]] ir::TypeRefPtr make_type_ref(ir::TypeRef ref) const {
        return make_owned<ir::TypeRef>(std::move(ref));
    }

    [[nodiscard]] ir::SymbolRef symbol_ref_from_symbol(MaybeCRef<Symbol> symbol,
                                                       std::string_view label) const {
        if (!symbol.has_value()) {
            throw std::logic_error("TypedProgram is missing required " + std::string(label) +
                                   " symbol");
        }
        const auto &value = symbol->get();
        return ir::SymbolRef{
            .kind = lower_symbol_ref_kind(value.kind),
            .canonical_name = value.canonical_name,
            .local_name = value.local_name,
            .module_name = value.module_name,
            .id = value.id.value,
        };
    }

    // =====================================================================
    // Type-ref builders
    // =====================================================================

    [[nodiscard]] ir::TypeRef type_ref_from_type(const Type &type) const {
        return type.visit(types::Overloads{
            [&](const types::AnyT &) {
                return make_type_ref_value(ir::TypeRefKind::Any, type.describe());
            },
            [&](const types::NeverT &) {
                return make_type_ref_value(ir::TypeRefKind::Never, type.describe());
            },
            [&](const types::ErrorT &) -> ir::TypeRef {
                throw std::logic_error(
                    "TypedProgram contains an error type at the IR lowering boundary");
            },
            [&](const types::UnitT &) {
                return make_type_ref_value(ir::TypeRefKind::Unit, type.describe());
            },
            [&](const types::BoolT &) {
                return make_type_ref_value(ir::TypeRefKind::Bool, type.describe());
            },
            [&](const types::IntT &) {
                return make_type_ref_value(ir::TypeRefKind::Int, type.describe());
            },
            [&](const types::FloatT &) {
                return make_type_ref_value(ir::TypeRefKind::Float, type.describe());
            },
            [&](const types::StringT &) {
                return make_type_ref_value(ir::TypeRefKind::String, type.describe());
            },
            [&](const types::BoundedStringT &value) {
                auto ref = make_type_ref_value(ir::TypeRefKind::BoundedString, type.describe());
                ref.string_bounds = std::make_pair(value.minimum, value.maximum);
                return ref;
            },
            [&](const types::UUIDT &) {
                return make_type_ref_value(ir::TypeRefKind::UUID, type.describe());
            },
            [&](const types::TimestampT &) {
                return make_type_ref_value(ir::TypeRefKind::Timestamp, type.describe());
            },
            [&](const types::DurationT &) {
                return make_type_ref_value(ir::TypeRefKind::Duration, type.describe());
            },
            [&](const types::DecimalT &value) {
                auto ref = make_type_ref_value(ir::TypeRefKind::Decimal, type.describe());
                ref.decimal_scale = value.scale;
                return ref;
            },
            [&](const types::StructT &value) {
                auto ref = make_type_ref_value(ir::TypeRefKind::Struct, type.describe());
                ref.canonical_name = value.canonical_name;
                return ref;
            },
            [&](const types::EnumT &value) {
                auto ref = make_type_ref_value(ir::TypeRefKind::Enum, type.describe());
                ref.canonical_name = value.canonical_name;
                return ref;
            },
            [&](const types::EnumVariantT &value) {
                auto ref = make_type_ref_value(ir::TypeRefKind::Enum, type.describe());
                ref.canonical_name = value.canonical_name;
                ref.variant_name = value.variant_name;
                return ref;
            },
            [&](const types::OptionalT &value) {
                auto ref = make_type_ref_value(ir::TypeRefKind::Optional, type.describe());
                if (value.inner != nullptr)
                    ref.first = make_type_ref(type_ref_from_type(*value.inner));
                return ref;
            },
            [&](const types::ListT &value) {
                auto ref = make_type_ref_value(ir::TypeRefKind::List, type.describe());
                if (value.element != nullptr)
                    ref.first = make_type_ref(type_ref_from_type(*value.element));
                return ref;
            },
            [&](const types::SetT &value) {
                auto ref = make_type_ref_value(ir::TypeRefKind::Set, type.describe());
                if (value.element != nullptr)
                    ref.first = make_type_ref(type_ref_from_type(*value.element));
                return ref;
            },
            [&](const types::MapT &value) {
                auto ref = make_type_ref_value(ir::TypeRefKind::Map, type.describe());
                if (value.key != nullptr)
                    ref.first = make_type_ref(type_ref_from_type(*value.key));
                if (value.value != nullptr)
                    ref.second = make_type_ref(type_ref_from_type(*value.value));
                return ref;
            },
        });
    }

    [[nodiscard]] ir::TypeRef type_ref_from_required_type(TypePtr type,
                                                          SourceRange source_range,
                                                          std::string_view label) const {
        if (type == nullptr) {
            throw std::logic_error("TypedProgram is missing required " + std::string(label));
        }
        auto ref = type_ref_from_type(*type);
        ref.source_range = source_range;
        return ref;
    }

    // =====================================================================
    // Typed-tree-only helpers (zero AST dereference)
    // =====================================================================

    [[nodiscard]] std::string render_call_target(const TypedExpr &expr) const {
        if (expr.resolved_symbol.has_value())
            if (const auto symbol = typed_program_->find_symbol(*expr.resolved_symbol);
                symbol.has_value())
                return symbol->get().canonical_name;
        return expr.semantic_name;
    }

    [[nodiscard]] std::string render_struct_target(const TypedExpr &expr) const {
        if (expr.resolved_symbol.has_value())
            if (const auto symbol = typed_program_->find_symbol(*expr.resolved_symbol);
                symbol.has_value())
                return symbol->get().canonical_name;
        return expr.semantic_name;
    }

    [[nodiscard]] std::string render_qualified_value(const TypedExpr &expr) const {
        if (expr.resolved_symbol.has_value()) {
            if (const auto symbol = typed_program_->find_symbol(*expr.resolved_symbol);
                symbol.has_value()) {
                if (symbol->get().kind == SymbolKind::Const)
                    return symbol->get().canonical_name;
                const auto &canonical = symbol->get().canonical_name;
                if (!expr.semantic_name.empty()) {
                    const auto separator = expr.semantic_name.rfind("::");
                    if (separator != std::string::npos &&
                        separator + 2 < expr.semantic_name.size()) {
                        const auto variant = expr.semantic_name.substr(separator + 2);
                        if (!variant.empty() && variant != canonical)
                            return canonical + "::" + variant;
                    }
                }
                return canonical;
            }
        }
        return expr.semantic_name;
    }

    [[nodiscard]] ir::Path lower_path_from_typed(const TypedExpr &expr) const {
        return ir::Path{
            .root_kind = assign_root_to_ir(expr.path_root_kind),
            .root_name = expr.path_root,
            .members = expr.member_path,
        };
    }

    [[nodiscard]] const TypedExpr *child_by_role(const TypedExpr &parent,
                                                 TypedExprChildRole role) const {
        for (const auto &child : parent.children) {
            if (child.role != role)
                continue;
            const TypedExpr *target = resolve_child(*typed_program_, child);
            if (target != nullptr)
                return target;
        }
        return nullptr;
    }

    // Nested-dispatchable wrapper (so TypedExprPerKindLowerer can call a
    // static-looking method while resolving through the active TypedProgram).
    static const TypedExpr *resolve_child_by_role(const TypedIrLowerer &self,
                                                  const TypedExpr &parent,
                                                  TypedExprChildRole role) {
        return self.child_by_role(parent, role);
    }

    // =====================================================================
    // Expression lowering – 100% typed-tree based via typed_visit
    // =====================================================================

    struct TypedExprPerKindLowerer {
        const TypedIrLowerer &self;
        SourceRange range;

        ir::ExprRef visit_bool_literal(const TypedExpr &e) const {
            return self.make_expr(ir::BoolLiteralExpr{.value = e.bool_value}, range);
        }
        ir::ExprRef visit_integer_literal(const TypedExpr &e) const {
            return self.make_expr(
                ir::IntegerLiteralExpr{
                    .spelling = e.literal_spelling.empty() ? std::string{"0"} : e.literal_spelling,
                },
                range);
        }
        ir::ExprRef visit_float_literal(const TypedExpr &e) const {
            return self.make_expr(ir::FloatLiteralExpr{.spelling = e.literal_spelling}, range);
        }
        ir::ExprRef visit_decimal_literal(const TypedExpr &e) const {
            return self.make_expr(ir::DecimalLiteralExpr{.spelling = e.literal_spelling}, range);
        }
        ir::ExprRef visit_string_literal(const TypedExpr &e) const {
            return self.make_expr(ir::StringLiteralExpr{.spelling = e.literal_spelling}, range);
        }
        ir::ExprRef visit_duration_literal(const TypedExpr &e) const {
            return self.make_expr(
                ir::DurationLiteralExpr{
                    .spelling = e.literal_spelling.empty() ? e.semantic_name : e.literal_spelling,
                },
                range);
        }
        ir::ExprRef visit_none_literal(const TypedExpr &) const {
            return self.make_expr(ir::NoneLiteralExpr{}, range);
        }
        ir::ExprRef visit_some(const TypedExpr &e) const {
            const TypedExpr *operand =
                TypedIrLowerer::resolve_child_by_role(self, e, TypedExprChildRole::Operand);
            return self.make_expr(
                ir::SomeExpr{.value = operand ? self.lower_typed_expr(*operand) : nullptr}, range);
        }
        ir::ExprRef visit_path(const TypedExpr &e) const {
            return self.make_expr(ir::PathExpr{.path = self.lower_path_from_typed(e)}, range);
        }
        ir::ExprRef visit_qualified_value(const TypedExpr &e) const {
            return self.make_expr(ir::QualifiedValueExpr{.value = self.render_qualified_value(e)},
                                  range);
        }
        ir::ExprRef visit_call(const TypedExpr &e) const {
            ir::CallExpr call{.callee = self.render_call_target(e), .arguments = {}};
            for (const auto &child : e.children) {
                if (child.role != TypedExprChildRole::Argument)
                    continue;
                const TypedExpr *target = resolve_child(*self.typed_program_, child);
                if (target != nullptr)
                    call.arguments.push_back(self.lower_typed_expr(*target));
            }
            return self.make_expr(std::move(call), range);
        }
        ir::ExprRef visit_struct_literal(const TypedExpr &e) const {
            ir::StructLiteralExpr literal{.type_name = self.render_struct_target(e), .fields = {}};
            for (const auto &child : e.children) {
                if (child.role != TypedExprChildRole::StructFieldValue)
                    continue;
                const TypedExpr *target = resolve_child(*self.typed_program_, child);
                if (target != nullptr) {
                    literal.fields.push_back(ir::StructFieldInit{
                        .name = child.name,
                        .value = self.lower_typed_expr(*target),
                    });
                }
            }
            return self.make_expr(std::move(literal), range);
        }
        ir::ExprRef visit_list_literal(const TypedExpr &e) const {
            ir::ListLiteralExpr literal;
            for (const auto &child : e.children) {
                if (child.role != TypedExprChildRole::CollectionElement)
                    continue;
                const TypedExpr *target = resolve_child(*self.typed_program_, child);
                if (target != nullptr)
                    literal.items.push_back(self.lower_typed_expr(*target));
            }
            return self.make_expr(std::move(literal), range);
        }
        ir::ExprRef visit_set_literal(const TypedExpr &e) const {
            ir::SetLiteralExpr literal;
            for (const auto &child : e.children) {
                if (child.role != TypedExprChildRole::CollectionElement)
                    continue;
                const TypedExpr *target = resolve_child(*self.typed_program_, child);
                if (target != nullptr)
                    literal.items.push_back(self.lower_typed_expr(*target));
            }
            return self.make_expr(std::move(literal), range);
        }
        ir::ExprRef visit_map_literal(const TypedExpr &e) const {
            ir::MapLiteralExpr literal;
            // Children are emitted MapKey,MapValue,MapKey,MapValue,... by
            // `typed_children_for`. Iterate pairwise so order is identical to
            // the AST visitor (T1.2 fingerprint-equivalence requirement).
            for (std::size_t i = 0; i + 1 < e.children.size();) {
                const auto &key_child = e.children[i];
                const auto &value_child = e.children[i + 1];
                const TypedExpr *k = resolve_child(*self.typed_program_, key_child);
                const TypedExpr *v = resolve_child(*self.typed_program_, value_child);
                if (key_child.role == TypedExprChildRole::MapKey && k != nullptr &&
                    value_child.role == TypedExprChildRole::MapValue && v != nullptr) {
                    literal.entries.push_back(ir::MapEntryExpr{
                        .key = self.lower_typed_expr(*k),
                        .value = self.lower_typed_expr(*v),
                    });
                    i += 2;
                } else {
                    ++i;
                }
            }
            return self.make_expr(std::move(literal), range);
        }
        ir::ExprRef visit_unary(const TypedExpr &e) const {
            const TypedExpr *operand =
                TypedIrLowerer::resolve_child_by_role(self, e, TypedExprChildRole::Operand);
            return self.make_expr(
                ir::UnaryExpr{
                    .op = lower_expr_unary_op(e.unary_op),
                    .operand = operand ? self.lower_typed_expr(*operand) : nullptr,
                },
                range);
        }
        ir::ExprRef visit_binary(const TypedExpr &e) const {
            const TypedExpr *lhs =
                TypedIrLowerer::resolve_child_by_role(self, e, TypedExprChildRole::LeftOperand);
            const TypedExpr *rhs =
                TypedIrLowerer::resolve_child_by_role(self, e, TypedExprChildRole::RightOperand);
            return self.make_expr(
                ir::BinaryExpr{
                    .op = lower_expr_binary_op(e.binary_op),
                    .lhs = lhs ? self.lower_typed_expr(*lhs) : nullptr,
                    .rhs = rhs ? self.lower_typed_expr(*rhs) : nullptr,
                },
                range);
        }
        ir::ExprRef visit_member_access(const TypedExpr &e) const {
            const TypedExpr *base =
                TypedIrLowerer::resolve_child_by_role(self, e, TypedExprChildRole::Base);
            return self.make_expr(
                ir::MemberAccessExpr{
                    .base = base ? self.lower_typed_expr(*base) : nullptr,
                    .member = e.member_name,
                },
                range);
        }
        ir::ExprRef visit_index_access(const TypedExpr &e) const {
            const TypedExpr *base =
                TypedIrLowerer::resolve_child_by_role(self, e, TypedExprChildRole::Base);
            const TypedExpr *idx =
                TypedIrLowerer::resolve_child_by_role(self, e, TypedExprChildRole::Index);
            return self.make_expr(
                ir::IndexAccessExpr{
                    .base = base ? self.lower_typed_expr(*base) : nullptr,
                    .index = idx ? self.lower_typed_expr(*idx) : nullptr,
                },
                range);
        }
        ir::ExprRef visit_group(const TypedExpr &e) const {
            // Unwrap group — parentheses have no semantic meaning in IR
            const TypedExpr *inner =
                TypedIrLowerer::resolve_child_by_role(self, e, TypedExprChildRole::Grouped);
            if (inner != nullptr) {
                return self.lower_typed_expr(*inner);
            }
            return self.make_expr(ir::NoneLiteralExpr{}, range);
        }
        ir::ExprRef visit_unknown(const TypedExpr &e) const {
            (void)e;
            return nullptr;
        }
    };

    // Forward-declared so the visitor above can recurse into children.
    [[nodiscard]] ir::ExprRef lower_typed_expr(const TypedExpr &expr) const {
        auto result = typed_visit(expr, TypedExprPerKindLowerer{*this, expr.range});
        if (result != nullptr) {
            result->resolved_type =
                expr.type != nullptr ? type_ref_from_type(*expr.type) : ir::TypeRef{};
        }
        return result;
    }

    // Range-based lookup from declaration metadata into the typed expression store.
    [[nodiscard]] ir::ExprRef lower_expr_range(SourceRange range) const {
        const TypedExpr *typed = typed_program_->find_expr_by_range(range, current_source_id_);
        if (typed != nullptr)
            return lower_typed_expr(*typed);
        return nullptr;
    }

    // =====================================================================
    // Temporal / block range lowering (typed-store only)
    // =====================================================================

    [[nodiscard]] ir::TemporalExprPtr lower_temporal_range(SourceRange range) const {
        const TypedTemporalExpr *tte =
            typed_program_->find_temporal_by_range(range, current_source_id_);
        if (tte == nullptr) {
            return nullptr;
        }
        return lower_typed_temporal(*tte);
    }

    [[nodiscard]] ir::Block lower_block_range(SourceRange range) const {
        const TypedBlock *tb = typed_program_->find_block_by_range(range, current_source_id_);
        if (tb == nullptr) {
            return ir::Block{.statements = {}, .source_range = range};
        }
        return lower_typed_block(*tb);
    }

    // =====================================================================
    // Typed-store statement / block lowering (T1.6 + T1.7 P3)
    //
    // The dispatch chain below walks TypedProgram::statements /
    // TypedProgram::blocks (the flat typed-store) instead of the AST.
    // Expression payloads come from TypedStatement::children_expr_index via
    // lower_typed_expr; then/else blocks come from TypedStatement block
    // indexes. All primitive payloads (let type_ref, assign target path,
    // goto state, assert message) are read directly from the TypedStatement
    // mirror fields – no AST bridge is retained (removed in T1.7 P2).
    // =====================================================================

    // Map a TypedTemporalOp enum to the corresponding IR unary operator.
    // Only valid for TemporalNot / TemporalNext / TemporalAlways / TemporalEventually.
    [[nodiscard]] static ir::TemporalUnaryOp temporal_op_to_ir_unary(TypedTemporalOp op) {
        switch (op) {
        case TypedTemporalOp::TemporalNot:
            return ir::TemporalUnaryOp::Not;
        case TypedTemporalOp::TemporalNext:
            return ir::TemporalUnaryOp::Next;
        case TypedTemporalOp::TemporalAlways:
            return ir::TemporalUnaryOp::Always;
        case TypedTemporalOp::TemporalEventually:
            return ir::TemporalUnaryOp::Eventually;
        default:
            break;
        }
        throw std::logic_error("TypedTemporalOp is not a unary temporal operator");
    }

    // Map a TypedTemporalOp enum to the corresponding IR binary operator.
    // Only valid for TemporalAnd / TemporalOr / TemporalImply / TemporalUntil.
    [[nodiscard]] static ir::TemporalBinaryOp temporal_op_to_ir_binary(TypedTemporalOp op) {
        switch (op) {
        case TypedTemporalOp::TemporalAnd:
            return ir::TemporalBinaryOp::And;
        case TypedTemporalOp::TemporalOr:
            return ir::TemporalBinaryOp::Or;
        case TypedTemporalOp::TemporalImply:
            return ir::TemporalBinaryOp::Implies;
        case TypedTemporalOp::TemporalUntil:
            return ir::TemporalBinaryOp::Until;
        default:
            break;
        }
        throw std::logic_error("TypedTemporalOp is not a binary temporal operator");
    }

    // Map AssignTargetRootKind to the corresponding IR PathRootKind.
    [[nodiscard]] static ir::PathRootKind assign_root_to_ir(AssignTargetRootKind kind) {
        switch (kind) {
        case AssignTargetRootKind::Identifier:
            return ir::PathRootKind::Identifier;
        case AssignTargetRootKind::Input:
            return ir::PathRootKind::Input;
        case AssignTargetRootKind::Context:
            return ir::PathRootKind::Context;
        case AssignTargetRootKind::Output:
            return ir::PathRootKind::Output;
        case AssignTargetRootKind::State:
            return ir::PathRootKind::State;
        case AssignTargetRootKind::Local:
            return ir::PathRootKind::Local;
        }
        throw std::logic_error("AssignTargetRootKind carries an unsupported value");
    }

    // =====================================================================
    // Typed-tree temporal lowering (AST-free dispatch via TypedTemporalExpr)
    // =====================================================================
    //
    // lower_typed_temporal walks the flat TypedProgram::temporal_exprs store
    // by index and builds IR nodes. When a sub-index references the
    // expressions store (Atom leaf) we re-enter lower_typed_expr. The
    // payload_spelling carries only literal data for name/state temporal forms.
    [[nodiscard]] ir::TemporalExprPtr lower_typed_temporal(const TypedTemporalExpr &te) const {
        const auto make = [this, &te](auto node) {
            return make_temporal(std::move(node), te.range);
        };
        // Dispatch on the strongly-typed `op` enum; payload_spelling is only
        // the data field for name/state temporal literals.
        switch (te.op) {
        case TypedTemporalOp::Atom: {
            // Atom: child references the expressions flat store.
            ir::ExprRef embedded = nullptr;
            if (!te.children_index.empty() && te.children_index[0] != UINT32_MAX &&
                te.children_index[0] < typed_program_->expressions.size()) {
                embedded = lower_typed_expr(typed_program_->expressions[te.children_index[0]]);
            }
            return make(ir::EmbeddedTemporalExpr{.expr = std::move(embedded)});
        }
        case TypedTemporalOp::NameLiteralCalled: {
            // payload_spelling carries the canonical capability name directly
            // (no "called:" prefix in the new format).
            return make(ir::CalledTemporalExpr{.capability = te.payload_spelling});
        }
        case TypedTemporalOp::NameLiteralRunning: {
            return make(ir::RunningTemporalExpr{.node = te.payload_spelling});
        }
        case TypedTemporalOp::NameLiteralCompleted: {
            // payload_spelling encodes "node_name|optional_state_name".
            const auto pipe = te.payload_spelling.find('|');
            if (pipe == std::string::npos) {
                return make(ir::CompletedTemporalExpr{
                    .node = te.payload_spelling,
                    .state_name = std::nullopt,
                });
            }
            const std::string node = te.payload_spelling.substr(0, pipe);
            const std::string state = te.payload_spelling.substr(pipe + 1);
            return make(ir::CompletedTemporalExpr{
                .node = node,
                .state_name =
                    state.empty() ? std::nullopt : std::optional<std::string>(std::move(state)),
            });
        }
        case TypedTemporalOp::StateLiteral: {
            return make(ir::InStateTemporalExpr{.state = te.payload_spelling});
        }
        case TypedTemporalOp::TemporalNot:
        case TypedTemporalOp::TemporalNext:
        case TypedTemporalOp::TemporalAlways:
        case TypedTemporalOp::TemporalEventually: {
            ir::TemporalExprPtr operand = nullptr;
            if (!te.children_index.empty() && te.children_index[0] != UINT32_MAX &&
                te.children_index[0] < typed_program_->temporal_exprs.size()) {
                operand =
                    lower_typed_temporal(typed_program_->temporal_exprs[te.children_index[0]]);
            }
            return make(ir::TemporalUnaryExpr{
                .op = temporal_op_to_ir_unary(te.op),
                .operand = std::move(operand),
            });
        }
        case TypedTemporalOp::TemporalAnd:
        case TypedTemporalOp::TemporalOr:
        case TypedTemporalOp::TemporalImply:
        case TypedTemporalOp::TemporalUntil: {
            ir::TemporalExprPtr lhs = nullptr;
            ir::TemporalExprPtr rhs = nullptr;
            if (te.children_index.size() >= 2) {
                if (te.children_index[0] != UINT32_MAX &&
                    te.children_index[0] < typed_program_->temporal_exprs.size()) {
                    lhs =
                        lower_typed_temporal(typed_program_->temporal_exprs[te.children_index[0]]);
                }
                if (te.children_index[1] != UINT32_MAX &&
                    te.children_index[1] < typed_program_->temporal_exprs.size()) {
                    rhs =
                        lower_typed_temporal(typed_program_->temporal_exprs[te.children_index[1]]);
                }
            }
            return make(ir::TemporalBinaryExpr{
                .op = temporal_op_to_ir_binary(te.op),
                .lhs = std::move(lhs),
                .rhs = std::move(rhs),
            });
        }
        }

        throw std::logic_error("TypedTemporalExpr carries an unsupported op");
    }

    // Let type_ref construction is fully typed-store based. The source spelling
    // parser was removed intentionally: missing required type facts fail at the
    // IR lowering boundary.
    [[nodiscard]] ir::TypeRef inferred_typed_let_type_ref(const TypedStatement &stmt,
                                                          const TypedExpr *initializer) const {
        switch (stmt.let_type_ref_strategy) {
        case LetTypeRefStrategy::FromSyntax:
            if (stmt.let_type != nullptr) {
                return type_ref_from_type(*stmt.let_type);
            }
            return type_ref_from_required_type(nullptr, stmt.range, "let binding type");
        case LetTypeRefStrategy::NoAnnotation:
        case LetTypeRefStrategy::FromInitializerType:
            if (initializer != nullptr && initializer->type != nullptr) {
                return type_ref_from_type(*initializer->type);
            }
            return type_ref_from_required_type(stmt.let_type, stmt.range, "let binding type");
        case LetTypeRefStrategy::MissingType:
            return type_ref_from_required_type(nullptr, stmt.range, "let binding type");
        }
        throw std::logic_error("TypedStatement carries an unsupported let type strategy");
    }

    // T1.7 P2: assign target path construction – 100% from typed payloads.
    // Split stmt.target_name on '.' to recover root_name + member chain; use
    // stmt.assign_target_root_kind to produce the matching ir::PathRootKind.
    [[nodiscard]] ir::Path typed_assign_target_path(const TypedStatement &stmt) const {
        ir::PathRootKind root_kind = assign_root_to_ir(stmt.assign_target_root_kind);
        std::string root_name;
        std::vector<std::string> members;

        const std::string &spelling = stmt.target_name;
        std::size_t dot = spelling.find('.');
        if (dot == std::string::npos) {
            root_name = spelling;
        } else {
            root_name = spelling.substr(0, dot);
            std::string_view rest = std::string_view(spelling).substr(dot + 1);
            while (!rest.empty()) {
                const std::size_t nd = rest.find('.');
                if (nd == std::string_view::npos) {
                    members.emplace_back(rest);
                    break;
                }
                members.emplace_back(rest.substr(0, nd));
                rest = rest.substr(nd + 1);
            }
        }
        return ir::Path{
            .root_kind = root_kind,
            .root_name = std::move(root_name),
            .members = std::move(members),
        };
    }

    // Typed-store statement lowering visitor. Each method builds the Semantic IR
    // statement variant directly from the TypedStatement payload.
    struct TypedStmtPerKindLowerer {
        const TypedIrLowerer &self;
        SourceRange range;

        const TypedExpr *child_expr(std::size_t index) const {
            if (index >= self.current_statement_->children_expr_index.size())
                return nullptr;
            const auto expr_idx = self.current_statement_->children_expr_index[index];
            if (expr_idx == UINT32_MAX)
                return nullptr;
            if (expr_idx >= self.typed_program_->expressions.size())
                return nullptr;
            return &self.typed_program_->expressions[expr_idx];
        }

        ir::StatementPtr visit_let_stmt(const TypedStatement &stmt) const {
            const TypedExpr *initializer = child_expr(0);
            return self.make_statement(
                ir::LetStatement{
                    .name = stmt.target_name,
                    .type_ref = self.inferred_typed_let_type_ref(stmt, initializer),
                    .initializer = initializer ? self.lower_typed_expr(*initializer) : nullptr,
                },
                range);
        }

        ir::StatementPtr visit_assign_stmt(const TypedStatement &stmt) const {
            const TypedExpr *value = child_expr(0);
            return self.make_statement(
                ir::AssignStatement{
                    .target = self.typed_assign_target_path(stmt),
                    .value = value ? self.lower_typed_expr(*value) : nullptr,
                },
                range);
        }

        ir::StatementPtr visit_if_stmt(const TypedStatement &stmt) const {
            const TypedExpr *condition = child_expr(0);
            const auto *then_block =
                stmt.then_block_index != UINT32_MAX &&
                        stmt.then_block_index < self.typed_program_->blocks.size()
                    ? &self.typed_program_->blocks[stmt.then_block_index]
                    : nullptr;
            const auto *else_block =
                stmt.else_block_index != UINT32_MAX &&
                        stmt.else_block_index < self.typed_program_->blocks.size()
                    ? &self.typed_program_->blocks[stmt.else_block_index]
                    : nullptr;
            auto else_ptr =
                else_block ? make_owned<ir::Block>(self.lower_typed_block(*else_block)) : nullptr;
            return self.make_statement(
                ir::IfStatement{
                    .condition = condition ? self.lower_typed_expr(*condition) : nullptr,
                    .then_block = then_block
                                      ? make_owned<ir::Block>(self.lower_typed_block(*then_block))
                                      : nullptr,
                    .else_block = std::move(else_ptr),
                },
                range);
        }

        ir::StatementPtr visit_goto_stmt(const TypedStatement &stmt) const {
            // T1.7 P2: read directly from the typed payload. Empty malformed
            // payloads remain empty so BackendReady verification reports the
            // missing state instead of fabricating placeholder text.
            std::string target = stmt.goto_target_state;
            return self.make_statement(ir::GotoStatement{.target_state = std::move(target)}, range);
        }

        ir::StatementPtr visit_return_stmt([[maybe_unused]] const TypedStatement &stmt) const {
            const TypedExpr *value = child_expr(0);
            return self.make_statement(
                ir::ReturnStatement{
                    .value = value ? self.lower_typed_expr(*value) : nullptr,
                },
                range);
        }

        // T1.7 P2: assert message is stored on stmt.assert_message but the current
        // IR AssertStatement shape carries only a condition field. The typed payload
        // is used directly for the condition.
        ir::StatementPtr visit_assert_stmt([[maybe_unused]] const TypedStatement &stmt) const {
            const TypedExpr *condition = child_expr(0);
            return self.make_statement(
                ir::AssertStatement{
                    .condition = condition ? self.lower_typed_expr(*condition) : nullptr,
                },
                range);
        }

        ir::StatementPtr visit_expr_stmt([[maybe_unused]] const TypedStatement &stmt) const {
            const TypedExpr *expr = child_expr(0);
            return self.make_statement(
                ir::ExprStatement{
                    .expr = expr ? self.lower_typed_expr(*expr) : nullptr,
                },
                range);
        }

        ir::StatementPtr visit_unknown_stmt(const TypedStatement &stmt) const {
            return self.make_statement(
                ir::ExprStatement{
                    .expr = nullptr,
                },
                stmt.range);
        }
    };

    // Forward declarations.
    [[nodiscard]] ir::Block lower_typed_block(const TypedBlock &block) const;
    [[nodiscard]] ir::StatementPtr lower_typed_statement(const TypedStatement &stmt) const;

    // Current statement pointer, set for the lifetime of each
    // lower_typed_statement dispatch so the visitor can read
    // children_expr_index through the TypedIrLowerer (avoids passing the
    // pointer through every visit method).
    mutable const TypedStatement *current_statement_{nullptr};

    // =====================================================================
    // Flow / workflow summaries.
    // =====================================================================

    void collect_called_targets_from_expr(const ir::Expr &expr,
                                          std::vector<std::string> &called_targets) const {
        std::visit(Overloaded{
                       [](const ir::NoneLiteralExpr &) {},
                       [](const ir::BoolLiteralExpr &) {},
                       [](const ir::IntegerLiteralExpr &) {},
                       [](const ir::FloatLiteralExpr &) {},
                       [](const ir::DecimalLiteralExpr &) {},
                       [](const ir::StringLiteralExpr &) {},
                       [](const ir::DurationLiteralExpr &) {},
                       [this, &called_targets](const ir::SomeExpr &value) {
                           collect_called_targets_from_expr(*value.value, called_targets);
                       },
                       [](const ir::PathExpr &) {},
                       [](const ir::QualifiedValueExpr &) {},
                       [this, &called_targets](const ir::CallExpr &value) {
                           push_unique_value(called_targets, value.callee);
                           for (const auto &a : value.arguments)
                               collect_called_targets_from_expr(*a, called_targets);
                       },
                       [this, &called_targets](const ir::StructLiteralExpr &value) {
                           for (const auto &f : value.fields)
                               collect_called_targets_from_expr(*f.value, called_targets);
                       },
                       [this, &called_targets](const ir::ListLiteralExpr &value) {
                           for (const auto &i : value.items)
                               collect_called_targets_from_expr(*i, called_targets);
                       },
                       [this, &called_targets](const ir::SetLiteralExpr &value) {
                           for (const auto &i : value.items)
                               collect_called_targets_from_expr(*i, called_targets);
                       },
                       [this, &called_targets](const ir::MapLiteralExpr &value) {
                           for (const auto &e : value.entries) {
                               collect_called_targets_from_expr(*e.key, called_targets);
                               collect_called_targets_from_expr(*e.value, called_targets);
                           }
                       },
                       [this, &called_targets](const ir::UnaryExpr &value) {
                           collect_called_targets_from_expr(*value.operand, called_targets);
                       },
                       [this, &called_targets](const ir::BinaryExpr &value) {
                           collect_called_targets_from_expr(*value.lhs, called_targets);
                           collect_called_targets_from_expr(*value.rhs, called_targets);
                       },
                       [this, &called_targets](const ir::MemberAccessExpr &value) {
                           collect_called_targets_from_expr(*value.base, called_targets);
                       },
                       [this, &called_targets](const ir::IndexAccessExpr &value) {
                           collect_called_targets_from_expr(*value.base, called_targets);
                           collect_called_targets_from_expr(*value.index, called_targets);
                       },
                   },
                   expr.node);
    }

    void merge_flow_summary(ir::StateHandler::Summary &target,
                            const ir::StateHandler::Summary &other) const {
        for (const auto &gt : other.goto_targets)
            push_unique_value(target.goto_targets, gt);
        for (const auto &ap : other.assigned_paths)
            push_unique_path(target.assigned_paths, ap);
        for (const auto &ct : other.called_targets)
            push_unique_value(target.called_targets, ct);
        target.may_return = target.may_return || other.may_return;
        target.assert_count += other.assert_count;
    }

    // Recursive summary helpers. Kept as std::function-wrapped mutual recursion
    // because the summary walk has mutually recursive expression / statement
    // shapes.
    void collect_workflow_value_reads(const ir::Expr &expr,
                                      const std::vector<std::string> &node_names,
                                      std::vector<ir::WorkflowValueRead> &reads) const {
        std::visit(Overloaded{
                       [](const ir::NoneLiteralExpr &) {},
                       [](const ir::BoolLiteralExpr &) {},
                       [](const ir::IntegerLiteralExpr &) {},
                       [](const ir::FloatLiteralExpr &) {},
                       [](const ir::DecimalLiteralExpr &) {},
                       [](const ir::StringLiteralExpr &) {},
                       [](const ir::DurationLiteralExpr &) {},
                       [this, &node_names, &reads](const ir::SomeExpr &value) {
                           collect_workflow_value_reads(*value.value, node_names, reads);
                       },
                       [&](const ir::PathExpr &value) {
                           if (value.path.root_kind == ir::PathRootKind::Input) {
                               push_unique_workflow_value_read(
                                   reads,
                                   ir::WorkflowValueRead{
                                       .kind = ir::WorkflowValueSourceKind::WorkflowInput,
                                       .root_name = value.path.root_name,
                                       .members = value.path.members,
                                   });
                               return;
                           }
                           if (value.path.root_kind != ir::PathRootKind::Identifier)
                               return;
                           if (std::find(node_names.begin(),
                                         node_names.end(),
                                         value.path.root_name) == node_names.end())
                               return;
                           push_unique_workflow_value_read(
                               reads,
                               ir::WorkflowValueRead{
                                   .kind = ir::WorkflowValueSourceKind::WorkflowNodeOutput,
                                   .root_name = value.path.root_name,
                                   .members = value.path.members,
                               });
                       },
                       [](const ir::QualifiedValueExpr &) {},
                       [this, &node_names, &reads](const ir::CallExpr &value) {
                           for (const auto &a : value.arguments)
                               collect_workflow_value_reads(*a, node_names, reads);
                       },
                       [this, &node_names, &reads](const ir::StructLiteralExpr &value) {
                           for (const auto &f : value.fields)
                               collect_workflow_value_reads(*f.value, node_names, reads);
                       },
                       [this, &node_names, &reads](const ir::ListLiteralExpr &value) {
                           for (const auto &i : value.items)
                               collect_workflow_value_reads(*i, node_names, reads);
                       },
                       [this, &node_names, &reads](const ir::SetLiteralExpr &value) {
                           for (const auto &i : value.items)
                               collect_workflow_value_reads(*i, node_names, reads);
                       },
                       [this, &node_names, &reads](const ir::MapLiteralExpr &value) {
                           for (const auto &e : value.entries) {
                               collect_workflow_value_reads(*e.key, node_names, reads);
                               collect_workflow_value_reads(*e.value, node_names, reads);
                           }
                       },
                       [this, &node_names, &reads](const ir::UnaryExpr &value) {
                           collect_workflow_value_reads(*value.operand, node_names, reads);
                       },
                       [this, &node_names, &reads](const ir::BinaryExpr &value) {
                           collect_workflow_value_reads(*value.lhs, node_names, reads);
                           collect_workflow_value_reads(*value.rhs, node_names, reads);
                       },
                       [this, &node_names, &reads](const ir::MemberAccessExpr &value) {
                           collect_workflow_value_reads(*value.base, node_names, reads);
                       },
                       [this, &node_names, &reads](const ir::IndexAccessExpr &value) {
                           collect_workflow_value_reads(*value.base, node_names, reads);
                           collect_workflow_value_reads(*value.index, node_names, reads);
                       },
                   },
                   expr.node);
    }

    [[nodiscard]] ir::WorkflowExprSummary
    summarize_workflow_expr(const ir::Expr &expr,
                            const std::vector<std::string> &node_names) const {
        ir::WorkflowExprSummary summary;
        collect_workflow_value_reads(expr, node_names, summary.reads);
        return summary;
    }

    [[nodiscard]] ir::StateHandler::Summary summarize_block(const ir::Block &block) const;

    [[nodiscard]] ir::StateHandler::Summary
    summarize_statement(const ir::Statement &statement) const {
        return std::visit(
            Overloaded{
                [this](const ir::LetStatement &value) {
                    ir::StateHandler::Summary s;
                    collect_called_targets_from_expr(*value.initializer, s.called_targets);
                    return s;
                },
                [this](const ir::AssignStatement &value) {
                    ir::StateHandler::Summary s;
                    push_unique_path(s.assigned_paths, value.target);
                    collect_called_targets_from_expr(*value.value, s.called_targets);
                    return s;
                },
                [this](const ir::IfStatement &value) {
                    ir::StateHandler::Summary s;
                    collect_called_targets_from_expr(*value.condition, s.called_targets);
                    const auto then_summary = summarize_block(*value.then_block);
                    merge_flow_summary(s, then_summary);
                    ir::StateHandler::Summary else_summary;
                    if (value.else_block) {
                        else_summary = summarize_block(*value.else_block);
                        merge_flow_summary(s, else_summary);
                    }
                    s.may_fallthrough = !value.else_block || then_summary.may_fallthrough ||
                                        else_summary.may_fallthrough;
                    return s;
                },
                [](const ir::GotoStatement &value) {
                    ir::StateHandler::Summary s;
                    s.goto_targets.push_back(value.target_state);
                    s.may_fallthrough = false;
                    return s;
                },
                [this](const ir::ReturnStatement &value) {
                    ir::StateHandler::Summary s;
                    if (value.value)
                        collect_called_targets_from_expr(*value.value, s.called_targets);
                    s.may_return = true;
                    s.may_fallthrough = false;
                    return s;
                },
                [this](const ir::AssertStatement &value) {
                    ir::StateHandler::Summary s;
                    collect_called_targets_from_expr(*value.condition, s.called_targets);
                    s.assert_count = 1;
                    return s;
                },
                [this](const ir::ExprStatement &value) {
                    ir::StateHandler::Summary s;
                    collect_called_targets_from_expr(*value.expr, s.called_targets);
                    return s;
                },
            },
            statement.node);
    }

    // =====================================================================
    // Declaration lowering (Typed HIR is the canonical structural source)
    // =====================================================================

    template <typename Payload>
    [[nodiscard]] const Payload &require_payload(const TypedDecl &decl,
                                                 std::string_view label) const {
        const auto *payload = payload_as<Payload>(&decl);
        if (payload == nullptr) {
            throw std::logic_error("TypedDecl is missing required " + std::string(label) +
                                   " payload");
        }
        return *payload;
    }

    [[nodiscard]] ir::Decl lower_typed_declaration(const TypedDecl &declaration) const {
        switch (declaration.kind) {
        case ast::NodeKind::ModuleDecl:
            return lower_typed_module(declaration);
        case ast::NodeKind::ImportDecl:
            return lower_typed_import(declaration);
        case ast::NodeKind::ConstDecl:
            return lower_typed_const(declaration);
        case ast::NodeKind::TypeAliasDecl:
            return lower_typed_type_alias(declaration);
        case ast::NodeKind::StructDecl:
            return lower_typed_struct(declaration);
        case ast::NodeKind::EnumDecl:
            return lower_typed_enum(declaration);
        case ast::NodeKind::CapabilityDecl:
            return lower_typed_capability(declaration);
        case ast::NodeKind::PredicateDecl:
            return lower_typed_predicate(declaration);
        case ast::NodeKind::AgentDecl:
            return lower_typed_agent(declaration);
        case ast::NodeKind::ContractDecl:
            return lower_typed_contract(declaration);
        case ast::NodeKind::FlowDecl:
            return lower_typed_flow(declaration);
        case ast::NodeKind::WorkflowDecl:
            return lower_typed_workflow(declaration);
        case ast::NodeKind::FnDecl:
            // P2c (RFC §3.2.2 / §6): fn-declaration lowering from the typed
            // HIR signature. P2b indexes a FnDecl into the typed program with
            // a FnTypeInfo payload (resolved params, return type, generic
            // type-parameter names, effect clause); this lowers that payload
            // to an ir::FnDecl.
            return lower_typed_fn(declaration);
        case ast::NodeKind::TraitDecl:
        case ast::NodeKind::ImplDecl:
            // P3 (RFC §3.2.2 / type-system §1.3 / §1.4): trait/impl lowering
            // lands in P3b. P3a does not index trait/impl declarations into
            // the typed program, so this branch is unreachable today; it
            // exists only to keep the NodeKind switch exhaustive.
            break;
        case ast::NodeKind::Program:
            break;
        }
        throw std::logic_error("TypedProgram contains unsupported declaration kind");
    }

    [[nodiscard]] ir::SymbolRef symbol_ref_from_decl(const TypedDecl &decl,
                                                     std::string_view label) const {
        return symbol_ref_from_symbol(typed_program_->find_symbol(decl.symbol), label);
    }

    [[nodiscard]] ir::ModuleDecl lower_typed_module(const TypedDecl &decl) const {
        const auto &info = require_payload<ModuleDeclInfo>(decl, "module");
        const auto name = info.name;
        current_module_name_ = name;
        return with_provenance(ir::ModuleDecl{.provenance = {}, .name = name},
                               info.declaration_range);
    }

    [[nodiscard]] ir::ImportDecl lower_typed_import(const TypedDecl &decl) const {
        const auto &info = require_payload<ImportDeclInfo>(decl, "import");
        return with_provenance(
            ir::ImportDecl{
                .provenance = {},
                .path = info.target_module,
                .alias = !info.alias.empty() ? std::make_optional(info.alias) : std::nullopt,
            },
            info.declaration_range);
    }

    [[nodiscard]] ir::ConstDecl lower_typed_const(const TypedDecl &decl) const {
        const auto &info = require_payload<ConstDeclInfo>(decl, "const");
        const auto name = info.canonical_name;
        return with_provenance(
            ir::ConstDecl{
                .provenance = {},
                .name = name,
                .value = lower_expr_range(info.value_range),
                .type_ref = type_ref_from_required_type(info.type, info.type_range, "const type"),
                .symbol_ref = symbol_ref_from_decl(decl, "const declaration"),
            },
            info.declaration_range);
    }

    [[nodiscard]] ir::TypeAliasDecl lower_typed_type_alias(const TypedDecl &decl) const {
        const auto &info = require_payload<TypeAliasDeclInfo>(decl, "type alias");
        const auto name = info.canonical_name;
        return with_provenance(
            ir::TypeAliasDecl{
                .provenance = {},
                .name = name,
                .aliased_type_ref = type_ref_from_required_type(
                    info.aliased_type, info.aliased_type_range, "type alias target"),
                .symbol_ref = symbol_ref_from_decl(decl, "type alias declaration"),
            },
            info.declaration_range);
    }

    [[nodiscard]] ir::StructDecl lower_typed_struct(const TypedDecl &decl) const {
        const auto &info = require_payload<StructTypeInfo>(decl, "struct");
        const auto name = info.canonical_name;
        ir::StructDecl lowered = with_provenance(
            ir::StructDecl{
                .provenance = {},
                .name = name,
                .fields = {},
                .symbol_ref = symbol_ref_from_decl(decl, "struct declaration"),
            },
            info.declaration_range);
        lowered.fields.reserve(info.fields.size());
        for (const auto &field : info.fields) {
            lowered.fields.push_back(ir::FieldDecl{
                .name = field.name,
                .default_value =
                    field.has_default ? lower_expr_range(field.default_value_range) : nullptr,
                .type_ref =
                    type_ref_from_required_type(field.type, field.declaration_range, "field type"),
                .source_range = field.declaration_range,
            });
        }
        return lowered;
    }

    [[nodiscard]] ir::EnumDecl lower_typed_enum(const TypedDecl &decl) const {
        const auto &info = require_payload<EnumTypeInfo>(decl, "enum");
        const auto name = info.canonical_name;
        ir::EnumDecl lowered = with_provenance(
            ir::EnumDecl{
                .provenance = {},
                .name = name,
                .variants = {},
                .symbol_ref = symbol_ref_from_decl(decl, "enum declaration"),
            },
            info.declaration_range);
        lowered.variants.reserve(info.variants.size());
        for (const auto &variant : info.variants) {
            lowered.variants.push_back(variant.name);
        }
        return lowered;
    }

    [[nodiscard]] std::vector<ir::ParamDecl>
    lower_params(const std::vector<ParamTypeInfo> &params) const {
        std::vector<ir::ParamDecl> result;
        result.reserve(params.size());
        for (const auto &param : params) {
            result.push_back(ir::ParamDecl{
                .name = param.name,
                .type_ref = type_ref_from_required_type(
                    param.type, param.declaration_range, "parameter type"),
                .source_range = param.declaration_range,
            });
        }
        return result;
    }

    [[nodiscard]] ir::CapabilityDecl lower_typed_capability(const TypedDecl &decl) const {
        const auto &info = require_payload<CapabilityTypeInfo>(decl, "capability");
        const auto name = info.canonical_name;
        return with_provenance(
            ir::CapabilityDecl{
                .provenance = {},
                .name = name,
                .params = lower_params(info.params),
                .return_type_ref = type_ref_from_required_type(
                    info.return_type, info.declaration_range, "capability return type"),
                .effect = lower_capability_effect_from_info(info.effect),
                .symbol_ref = symbol_ref_from_decl(decl, "capability declaration"),
            },
            info.declaration_range);
    }

    [[nodiscard]] ir::PredicateDecl lower_typed_predicate(const TypedDecl &decl) const {
        const auto &info = require_payload<PredicateTypeInfo>(decl, "predicate");
        const auto name = info.canonical_name;
        return with_provenance(
            ir::PredicateDecl{
                .provenance = {},
                .name = name,
                .params = lower_params(info.params),
                .symbol_ref = symbol_ref_from_decl(decl, "predicate declaration"),
            },
            info.declaration_range);
    }

    [[nodiscard]] ir::AgentDecl lower_typed_agent(const TypedDecl &decl) const {
        const auto &info = require_payload<AgentTypeInfo>(decl, "agent");
        const auto name = info.canonical_name;
        ir::AgentDecl lowered = with_provenance(
            ir::AgentDecl{
                .provenance = {},
                .name = name,
                .states = info.states,
                .initial_state = info.initial_state,
                .final_states = info.final_states,
                .quota = {},
                .transitions = {},
                .input_type_ref = type_ref_from_required_type(
                    info.input_type, info.declaration_range, "agent input type"),
                .context_type_ref = type_ref_from_required_type(
                    info.context_type, info.declaration_range, "agent context type"),
                .output_type_ref = type_ref_from_required_type(
                    info.output_type, info.declaration_range, "agent output type"),
                .capability_refs = {},
                .symbol_ref = symbol_ref_from_decl(decl, "agent declaration"),
            },
            info.declaration_range);
        lowered.capability_refs.reserve(info.capability_symbols.size());
        for (const auto capability_symbol : info.capability_symbols) {
            const auto symbol = typed_program_->find_symbol(capability_symbol);
            lowered.capability_refs.push_back(
                symbol_ref_from_symbol(symbol, "agent capability reference"));
        }
        lowered.quota.reserve(info.quota.size());
        for (const auto &quota : info.quota) {
            lowered.quota.push_back(ir::QuotaItem{.name = quota.name, .value = quota.value});
        }
        lowered.transitions.reserve(info.transitions.size());
        for (const auto &transition : info.transitions) {
            lowered.transitions.push_back(ir::TransitionDecl{
                .from_state = transition.from_state,
                .to_state = transition.to_state,
            });
        }
        return lowered;
    }

    [[nodiscard]] ir::ContractDecl lower_typed_contract(const TypedDecl &decl) const {
        const auto &info = require_payload<ContractTypeInfo>(decl, "contract");
        const auto target_symbol = typed_program_->find_symbol(info.target_symbol);
        const auto target =
            target_symbol.has_value() ? target_symbol->get().canonical_name : info.target_name;
        ir::ContractDecl lowered = with_provenance(
            ir::ContractDecl{
                .provenance = {},
                .clauses = {},
                .target_ref = symbol_ref_from_symbol(target_symbol, "contract target"),
            },
            info.declaration_range);
        lowered.clauses.reserve(info.clauses.size());
        for (const auto &clause : info.clauses) {
            lowered.clauses.push_back(ir::ContractClause{
                .kind = lower_contract_clause_kind(
                    static_cast<ast::ContractClauseKind>(clause.clause_kind)),
                .value = clause.is_temporal
                             ? std::variant<ir::ExprRef, ir::TemporalExprPtr>{lower_temporal_range(
                                   clause.expr_range)}
                             : std::variant<ir::ExprRef, ir::TemporalExprPtr>{lower_expr_range(
                                   clause.expr_range)},
                .source_range = clause.source_range,
            });
        }
        return lowered;
    }

    [[nodiscard]] ir::FlowDecl lower_typed_flow(const TypedDecl &decl) const {
        const auto &info = require_payload<FlowTypeInfo>(decl, "flow");
        const auto target_symbol = typed_program_->find_symbol(info.target_symbol);
        const auto target =
            target_symbol.has_value() ? target_symbol->get().canonical_name : info.target_name;
        ir::FlowDecl lowered = with_provenance(
            ir::FlowDecl{
                .provenance = {},
                .state_handlers = {},
                .target_ref = symbol_ref_from_symbol(target_symbol, "flow target"),
            },
            info.declaration_range);
        lowered.state_handlers.reserve(info.state_handlers.size());
        for (const auto &handler : info.state_handlers) {
            ir::StateHandler state_handler{
                .state_name = handler.state_name,
                .policy = {},
                .body = lower_block_range(handler.body_range),
                .source_range = handler.source_range,
            };
            state_handler.policy.reserve(handler.policy.size());
            for (const auto &policy_item : handler.policy) {
                switch (policy_item.kind) {
                case StatePolicyKind::Retry:
                    state_handler.policy.push_back(ir::RetryPolicy{.limit = policy_item.value});
                    break;
                case StatePolicyKind::RetryOn:
                    state_handler.policy.push_back(
                        ir::RetryOnPolicy{.targets = policy_item.retry_on_targets});
                    break;
                case StatePolicyKind::Timeout:
                    state_handler.policy.push_back(
                        ir::TimeoutPolicy{.duration = policy_item.value});
                    break;
                }
            }
            lowered.state_handlers.push_back(std::move(state_handler));
        }
        return lowered;
    }

    [[nodiscard]] ir::WorkflowDecl lower_typed_workflow(const TypedDecl &decl) const {
        const auto &info = require_payload<WorkflowTypeInfo>(decl, "workflow");
        const auto name = info.canonical_name;
        ir::WorkflowDecl lowered = with_provenance(
            ir::WorkflowDecl{
                .provenance = {},
                .name = name,
                .nodes = {},
                .safety = {},
                .liveness = {},
                .return_value = lower_expr_range(info.return_value_range),
                .input_type_ref = type_ref_from_required_type(
                    info.input_type, info.declaration_range, "workflow input type"),
                .output_type_ref = type_ref_from_required_type(
                    info.output_type, info.declaration_range, "workflow output type"),
                .symbol_ref = symbol_ref_from_decl(decl, "workflow declaration"),
            },
            info.declaration_range);
        lowered.nodes.reserve(info.nodes.size());
        for (const auto &node : info.nodes) {
            const auto node_target = typed_program_->find_symbol(node.target_symbol);
            const auto target_name =
                node_target.has_value() ? node_target->get().canonical_name : node.target_name;
            lowered.nodes.push_back(ir::WorkflowNode{
                .name = node.name,
                .input = lower_expr_range(node.input_expr_range),
                .after = node.after,
                .target_ref = symbol_ref_from_symbol(node_target, "workflow node target"),
                .source_range = node.source_range,
            });
        }
        lowered.safety.reserve(info.safety_ranges.size());
        for (const auto range : info.safety_ranges) {
            lowered.safety.push_back(lower_temporal_range(range));
        }
        lowered.liveness.reserve(info.liveness_ranges.size());
        for (const auto range : info.liveness_ranges) {
            lowered.liveness.push_back(lower_temporal_range(range));
        }
        return lowered;
    }

    // P2c (RFC §3.2.2 / §1.2 / §2): lower a top-level `fn` declaration from
    // its FnTypeInfo payload. The IR keeps the resolved signature surface
    // (params, optional return type, generic type-parameter names, effect
    // clause). The fn body is not lowered here — the existing typed-block →
    // IR body machinery consumes a TypedBlock, and P2b type-checks the fn
    // body structurally without indexing it as a TypedBlock, so there is no
    // block to lower yet. `has_body` is preserved so downstream tooling can
    // distinguish a prototype (`fn name(...);`) from a definition.
    [[nodiscard]] ir::FnDecl lower_typed_fn(const TypedDecl &decl) const {
        const auto &info = require_payload<FnTypeInfo>(decl, "fn");

        ir::FnEffectClause effect;
        switch (static_cast<ast::EffectClauseKind>(info.effect.kind)) {
        case ast::EffectClauseKind::Pure:
            effect.kind = ir::FnEffectKind::Pure;
            break;
        case ast::EffectClauseKind::Nondet:
            effect.kind = ir::FnEffectKind::Nondet;
            break;
        case ast::EffectClauseKind::Capability:
            effect.kind = ir::FnEffectKind::Capability;
            break;
        }
        effect.source_range = info.effect.source_range;
        effect.capabilities.reserve(info.effect.capabilities.size());
        for (const auto capability_symbol : info.effect.capabilities) {
            effect.capabilities.push_back(
                symbol_ref_from_symbol(typed_program_->find_symbol(capability_symbol),
                                       "fn effect capability"));
        }

        ir::FnDecl lowered = with_provenance(
            ir::FnDecl{
                .provenance = {},
                .name = info.canonical_name,
                .params = lower_params(info.params),
                .return_type_ref = {},
                .has_return_type = info.return_type != nullptr,
                .effect = std::move(effect),
                .type_param_names = info.type_param_names,
                .has_body = info.has_body,
                .symbol_ref = symbol_ref_from_decl(decl, "fn declaration"),
            },
            info.declaration_range);

        if (info.return_type != nullptr) {
            lowered.return_type_ref = type_ref_from_required_type(
                info.return_type, info.return_type_range, "fn return type");
        }
        return lowered;
    }
};

// Out-of-line for the mutual recursion.
inline ir::Block TypedIrLowerer::lower_typed_block(const TypedBlock &block) const {
    ir::Block ir_block{.statements = {}, .source_range = block.range};
    ir_block.statements.reserve(block.statement_indexes.size());
    for (const auto &stmt_idx : block.statement_indexes) {
        if (stmt_idx == UINT32_MAX)
            continue;
        if (stmt_idx >= typed_program_->statements.size())
            continue;
        ir_block.statements.push_back(lower_typed_statement(typed_program_->statements[stmt_idx]));
    }
    return ir_block;
}

inline ir::StatementPtr TypedIrLowerer::lower_typed_statement(const TypedStatement &stmt) const {
    // Temporarily bind the current statement so the visitor can reach
    // children_expr_index through the class pointer. Resets the pointer on
    // exit so re-entrant calls (nested if-else blocks) do not leak state.
    const TypedStatement *prev = current_statement_;
    current_statement_ = &stmt;
    struct Restore {
        const TypedStatement **slot;
        const TypedStatement *prev;
        ~Restore() {
            *slot = prev;
        }
    } restore{&current_statement_, prev};
    return typed_visit(stmt, TypedStmtPerKindLowerer{*this, stmt.range});
}

// Out-of-line for the mutual recursion.
inline ir::StateHandler::Summary TypedIrLowerer::summarize_block(const ir::Block &block) const {
    ir::StateHandler::Summary summary;
    for (const auto &statement : block.statements) {
        if (!summary.may_fallthrough)
            break;
        const auto stmt_summary = summarize_statement(*statement);
        merge_flow_summary(summary, stmt_summary);
        summary.may_fallthrough = stmt_summary.may_fallthrough;
    }
    return summary;
}
} // namespace

// =====================================================================
// Public entry points. Routes to TypedIrLowerer against the caller's
// TypedProgram, so post-typecheck edits to program.expressions are
// honoured (the key behavioural guarantee of the P3 boundary).
// =====================================================================

namespace {

ir::Program lower_typed_program_impl(const TypedProgram &program) {
    auto program_ir = TypedIrLowerer(program).lower();
    ir::recompute_derived_analyses(program_ir, ir::ProgramPhase::Analyzed);
    return program_ir;
}

} // anonymous namespace

ir::Program lower_typed_program(const TypedProgram &program, const ast::Program &ast_program) {
    (void)ast_program;
    return lower_typed_program_impl(program);
}

ir::Program lower_typed_program(const TypedProgram &program, const SourceGraph &source_graph) {
    (void)source_graph;
    return lower_typed_program_impl(program);
}

ir::Program lower_typed_program(const TypedProgram &program) {
    return lower_typed_program_impl(program);
}

// NOTE: collect_formal_observations lives in ir_lower.cpp to avoid ODR
// violations. This TU (typed_hir_lower.cpp) only lowers; both TUs are linked
// into the same binaries in this project so no separate copy is required.

} // namespace ahfl
