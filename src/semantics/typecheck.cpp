#include "ahfl/semantics/typecheck.hpp"

#include "ahfl/frontend/frontend.hpp"

#include <algorithm>
#include <cctype>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ahfl {

namespace {

using BindingMap = std::unordered_map<std::string, TypePtr>;

enum class CallContext {
    PureOnly,
    Flow,
    Workflow,
};

struct TypedValue {
    TypePtr type;
    bool is_pure{true};
};

struct ValueContext {
    BindingMap bindings;
    CallContext call_context{CallContext::PureOnly};
    std::optional<SymbolId> current_agent;
};

[[nodiscard]] bool same_range(SourceRange lhs, SourceRange rhs) noexcept {
    return lhs.begin_offset == rhs.begin_offset && lhs.end_offset == rhs.end_offset;
}

[[nodiscard]] TypePtr make_any_type() {
    return Type::make(TypeKind::Any);
}

[[nodiscard]] TypePtr clone_or_any(MaybeCRef<Type> type) {
    if (!type.has_value()) {
        return make_any_type();
    }

    return type->get().clone();
}

[[nodiscard]] bool is_error_type(const Type &type) noexcept {
    return type.kind == TypeKind::Any || type.kind == TypeKind::Never;
}

[[nodiscard]] bool is_bool_type(const Type &type) noexcept {
    return type.kind == TypeKind::Bool;
}

[[nodiscard]] bool is_numeric_type(const Type &type) noexcept {
    return type.kind == TypeKind::Int || type.kind == TypeKind::Float ||
           type.kind == TypeKind::Decimal;
}

[[nodiscard]] std::int64_t parse_decimal_scale(std::string_view text) {
    const auto dot_index = text.find('.');
    if (dot_index == std::string_view::npos) {
        return 0;
    }

    std::int64_t scale = 0;
    for (std::size_t index = dot_index + 1; index < text.size(); ++index) {
        if (!std::isdigit(static_cast<unsigned char>(text[index]))) {
            break;
        }

        ++scale;
    }

    return scale;
}

[[nodiscard]] BindingMap clone_bindings(const BindingMap &bindings) {
    BindingMap result;
    result.reserve(bindings.size());

    for (const auto &[name, type] : bindings) {
        result.emplace(name, type ? type->clone() : make_any_type());
    }

    return result;
}

[[nodiscard]] MaybeCRef<Type> find_binding(const BindingMap &bindings, std::string_view name) {
    if (const auto iter = bindings.find(std::string(name));
        iter != bindings.end() && static_cast<bool>(iter->second)) {
        return std::cref(*iter->second);
    }

    return std::nullopt;
}

template <typename T>
[[nodiscard]] MaybeCRef<T>
find_decl_ref(const std::unordered_map<std::size_t, std::reference_wrapper<const T>> &map,
              SymbolId id) {
    if (const auto iter = map.find(id.value); iter != map.end()) {
        return std::cref(iter->second.get());
    }

    return std::nullopt;
}
} // namespace

class TypeCheckPass final {
  public:
    TypeCheckPass(const ast::Program &program, const ResolveResult &resolve_result)
        : program_(&program), resolve_result_(resolve_result) {}
    TypeCheckPass(const SourceGraph &graph, const ResolveResult &resolve_result)
        : graph_(&graph), resolve_result_(resolve_result) {}

    [[nodiscard]] TypeCheckResult run();

  private:
    const ast::Program *program_{nullptr};
    const SourceGraph *graph_{nullptr};
    const ResolveResult &resolve_result_;
    TypeCheckResult result_;
    const SourceUnit *current_source_{nullptr};
    std::optional<SourceId> current_source_id_;
    std::string current_module_name_;

    std::unordered_map<std::size_t, std::reference_wrapper<const ast::TypeAliasDecl>>
        type_alias_decls_;
    std::unordered_map<std::size_t, std::reference_wrapper<const ast::ConstDecl>> const_decls_;
    std::unordered_map<std::size_t, std::reference_wrapper<const ast::StructDecl>> struct_decls_;
    std::unordered_map<std::size_t, std::reference_wrapper<const ast::EnumDecl>> enum_decls_;
    std::unordered_map<std::size_t, std::reference_wrapper<const ast::CapabilityDecl>>
        capability_decls_;
    std::unordered_map<std::size_t, std::reference_wrapper<const ast::PredicateDecl>>
        predicate_decls_;
    std::unordered_map<std::size_t, std::reference_wrapper<const ast::AgentDecl>> agent_decls_;
    std::unordered_map<std::size_t, std::reference_wrapper<const ast::WorkflowDecl>>
        workflow_decls_;

    std::unordered_map<std::size_t, TypePtr> resolved_alias_types_;
    std::unordered_set<std::size_t> active_aliases_;

    void index_program_declarations(const ast::Program &program);
    void index_declarations();
    void build_type_environment();
    void build_const_types();
    void build_struct_types();
    void build_enum_types();
    void build_capability_types();
    void build_predicate_types();
    void build_agent_types();
    void build_workflow_types();

    void check_const_initializers_in_program(const ast::Program &program);
    void check_const_initializers();
    void check_struct_defaults();
    void check_contracts_in_program(const ast::Program &program);
    void check_contracts();
    void check_flows_in_program(const ast::Program &program);
    void check_flows();
    void check_workflows_in_program(const ast::Program &program);
    void check_workflows();
    void check_temporal_embedded_exprs(const ast::TemporalExprSyntax &expr,
                                       const ValueContext &context);

    void check_block(const ast::BlockSyntax &block,
                     ValueContext &context,
                     MaybeCRef<Type> expected_return_type,
                     std::string_view state_name = "");
    void check_statement(const ast::StatementSyntax &statement,
                         ValueContext &context,
                         MaybeCRef<Type> expected_return_type,
                         std::string_view state_name = "");

    void enter_source(const SourceUnit &source);
    void leave_source();
    [[nodiscard]] MaybeCRef<SourceUnit> source_unit_for(SourceId id) const;
    template <typename Fn> decltype(auto) with_symbol_context(SymbolId id, Fn &&fn);
    [[nodiscard]] MaybeCRef<Symbol> find_local_here(SymbolNamespace name_space,
                                                    std::string_view name) const;
    [[nodiscard]] MaybeCRef<ResolvedReference> find_reference_here(ReferenceKind kind,
                                                                   SourceRange range) const;
    void error_here(std::string message, SourceRange range);

    [[nodiscard]] MaybeCRef<Symbol> symbol_of(SymbolId id) const;
    [[nodiscard]] TypePtr resolve_type(const ast::TypeSyntax &type);
    [[nodiscard]] TypePtr resolve_named_type(const ast::QualifiedName &name);
    [[nodiscard]] TypePtr resolve_type_symbol(SymbolId id, SourceRange use_range);
    [[nodiscard]] TypePtr resolve_type_alias(SymbolId id, SourceRange use_range);

    [[nodiscard]] MaybeCRef<ast::TypeAliasDecl> alias_decl_of(SymbolId id) const;

    void remember_expression_type(const ast::ExprSyntax &expr, const TypedValue &typed);
    [[nodiscard]] bool check_assignable(const Type &source,
                                        const Type &target,
                                        SourceRange range,
                                        std::string_view context_label);

