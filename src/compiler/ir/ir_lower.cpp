#include "ahfl/compiler/ir/ir.hpp"

#include "ahfl/compiler/frontend/frontend.hpp"
#include "base/support/overloaded.hpp"
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

[[nodiscard]] bool paths_equal(const ir::Path &lhs, const ir::Path &rhs) {
    return lhs.root_kind == rhs.root_kind && lhs.root_name == rhs.root_name &&
           lhs.members == rhs.members;
}

[[nodiscard]] bool workflow_value_reads_equal(const ir::WorkflowValueRead &lhs,
                                              const ir::WorkflowValueRead &rhs) {
    return lhs.kind == rhs.kind && lhs.root_name == rhs.root_name && lhs.members == rhs.members;
}

[[nodiscard]] std::string logical_source_path(std::string_view module_name) {
    if (module_name.empty()) {
        return {};
    }

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

[[nodiscard]] std::string called_observation_symbol(std::string_view agent_name,
                                                    std::string_view capability_name) {
    return "agent__" + sanitize_identifier(agent_name) + "__called__" +
           sanitize_identifier(capability_name);
}

[[nodiscard]] std::string observation_scope_base_name(const ir::FormalObservationScope &scope) {
    switch (scope.kind) {
    case ir::FormalObservationScopeKind::ContractClause:
        return "contract__" + scope.owner + "__" + std::to_string(scope.clause_index);
    case ir::FormalObservationScopeKind::WorkflowSafetyClause:
        return "workflow__" + scope.owner + "__safety__" + std::to_string(scope.clause_index);
    case ir::FormalObservationScopeKind::WorkflowLivenessClause:
        return "workflow__" + scope.owner + "__liveness__" + std::to_string(scope.clause_index);
    }

    return "observation";
}

[[nodiscard]] std::string embedded_observation_symbol(const ir::FormalObservationScope &scope) {
    return sanitize_identifier(observation_scope_base_name(scope) + "__atom__" +
                               std::to_string(scope.atom_index));
}

[[nodiscard]] ir::PathRootKind lower_path_root_kind(ast::PathRootKind kind) {
    switch (kind) {
    case ast::PathRootKind::Identifier:
        return ir::PathRootKind::Identifier;
    case ast::PathRootKind::Input:
        return ir::PathRootKind::Input;
    case ast::PathRootKind::Output:
        return ir::PathRootKind::Output;
    }

    return ir::PathRootKind::Identifier;
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

[[nodiscard]] ir::TemporalUnaryOp lower_temporal_unary_op(ast::TemporalUnaryOp op) {
    switch (op) {
    case ast::TemporalUnaryOp::Always:
        return ir::TemporalUnaryOp::Always;
    case ast::TemporalUnaryOp::Eventually:
        return ir::TemporalUnaryOp::Eventually;
    case ast::TemporalUnaryOp::Next:
        return ir::TemporalUnaryOp::Next;
    case ast::TemporalUnaryOp::Not:
        return ir::TemporalUnaryOp::Not;
    }

    return ir::TemporalUnaryOp::Always;
}

[[nodiscard]] ir::TemporalBinaryOp lower_temporal_binary_op(ast::TemporalBinaryOp op) {
    switch (op) {
    case ast::TemporalBinaryOp::Implies:
        return ir::TemporalBinaryOp::Implies;
    case ast::TemporalBinaryOp::Or:
        return ir::TemporalBinaryOp::Or;
    case ast::TemporalBinaryOp::And:
        return ir::TemporalBinaryOp::And;
    case ast::TemporalBinaryOp::Until:
        return ir::TemporalBinaryOp::Until;
    }

    return ir::TemporalBinaryOp::Implies;
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
    }

    return ir::SymbolRefKind::Unknown;
}

[[nodiscard]] ir::TypeRefKind lower_type_ref_kind(TypeKind kind) {
    switch (kind) {
    case TypeKind::Any:
        return ir::TypeRefKind::Any;
    case TypeKind::Never:
        return ir::TypeRefKind::Never;
    case TypeKind::Unit:
        return ir::TypeRefKind::Unit;
    case TypeKind::Bool:
        return ir::TypeRefKind::Bool;
    case TypeKind::Int:
        return ir::TypeRefKind::Int;
    case TypeKind::Float:
        return ir::TypeRefKind::Float;
    case TypeKind::String:
        return ir::TypeRefKind::String;
    case TypeKind::BoundedString:
        return ir::TypeRefKind::BoundedString;
    case TypeKind::UUID:
        return ir::TypeRefKind::UUID;
    case TypeKind::Timestamp:
        return ir::TypeRefKind::Timestamp;
    case TypeKind::Duration:
        return ir::TypeRefKind::Duration;
    case TypeKind::Decimal:
        return ir::TypeRefKind::Decimal;
    case TypeKind::Struct:
        return ir::TypeRefKind::Struct;
    case TypeKind::Enum:
        return ir::TypeRefKind::Enum;
    case TypeKind::Optional:
        return ir::TypeRefKind::Optional;
    case TypeKind::List:
        return ir::TypeRefKind::List;
    case TypeKind::Set:
        return ir::TypeRefKind::Set;
    case TypeKind::Map:
        return ir::TypeRefKind::Map;
    }

    return ir::TypeRefKind::Unresolved;
}

template <typename T> void push_unique_value(std::vector<T> &values, const T &value) {
    for (const auto &existing : values) {
        if (existing == value) {
            return;
        }
    }

    values.push_back(value);
}

void push_unique_path(std::vector<ir::Path> &values, const ir::Path &value) {
    for (const auto &existing : values) {
        if (paths_equal(existing, value)) {
            return;
        }
    }

    values.push_back(value);
}

void push_unique_workflow_value_read(std::vector<ir::WorkflowValueRead> &values,
                                     const ir::WorkflowValueRead &value) {
    for (const auto &existing : values) {
        if (workflow_value_reads_equal(existing, value)) {
            return;
        }
    }

    values.push_back(value);
}

[[nodiscard]] std::string quota_item_name(ast::AgentQuotaItemKind kind) {
    switch (kind) {
    case ast::AgentQuotaItemKind::MaxToolCalls:
        return "max_tool_calls";
    case ast::AgentQuotaItemKind::MaxExecutionTime:
        return "max_execution_time";
    }

    return "<invalid-quota-item>";
}

class IrLowerer final {
  public:
    IrLowerer(const ast::Program &program,
              const ResolveResult &resolve_result,
              const TypeCheckResult &type_check_result)
        : program_(&program), resolve_result_(resolve_result),
          type_check_result_(type_check_result) {}
    IrLowerer(const SourceGraph &graph,
              const ResolveResult &resolve_result,
              const TypeCheckResult &type_check_result)
        : graph_(&graph), resolve_result_(resolve_result), type_check_result_(type_check_result) {}

    [[nodiscard]] ir::Program lower() const {
        ir::Program program_ir;
        if (graph_ != nullptr) {
            std::size_t declaration_count = 0;
            for (const auto &source : graph_->sources) {
                declaration_count += source.program ? source.program->declarations.size() : 0;
            }
            program_ir.declarations.reserve(declaration_count);

            for (const auto &source : graph_->sources) {
                if (!source.program) {
                    continue;
                }

                enter_source(source);
                for (const auto &declaration : source.program->declarations) {
                    program_ir.declarations.push_back(lower_declaration(*declaration));
                }
                leave_source();
            }

            return program_ir;
        }

        if (program_ != nullptr) {
            program_ir.declarations.reserve(program_->declarations.size());
            for (const auto &declaration : program_->declarations) {
                program_ir.declarations.push_back(lower_declaration(*declaration));
            }
        }

        return program_ir;
    }

  private:
    const ast::Program *program_{nullptr};
    const SourceGraph *graph_{nullptr};
    const ResolveResult &resolve_result_;
    const TypeCheckResult &type_check_result_;
    mutable const SourceUnit *current_source_{nullptr};
    mutable std::optional<SourceId> current_source_id_;
    mutable std::string current_module_name_;

    template <typename T>
    [[nodiscard]] ir::ExprPtr
    make_expr(T node, std::optional<SourceRange> source_range = std::nullopt) const {
        auto expr = make_owned<ir::Expr>();
        expr->node = std::move(node);
        expr->source_range = source_range;
        return expr;
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
        return statement;
    }

    [[nodiscard]] const SymbolTable &symbols() const noexcept {
        return resolve_result_.symbol_table;
    }

    [[nodiscard]] const TypeEnvironment &environment() const noexcept {
        return type_check_result_.environment;
    }

    void enter_source(const SourceUnit &source) const {
        current_source_ = &source;
        current_source_id_ = source.id;
        current_module_name_ = source.module_name;
    }

    void leave_source() const {
        current_source_ = nullptr;
        current_source_id_.reset();
        current_module_name_.clear();
    }

    [[nodiscard]] ir::DeclarationProvenance
    current_provenance(std::optional<SourceRange> source_range = std::nullopt) const {
        if (graph_ == nullptr || current_module_name_.empty()) {
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
        return declaration;
    }

    [[nodiscard]] MaybeCRef<Symbol> find_local_symbol_here(SymbolNamespace name_space,
                                                           std::string_view name) const {
        if (!current_module_name_.empty()) {
            return symbols().find_local(name_space, name, current_module_name_);
        }

        return symbols().find_local(name_space, name);
    }

    [[nodiscard]] MaybeCRef<Symbol> symbol_from_reference_here(ReferenceKind kind,
                                                               SourceRange range) const {
        const auto reference = resolve_result_.find_reference(kind, range, current_source_id_);
        if (!reference.has_value()) {
            return std::nullopt;
        }

        return symbols().get(reference->get().target);
    }

    [[nodiscard]] std::string canonical_name_from_reference_here(ReferenceKind kind,
                                                                 SourceRange range,
                                                                 std::string_view fallback) const {
        const auto symbol = symbol_from_reference_here(kind, range);
        if (!symbol.has_value()) {
            return std::string(fallback);
        }

        return symbol->get().canonical_name;
    }

    [[nodiscard]] std::string canonical_local_name_here(SymbolNamespace name_space,
                                                        std::string_view local_name) const {
        const auto symbol = find_local_symbol_here(name_space, local_name);
        if (!symbol.has_value()) {
            return std::string(local_name);
        }

        return symbol->get().canonical_name;
    }

    [[nodiscard]] std::string describe_type(MaybeCRef<Type> type) const {
        if (!type.has_value()) {
            return "Any";
        }

        return type->get().describe();
    }

    [[nodiscard]] ir::TypeRefPtr make_type_ref(ir::TypeRef ref) const {
        return make_owned<ir::TypeRef>(std::move(ref));
    }

    [[nodiscard]] ir::SymbolRef symbol_ref_from_symbol(MaybeCRef<Symbol> symbol,
                                                       ir::SymbolRefKind fallback_kind,
                                                       std::string_view fallback) const {
        if (!symbol.has_value()) {
            return ir::SymbolRef{
                .kind = fallback_kind,
                .canonical_name = std::string(fallback),
                .local_name = std::string(fallback),
                .module_name = {},
            };
        }

        const auto &value = symbol->get();
        return ir::SymbolRef{
            .kind = lower_symbol_ref_kind(value.kind),
            .canonical_name = value.canonical_name,
            .local_name = value.local_name,
            .module_name = value.module_name,
        };
    }

    [[nodiscard]] ir::SymbolRef symbol_ref_from_reference_here(ReferenceKind kind,
                                                               SourceRange range,
                                                               ir::SymbolRefKind fallback_kind,
                                                               std::string_view fallback) const {
        return symbol_ref_from_symbol(
            symbol_from_reference_here(kind, range), fallback_kind, fallback);
    }

    [[nodiscard]] ir::TypeRef type_ref_from_type(const Type &type) const {
        ir::TypeRef ref{
            .kind = lower_type_ref_kind(type.kind),
            .display_name = type.describe(),
            .canonical_name = {},
            .string_bounds = type.string_bounds,
            .decimal_scale = type.decimal_scale,
            .first = nullptr,
            .second = nullptr,
        };

        if (type.kind == TypeKind::Struct || type.kind == TypeKind::Enum) {
            ref.canonical_name = type.name;
        }
        if (type.first) {
            ref.first = make_type_ref(type_ref_from_type(*type.first));
        }
        if (type.second) {
            ref.second = make_type_ref(type_ref_from_type(*type.second));
        }

        return ref;
    }

    [[nodiscard]] ir::TypeRef type_ref_from_maybe(MaybeCRef<Type> type,
                                                  std::string_view fallback = "Any") const {
        if (!type.has_value()) {
            return ir::TypeRef{
                .kind = ir::TypeRefKind::Unresolved,
                .display_name = std::string(fallback),
            };
        }

        return type_ref_from_type(type->get());
    }

    [[nodiscard]] ir::TypeRef named_type_ref_from_syntax(const ast::TypeSyntax &type) const {
        const auto fallback = type.name ? type.name->spelling() : std::string{"<missing-type>"};
        MaybeCRef<Symbol> symbol;
        if (type.name) {
            symbol = symbol_from_reference_here(ReferenceKind::TypeName, type.name->range);
        }
        auto ref = ir::TypeRef{
            .kind = ir::TypeRefKind::Unresolved,
            .display_name = std::string(fallback),
            .canonical_name = {},
        };

        if (!symbol.has_value()) {
            return ref;
        }

        const auto &value = symbol->get();
        ref.display_name = value.canonical_name;
        ref.canonical_name = value.canonical_name;
        if (value.kind == SymbolKind::Struct) {
            ref.kind = ir::TypeRefKind::Struct;
        } else if (value.kind == SymbolKind::Enum) {
            ref.kind = ir::TypeRefKind::Enum;
        }

        return ref;
    }

    [[nodiscard]] ir::TypeRef type_ref_from_syntax(const ast::TypeSyntax &type) const {
        switch (type.kind) {
        case ast::TypeSyntaxKind::Unit:
            return ir::TypeRef{.kind = ir::TypeRefKind::Unit, .display_name = type.spelling()};
        case ast::TypeSyntaxKind::Bool:
            return ir::TypeRef{.kind = ir::TypeRefKind::Bool, .display_name = type.spelling()};
        case ast::TypeSyntaxKind::Int:
            return ir::TypeRef{.kind = ir::TypeRefKind::Int, .display_name = type.spelling()};
        case ast::TypeSyntaxKind::Float:
            return ir::TypeRef{.kind = ir::TypeRefKind::Float, .display_name = type.spelling()};
        case ast::TypeSyntaxKind::String:
            return ir::TypeRef{.kind = ir::TypeRefKind::String, .display_name = type.spelling()};
        case ast::TypeSyntaxKind::UUID:
            return ir::TypeRef{.kind = ir::TypeRefKind::UUID, .display_name = type.spelling()};
        case ast::TypeSyntaxKind::Timestamp:
            return ir::TypeRef{
                .kind = ir::TypeRefKind::Timestamp,
                .display_name = type.spelling(),
            };
        case ast::TypeSyntaxKind::Duration:
            return ir::TypeRef{
                .kind = ir::TypeRefKind::Duration,
                .display_name = type.spelling(),
            };
        case ast::TypeSyntaxKind::BoundedString:
            return ir::TypeRef{
                .kind = ir::TypeRefKind::BoundedString,
                .display_name = type.spelling(),
                .string_bounds = type.string_bounds,
            };
        case ast::TypeSyntaxKind::Decimal:
            return ir::TypeRef{
                .kind = ir::TypeRefKind::Decimal,
                .display_name = type.spelling(),
                .decimal_scale = type.decimal_scale,
            };
        case ast::TypeSyntaxKind::Named:
            return named_type_ref_from_syntax(type);
        case ast::TypeSyntaxKind::Optional: {
            auto ref = ir::TypeRef{
                .kind = ir::TypeRefKind::Optional,
                .display_name = type.spelling(),
            };
            ref.first = type.first ? make_type_ref(type_ref_from_syntax(*type.first)) : nullptr;
            return ref;
        }
        case ast::TypeSyntaxKind::List: {
            auto ref = ir::TypeRef{
                .kind = ir::TypeRefKind::List,
                .display_name = type.spelling(),
            };
            ref.first = type.first ? make_type_ref(type_ref_from_syntax(*type.first)) : nullptr;
            return ref;
        }
        case ast::TypeSyntaxKind::Set: {
            auto ref = ir::TypeRef{
                .kind = ir::TypeRefKind::Set,
                .display_name = type.spelling(),
            };
            ref.first = type.first ? make_type_ref(type_ref_from_syntax(*type.first)) : nullptr;
            return ref;
        }
        case ast::TypeSyntaxKind::Map: {
            auto ref = ir::TypeRef{
                .kind = ir::TypeRefKind::Map,
                .display_name = type.spelling(),
            };
            ref.first = type.first ? make_type_ref(type_ref_from_syntax(*type.first)) : nullptr;
            ref.second = type.second ? make_type_ref(type_ref_from_syntax(*type.second)) : nullptr;
            return ref;
        }
        }

        return ir::TypeRef{.kind = ir::TypeRefKind::Unresolved, .display_name = "<invalid-type>"};
    }

    [[nodiscard]] std::string render_type_syntax(const ast::TypeSyntax &type) const {
        switch (type.kind) {
        case ast::TypeSyntaxKind::Unit:
        case ast::TypeSyntaxKind::Bool:
        case ast::TypeSyntaxKind::Int:
        case ast::TypeSyntaxKind::Float:
        case ast::TypeSyntaxKind::String:
        case ast::TypeSyntaxKind::UUID:
        case ast::TypeSyntaxKind::Timestamp:
        case ast::TypeSyntaxKind::Duration:
        case ast::TypeSyntaxKind::BoundedString:
        case ast::TypeSyntaxKind::Decimal:
            return type.spelling();
        case ast::TypeSyntaxKind::Named:
            return canonical_name_from_reference_here(
                ReferenceKind::TypeName, type.name->range, type.name->spelling());
        case ast::TypeSyntaxKind::Optional:
            return "Optional<" + render_type_syntax(*type.first) + ">";
        case ast::TypeSyntaxKind::List:
            return "List<" + render_type_syntax(*type.first) + ">";
        case ast::TypeSyntaxKind::Set:
            return "Set<" + render_type_syntax(*type.first) + ">";
        case ast::TypeSyntaxKind::Map:
            return "Map<" + render_type_syntax(*type.first) + ", " +
                   render_type_syntax(*type.second) + ">";
        }

        return "<invalid-type>";
    }

    [[nodiscard]] std::vector<std::string>
    lower_retry_targets(const std::vector<Owned<ast::QualifiedName>> &targets) const {
        std::vector<std::string> names;
        names.reserve(targets.size());

        for (const auto &target : targets) {
            names.push_back(target->spelling());
        }

        return names;
    }

    [[nodiscard]] std::string render_call_target(const ast::QualifiedName &name) const {
        return canonical_name_from_reference_here(
            ReferenceKind::CallTarget, name.range, name.spelling());
    }

    [[nodiscard]] std::string render_struct_target(const ast::QualifiedName &name) const {
        return canonical_name_from_reference_here(
            ReferenceKind::TypeName, name.range, name.spelling());
    }

    [[nodiscard]] std::string render_qualified_value(const ast::ExprSyntax &expr) const {
        const auto const_symbol =
            symbol_from_reference_here(ReferenceKind::ConstValue, expr.qualified_name->range);
        if (const_symbol.has_value()) {
            return const_symbol->get().canonical_name;
        }

        const auto owner_symbol = symbol_from_reference_here(ReferenceKind::QualifiedValueOwnerType,
                                                             expr.qualified_name->range);
        if (!owner_symbol.has_value()) {
            return expr.qualified_name->spelling();
        }

        const auto &segments = expr.qualified_name->segments;
        if (segments.empty()) {
            return owner_symbol->get().canonical_name;
        }

        return owner_symbol->get().canonical_name + "::" + segments.back();
    }

    [[nodiscard]] ir::Path lower_path(const ast::PathSyntax &path) const {
        return ir::Path{
            .root_kind = lower_path_root_kind(path.root_kind),
            .root_name = path.root_name,
            .members = path.members,
        };
    }

    [[nodiscard]] ir::ExprPtr lower_expr(const ast::ExprSyntax &expr) const {
        const auto make = [this, &expr](auto node) {
            return make_expr(std::move(node), expr.range);
        };

        switch (expr.kind) {
        case ast::ExprSyntaxKind::BoolLiteral:
            return make(ir::BoolLiteralExpr{.value = expr.bool_value});
        case ast::ExprSyntaxKind::IntegerLiteral:
            return make(ir::IntegerLiteralExpr{
                .spelling = expr.integer_literal ? expr.integer_literal->spelling : "0",
            });
        case ast::ExprSyntaxKind::FloatLiteral:
            return make(ir::FloatLiteralExpr{.spelling = expr.text});
        case ast::ExprSyntaxKind::DecimalLiteral:
            return make(ir::DecimalLiteralExpr{.spelling = expr.text});
        case ast::ExprSyntaxKind::StringLiteral:
            return make(ir::StringLiteralExpr{.spelling = expr.text});
        case ast::ExprSyntaxKind::DurationLiteral:
            return make(ir::DurationLiteralExpr{
                .spelling = expr.duration_literal ? expr.duration_literal->spelling : expr.text,
            });
        case ast::ExprSyntaxKind::NoneLiteral:
            return make(ir::NoneLiteralExpr{});
        case ast::ExprSyntaxKind::Some:
            return make(ir::SomeExpr{.value = lower_expr(*expr.first)});
        case ast::ExprSyntaxKind::Path:
            return make(ir::PathExpr{.path = lower_path(*expr.path)});
        case ast::ExprSyntaxKind::QualifiedValue:
            return make(ir::QualifiedValueExpr{.value = render_qualified_value(expr)});
        case ast::ExprSyntaxKind::Call: {
            ir::CallExpr call{
                .callee = render_call_target(*expr.qualified_name),
                .arguments = {},
            };
            call.arguments.reserve(expr.items.size());
            for (const auto &item : expr.items) {
                call.arguments.push_back(lower_expr(*item));
            }
            return make(std::move(call));
        }
        case ast::ExprSyntaxKind::StructLiteral: {
            ir::StructLiteralExpr literal{
                .type_name = render_struct_target(*expr.qualified_name),
                .fields = {},
            };
            literal.fields.reserve(expr.struct_fields.size());
            for (const auto &field : expr.struct_fields) {
                literal.fields.push_back(ir::StructFieldInit{
                    .name = field->field_name,
                    .value = lower_expr(*field->value),
                });
            }
            return make(std::move(literal));
        }
        case ast::ExprSyntaxKind::ListLiteral: {
            ir::ListLiteralExpr literal;
            literal.items.reserve(expr.items.size());
            for (const auto &item : expr.items) {
                literal.items.push_back(lower_expr(*item));
            }
            return make(std::move(literal));
        }
        case ast::ExprSyntaxKind::SetLiteral: {
            ir::SetLiteralExpr literal;
            literal.items.reserve(expr.items.size());
            for (const auto &item : expr.items) {
                literal.items.push_back(lower_expr(*item));
            }
            return make(std::move(literal));
        }
        case ast::ExprSyntaxKind::MapLiteral: {
            ir::MapLiteralExpr literal;
            literal.entries.reserve(expr.map_entries.size());
            for (const auto &entry : expr.map_entries) {
                literal.entries.push_back(ir::MapEntryExpr{
                    .key = lower_expr(*entry->key),
                    .value = lower_expr(*entry->value),
                });
            }
            return make(std::move(literal));
        }
        case ast::ExprSyntaxKind::Unary:
            return make(ir::UnaryExpr{
                .op = lower_expr_unary_op(expr.unary_op),
                .operand = lower_expr(*expr.first),
            });
        case ast::ExprSyntaxKind::Binary:
            return make(ir::BinaryExpr{
                .op = lower_expr_binary_op(expr.binary_op),
                .lhs = lower_expr(*expr.first),
                .rhs = lower_expr(*expr.second),
            });
        case ast::ExprSyntaxKind::MemberAccess:
            return make(ir::MemberAccessExpr{
                .base = lower_expr(*expr.first),
                .member = expr.name,
            });
        case ast::ExprSyntaxKind::IndexAccess:
            return make(ir::IndexAccessExpr{
                .base = lower_expr(*expr.first),
                .index = lower_expr(*expr.second),
            });
        case ast::ExprSyntaxKind::Group:
            return make(ir::GroupExpr{.expr = lower_expr(*expr.first)});
        }

        return make(ir::QualifiedValueExpr{.value = "<invalid-expr>"});
    }

    [[nodiscard]] ir::TemporalExprPtr lower_temporal(const ast::TemporalExprSyntax &expr) const {
        const auto make = [this, &expr](auto node) {
            return make_temporal(std::move(node), expr.range);
        };

        switch (expr.kind) {
        case ast::TemporalExprSyntaxKind::EmbeddedExpr:
            return make(ir::EmbeddedTemporalExpr{.expr = lower_expr(*expr.expr)});
        case ast::TemporalExprSyntaxKind::Called:
            return make(ir::CalledTemporalExpr{
                .capability = canonical_name_from_reference_here(
                    ReferenceKind::TemporalCapability, expr.range, expr.name),
            });
        case ast::TemporalExprSyntaxKind::InState:
            return make(ir::InStateTemporalExpr{.state = expr.name});
        case ast::TemporalExprSyntaxKind::Running:
            return make(ir::RunningTemporalExpr{.node = expr.name});
        case ast::TemporalExprSyntaxKind::Completed:
            return make(ir::CompletedTemporalExpr{
                .node = expr.name,
                .state_name = expr.state_name,
            });
        case ast::TemporalExprSyntaxKind::Unary:
            return make(ir::TemporalUnaryExpr{
                .op = lower_temporal_unary_op(expr.unary_op),
                .operand = lower_temporal(*expr.first),
            });
        case ast::TemporalExprSyntaxKind::Binary:
            return make(ir::TemporalBinaryExpr{
                .op = lower_temporal_binary_op(expr.binary_op),
                .lhs = lower_temporal(*expr.first),
                .rhs = lower_temporal(*expr.second),
            });
        }

        return make(ir::CalledTemporalExpr{.capability = "<invalid-temporal-expr>"});
    }

    [[nodiscard]] std::string inferred_statement_type(const ast::LetStmtSyntax &statement) const {
        if (statement.type) {
            return render_type_syntax(*statement.type);
        }

        if (statement.initializer) {
            const auto expression_type = type_check_result_.find_expression_type(
                statement.initializer->range, current_source_id_);
            if (expression_type.has_value() && expression_type->get().type) {
                return expression_type->get().type->describe();
            }
        }

        return "Any";
    }

    [[nodiscard]] ir::Block lower_block(const ast::BlockSyntax &block) const {
        ir::Block ir_block{
            .statements = {},
            .source_range = block.range,
        };
        ir_block.statements.reserve(block.statements.size());

        for (const auto &statement : block.statements) {
            ir_block.statements.push_back(lower_statement(*statement));
        }

        return ir_block;
    }

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
                           for (const auto &argument : value.arguments) {
                               collect_called_targets_from_expr(*argument, called_targets);
                           }
                       },
                       [this, &called_targets](const ir::StructLiteralExpr &value) {
                           for (const auto &field : value.fields) {
                               collect_called_targets_from_expr(*field.value, called_targets);
                           }
                       },
                       [this, &called_targets](const ir::ListLiteralExpr &value) {
                           for (const auto &item : value.items) {
                               collect_called_targets_from_expr(*item, called_targets);
                           }
                       },
                       [this, &called_targets](const ir::SetLiteralExpr &value) {
                           for (const auto &item : value.items) {
                               collect_called_targets_from_expr(*item, called_targets);
                           }
                       },
                       [this, &called_targets](const ir::MapLiteralExpr &value) {
                           for (const auto &entry : value.entries) {
                               collect_called_targets_from_expr(*entry.key, called_targets);
                               collect_called_targets_from_expr(*entry.value, called_targets);
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
                       [this, &called_targets](const ir::GroupExpr &value) {
                           collect_called_targets_from_expr(*value.expr, called_targets);
                       },
                   },
                   expr.node);
    }

    void merge_flow_summary(ir::StateHandler::Summary &target,
                            const ir::StateHandler::Summary &other) const {
        for (const auto &goto_target : other.goto_targets) {
            push_unique_value(target.goto_targets, goto_target);
        }

        for (const auto &assigned_path : other.assigned_paths) {
            push_unique_path(target.assigned_paths, assigned_path);
        }

        for (const auto &called_target : other.called_targets) {
            push_unique_value(target.called_targets, called_target);
        }

        target.may_return = target.may_return || other.may_return;
        target.assert_count += other.assert_count;
    }

    [[nodiscard]] ir::StateHandler::Summary
    summarize_statement(const ir::Statement &statement) const {
        return std::visit(
            Overloaded{
                [this](const ir::LetStatement &value) {
                    ir::StateHandler::Summary summary;
                    collect_called_targets_from_expr(*value.initializer, summary.called_targets);
                    return summary;
                },
                [this](const ir::AssignStatement &value) {
                    ir::StateHandler::Summary summary;
                    push_unique_path(summary.assigned_paths, value.target);
                    collect_called_targets_from_expr(*value.value, summary.called_targets);
                    return summary;
                },
                [this](const ir::IfStatement &value) {
                    ir::StateHandler::Summary summary;
                    collect_called_targets_from_expr(*value.condition, summary.called_targets);

                    const auto then_summary = summarize_block(*value.then_block);
                    merge_flow_summary(summary, then_summary);

                    ir::StateHandler::Summary else_summary;
                    if (value.else_block) {
                        else_summary = summarize_block(*value.else_block);
                        merge_flow_summary(summary, else_summary);
                    }

                    summary.may_fallthrough = !value.else_block || then_summary.may_fallthrough ||
                                              else_summary.may_fallthrough;
                    return summary;
                },
                [](const ir::GotoStatement &value) {
                    ir::StateHandler::Summary summary;
                    summary.goto_targets.push_back(value.target_state);
                    summary.may_fallthrough = false;
                    return summary;
                },
                [this](const ir::ReturnStatement &value) {
                    ir::StateHandler::Summary summary;
                    if (value.value) {
                        collect_called_targets_from_expr(*value.value, summary.called_targets);
                    }
                    summary.may_return = true;
                    summary.may_fallthrough = false;
                    return summary;
                },
                [this](const ir::AssertStatement &value) {
                    ir::StateHandler::Summary summary;
                    collect_called_targets_from_expr(*value.condition, summary.called_targets);
                    summary.assert_count = 1;
                    return summary;
                },
                [this](const ir::ExprStatement &value) {
                    ir::StateHandler::Summary summary;
                    collect_called_targets_from_expr(*value.expr, summary.called_targets);
                    return summary;
                },
            },
            statement.node);
    }

    [[nodiscard]] ir::StateHandler::Summary summarize_block(const ir::Block &block) const {
        ir::StateHandler::Summary summary;

        for (const auto &statement : block.statements) {
            if (!summary.may_fallthrough) {
                break;
            }

            const auto statement_summary = summarize_statement(*statement);
            merge_flow_summary(summary, statement_summary);
            summary.may_fallthrough = statement_summary.may_fallthrough;
        }

        return summary;
    }

    [[nodiscard]] ir::StatementPtr lower_statement(const ast::StatementSyntax &statement) const {
        const auto make = [this, &statement](auto node) {
            return make_statement(std::move(node), statement.range);
        };

        switch (statement.kind) {
        case ast::StatementSyntaxKind::Let:
            return make(ir::LetStatement{
                .name = statement.let_stmt->name,
                .type = inferred_statement_type(*statement.let_stmt),
                .initializer = lower_expr(*statement.let_stmt->initializer),
            });
        case ast::StatementSyntaxKind::Assign:
            return make(ir::AssignStatement{
                .target = lower_path(*statement.assign_stmt->target),
                .value = lower_expr(*statement.assign_stmt->value),
            });
        case ast::StatementSyntaxKind::If:
            return make(ir::IfStatement{
                .condition = lower_expr(*statement.if_stmt->condition),
                .then_block = make_owned<ir::Block>(lower_block(*statement.if_stmt->then_block)),
                .else_block = statement.if_stmt->else_block ? make_owned<ir::Block>(lower_block(
                                                                  *statement.if_stmt->else_block))
                                                            : nullptr,
            });
        case ast::StatementSyntaxKind::Goto:
            return make(ir::GotoStatement{.target_state = statement.goto_stmt->target_state});
        case ast::StatementSyntaxKind::Return:
            return make(ir::ReturnStatement{
                .value = statement.return_stmt->value ? lower_expr(*statement.return_stmt->value)
                                                      : nullptr,
            });
        case ast::StatementSyntaxKind::Assert:
            return make(
                ir::AssertStatement{.condition = lower_expr(*statement.assert_stmt->condition)});
        case ast::StatementSyntaxKind::Expr:
            return make(ir::ExprStatement{
                .expr = lower_expr(*statement.expr_stmt->expr),
            });
        }

        return make(ir::ExprStatement{
            .expr =
                make_expr(ir::QualifiedValueExpr{.value = "<invalid-statement>"}, statement.range),
        });
    }

    void collect_workflow_value_reads(const ir::Expr &expr,
                                      const std::vector<std::string> &workflow_node_names,
                                      std::vector<ir::WorkflowValueRead> &reads) const {
        std::visit(
            Overloaded{
                [](const ir::NoneLiteralExpr &) {},
                [](const ir::BoolLiteralExpr &) {},
                [](const ir::IntegerLiteralExpr &) {},
                [](const ir::FloatLiteralExpr &) {},
                [](const ir::DecimalLiteralExpr &) {},
                [](const ir::StringLiteralExpr &) {},
                [](const ir::DurationLiteralExpr &) {},
                [this, &workflow_node_names, &reads](const ir::SomeExpr &value) {
                    collect_workflow_value_reads(*value.value, workflow_node_names, reads);
                },
                [&workflow_node_names, &reads](const ir::PathExpr &value) {
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

                    if (value.path.root_kind != ir::PathRootKind::Identifier) {
                        return;
                    }

                    if (std::find(workflow_node_names.begin(),
                                  workflow_node_names.end(),
                                  value.path.root_name) == workflow_node_names.end()) {
                        return;
                    }

                    push_unique_workflow_value_read(
                        reads,
                        ir::WorkflowValueRead{
                            .kind = ir::WorkflowValueSourceKind::WorkflowNodeOutput,
                            .root_name = value.path.root_name,
                            .members = value.path.members,
                        });
                },
                [](const ir::QualifiedValueExpr &) {},
                [this, &workflow_node_names, &reads](const ir::CallExpr &value) {
                    for (const auto &argument : value.arguments) {
                        collect_workflow_value_reads(*argument, workflow_node_names, reads);
                    }
                },
                [this, &workflow_node_names, &reads](const ir::StructLiteralExpr &value) {
                    for (const auto &field : value.fields) {
                        collect_workflow_value_reads(*field.value, workflow_node_names, reads);
                    }
                },
                [this, &workflow_node_names, &reads](const ir::ListLiteralExpr &value) {
                    for (const auto &item : value.items) {
                        collect_workflow_value_reads(*item, workflow_node_names, reads);
                    }
                },
                [this, &workflow_node_names, &reads](const ir::SetLiteralExpr &value) {
                    for (const auto &item : value.items) {
                        collect_workflow_value_reads(*item, workflow_node_names, reads);
                    }
                },
                [this, &workflow_node_names, &reads](const ir::MapLiteralExpr &value) {
                    for (const auto &entry : value.entries) {
                        collect_workflow_value_reads(*entry.key, workflow_node_names, reads);
                        collect_workflow_value_reads(*entry.value, workflow_node_names, reads);
                    }
                },
                [this, &workflow_node_names, &reads](const ir::UnaryExpr &value) {
                    collect_workflow_value_reads(*value.operand, workflow_node_names, reads);
                },
                [this, &workflow_node_names, &reads](const ir::BinaryExpr &value) {
                    collect_workflow_value_reads(*value.lhs, workflow_node_names, reads);
                    collect_workflow_value_reads(*value.rhs, workflow_node_names, reads);
                },
                [this, &workflow_node_names, &reads](const ir::MemberAccessExpr &value) {
                    collect_workflow_value_reads(*value.base, workflow_node_names, reads);
                },
                [this, &workflow_node_names, &reads](const ir::IndexAccessExpr &value) {
                    collect_workflow_value_reads(*value.base, workflow_node_names, reads);
                    collect_workflow_value_reads(*value.index, workflow_node_names, reads);
                },
                [this, &workflow_node_names, &reads](const ir::GroupExpr &value) {
                    collect_workflow_value_reads(*value.expr, workflow_node_names, reads);
                },
            },
            expr.node);
    }

    [[nodiscard]] ir::WorkflowExprSummary
    summarize_workflow_expr(const ir::Expr &expr,
                            const std::vector<std::string> &workflow_node_names) const {
        ir::WorkflowExprSummary summary;
        collect_workflow_value_reads(expr, workflow_node_names, summary.reads);
        return summary;
    }

    [[nodiscard]] ir::Decl lower_declaration(const ast::Decl &declaration) const {
        switch (declaration.kind) {
        case ast::NodeKind::ModuleDecl:
            return lower_module(static_cast<const ast::ModuleDecl &>(declaration));
        case ast::NodeKind::ImportDecl:
            return lower_import(static_cast<const ast::ImportDecl &>(declaration));
        case ast::NodeKind::ConstDecl:
            return lower_const(static_cast<const ast::ConstDecl &>(declaration));
        case ast::NodeKind::TypeAliasDecl:
            return lower_type_alias(static_cast<const ast::TypeAliasDecl &>(declaration));
        case ast::NodeKind::StructDecl:
            return lower_struct(static_cast<const ast::StructDecl &>(declaration));
        case ast::NodeKind::EnumDecl:
            return lower_enum(static_cast<const ast::EnumDecl &>(declaration));
        case ast::NodeKind::CapabilityDecl:
            return lower_capability(static_cast<const ast::CapabilityDecl &>(declaration));
        case ast::NodeKind::PredicateDecl:
            return lower_predicate(static_cast<const ast::PredicateDecl &>(declaration));
        case ast::NodeKind::AgentDecl:
            return lower_agent(static_cast<const ast::AgentDecl &>(declaration));
        case ast::NodeKind::ContractDecl:
            return lower_contract(static_cast<const ast::ContractDecl &>(declaration));
        case ast::NodeKind::FlowDecl:
            return lower_flow(static_cast<const ast::FlowDecl &>(declaration));
        case ast::NodeKind::WorkflowDecl:
            return lower_workflow(static_cast<const ast::WorkflowDecl &>(declaration));
        case ast::NodeKind::Program:
            return ir::ModuleDecl{
                .provenance = {},
                .name = "<invalid-program-decl>",
            };
        }

        return ir::ModuleDecl{
            .provenance = {},
            .name = "<invalid-decl>",
        };
    }

    [[nodiscard]] ir::ModuleDecl lower_module(const ast::ModuleDecl &node) const {
        if (graph_ == nullptr) {
            current_module_name_ = node.name->spelling();
        }

        return with_provenance(
            ir::ModuleDecl{
                .provenance = {},
                .name = node.name->spelling(),
            },
            node.range);
    }

    [[nodiscard]] ir::ImportDecl lower_import(const ast::ImportDecl &node) const {
        return with_provenance(
            ir::ImportDecl{
                .provenance = {},
                .path = node.path->spelling(),
                .alias = node.alias.empty() ? std::nullopt : std::make_optional(node.alias),
            },
            node.range);
    }

    [[nodiscard]] ir::ConstDecl lower_const(const ast::ConstDecl &node) const {
        const auto symbol_name = canonical_local_name_here(SymbolNamespace::Consts, node.name);
        const auto const_symbol = find_local_symbol_here(SymbolNamespace::Consts, node.name);

        MaybeCRef<Type> const_type;
        if (const_symbol.has_value()) {
            const_type = environment().get_const_type(const_symbol->get().id);
        }

        return with_provenance(
            ir::ConstDecl{
                .provenance = {},
                .name = symbol_name,
                .type = describe_type(const_type),
                .value = lower_expr(*node.value),
                .type_ref = const_type.has_value() ? type_ref_from_type(const_type->get())
                                                   : type_ref_from_syntax(*node.type),
                .symbol_ref =
                    symbol_ref_from_symbol(const_symbol, ir::SymbolRefKind::Const, symbol_name),
            },
            node.range);
    }

    [[nodiscard]] ir::TypeAliasDecl lower_type_alias(const ast::TypeAliasDecl &node) const {
        const auto symbol = find_local_symbol_here(SymbolNamespace::Types, node.name);
        const auto symbol_name = symbol.has_value() ? symbol->get().canonical_name : node.name;

        return with_provenance(
            ir::TypeAliasDecl{
                .provenance = {},
                .name = symbol_name,
                .aliased_type = render_type_syntax(*node.aliased_type),
                .aliased_type_ref = type_ref_from_syntax(*node.aliased_type),
                .symbol_ref = symbol_ref_from_symbol(symbol, ir::SymbolRefKind::Type, symbol_name),
            },
            node.range);
    }

    [[nodiscard]] ir::StructDecl lower_struct(const ast::StructDecl &node) const {
        const auto symbol = find_local_symbol_here(SymbolNamespace::Types, node.name);
        const auto info =
            symbol.has_value() ? environment().get_struct(symbol->get().id) : std::nullopt;

        ir::StructDecl declaration = with_provenance(
            ir::StructDecl{
                .provenance = {},
                .name = symbol.has_value() ? symbol->get().canonical_name : node.name,
                .fields = {},
                .symbol_ref = symbol_ref_from_symbol(symbol, ir::SymbolRefKind::Type, node.name),
            },
            node.range);
        declaration.fields.reserve(node.fields.size());

        for (std::size_t index = 0; index < node.fields.size(); ++index) {
            const auto &field = node.fields[index];

            std::string field_type = render_type_syntax(*field->type);
            auto field_type_ref = type_ref_from_syntax(*field->type);
            if (info.has_value() && index < info->get().fields.size() &&
                info->get().fields[index].type) {
                field_type = info->get().fields[index].type->describe();
                field_type_ref = type_ref_from_type(*info->get().fields[index].type);
            }

            declaration.fields.push_back(ir::FieldDecl{
                .name = field->name,
                .type = std::move(field_type),
                .default_value = field->default_value ? lower_expr(*field->default_value) : nullptr,
                .type_ref = std::move(field_type_ref),
                .source_range = field->range,
            });
        }

        return declaration;
    }

    [[nodiscard]] ir::EnumDecl lower_enum(const ast::EnumDecl &node) const {
        const auto symbol = find_local_symbol_here(SymbolNamespace::Types, node.name);

        ir::EnumDecl declaration = with_provenance(
            ir::EnumDecl{
                .provenance = {},
                .name = symbol.has_value() ? symbol->get().canonical_name : node.name,
                .variants = {},
                .symbol_ref = symbol_ref_from_symbol(symbol, ir::SymbolRefKind::Type, node.name),
            },
            node.range);
        declaration.variants.reserve(node.variants.size());

        for (const auto &variant : node.variants) {
            declaration.variants.push_back(variant->name);
        }

        return declaration;
    }

    [[nodiscard]] std::vector<ir::ParamDecl>
    lower_params(const std::vector<Owned<ast::ParamDeclSyntax>> &params,
                 MaybeCRef<CapabilityTypeInfo> capability_info,
                 MaybeCRef<PredicateTypeInfo> predicate_info) const {
        std::vector<ir::ParamDecl> result;

        if (capability_info.has_value()) {
            result.reserve(capability_info->get().params.size());
            for (std::size_t index = 0; index < capability_info->get().params.size(); ++index) {
                const auto &param = capability_info->get().params[index];
                result.push_back(ir::ParamDecl{
                    .name = param.name,
                    .type = describe_type(borrow(param.type.get())),
                    .type_ref = type_ref_from_maybe(borrow(param.type.get())),
                    .source_range = index < params.size() ? ir::SourceRangeOpt{params[index]->range}
                                                          : std::nullopt,
                });
            }
            return result;
        }

        if (predicate_info.has_value()) {
            result.reserve(predicate_info->get().params.size());
            for (std::size_t index = 0; index < predicate_info->get().params.size(); ++index) {
                const auto &param = predicate_info->get().params[index];
                result.push_back(ir::ParamDecl{
                    .name = param.name,
                    .type = describe_type(borrow(param.type.get())),
                    .type_ref = type_ref_from_maybe(borrow(param.type.get())),
                    .source_range = index < params.size() ? ir::SourceRangeOpt{params[index]->range}
                                                          : std::nullopt,
                });
            }
            return result;
        }

        result.reserve(params.size());
        for (const auto &param : params) {
            result.push_back(ir::ParamDecl{
                .name = param->name,
                .type = render_type_syntax(*param->type),
                .type_ref = type_ref_from_syntax(*param->type),
                .source_range = param->range,
            });
        }

        return result;
    }

    [[nodiscard]] ir::CapabilityEffectSpec
    lower_capability_effect(const ast::CapabilityEffectSyntax *syntax) const {
        ir::CapabilityEffectSpec effect;
        if (syntax == nullptr) {
            return effect;
        }

        effect.declared = true;
        effect.kind = lower_capability_effect_kind(syntax->effect_kind);
        effect.receipt_mode = lower_capability_receipt_mode(syntax->receipt_mode);
        effect.retry_mode = lower_capability_retry_mode(syntax->retry_mode);
        effect.source_range = syntax->range;

        if (syntax->domain) {
            effect.domain = syntax->domain->spelling();
        }
        if (syntax->idempotency_key) {
            effect.idempotency_key = syntax->idempotency_key->spelling();
        }
        if (syntax->timeout) {
            effect.timeout = syntax->timeout->spelling;
        }
        if (syntax->compensation) {
            effect.compensation = syntax->compensation->spelling();
        }

        effect.policies.reserve(syntax->policies.size());
        for (const auto &policy : syntax->policies) {
            effect.policies.push_back(policy->spelling());
        }

        return effect;
    }

    [[nodiscard]] ir::CapabilityDecl lower_capability(const ast::CapabilityDecl &node) const {
        const auto symbol = find_local_symbol_here(SymbolNamespace::Capabilities, node.name);
        const auto info =
            symbol.has_value() ? environment().get_capability(symbol->get().id) : std::nullopt;
        const auto symbol_name = symbol.has_value() ? symbol->get().canonical_name : node.name;

        return with_provenance(
            ir::CapabilityDecl{
                .provenance = {},
                .name = symbol_name,
                .params = lower_params(node.params, info, std::nullopt),
                .return_type = info.has_value()
                                   ? describe_type(borrow(info->get().return_type.get()))
                                   : render_type_syntax(*node.return_type),
                .return_type_ref = info.has_value()
                                       ? type_ref_from_maybe(borrow(info->get().return_type.get()))
                                       : type_ref_from_syntax(*node.return_type),
                .effect = lower_capability_effect(node.effect.get()),
                .symbol_ref =
                    symbol_ref_from_symbol(symbol, ir::SymbolRefKind::Capability, symbol_name),
            },
            node.range);
    }

    [[nodiscard]] ir::PredicateDecl lower_predicate(const ast::PredicateDecl &node) const {
        const auto symbol = find_local_symbol_here(SymbolNamespace::Predicates, node.name);
        const auto info =
            symbol.has_value() ? environment().get_predicate(symbol->get().id) : std::nullopt;
        const auto symbol_name = symbol.has_value() ? symbol->get().canonical_name : node.name;

        return with_provenance(
            ir::PredicateDecl{
                .provenance = {},
                .name = symbol_name,
                .params = lower_params(node.params, std::nullopt, info),
                .symbol_ref =
                    symbol_ref_from_symbol(symbol, ir::SymbolRefKind::Predicate, symbol_name),
            },
            node.range);
    }

    [[nodiscard]] ir::AgentDecl lower_agent(const ast::AgentDecl &node) const {
        const auto symbol = find_local_symbol_here(SymbolNamespace::Agents, node.name);
        const auto info =
            symbol.has_value() ? environment().get_agent(symbol->get().id) : std::nullopt;

        ir::AgentDecl declaration = with_provenance(
            ir::AgentDecl{
                .provenance = {},
                .name = symbol.has_value() ? symbol->get().canonical_name : node.name,
                .input_type = info.has_value() ? describe_type(borrow(info->get().input_type.get()))
                                               : render_type_syntax(*node.input_type),
                .context_type = info.has_value()
                                    ? describe_type(borrow(info->get().context_type.get()))
                                    : render_type_syntax(*node.context_type),
                .output_type = info.has_value()
                                   ? describe_type(borrow(info->get().output_type.get()))
                                   : render_type_syntax(*node.output_type),
                .states = node.states,
                .initial_state = node.initial_state,
                .final_states = node.final_states,
                .capabilities = {},
                .quota = {},
                .transitions = {},
                .input_type_ref = info.has_value()
                                      ? type_ref_from_maybe(borrow(info->get().input_type.get()))
                                      : type_ref_from_syntax(*node.input_type),
                .context_type_ref =
                    info.has_value() ? type_ref_from_maybe(borrow(info->get().context_type.get()))
                                     : type_ref_from_syntax(*node.context_type),
                .output_type_ref = info.has_value()
                                       ? type_ref_from_maybe(borrow(info->get().output_type.get()))
                                       : type_ref_from_syntax(*node.output_type),
                .capability_refs = {},
                .symbol_ref = symbol_ref_from_symbol(symbol, ir::SymbolRefKind::Agent, node.name),
            },
            node.range);

        if (info.has_value()) {
            declaration.capabilities.reserve(info->get().capability_symbols.size());
            declaration.capability_refs.reserve(info->get().capability_symbols.size());
            for (const auto capability_symbol : info->get().capability_symbols) {
                const auto capability = symbols().get(capability_symbol);
                declaration.capabilities.push_back(capability.has_value()
                                                       ? capability->get().canonical_name
                                                       : "<missing-capability>");
                declaration.capability_refs.push_back(
                    symbol_ref_from_symbol(capability,
                                           ir::SymbolRefKind::Capability,
                                           capability.has_value() ? capability->get().canonical_name
                                                                  : "<missing-capability>"));
            }
        } else {
            declaration.capabilities.reserve(node.capabilities.size());
            declaration.capability_refs.reserve(node.capabilities.size());
            for (const auto &capability : node.capabilities) {
                const auto symbol =
                    find_local_symbol_here(SymbolNamespace::Capabilities, capability);
                const auto name =
                    symbol.has_value() ? symbol->get().canonical_name : std::string(capability);
                declaration.capabilities.push_back(name);
                declaration.capability_refs.push_back(
                    symbol_ref_from_symbol(symbol, ir::SymbolRefKind::Capability, name));
            }
        }

        if (node.quota) {
            declaration.quota.reserve(node.quota->items.size());
            for (const auto &item : node.quota->items) {
                std::string value;
                if (item->integer_value) {
                    value = item->integer_value->spelling;
                } else if (item->duration_value) {
                    value = item->duration_value->spelling;
                } else {
                    value = "<missing-quota-value>";
                }

                declaration.quota.push_back(ir::QuotaItem{
                    .name = quota_item_name(item->kind),
                    .value = std::move(value),
                });
            }
        }

        declaration.transitions.reserve(node.transitions.size());
        for (const auto &transition : node.transitions) {
            declaration.transitions.push_back(ir::TransitionDecl{
                .from_state = transition->from_state,
                .to_state = transition->to_state,
            });
        }

        return declaration;
    }

    [[nodiscard]] ir::ContractDecl lower_contract(const ast::ContractDecl &node) const {
        const auto target = canonical_name_from_reference_here(
            ReferenceKind::ContractTarget, node.target->range, node.target->spelling());
        ir::ContractDecl declaration = with_provenance(
            ir::ContractDecl{
                .provenance = {},
                .target = target,
                .clauses = {},
                .target_ref = symbol_ref_from_reference_here(ReferenceKind::ContractTarget,
                                                             node.target->range,
                                                             ir::SymbolRefKind::Agent,
                                                             target),
            },
            node.range);
        declaration.clauses.reserve(node.clauses.size());

        for (const auto &clause : node.clauses) {
            ir::ContractClause lowered_clause{
                .kind = lower_contract_clause_kind(clause->kind),
                .value = ir::ExprPtr{},
                .source_range = clause->range,
            };

            if (clause->expr) {
                lowered_clause.value = lower_expr(*clause->expr);
            } else {
                lowered_clause.value = lower_temporal(*clause->temporal_expr);
            }

            declaration.clauses.push_back(std::move(lowered_clause));
        }

        return declaration;
    }

    [[nodiscard]] ir::FlowDecl lower_flow(const ast::FlowDecl &node) const {
        const auto target = canonical_name_from_reference_here(
            ReferenceKind::FlowTarget, node.target->range, node.target->spelling());
        ir::FlowDecl declaration = with_provenance(
            ir::FlowDecl{
                .provenance = {},
                .target = target,
                .state_handlers = {},
                .target_ref = symbol_ref_from_reference_here(ReferenceKind::FlowTarget,
                                                             node.target->range,
                                                             ir::SymbolRefKind::Agent,
                                                             target),
            },
            node.range);
        declaration.state_handlers.reserve(node.state_handlers.size());

        for (const auto &handler : node.state_handlers) {
            ir::StateHandler state_handler{
                .state_name = handler->state_name,
                .policy = {},
                .body = lower_block(*handler->body),
                .summary = {},
                .source_range = handler->range,
            };

            if (handler->policy) {
                state_handler.policy.reserve(handler->policy->items.size());
                for (const auto &item : handler->policy->items) {
                    switch (item->kind) {
                    case ast::StatePolicyItemKind::Retry:
                        state_handler.policy.push_back(ir::RetryPolicy{
                            .limit =
                                item->retry_limit ? item->retry_limit->spelling : "<missing-retry>",
                        });
                        break;
                    case ast::StatePolicyItemKind::RetryOn:
                        state_handler.policy.push_back(ir::RetryOnPolicy{
                            .targets = lower_retry_targets(item->retry_on),
                        });
                        break;
                    case ast::StatePolicyItemKind::Timeout:
                        state_handler.policy.push_back(ir::TimeoutPolicy{
                            .duration =
                                item->timeout ? item->timeout->spelling : "<missing-timeout>",
                        });
                        break;
                    }
                }
            }

            state_handler.summary = summarize_block(state_handler.body);
            declaration.state_handlers.push_back(std::move(state_handler));
        }

        return declaration;
    }

    [[nodiscard]] ir::WorkflowDecl lower_workflow(const ast::WorkflowDecl &node) const {
        const auto symbol = find_local_symbol_here(SymbolNamespace::Workflows, node.name);
        const auto info =
            symbol.has_value() ? environment().get_workflow(symbol->get().id) : std::nullopt;

        std::vector<std::string> workflow_node_names;
        workflow_node_names.reserve(node.nodes.size());
        for (const auto &workflow_node : node.nodes) {
            workflow_node_names.push_back(workflow_node->name);
        }

        ir::WorkflowDecl declaration = with_provenance(
            ir::WorkflowDecl{
                .provenance = {},
                .name = symbol.has_value() ? symbol->get().canonical_name : node.name,
                .input_type = info.has_value() ? describe_type(borrow(info->get().input_type.get()))
                                               : render_type_syntax(*node.input_type),
                .output_type = info.has_value()
                                   ? describe_type(borrow(info->get().output_type.get()))
                                   : render_type_syntax(*node.output_type),
                .nodes = {},
                .safety = {},
                .liveness = {},
                .return_value = lower_expr(*node.return_value),
                .return_summary = {},
                .input_type_ref = info.has_value()
                                      ? type_ref_from_maybe(borrow(info->get().input_type.get()))
                                      : type_ref_from_syntax(*node.input_type),
                .output_type_ref = info.has_value()
                                       ? type_ref_from_maybe(borrow(info->get().output_type.get()))
                                       : type_ref_from_syntax(*node.output_type),
                .symbol_ref =
                    symbol_ref_from_symbol(symbol, ir::SymbolRefKind::Workflow, node.name),
            },
            node.range);
        declaration.return_summary =
            summarize_workflow_expr(*declaration.return_value, workflow_node_names);

        declaration.nodes.reserve(node.nodes.size());
        for (const auto &workflow_node : node.nodes) {
            auto input = lower_expr(*workflow_node->input);
            auto input_summary = summarize_workflow_expr(*input, workflow_node_names);
            const auto target =
                canonical_name_from_reference_here(ReferenceKind::WorkflowNodeTarget,
                                                   workflow_node->target->range,
                                                   workflow_node->target->spelling());
            declaration.nodes.push_back(ir::WorkflowNode{
                .name = workflow_node->name,
                .target = target,
                .input = std::move(input),
                .input_summary = std::move(input_summary),
                .after = workflow_node->after,
                .target_ref = symbol_ref_from_reference_here(ReferenceKind::WorkflowNodeTarget,
                                                             workflow_node->target->range,
                                                             ir::SymbolRefKind::Agent,
                                                             target),
                .source_range = workflow_node->range,
            });
        }

        declaration.safety.reserve(node.safety.size());
        for (const auto &formula : node.safety) {
            declaration.safety.push_back(lower_temporal(*formula));
        }

        declaration.liveness.reserve(node.liveness.size());
        for (const auto &formula : node.liveness) {
            declaration.liveness.push_back(lower_temporal(*formula));
        }

        return declaration;
    }
};