    [[nodiscard]] TypedValue check_expr(const ast::ExprSyntax &expr,
                                        const ValueContext &context,
                                        MaybeCRef<Type> expected_type = std::nullopt);
    [[nodiscard]] TypedValue check_expr_impl(const ast::ExprSyntax &expr,
                                             const ValueContext &context,
                                             MaybeCRef<Type> expected_type = std::nullopt);
    [[nodiscard]] TypedValue check_path(const ast::PathSyntax &path, const ValueContext &context);
    [[nodiscard]] TypedValue check_qualified_value(const ast::ExprSyntax &expr);
    [[nodiscard]] TypedValue check_call(const ast::ExprSyntax &expr, const ValueContext &context);
    [[nodiscard]] TypedValue check_struct_literal(const ast::ExprSyntax &expr,
                                                  const ValueContext &context);
    [[nodiscard]] TypedValue check_list_literal(const ast::ExprSyntax &expr,
                                                const ValueContext &context,
                                                MaybeCRef<Type> expected_type);
    [[nodiscard]] TypedValue check_set_literal(const ast::ExprSyntax &expr,
                                               const ValueContext &context,
                                               MaybeCRef<Type> expected_type);
    [[nodiscard]] TypedValue check_map_literal(const ast::ExprSyntax &expr,
                                               const ValueContext &context,
                                               MaybeCRef<Type> expected_type);
    [[nodiscard]] TypedValue check_unary_expr(const ast::ExprSyntax &expr,
                                              const ValueContext &context);
    [[nodiscard]] TypedValue check_binary_expr(const ast::ExprSyntax &expr,
                                               const ValueContext &context);
    [[nodiscard]] TypedValue check_member_access(const ast::ExprSyntax &expr,
                                                 const ValueContext &context);
    [[nodiscard]] TypedValue check_index_access(const ast::ExprSyntax &expr,
                                                const ValueContext &context);

    [[nodiscard]] TypePtr
    field_access(const Type &base_type, std::string_view field_name, SourceRange range);
    [[nodiscard]] TypedValue typed(TypePtr type, bool is_pure = true) const;
    [[nodiscard]] TypedValue error_typed(bool is_pure = true) const;
};

TypeCheckResult TypeCheckPass::run() {
    index_declarations();
    build_type_environment();
    check_const_initializers();
    check_struct_defaults();
    check_contracts();
    check_flows();
    check_workflows();
    return std::move(result_);
}

void TypeCheckPass::enter_source(const SourceUnit &source) {
    current_source_ = &source;
    current_source_id_ = source.id;
    current_module_name_ = source.module_name;
}

void TypeCheckPass::leave_source() {
    current_source_ = nullptr;
    current_source_id_.reset();
    current_module_name_.clear();
}

MaybeCRef<SourceUnit> TypeCheckPass::source_unit_for(SourceId id) const {
    if (graph_ == nullptr) {
        return std::nullopt;
    }

    for (const auto &source : graph_->sources) {
        if (source.id == id) {
            return std::cref(source);
        }
    }

    return std::nullopt;
}

template <typename Fn> decltype(auto) TypeCheckPass::with_symbol_context(SymbolId id, Fn &&fn) {
    const auto previous_source = current_source_;
    const auto previous_source_id = current_source_id_;
    const auto previous_module_name = current_module_name_;

    current_source_ = nullptr;
    current_source_id_.reset();
    current_module_name_.clear();

    if (const auto symbol = symbol_of(id); symbol.has_value()) {
        current_source_id_ = symbol->get().source_id;
        current_module_name_ = symbol->get().module_name;
        if (graph_ != nullptr && symbol->get().source_id.has_value()) {
            if (const auto source = source_unit_for(*symbol->get().source_id); source.has_value()) {
                current_source_ = &source->get();
            }
        }
    }

    using Result = std::invoke_result_t<Fn>;
    if constexpr (std::is_void_v<Result>) {
        std::forward<Fn>(fn)();
        current_source_ = previous_source;
        current_source_id_ = previous_source_id;
        current_module_name_ = previous_module_name;
    } else {
        Result result = std::forward<Fn>(fn)();
        current_source_ = previous_source;
        current_source_id_ = previous_source_id;
        current_module_name_ = previous_module_name;
        return result;
    }
}

MaybeCRef<Symbol> TypeCheckPass::find_local_here(SymbolNamespace name_space,
                                                 std::string_view name) const {
    if (!current_module_name_.empty()) {
        return resolve_result_.symbol_table.find_local(name_space, name, current_module_name_);
    }

    return resolve_result_.symbol_table.find_local(name_space, name);
}

MaybeCRef<ResolvedReference> TypeCheckPass::find_reference_here(ReferenceKind kind,
                                                                SourceRange range) const {
    return resolve_result_.find_reference(kind, range, current_source_id_);
}

void TypeCheckPass::error_here(std::string message, SourceRange range) {
    if (current_source_ != nullptr) {
        result_.diagnostics.error()
            .message(std::move(message))
            .range(range)
            .source(current_source_->source)
            .emit();
    } else {
        result_.diagnostics.error().message(std::move(message)).range(range).emit();
    }
}

void TypeCheckPass::index_program_declarations(const ast::Program &program) {
    for (const auto &declaration : program.declarations) {
        switch (declaration->kind) {
        case ast::NodeKind::ConstDecl: {
            const auto &decl = static_cast<const ast::ConstDecl &>(*declaration);
            if (const auto symbol = find_local_here(SymbolNamespace::Consts, decl.name);
                symbol.has_value()) {
                const_decls_.emplace(symbol->get().id.value, std::cref(decl));
            }
            break;
        }
        case ast::NodeKind::TypeAliasDecl: {
            const auto &decl = static_cast<const ast::TypeAliasDecl &>(*declaration);
            if (const auto symbol = find_local_here(SymbolNamespace::Types, decl.name);
                symbol.has_value()) {
                type_alias_decls_.emplace(symbol->get().id.value, std::cref(decl));
            }
            break;
        }
        case ast::NodeKind::StructDecl: {
            const auto &decl = static_cast<const ast::StructDecl &>(*declaration);
            if (const auto symbol = find_local_here(SymbolNamespace::Types, decl.name);
                symbol.has_value()) {
                struct_decls_.emplace(symbol->get().id.value, std::cref(decl));
            }
            break;
        }
        case ast::NodeKind::EnumDecl: {
            const auto &decl = static_cast<const ast::EnumDecl &>(*declaration);
            if (const auto symbol = find_local_here(SymbolNamespace::Types, decl.name);
                symbol.has_value()) {
                enum_decls_.emplace(symbol->get().id.value, std::cref(decl));
            }
            break;
        }
        case ast::NodeKind::CapabilityDecl: {
            const auto &decl = static_cast<const ast::CapabilityDecl &>(*declaration);
            if (const auto symbol = find_local_here(SymbolNamespace::Capabilities, decl.name);
                symbol.has_value()) {
                capability_decls_.emplace(symbol->get().id.value, std::cref(decl));
            }
            break;
        }
        case ast::NodeKind::PredicateDecl: {
            const auto &decl = static_cast<const ast::PredicateDecl &>(*declaration);
            if (const auto symbol = find_local_here(SymbolNamespace::Predicates, decl.name);
                symbol.has_value()) {
                predicate_decls_.emplace(symbol->get().id.value, std::cref(decl));
            }
            break;
        }
        case ast::NodeKind::AgentDecl: {
            const auto &decl = static_cast<const ast::AgentDecl &>(*declaration);
            if (const auto symbol = find_local_here(SymbolNamespace::Agents, decl.name);
                symbol.has_value()) {
                agent_decls_.emplace(symbol->get().id.value, std::cref(decl));
            }
            break;
        }
        case ast::NodeKind::WorkflowDecl: {
            const auto &decl = static_cast<const ast::WorkflowDecl &>(*declaration);
            if (const auto symbol = find_local_here(SymbolNamespace::Workflows, decl.name);
                symbol.has_value()) {
                workflow_decls_.emplace(symbol->get().id.value, std::cref(decl));
            }
            break;
        }
        case ast::NodeKind::Program:
        case ast::NodeKind::ModuleDecl:
        case ast::NodeKind::ImportDecl:
        case ast::NodeKind::ContractDecl:
        case ast::NodeKind::FlowDecl:
            break;
        }
    }
}

void TypeCheckPass::index_declarations() {
    if (graph_ != nullptr) {
        for (const auto &source : graph_->sources) {
            enter_source(source);
            index_program_declarations(
                require(source.program.get(), "source graph program must exist before typecheck"));
            leave_source();
        }
        return;
    }

    index_program_declarations(require(program_, "typecheck program must exist"));
}

MaybeCRef<Symbol> TypeCheckPass::symbol_of(SymbolId id) const {
    return resolve_result_.symbol_table.get(id);
}

MaybeCRef<ast::TypeAliasDecl> TypeCheckPass::alias_decl_of(SymbolId id) const {
    return find_decl_ref(type_alias_decls_, id);
}

void TypeCheckPass::build_type_environment() {
    build_const_types();
    build_struct_types();
    build_enum_types();
    build_capability_types();
    build_predicate_types();
    build_agent_types();
    build_workflow_types();
}

void TypeCheckPass::build_const_types() {
    for (const auto &[id, decl] : const_decls_) {
        with_symbol_context(SymbolId{id}, [&]() {
            if (!decl.get().type) {
                return;
            }

            result_.environment.const_types_.emplace(id, resolve_type(*decl.get().type));
        });
    }
}

void TypeCheckPass::build_struct_types() {
    for (const auto &[id, decl] : struct_decls_) {
        with_symbol_context(SymbolId{id}, [&]() {
            const auto symbol = symbol_of(SymbolId{id});
            if (!symbol.has_value()) {
                return;
            }

            StructTypeInfo info{
                .symbol = SymbolId{id},
                .canonical_name = symbol->get().canonical_name,
                .fields = {},
                .declaration_range = decl.get().range,
            };

            for (const auto &field : decl.get().fields) {
                info.fields.push_back(StructFieldInfo{
                    .name = field->name,
                    .type = resolve_type(*field->type),
                    .has_default = static_cast<bool>(field->default_value),
                    .declaration_range = field->range,
                });
            }

            result_.environment.structs_.emplace(id, std::move(info));
        });
    }
}

void TypeCheckPass::build_enum_types() {
    for (const auto &[id, decl] : enum_decls_) {
        with_symbol_context(SymbolId{id}, [&]() {
            const auto symbol = symbol_of(SymbolId{id});
            if (!symbol.has_value()) {
                return;
            }

            EnumTypeInfo info{
                .symbol = SymbolId{id},
                .canonical_name = symbol->get().canonical_name,
                .variants = {},
                .declaration_range = decl.get().range,
            };

            for (const auto &variant : decl.get().variants) {
                info.variants.push_back(EnumVariantInfo{
                    .name = variant->name,
                    .declaration_range = variant->range,
                });
            }

            result_.environment.enums_.emplace(id, std::move(info));
        });
    }
}

void TypeCheckPass::build_capability_types() {
    for (const auto &[id, decl] : capability_decls_) {
        with_symbol_context(SymbolId{id}, [&]() {
            const auto symbol = symbol_of(SymbolId{id});
            if (!symbol.has_value()) {
                return;
            }

            CapabilityTypeInfo info{
                .symbol = SymbolId{id},
                .canonical_name = symbol->get().canonical_name,
                .params = {},
                .return_type = nullptr,
                .declaration_range = decl.get().range,
            };

            for (const auto &param : decl.get().params) {
                info.params.push_back(ParamTypeInfo{
                    .name = param->name,
                    .type = resolve_type(*param->type),
                    .declaration_range = param->range,
                });
            }

            info.return_type = resolve_type(*decl.get().return_type);
            result_.environment.capabilities_.emplace(id, std::move(info));
        });
    }
}

void TypeCheckPass::build_predicate_types() {
    for (const auto &[id, decl] : predicate_decls_) {
        with_symbol_context(SymbolId{id}, [&]() {
            const auto symbol = symbol_of(SymbolId{id});
            if (!symbol.has_value()) {
                return;
            }

            PredicateTypeInfo info{
                .symbol = SymbolId{id},
                .canonical_name = symbol->get().canonical_name,
                .params = {},
                .declaration_range = decl.get().range,
            };

            for (const auto &param : decl.get().params) {
                info.params.push_back(ParamTypeInfo{
                    .name = param->name,
                    .type = resolve_type(*param->type),
                    .declaration_range = param->range,
                });
            }

            result_.environment.predicates_.emplace(id, std::move(info));
        });
    }
}

void TypeCheckPass::build_agent_types() {
    for (const auto &[id, decl] : agent_decls_) {
        with_symbol_context(SymbolId{id}, [&]() {
            const auto symbol = symbol_of(SymbolId{id});
            if (!symbol.has_value()) {
                return;
            }

            AgentTypeInfo info{
                .symbol = SymbolId{id},
                .canonical_name = symbol->get().canonical_name,
                .input_type = resolve_type(*decl.get().input_type),
                .context_type = resolve_type(*decl.get().context_type),
                .output_type = resolve_type(*decl.get().output_type),
                .capability_symbols = {},
                .declaration_range = decl.get().range,
            };

            if (info.input_type && info.input_type->kind != TypeKind::Struct) {
                error_here("agent input type must resolve to a struct type",
                           decl.get().input_type->range);
            }

            if (info.context_type && info.context_type->kind != TypeKind::Struct) {
                error_here("agent context type must resolve to a struct type",
                           decl.get().context_type->range);
            }

            if (info.output_type && info.output_type->kind != TypeKind::Struct) {
                error_here("agent output type must resolve to a struct type",
                           decl.get().output_type->range);
            }

            for (const auto &capability_name : decl.get().capabilities) {
                const auto capability_symbol =
                    find_local_here(SymbolNamespace::Capabilities, capability_name);
                if (!capability_symbol.has_value()) {
                    error_here("unknown capability '" + capability_name +
                                   "' in agent capability list",
                               decl.get().range);
                    continue;
                }

                info.capability_symbols.push_back(capability_symbol->get().id);
            }

            result_.environment.agents_.emplace(id, std::move(info));
        });
    }
}

void TypeCheckPass::build_workflow_types() {
    for (const auto &[id, decl] : workflow_decls_) {
        with_symbol_context(SymbolId{id}, [&]() {
            const auto symbol = symbol_of(SymbolId{id});
            if (!symbol.has_value()) {
                return;
            }

            WorkflowTypeInfo info{
                .symbol = SymbolId{id},
                .canonical_name = symbol->get().canonical_name,
                .input_type = resolve_type(*decl.get().input_type),
                .output_type = resolve_type(*decl.get().output_type),
                .declaration_range = decl.get().range,
            };

            if (info.input_type && info.input_type->kind != TypeKind::Struct) {
                error_here("workflow input type must resolve to a struct type",
                           decl.get().input_type->range);
            }

            if (info.output_type && info.output_type->kind != TypeKind::Struct) {
                error_here("workflow output type must resolve to a struct type",
                           decl.get().output_type->range);
            }

            result_.environment.workflows_.emplace(id, std::move(info));
        });
    }
}

TypePtr TypeCheckPass::resolve_type(const ast::TypeSyntax &type) {
    switch (type.kind) {
    case ast::TypeSyntaxKind::Unit:
        return Type::make(TypeKind::Unit);
    case ast::TypeSyntaxKind::Bool:
        return Type::make(TypeKind::Bool);
    case ast::TypeSyntaxKind::Int:
        return Type::make(TypeKind::Int);
    case ast::TypeSyntaxKind::Float:
        return Type::make(TypeKind::Float);
    case ast::TypeSyntaxKind::String:
        return Type::string();
    case ast::TypeSyntaxKind::BoundedString:
        return Type::bounded_string(type.string_bounds->first, type.string_bounds->second);
    case ast::TypeSyntaxKind::UUID:
        return Type::make(TypeKind::UUID);
    case ast::TypeSyntaxKind::Timestamp:
        return Type::make(TypeKind::Timestamp);
    case ast::TypeSyntaxKind::Duration:
        return Type::make(TypeKind::Duration);
    case ast::TypeSyntaxKind::Decimal:
        return Type::decimal(*type.decimal_scale);
    case ast::TypeSyntaxKind::Named:
        return resolve_named_type(*type.name);
    case ast::TypeSyntaxKind::Optional:
        return Type::optional(resolve_type(*type.first));
    case ast::TypeSyntaxKind::List:
        return Type::list(resolve_type(*type.first));
    case ast::TypeSyntaxKind::Set:
        return Type::set(resolve_type(*type.first));
    case ast::TypeSyntaxKind::Map:
        return Type::map(resolve_type(*type.first), resolve_type(*type.second));
    }

    return make_any_type();
}

TypePtr TypeCheckPass::resolve_named_type(const ast::QualifiedName &name) {
    const auto reference = find_reference_here(ReferenceKind::TypeName, name.range);
    if (!reference.has_value()) {
        error_here("unable to resolve type '" + name.spelling() + "'", name.range);
        return make_any_type();
    }

    return resolve_type_symbol(reference->get().target, name.range);
}