class FormalObservationCollector final {
  public:
    [[nodiscard]] std::vector<ir::FormalObservation> collect(const ir::Program &program) {
        observations_.clear();
        observation_index_by_symbol_.clear();

        for (const auto &declaration : program.declarations) {
            std::visit(Overloaded{
                           [this](const ir::AgentDecl &agent) { collect_agent(agent); },
                           [this](const ir::ContractDecl &contract) { collect_contract(contract); },
                           [this](const ir::WorkflowDecl &workflow) { collect_workflow(workflow); },
                           [](const auto &) {},
                       },
                       declaration);
        }

        return observations_;
    }

  private:
    std::vector<ir::FormalObservation> observations_;
    std::unordered_map<std::string, std::size_t> observation_index_by_symbol_;

    void collect_agent(const ir::AgentDecl &agent) {
        for (const auto &capability : agent.capabilities) {
            add_called_observation(agent.name, capability);
        }
    }

    void collect_contract(const ir::ContractDecl &contract) {
        for (std::size_t clause_index = 0; clause_index < contract.clauses.size(); ++clause_index) {
            const auto expr = std::get_if<ir::ExprPtr>(&contract.clauses[clause_index].value);
            if (expr != nullptr) {
                add_embedded_observation(ir::FormalObservationScope{
                    .kind = ir::FormalObservationScopeKind::ContractClause,
                    .owner = contract.target,
                    .clause_index = clause_index,
                    .atom_index = 0,
                });
                continue;
            }

            const auto temporal =
                std::get_if<ir::TemporalExprPtr>(&contract.clauses[clause_index].value);
            if (temporal != nullptr) {
                std::size_t atom_index = 0;
                collect_contract_formula(**temporal, contract.target, clause_index, atom_index);
            }
        }
    }