TypePtr TypeCheckPass::resolve_type_symbol(SymbolId id, SourceRange use_range) {
    const auto symbol = symbol_of(id);
    if (!symbol.has_value()) {
        error_here("resolved type symbol is missing", use_range);
        return make_any_type();
    }

    switch (symbol->get().kind) {
    case SymbolKind::Struct:
        return Type::struct_type(symbol->get().canonical_name);
    case SymbolKind::Enum:
        return Type::enum_type(symbol->get().canonical_name);
    case SymbolKind::TypeAlias:
        return resolve_type_alias(id, use_range);
    case SymbolKind::Const:
    case SymbolKind::Capability:
    case SymbolKind::Predicate:
    case SymbolKind::Agent:
    case SymbolKind::Workflow:
        error_here("symbol '" + symbol->get().canonical_name + "' does not name a type", use_range);
        return make_any_type();
    }

    return make_any_type();
}

TypePtr TypeCheckPass::resolve_type_alias(SymbolId id, SourceRange use_range) {
    if (const auto cached = resolved_alias_types_.find(id.value);
        cached != resolved_alias_types_.end()) {
        return cached->second ? cached->second->clone() : make_any_type();
    }

    if (!active_aliases_.insert(id.value).second) {
        error_here("type alias cycle reached during type resolution", use_range);
        return make_any_type();
    }

    const auto alias_decl = alias_decl_of(id);
    if (!alias_decl.has_value()) {
        active_aliases_.erase(id.value);
        error_here("type alias declaration is missing", use_range);
        return make_any_type();
    }

    auto resolved =
        with_symbol_context(id, [&]() { return resolve_type(*alias_decl->get().aliased_type); });
    resolved_alias_types_.emplace(id.value, resolved ? resolved->clone() : make_any_type());
    active_aliases_.erase(id.value);
    return resolved;
}

void TypeCheckPass::remember_expression_type(const ast::ExprSyntax &expr, const TypedValue &typed) {
    auto &expression_types = result_.mutable_expression_types();
    for (auto &entry : expression_types) {
        if (same_range(entry.range, expr.range) && entry.source_id == current_source_id_) {
            entry.type = typed.type ? typed.type->clone() : make_any_type();
            entry.is_pure = typed.is_pure;
            return;
        }
    }

    expression_types.push_back(ExpressionTypeInfo{
        .range = expr.range,
        .source_id = current_source_id_,
        .type = typed.type ? typed.type->clone() : make_any_type(),
        .is_pure = typed.is_pure,
    });
}

bool TypeCheckPass::check_assignable(const Type &source,
                                     const Type &target,
                                     SourceRange range,
                                     std::string_view context_label) {
    if (is_error_type(source) || is_error_type(target)) {
        return true;
    }

    if (is_subtype_of(source, target)) {
        return true;
    }

    error_here("type mismatch in " + std::string(context_label) + ": expected " +
                   target.describe() + ", got " + source.describe(),
               range);
    return false;
}

TypedValue TypeCheckPass::typed(TypePtr type, bool is_pure) const {
    return TypedValue{
        .type = std::move(type),
        .is_pure = is_pure,
    };
}

TypedValue TypeCheckPass::error_typed(bool is_pure) const {
    return TypedValue{
        .type = make_any_type(),
        .is_pure = is_pure,
    };
}

TypePtr
TypeCheckPass::field_access(const Type &base_type, std::string_view field_name, SourceRange range) {
    if (is_error_type(base_type)) {
        return make_any_type();
    }

    if (base_type.kind != TypeKind::Struct) {
        error_here("member access requires a struct value, got " + base_type.describe(), range);
        return make_any_type();
    }

    const auto struct_info = result_.environment.find_struct(base_type.name);
    if (!struct_info.has_value()) {
        error_here("unknown struct type '" + base_type.name + "'", range);
        return make_any_type();
    }

    const auto field = struct_info->get().find_field(field_name);
    if (!field.has_value()) {
        error_here("unknown field '" + std::string(field_name) + "' on struct '" + base_type.name +
                       "'",
                   range);
        return make_any_type();
    }

    return field->get().type ? field->get().type->clone() : make_any_type();
}

TypedValue TypeCheckPass::check_expr(const ast::ExprSyntax &expr,
                                     const ValueContext &context,
                                     MaybeCRef<Type> expected_type) {
    auto value = check_expr_impl(expr, context, expected_type);
    remember_expression_type(expr, value);
    return value;
}

TypedValue TypeCheckPass::check_expr_impl(const ast::ExprSyntax &expr,
                                          const ValueContext &context,
                                          MaybeCRef<Type> expected_type) {
    switch (expr.kind) {
    case ast::ExprSyntaxKind::BoolLiteral:
        return typed(Type::make(TypeKind::Bool));
    case ast::ExprSyntaxKind::IntegerLiteral:
        return typed(Type::make(TypeKind::Int));
    case ast::ExprSyntaxKind::FloatLiteral:
        return typed(Type::make(TypeKind::Float));
    case ast::ExprSyntaxKind::DecimalLiteral:
        return typed(Type::decimal(parse_decimal_scale(expr.text)));
    case ast::ExprSyntaxKind::StringLiteral:
        return typed(Type::string());
    case ast::ExprSyntaxKind::DurationLiteral:
        return typed(Type::make(TypeKind::Duration));
    case ast::ExprSyntaxKind::NoneLiteral:
        if (expected_type.has_value() && expected_type->get().kind == TypeKind::Optional) {
            return typed(expected_type->get().clone());
        }

        error_here("cannot infer type of 'none' without an expected Optional<T> context",
                   expr.range);
        return error_typed();
    case ast::ExprSyntaxKind::Some: {
        MaybeCRef<Type> inner_expected = std::nullopt;
        if (expected_type.has_value() && expected_type->get().kind == TypeKind::Optional &&
            expected_type->get().first) {
            inner_expected = std::cref(*expected_type->get().first);
        }

        const auto inner = check_expr(*expr.first, context, inner_expected);
        return typed(Type::optional(inner.type ? inner.type->clone() : make_any_type()),
                     inner.is_pure);
    }
    case ast::ExprSyntaxKind::Path:
        return check_path(*expr.path, context);
    case ast::ExprSyntaxKind::QualifiedValue:
        return check_qualified_value(expr);
    case ast::ExprSyntaxKind::Call:
        return check_call(expr, context);
    case ast::ExprSyntaxKind::StructLiteral:
        return check_struct_literal(expr, context);
    case ast::ExprSyntaxKind::ListLiteral:
        return check_list_literal(expr, context, expected_type);
    case ast::ExprSyntaxKind::SetLiteral:
        return check_set_literal(expr, context, expected_type);
    case ast::ExprSyntaxKind::MapLiteral:
        return check_map_literal(expr, context, expected_type);
    case ast::ExprSyntaxKind::Unary:
        return check_unary_expr(expr, context);
    case ast::ExprSyntaxKind::Binary:
        return check_binary_expr(expr, context);
    case ast::ExprSyntaxKind::MemberAccess:
        return check_member_access(expr, context);
    case ast::ExprSyntaxKind::IndexAccess:
        return check_index_access(expr, context);
    case ast::ExprSyntaxKind::Group:
        return check_expr(*expr.first, context, expected_type);
    }

    return error_typed();
}

TypedValue TypeCheckPass::check_path(const ast::PathSyntax &path, const ValueContext &context) {
    const auto root = find_binding(context.bindings, path.root_name);
    if (!root.has_value()) {
        error_here("unknown value '" + path.root_name + "'", path.range);
        return error_typed();
    }

    auto current = root->get().clone();
    for (const auto &member : path.members) {
        current = field_access(*current, member, path.range);
    }

    return typed(std::move(current));
}

TypedValue TypeCheckPass::check_qualified_value(const ast::ExprSyntax &expr) {
    if (const auto const_reference =
            find_reference_here(ReferenceKind::ConstValue, expr.qualified_name->range);
        const_reference.has_value()) {
        const auto const_type = result_.environment.get_const_type(const_reference->get().target);
        if (!const_type.has_value()) {
            error_here("constant type is missing for '" + expr.qualified_name->spelling() + "'",
                       expr.range);
            return error_typed();
        }

        return typed(const_type->get().clone());
    }

    const auto owner_reference =
        find_reference_here(ReferenceKind::QualifiedValueOwnerType, expr.qualified_name->range);
    if (!owner_reference.has_value()) {
        error_here("unknown qualified value '" + expr.qualified_name->spelling() + "'", expr.range);
        return error_typed();
    }

    auto owner_type = resolve_type_symbol(owner_reference->get().target, expr.range);
    if (!owner_type || owner_type->kind != TypeKind::Enum) {
        error_here("qualified value '" + expr.qualified_name->spelling() +
                       "' must refer to a constant or enum variant",
                   expr.range);
        return error_typed();
    }

    const auto enum_info = result_.environment.find_enum(owner_type->name);
    if (!enum_info.has_value()) {
        error_here("enum type '" + owner_type->name + "' is missing", expr.range);
        return error_typed();
    }

    const auto &segments = expr.qualified_name->segments;
    if (segments.empty() || !enum_info->get().has_variant(segments.back())) {
        error_here("unknown enum variant '" + expr.qualified_name->spelling() + "'", expr.range);
        return error_typed();
    }

    return typed(std::move(owner_type));
}

TypedValue TypeCheckPass::check_call(const ast::ExprSyntax &expr, const ValueContext &context) {
    const auto reference =
        find_reference_here(ReferenceKind::CallTarget, expr.qualified_name->range);
    if (!reference.has_value()) {
        error_here("unknown callable '" + expr.qualified_name->spelling() + "'", expr.range);
        return error_typed(false);
    }

    const auto symbol = symbol_of(reference->get().target);
    if (!symbol.has_value()) {
        error_here("call target symbol is missing", expr.range);
        return error_typed(false);
    }

    if (symbol->get().kind == SymbolKind::Capability) {
        const auto capability = result_.environment.get_capability(reference->get().target);
        if (!capability.has_value()) {
            error_here("capability type info is missing for '" + expr.qualified_name->spelling() +
                           "'",
                       expr.range);
            return error_typed(false);
        }

        if (context.call_context != CallContext::Flow) {
            error_here("capability call '" + expr.qualified_name->spelling() +
                           "' is not allowed in this context",
                       expr.range);
        }

        if (context.current_agent.has_value()) {
            const auto agent_info = result_.environment.get_agent(*context.current_agent);
            if (agent_info.has_value()) {
                const auto allowed = std::find(agent_info->get().capability_symbols.begin(),
                                               agent_info->get().capability_symbols.end(),
                                               reference->get().target);
                if (allowed == agent_info->get().capability_symbols.end()) {
                    error_here("capability '" + expr.qualified_name->spelling() +
                                   "' is not declared in the current agent capabilities",
                               expr.range);
                }
            }
        }

        if (expr.items.size() != capability->get().params.size()) {
            error_here("capability '" + expr.qualified_name->spelling() + "' expects " +
                           std::to_string(capability->get().params.size()) + " argument(s), got " +
                           std::to_string(expr.items.size()),
                       expr.range);
        }

        const auto limit = std::min(expr.items.size(), capability->get().params.size());
        for (std::size_t index = 0; index < limit; ++index) {
            const auto argument = check_expr(
                *expr.items[index], context, std::cref(*capability->get().params[index].type));
            (void)check_assignable(*argument.type,
                                   *capability->get().params[index].type,
                                   expr.items[index]->range,
                                   "capability argument");
        }

        return typed(capability->get().return_type ? capability->get().return_type->clone()
                                                   : make_any_type(),
                     false);
    }

    const auto predicate = result_.environment.get_predicate(reference->get().target);
    if (!predicate.has_value()) {
        error_here("predicate type info is missing for '" + expr.qualified_name->spelling() + "'",
                   expr.range);
        return error_typed();
    }

    if (expr.items.size() != predicate->get().params.size()) {
        error_here("predicate '" + expr.qualified_name->spelling() + "' expects " +
                       std::to_string(predicate->get().params.size()) + " argument(s), got " +
                       std::to_string(expr.items.size()),
                   expr.range);
    }

    bool is_pure = true;
    const auto limit = std::min(expr.items.size(), predicate->get().params.size());
    for (std::size_t index = 0; index < limit; ++index) {
        const auto argument = check_expr(
            *expr.items[index], context, std::cref(*predicate->get().params[index].type));
        if (!argument.is_pure) {
            error_here("predicate arguments must be pure expressions", expr.items[index]->range);
            is_pure = false;
        }

        (void)check_assignable(*argument.type,
                               *predicate->get().params[index].type,
                               expr.items[index]->range,
                               "predicate argument");
    }

    return typed(Type::make(TypeKind::Bool), is_pure);
}

TypedValue TypeCheckPass::check_struct_literal(const ast::ExprSyntax &expr,
                                               const ValueContext &context) {
    const auto reference = find_reference_here(ReferenceKind::TypeName, expr.qualified_name->range);
    if (!reference.has_value()) {
        error_here("unknown struct type '" + expr.qualified_name->spelling() + "'", expr.range);
        return error_typed();
    }

    auto struct_type = resolve_type_symbol(reference->get().target, expr.range);
    if (!struct_type || struct_type->kind != TypeKind::Struct) {
        error_here("struct literal target '" + expr.qualified_name->spelling() +
                       "' does not resolve to a struct type",
                   expr.range);
        return error_typed();
    }

    const auto struct_info = result_.environment.find_struct(struct_type->name);
    if (!struct_info.has_value()) {
        error_here("unknown struct type '" + struct_type->name + "'", expr.range);
        return error_typed();
    }

    std::unordered_set<std::string> seen_fields;
    bool is_pure = true;

    for (const auto &field_init : expr.struct_fields) {
        if (!seen_fields.insert(field_init->field_name).second) {
            error_here("duplicate field '" + field_init->field_name + "' in struct literal",
                       field_init->range);
            continue;
        }

        const auto field = struct_info->get().find_field(field_init->field_name);
        if (!field.has_value()) {
            error_here("unknown field '" + field_init->field_name + "' in struct literal for '" +
                           struct_type->name + "'",
                       field_init->range);
            continue;
        }

        const auto value = check_expr(*field_init->value, context, std::cref(*field->get().type));
        is_pure = is_pure && value.is_pure;
        (void)check_assignable(
            *value.type, *field->get().type, field_init->value->range, "struct field");
    }

    for (const auto &field : struct_info->get().fields) {
        if (!seen_fields.contains(field.name)) {
            error_here("missing field '" + field.name + "' in struct literal", expr.range);
        }
    }

    return typed(std::move(struct_type), is_pure);
}

TypedValue TypeCheckPass::check_list_literal(const ast::ExprSyntax &expr,
                                             const ValueContext &context,
                                             MaybeCRef<Type> expected_type) {
    MaybeCRef<Type> element_expected = std::nullopt;
    if (expected_type.has_value() && expected_type->get().kind == TypeKind::List &&
        expected_type->get().first) {
        element_expected = std::cref(*expected_type->get().first);
    }

    if (expr.items.empty()) {
        if (expected_type.has_value() && expected_type->get().kind == TypeKind::List) {
            return typed(expected_type->get().clone());
        }

        error_here("cannot infer type of empty list literal", expr.range);
        return typed(Type::list(make_any_type()));
    }

    auto element_type = clone_or_any(element_expected);
    bool have_element_type = element_expected.has_value();
    bool is_pure = true;

    for (const auto &item : expr.items) {
        const auto value = check_expr(*item, context, element_expected);
        is_pure = is_pure && value.is_pure;

        if (!have_element_type) {
            element_type = value.type ? value.type->clone() : make_any_type();
            have_element_type = true;
            continue;
        }

        (void)check_assignable(*value.type, *element_type, item->range, "list element");
    }

    return typed(Type::list(std::move(element_type)), is_pure);
}