    void collect_workflow(const ir::WorkflowDecl &workflow) {
        for (std::size_t clause_index = 0; clause_index < workflow.safety.size(); ++clause_index) {
            std::size_t atom_index = 0;
            collect_workflow_formula(*workflow.safety[clause_index],
                                     ir::FormalObservationScopeKind::WorkflowSafetyClause,
                                     workflow.name,
                                     clause_index,
                                     atom_index);
        }

        for (std::size_t clause_index = 0; clause_index < workflow.liveness.size();
             ++clause_index) {
            std::size_t atom_index = 0;
            collect_workflow_formula(*workflow.liveness[clause_index],
                                     ir::FormalObservationScopeKind::WorkflowLivenessClause,
                                     workflow.name,
                                     clause_index,
                                     atom_index);
        }
    }

    void collect_contract_formula(const ir::TemporalExpr &expr,
                                  std::string_view agent_name,
                                  std::size_t clause_index,
                                  std::size_t &atom_index) {
        std::visit(
            Overloaded{
                [&](const ir::EmbeddedTemporalExpr &) {
                    add_embedded_observation(ir::FormalObservationScope{
                        .kind = ir::FormalObservationScopeKind::ContractClause,
                        .owner = std::string(agent_name),
                        .clause_index = clause_index,
                        .atom_index = atom_index++,
                    });
                },
                [&](const ir::CalledTemporalExpr &value) {
                    add_called_observation(agent_name, value.capability);
                },
                [&](const ir::TemporalUnaryExpr &value) {
                    collect_contract_formula(*value.operand, agent_name, clause_index, atom_index);
                },
                [&](const ir::TemporalBinaryExpr &value) {
                    collect_contract_formula(*value.lhs, agent_name, clause_index, atom_index);
                    collect_contract_formula(*value.rhs, agent_name, clause_index, atom_index);
                },
                [&](const auto &) {},
            },
            expr.node);
    }