TypedValue TypeCheckPass::check_set_literal(const ast::ExprSyntax &expr,
                                            const ValueContext &context,
                                            MaybeCRef<Type> expected_type) {
    MaybeCRef<Type> element_expected = std::nullopt;
    if (expected_type.has_value() && expected_type->get().kind == TypeKind::Set &&
        expected_type->get().first) {
        element_expected = std::cref(*expected_type->get().first);
    }

    if (expr.items.empty()) {
        if (expected_type.has_value() && expected_type->get().kind == TypeKind::Set) {
            return typed(expected_type->get().clone());
        }

        error_here("cannot infer type of empty set literal", expr.range);
        return typed(Type::set(make_any_type()));
    }

    auto element_type = clone_or_any(element_expected);
    bool have_element_type = element_expected.has_value();
    bool is_pure = true;

    for (const auto &item : expr.items) {
        const auto value = check_expr(*item, context, element_expected);
        is_pure = is_pure && value.is_pure;

        if (!have_element_type) {
            element_type = value.type ? value.type->clone() : make_any_type();
            have_element_type = true;
            continue;
        }

        (void)check_assignable(*value.type, *element_type, item->range, "set element");
    }

    return typed(Type::set(std::move(element_type)), is_pure);
}

TypedValue TypeCheckPass::check_map_literal(const ast::ExprSyntax &expr,
                                            const ValueContext &context,
                                            MaybeCRef<Type> expected_type) {
    MaybeCRef<Type> key_expected = std::nullopt;
    MaybeCRef<Type> value_expected = std::nullopt;
    if (expected_type.has_value() && expected_type->get().kind == TypeKind::Map &&
        expected_type->get().first && expected_type->get().second) {
        key_expected = std::cref(*expected_type->get().first);
        value_expected = std::cref(*expected_type->get().second);
    }

    if (expr.map_entries.empty()) {
        if (expected_type.has_value() && expected_type->get().kind == TypeKind::Map) {
            return typed(expected_type->get().clone());
        }

        error_here("cannot infer type of empty map literal", expr.range);
        return typed(Type::map(make_any_type(), make_any_type()));
    }

    auto key_type = clone_or_any(key_expected);
    auto value_type = clone_or_any(value_expected);
    bool have_key_type = key_expected.has_value();
    bool have_value_type = value_expected.has_value();
    bool is_pure = true;

    for (const auto &entry : expr.map_entries) {
        const auto key = check_expr(*entry->key, context, key_expected);
        const auto value = check_expr(*entry->value, context, value_expected);
        is_pure = is_pure && key.is_pure && value.is_pure;

        if (!have_key_type) {
            key_type = key.type ? key.type->clone() : make_any_type();
            have_key_type = true;
        } else {
            (void)check_assignable(*key.type, *key_type, entry->key->range, "map key");
        }

        if (!have_value_type) {
            value_type = value.type ? value.type->clone() : make_any_type();
            have_value_type = true;
        } else {
            (void)check_assignable(*value.type, *value_type, entry->value->range, "map value");
        }
    }

    return typed(Type::map(std::move(key_type), std::move(value_type)), is_pure);
}

TypedValue TypeCheckPass::check_unary_expr(const ast::ExprSyntax &expr,
                                           const ValueContext &context) {
    const auto operand = check_expr(*expr.first, context);
    switch (expr.unary_op) {
    case ast::ExprUnaryOp::Not:
        if (!is_bool_type(*operand.type) && !is_error_type(*operand.type)) {
            error_here("logical not requires Bool, got " + operand.type->describe(), expr.range);
        }
        return typed(Type::make(TypeKind::Bool), operand.is_pure);
    case ast::ExprUnaryOp::Negate:
    case ast::ExprUnaryOp::Positive:
        if (!is_numeric_type(*operand.type) && !is_error_type(*operand.type)) {
            error_here("numeric unary operator requires Int, Float, or Decimal, got " +
                           operand.type->describe(),
                       expr.range);
        }
        return typed(operand.type ? operand.type->clone() : make_any_type(), operand.is_pure);
    }

    return error_typed(operand.is_pure);
}

TypedValue TypeCheckPass::check_binary_expr(const ast::ExprSyntax &expr,
                                            const ValueContext &context) {
    const auto lhs = check_expr(*expr.first, context);
    const auto rhs = check_expr(*expr.second, context);
    const bool is_pure = lhs.is_pure && rhs.is_pure;

    const auto comparable = [&]() {
        return is_error_type(*lhs.type) || is_error_type(*rhs.type) ||
               is_subtype_of(*lhs.type, *rhs.type) || is_subtype_of(*rhs.type, *lhs.type);
    };

    switch (expr.binary_op) {
    case ast::ExprBinaryOp::Implies:
    case ast::ExprBinaryOp::Or:
    case ast::ExprBinaryOp::And:
        if ((!is_bool_type(*lhs.type) || !is_bool_type(*rhs.type)) && !is_error_type(*lhs.type) &&
            !is_error_type(*rhs.type)) {
            error_here("logical operator requires Bool operands", expr.range);
        }
        return typed(Type::make(TypeKind::Bool), is_pure);
    case ast::ExprBinaryOp::Equal:
    case ast::ExprBinaryOp::NotEqual:
    case ast::ExprBinaryOp::Less:
    case ast::ExprBinaryOp::LessEqual:
    case ast::ExprBinaryOp::Greater:
    case ast::ExprBinaryOp::GreaterEqual:
        if (!comparable()) {
            error_here("comparison operands are not type-compatible: " + lhs.type->describe() +
                           " vs " + rhs.type->describe(),
                       expr.range);
        }
        return typed(Type::make(TypeKind::Bool), is_pure);
    case ast::ExprBinaryOp::Add:
        if (lhs.type->kind == TypeKind::String && rhs.type->kind == TypeKind::String) {
            return typed(Type::string(), is_pure);
        }

        if (lhs.type->kind == TypeKind::Decimal && rhs.type->kind == TypeKind::Decimal &&
            lhs.type->decimal_scale == rhs.type->decimal_scale) {
            return typed(lhs.type->clone(), is_pure);
        }

        if (lhs.type->kind == TypeKind::Int && rhs.type->kind == TypeKind::Int) {
            return typed(Type::make(TypeKind::Int), is_pure);
        }

        if (lhs.type->kind == TypeKind::Float && rhs.type->kind == TypeKind::Float) {
            return typed(Type::make(TypeKind::Float), is_pure);
        }

        if (!is_error_type(*lhs.type) && !is_error_type(*rhs.type)) {
            error_here("operator '+' is not defined for " + lhs.type->describe() + " and " +
                           rhs.type->describe(),
                       expr.range);
        }
        return error_typed(is_pure);
    case ast::ExprBinaryOp::Subtract:
        if (lhs.type->kind == TypeKind::Decimal && rhs.type->kind == TypeKind::Decimal &&
            lhs.type->decimal_scale == rhs.type->decimal_scale) {
            return typed(lhs.type->clone(), is_pure);
        }

        if (lhs.type->kind == TypeKind::Int && rhs.type->kind == TypeKind::Int) {
            return typed(Type::make(TypeKind::Int), is_pure);
        }

        if (lhs.type->kind == TypeKind::Float && rhs.type->kind == TypeKind::Float) {
            return typed(Type::make(TypeKind::Float), is_pure);
        }

        if (!is_error_type(*lhs.type) && !is_error_type(*rhs.type)) {
            error_here("operator '-' is not defined for " + lhs.type->describe() + " and " +
                           rhs.type->describe(),
                       expr.range);
        }
        return error_typed(is_pure);
    case ast::ExprBinaryOp::Multiply:
    case ast::ExprBinaryOp::Divide:
        if (lhs.type->kind == TypeKind::Int && rhs.type->kind == TypeKind::Int) {
            return typed(Type::make(TypeKind::Int), is_pure);
        }

        if (lhs.type->kind == TypeKind::Float && rhs.type->kind == TypeKind::Float) {
            return typed(Type::make(TypeKind::Float), is_pure);
        }

        if (!is_error_type(*lhs.type) && !is_error_type(*rhs.type)) {
            error_here("arithmetic operator is not defined for " + lhs.type->describe() + " and " +
                           rhs.type->describe(),
                       expr.range);
        }
        return error_typed(is_pure);
    case ast::ExprBinaryOp::Modulo:
        if (lhs.type->kind == TypeKind::Int && rhs.type->kind == TypeKind::Int) {
            return typed(Type::make(TypeKind::Int), is_pure);
        }

        if (!is_error_type(*lhs.type) && !is_error_type(*rhs.type)) {
            error_here("operator '%' requires Int operands", expr.range);
        }
        return error_typed(is_pure);
    }

    return error_typed(is_pure);
}