    void collect_workflow_formula(const ir::TemporalExpr &expr,
                                  ir::FormalObservationScopeKind scope_kind,
                                  std::string_view workflow_name,
                                  std::size_t clause_index,
                                  std::size_t &atom_index) {
        std::visit(Overloaded{
                       [&](const ir::EmbeddedTemporalExpr &) {
                           add_embedded_observation(ir::FormalObservationScope{
                               .kind = scope_kind,
                               .owner = std::string(workflow_name),
                               .clause_index = clause_index,
                               .atom_index = atom_index++,
                           });
                       },
                       [&](const ir::TemporalUnaryExpr &value) {
                           collect_workflow_formula(
                               *value.operand, scope_kind, workflow_name, clause_index, atom_index);
                       },
                       [&](const ir::TemporalBinaryExpr &value) {
                           collect_workflow_formula(
                               *value.lhs, scope_kind, workflow_name, clause_index, atom_index);
                           collect_workflow_formula(
                               *value.rhs, scope_kind, workflow_name, clause_index, atom_index);
                       },
                       [&](const auto &) {},
                   },
                   expr.node);
    }

    void add_called_observation(std::string_view agent_name, std::string_view capability_name) {
        const auto symbol = called_observation_symbol(agent_name, capability_name);
        if (observation_index_by_symbol_.contains(symbol)) {
            return;
        }

        observation_index_by_symbol_.emplace(symbol, observations_.size());
        observations_.push_back(ir::FormalObservation{
            .symbol = symbol,
            .node =
                ir::CalledCapabilityObservation{
                    .agent = std::string(agent_name),
                    .capability = std::string(capability_name),
                },
        });
    }

    void add_embedded_observation(ir::FormalObservationScope scope) {
        const auto symbol = embedded_observation_symbol(scope);
        if (observation_index_by_symbol_.contains(symbol)) {
            return;
        }

        observation_index_by_symbol_.emplace(symbol, observations_.size());
        observations_.push_back(ir::FormalObservation{
            .symbol = symbol,
            .node =
                ir::EmbeddedBoolObservation{
                    .scope = std::move(scope),
                },
        });
    }
};

} // namespace

ir::Program lower_program_ir(const ast::Program &program,
                             const ResolveResult &resolve_result,
                             const TypeCheckResult &type_check_result) {
    IrLowerer lowerer(program, resolve_result, type_check_result);
    auto program_ir = lowerer.lower();
    program_ir.formal_observations = collect_formal_observations(program_ir);
    return program_ir;
}

ir::Program lower_program_ir(const SourceGraph &graph,
                             const ResolveResult &resolve_result,
                             const TypeCheckResult &type_check_result) {
    IrLowerer lowerer(graph, resolve_result, type_check_result);
    auto program_ir = lowerer.lower();
    program_ir.formal_observations = collect_formal_observations(program_ir);
    return program_ir;
}

std::vector<ir::FormalObservation> collect_formal_observations(const ir::Program &program) {
    FormalObservationCollector collector;
    return collector.collect(program);
}

} // namespace ahfl