TypedValue TypeCheckPass::check_member_access(const ast::ExprSyntax &expr,
                                              const ValueContext &context) {
    const auto base = check_expr(*expr.first, context);
    return typed(field_access(*base.type, expr.name, expr.range), base.is_pure);
}

TypedValue TypeCheckPass::check_index_access(const ast::ExprSyntax &expr,
                                             const ValueContext &context) {
    const auto collection = check_expr(*expr.first, context);
    const auto index = check_expr(*expr.second, context);
    const bool is_pure = collection.is_pure && index.is_pure;

    if (collection.type->kind == TypeKind::List) {
        if (index.type->kind != TypeKind::Int && !is_error_type(*index.type)) {
            error_here("list index must have type Int", expr.second->range);
        }

        if (collection.type->first) {
            return typed(collection.type->first->clone(), is_pure);
        }

        return error_typed(is_pure);
    }

    if (collection.type->kind == TypeKind::Map) {
        if (collection.type->first) {
            (void)check_assignable(
                *index.type, *collection.type->first, expr.second->range, "map index");
        }

        if (collection.type->second) {
            return typed(collection.type->second->clone(), is_pure);
        }

        return error_typed(is_pure);
    }

    if (!is_error_type(*collection.type)) {
        error_here("index access requires a List or Map value, got " + collection.type->describe(),
                   expr.range);
    }

    return error_typed(is_pure);
}

void TypeCheckPass::check_const_initializers_in_program(const ast::Program &program) {
    for (const auto &declaration : program.declarations) {
        if (declaration->kind != ast::NodeKind::ConstDecl) {
            continue;
        }

        const auto &decl = static_cast<const ast::ConstDecl &>(*declaration);
        const auto symbol = find_local_here(SymbolNamespace::Consts, decl.name);
        if (!symbol.has_value()) {
            continue;
        }

        const auto declared_type = result_.environment.get_const_type(symbol->get().id);
        if (!declared_type.has_value()) {
            continue;
        }

        const ValueContext context;
        const auto value = check_expr(*decl.value, context, declared_type);
        (void)check_assignable(
            *value.type, declared_type->get(), decl.value->range, "const initializer");
        if (!value.is_pure) {
            error_here("const initializer must be a pure expression", decl.value->range);
        }
    }
}

void TypeCheckPass::check_const_initializers() {
    if (graph_ != nullptr) {
        for (const auto &source : graph_->sources) {
            enter_source(source);
            check_const_initializers_in_program(
                require(source.program.get(), "source graph program must exist before typecheck"));
            leave_source();
        }
        return;
    }

    check_const_initializers_in_program(require(program_, "typecheck program must exist"));
}

void TypeCheckPass::check_struct_defaults() {
    for (const auto &[id, decl] : struct_decls_) {
        with_symbol_context(SymbolId{id}, [&]() {
            const auto struct_info = result_.environment.get_struct(SymbolId{id});
            if (!struct_info.has_value()) {
                return;
            }

            for (std::size_t index = 0; index < decl.get().fields.size(); ++index) {
                const auto &field_decl = decl.get().fields[index];
                if (!field_decl->default_value) {
                    continue;
                }

                const auto &field_info = struct_info->get().fields[index];
                const ValueContext context;
                const auto value =
                    check_expr(*field_decl->default_value, context, std::cref(*field_info.type));
                (void)check_assignable(*value.type,
                                       *field_info.type,
                                       field_decl->default_value->range,
                                       "struct field default");
                if (!value.is_pure) {
                    error_here("struct field default value must be pure",
                               field_decl->default_value->range);
                }
            }
        });
    }
}

void TypeCheckPass::check_contracts_in_program(const ast::Program &program) {
    for (const auto &declaration : program.declarations) {
        if (declaration->kind != ast::NodeKind::ContractDecl) {
            continue;
        }

        const auto &decl = static_cast<const ast::ContractDecl &>(*declaration);
        const auto target = find_reference_here(ReferenceKind::ContractTarget, decl.target->range);
        if (!target.has_value()) {
            continue;
        }

        const auto agent_info = result_.environment.get_agent(target->get().target);
        if (!agent_info.has_value()) {
            continue;
        }

        for (const auto &clause : decl.clauses) {
            if (!clause->expr && clause->temporal_expr) {
                ValueContext context;
                context.bindings.emplace("input",
                                         clone_or_any(std::cref(*agent_info->get().input_type)));
                check_temporal_embedded_exprs(*clause->temporal_expr, context);
                continue;
            }

            const auto bool_type = Type::make(TypeKind::Bool);
            ValueContext context;
            context.bindings.emplace("input",
                                     clone_or_any(std::cref(*agent_info->get().input_type)));
            if (clause->kind == ast::ContractClauseKind::Ensures) {
                context.bindings.emplace("output",
                                         clone_or_any(std::cref(*agent_info->get().output_type)));
            }

            const auto value = check_expr(*clause->expr, context, std::cref(*bool_type));
            if (!is_bool_type(*value.type) && !is_error_type(*value.type)) {
                error_here("contract expression must have type Bool", clause->expr->range);
            }
            if (!value.is_pure) {
                error_here("contract expression must be pure", clause->expr->range);
            }
        }
    }
}

void TypeCheckPass::check_contracts() {
    if (graph_ != nullptr) {
        for (const auto &source : graph_->sources) {
            enter_source(source);
            check_contracts_in_program(
                require(source.program.get(), "source graph program must exist before typecheck"));
            leave_source();
        }
        return;
    }

    check_contracts_in_program(require(program_, "typecheck program must exist"));
}

void TypeCheckPass::check_temporal_embedded_exprs(const ast::TemporalExprSyntax &expr,
                                                  const ValueContext &context) {
    switch (expr.kind) {
    case ast::TemporalExprSyntaxKind::EmbeddedExpr: {
        const auto bool_type = Type::make(TypeKind::Bool);
        const auto value = check_expr(*expr.expr, context, std::cref(*bool_type));
        if (!is_bool_type(*value.type) && !is_error_type(*value.type)) {
            error_here("temporal embedded expression must have type Bool", expr.expr->range);
        }
        if (!value.is_pure) {
            error_here("temporal embedded expression must be pure", expr.expr->range);
        }
        break;
    }
    case ast::TemporalExprSyntaxKind::Called:
    case ast::TemporalExprSyntaxKind::InState:
    case ast::TemporalExprSyntaxKind::Running:
    case ast::TemporalExprSyntaxKind::Completed:
        break;
    case ast::TemporalExprSyntaxKind::Unary:
        check_temporal_embedded_exprs(*expr.first, context);
        break;
    case ast::TemporalExprSyntaxKind::Binary:
        check_temporal_embedded_exprs(*expr.first, context);
        check_temporal_embedded_exprs(*expr.second, context);
        break;
    }
}

void TypeCheckPass::check_flows_in_program(const ast::Program &program) {
    for (const auto &declaration : program.declarations) {
        if (declaration->kind != ast::NodeKind::FlowDecl) {
            continue;
        }

        const auto &decl = static_cast<const ast::FlowDecl &>(*declaration);
        const auto target = find_reference_here(ReferenceKind::FlowTarget, decl.target->range);
        if (!target.has_value()) {
            continue;
        }

        const auto agent_info = result_.environment.get_agent(target->get().target);
        if (!agent_info.has_value()) {
            continue;
        }

        for (const auto &handler : decl.state_handlers) {
            ValueContext context;
            context.call_context = CallContext::Flow;
            context.current_agent = target->get().target;
            context.bindings.emplace("input",
                                     clone_or_any(std::cref(*agent_info->get().input_type)));
            context.bindings.emplace("ctx",
                                     clone_or_any(std::cref(*agent_info->get().context_type)));
            check_block(*handler->body,
                        context,
                        std::cref(*agent_info->get().output_type),
                        handler->state_name);
        }
    }
}

void TypeCheckPass::check_flows() {
    if (graph_ != nullptr) {
        for (const auto &source : graph_->sources) {
            enter_source(source);
            check_flows_in_program(
                require(source.program.get(), "source graph program must exist before typecheck"));
            leave_source();
        }
        return;
    }

    check_flows_in_program(require(program_, "typecheck program must exist"));
}

void TypeCheckPass::check_workflows_in_program(const ast::Program &program) {
    for (const auto &declaration : program.declarations) {
        if (declaration->kind != ast::NodeKind::WorkflowDecl) {
            continue;
        }

        const auto &decl = static_cast<const ast::WorkflowDecl &>(*declaration);
        const auto workflow_symbol = find_local_here(SymbolNamespace::Workflows, decl.name);
        if (!workflow_symbol.has_value()) {
            continue;
        }

        const auto workflow_info = result_.environment.get_workflow(workflow_symbol->get().id);
        if (!workflow_info.has_value()) {
            continue;
        }

        BindingMap all_node_outputs;

        for (const auto &node : decl.nodes) {
            const auto target =
                find_reference_here(ReferenceKind::WorkflowNodeTarget, node->target->range);
            if (!target.has_value()) {
                continue;
            }

            const auto agent_info = result_.environment.get_agent(target->get().target);
            if (!agent_info.has_value()) {
                continue;
            }

            all_node_outputs.emplace(node->name,
                                     clone_or_any(std::cref(*agent_info->get().output_type)));
        }

        for (const auto &node : decl.nodes) {
            const auto target =
                find_reference_here(ReferenceKind::WorkflowNodeTarget, node->target->range);
            if (!target.has_value()) {
                continue;
            }

            const auto agent_info = result_.environment.get_agent(target->get().target);
            if (!agent_info.has_value()) {
                continue;
            }

            ValueContext context;
            context.call_context = CallContext::Workflow;
            context.bindings.emplace("input",
                                     clone_or_any(std::cref(*workflow_info->get().input_type)));

            for (const auto &dependency : node->after) {
                const auto dependency_type = find_binding(all_node_outputs, dependency);
                if (dependency_type.has_value()) {
                    context.bindings.emplace(dependency, dependency_type->get().clone());
                }
            }

            const auto argument =
                check_expr(*node->input, context, std::cref(*agent_info->get().input_type));
            (void)check_assignable(*argument.type,
                                   *agent_info->get().input_type,
                                   node->input->range,
                                   "workflow node input");
        }

        ValueContext return_context;
        return_context.call_context = CallContext::Workflow;
        return_context.bindings.emplace("input",
                                        clone_or_any(std::cref(*workflow_info->get().input_type)));
        for (const auto &[name, type] : all_node_outputs) {
            return_context.bindings.emplace(name, type ? type->clone() : make_any_type());
        }

        for (const auto &formula : decl.safety) {
            check_temporal_embedded_exprs(*formula, return_context);
        }

        for (const auto &formula : decl.liveness) {
            check_temporal_embedded_exprs(*formula, return_context);
        }

        const auto return_value = check_expr(
            *decl.return_value, return_context, std::cref(*workflow_info->get().output_type));
        (void)check_assignable(*return_value.type,
                               *workflow_info->get().output_type,
                               decl.return_value->range,
                               "workflow return");
    }
}

void TypeCheckPass::check_workflows() {
    if (graph_ != nullptr) {
        for (const auto &source : graph_->sources) {
            enter_source(source);
            check_workflows_in_program(
                require(source.program.get(), "source graph program must exist before typecheck"));
            leave_source();
        }
        return;
    }

    check_workflows_in_program(require(program_, "typecheck program must exist"));
}

void TypeCheckPass::check_block(const ast::BlockSyntax &block,
                                ValueContext &context,
                                MaybeCRef<Type> expected_return_type,
                                std::string_view state_name) {
    for (const auto &statement : block.statements) {
        check_statement(*statement, context, expected_return_type, state_name);
    }
}

void TypeCheckPass::check_statement(const ast::StatementSyntax &statement,
                                    ValueContext &context,
                                    MaybeCRef<Type> expected_return_type,
                                    std::string_view state_name) {
    (void)state_name;

    switch (statement.kind) {
    case ast::StatementSyntaxKind::Let: {
        TypePtr annotated_type;
        MaybeCRef<Type> expected = std::nullopt;
        if (statement.let_stmt->type) {
            annotated_type = resolve_type(*statement.let_stmt->type);
            expected = std::cref(*annotated_type);
        }

        const auto initializer = check_expr(*statement.let_stmt->initializer, context, expected);
        if (expected.has_value()) {
            (void)check_assignable(*initializer.type,
                                   expected->get(),
                                   statement.let_stmt->initializer->range,
                                   "let initializer");
            context.bindings[statement.let_stmt->name] =
                annotated_type ? annotated_type->clone() : make_any_type();
        } else {
            context.bindings[statement.let_stmt->name] =
                initializer.type ? initializer.type->clone() : make_any_type();
        }
        break;
    }
    case ast::StatementSyntaxKind::Assign: {
        const auto target = check_path(*statement.assign_stmt->target, context);
        const auto value =
            check_expr(*statement.assign_stmt->value, context, std::cref(*target.type));
        (void)check_assignable(
            *value.type, *target.type, statement.assign_stmt->value->range, "assignment");
        break;
    }
    case ast::StatementSyntaxKind::If: {
        auto condition_context = ValueContext{
            .bindings = clone_bindings(context.bindings),
            .call_context = CallContext::PureOnly,
            .current_agent = context.current_agent,
        };
        const auto condition = check_expr(*statement.if_stmt->condition, condition_context);
        if (!is_bool_type(*condition.type) && !is_error_type(*condition.type)) {
            error_here("if condition must have type Bool", statement.if_stmt->condition->range);
        }
        if (!condition.is_pure) {
            error_here("if condition must be pure", statement.if_stmt->condition->range);
        }

        auto then_context = ValueContext{
            .bindings = clone_bindings(context.bindings),
            .call_context = context.call_context,
            .current_agent = context.current_agent,
        };
        check_block(*statement.if_stmt->then_block, then_context, expected_return_type, state_name);

        if (statement.if_stmt->else_block) {
            auto else_context = ValueContext{
                .bindings = clone_bindings(context.bindings),
                .call_context = context.call_context,
                .current_agent = context.current_agent,
            };
            check_block(
                *statement.if_stmt->else_block, else_context, expected_return_type, state_name);
        }
        break;
    }
    case ast::StatementSyntaxKind::Goto:
        break;
    case ast::StatementSyntaxKind::Return: {
        if (!expected_return_type.has_value()) {
            break;
        }

        const auto value = check_expr(*statement.return_stmt->value, context, expected_return_type);
        (void)check_assignable(*value.type,
                               expected_return_type->get(),
                               statement.return_stmt->value->range,
                               "return");
        break;
    }
    case ast::StatementSyntaxKind::Assert: {
        auto condition_context = ValueContext{
            .bindings = clone_bindings(context.bindings),
            .call_context = CallContext::PureOnly,
            .current_agent = context.current_agent,
        };
        const auto condition = check_expr(*statement.assert_stmt->condition, condition_context);
        if (!is_bool_type(*condition.type) && !is_error_type(*condition.type)) {
            error_here("assert condition must have type Bool",
                       statement.assert_stmt->condition->range);
        }
        if (!condition.is_pure) {
            error_here("assert condition must be pure", statement.assert_stmt->condition->range);
        }
        break;
    }
    case ast::StatementSyntaxKind::Expr:
        (void)check_expr(*statement.expr_stmt->expr, context);
        break;
    }
}

TypeCheckResult TypeChecker::check(const ast::Program &program,
                                   const ResolveResult &resolve_result) const {
    TypeCheckPass pass(program, resolve_result);
    return pass.run();
}

TypeCheckResult TypeChecker::check(const SourceGraph &graph,
                                   const ResolveResult &resolve_result) const {
    TypeCheckPass pass(graph, resolve_result);
    return pass.run();
}

} // namespace ahfl
