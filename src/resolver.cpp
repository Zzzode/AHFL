#include "ahfl/resolver.hpp"

#include <algorithm>
#include <functional>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ahfl {

namespace {

[[nodiscard]] std::string join_segments(const std::vector<std::string> &segments) {
    std::ostringstream builder;

    for (std::size_t index = 0; index < segments.size(); ++index) {
        if (index != 0) {
            builder << "::";
        }

        builder << segments[index];
    }

    return builder.str();
}

[[nodiscard]] std::string namespace_name(SymbolNamespace name_space) {
    switch (name_space) {
    case SymbolNamespace::Types:
        return "type";
    case SymbolNamespace::Consts:
        return "const";
    case SymbolNamespace::Capabilities:
        return "capability";
    case SymbolNamespace::Predicates:
        return "predicate";
    case SymbolNamespace::Agents:
        return "agent";
    case SymbolNamespace::Workflows:
        return "workflow";
    }

    return "symbol";
}

[[nodiscard]] ast::QualifiedName make_single_segment_name(std::string name, SourceRange range) {
    return ast::QualifiedName{
        .range = range,
        .segments = {std::move(name)},
    };
}

[[nodiscard]] ast::QualifiedName owner_name_of(const ast::QualifiedName &name) {
    ast::QualifiedName owner;
    owner.range = name.range;
    owner.segments.assign(name.segments.begin(), name.segments.end() - 1);
    return owner;
}

} // namespace

namespace detail {

class ResolverPass final : public ast::Visitor {
  public:
    [[nodiscard]] ResolveResult run(const ast::Program &program) {
        auto &mutable_program = const_cast<ast::Program &>(program);

        current_pass_ = Pass::CollectImports;
        mutable_program.accept(*this);

        current_pass_ = Pass::RegisterSymbols;
        mutable_program.accept(*this);

        current_pass_ = Pass::ResolveReferences;
        mutable_program.accept(*this);

        detect_type_alias_cycles();
        return std::move(result_);
    }

    void visit(ast::Program &node) override {
        for (auto &declaration : node.declarations) {
            declaration->accept(*this);
        }
    }

    void visit(ast::ModuleDecl &node) override {
        if (current_pass_ != Pass::CollectImports) {
            return;
        }

        const auto module_name = node.name->spelling();
        if (!module_name_.has_value()) {
            module_name_ = module_name;
            module_range_ = node.range;
            return;
        }

        result_.diagnostics.error(
            "multiple module declarations are not supported in one source file", node.range);
        result_.diagnostics.note("first module declaration is here", module_range_);
    }

    void visit(ast::ImportDecl &node) override {
        if (current_pass_ != Pass::CollectImports) {
            return;
        }

        if (node.alias.empty()) {
            result_.imports.push_back(ImportBinding{
                .alias = "",
                .target_module = node.path->spelling(),
                .declaration_range = node.range,
            });
            return;
        }

        if (const auto existing = import_alias_index_.find(node.alias);
            existing != import_alias_index_.end()) {
            result_.diagnostics.error("duplicate import alias '" + node.alias + "'", node.range);
            result_.diagnostics.note("previous import alias is here",
                                     result_.imports[existing->second].declaration_range);
            return;
        }

        import_alias_index_.emplace(node.alias, result_.imports.size());
        import_aliases_.emplace(node.alias, node.path->spelling());
        result_.imports.push_back(ImportBinding{
            .alias = node.alias,
            .target_module = node.path->spelling(),
            .declaration_range = node.range,
        });
    }

    void visit(ast::ConstDecl &node) override {
        if (current_pass_ == Pass::RegisterSymbols) {
            (void)register_symbol(
                SymbolNamespace::Consts, SymbolKind::Const, node.name, node.range);
            return;
        }

        if (current_pass_ == Pass::ResolveReferences) {
            resolve_type(*node.type);
            resolve_declaration_expr(*node.value);
        }
    }

    void visit(ast::TypeAliasDecl &node) override {
        if (current_pass_ == Pass::RegisterSymbols) {
            (void)register_symbol(
                SymbolNamespace::Types, SymbolKind::TypeAlias, node.name, node.range);
            return;
        }

        if (current_pass_ == Pass::ResolveReferences) {
            current_type_alias_ = find_registered_symbol(SymbolNamespace::Types, node.name);
            resolve_type(*node.aliased_type);
            current_type_alias_.reset();
        }
    }

    void visit(ast::StructDecl &node) override {
        if (current_pass_ == Pass::RegisterSymbols) {
            (void)register_symbol(
                SymbolNamespace::Types, SymbolKind::Struct, node.name, node.range);
            return;
        }

        if (current_pass_ == Pass::ResolveReferences) {
            for (const auto &field : node.fields) {
                resolve_type(*field->type);
                if (field->default_value) {
                    resolve_declaration_expr(*field->default_value);
                }
            }
        }
    }

    void visit(ast::EnumDecl &node) override {
        if (current_pass_ == Pass::RegisterSymbols) {
            (void)register_symbol(SymbolNamespace::Types, SymbolKind::Enum, node.name, node.range);
        }
    }

    void visit(ast::CapabilityDecl &node) override {
        if (current_pass_ == Pass::RegisterSymbols) {
            (void)register_symbol(
                SymbolNamespace::Capabilities, SymbolKind::Capability, node.name, node.range);
            return;
        }

        if (current_pass_ == Pass::ResolveReferences) {
            for (const auto &param : node.params) {
                resolve_type(*param->type);
            }

            resolve_type(*node.return_type);
        }
    }

    void visit(ast::PredicateDecl &node) override {
        if (current_pass_ == Pass::RegisterSymbols) {
            (void)register_symbol(
                SymbolNamespace::Predicates, SymbolKind::Predicate, node.name, node.range);
            return;
        }

        if (current_pass_ == Pass::ResolveReferences) {
            for (const auto &param : node.params) {
                resolve_type(*param->type);
            }
        }
    }

    void visit(ast::AgentDecl &node) override {
        if (current_pass_ == Pass::RegisterSymbols) {
            (void)register_symbol(
                SymbolNamespace::Agents, SymbolKind::Agent, node.name, node.range);
            return;
        }

        if (current_pass_ == Pass::ResolveReferences) {
            resolve_type(*node.input_type);
            resolve_type(*node.context_type);
            resolve_type(*node.output_type);
        }
    }

    void visit(ast::ContractDecl &node) override {
        if (current_pass_ != Pass::ResolveReferences) {
            return;
        }

        (void)resolve_reference(
            SymbolNamespace::Agents, *node.target, ReferenceKind::ContractTarget, "agent");

        for (const auto &clause : node.clauses) {
            if (clause->expr) {
                resolve_declaration_expr(*clause->expr);
            }

            if (clause->temporal_expr) {
                resolve_temporal_expr(*clause->temporal_expr);
            }
        }
    }

    void visit(ast::FlowDecl &node) override {
        if (current_pass_ != Pass::ResolveReferences) {
            return;
        }

        (void)resolve_reference(
            SymbolNamespace::Agents, *node.target, ReferenceKind::FlowTarget, "agent");

        for (const auto &handler : node.state_handlers) {
            resolve_block_types(*handler->body);
            resolve_block_exprs(*handler->body);
        }
    }

    void visit(ast::WorkflowDecl &node) override {
        if (current_pass_ == Pass::RegisterSymbols) {
            (void)register_symbol(
                SymbolNamespace::Workflows, SymbolKind::Workflow, node.name, node.range);
            return;
        }

        if (current_pass_ == Pass::ResolveReferences) {
            resolve_type(*node.input_type);
            resolve_type(*node.output_type);

            for (const auto &workflow_node : node.nodes) {
                (void)resolve_reference(SymbolNamespace::Agents,
                                        *workflow_node->target,
                                        ReferenceKind::WorkflowNodeTarget,
                                        "agent");
                resolve_declaration_expr(*workflow_node->input);
            }

            for (const auto &formula : node.safety) {
                resolve_temporal_expr(*formula);
            }

            for (const auto &formula : node.liveness) {
                resolve_temporal_expr(*formula);
            }

            resolve_declaration_expr(*node.return_value);
        }
    }

  private:
    enum class Pass {
        CollectImports,
        RegisterSymbols,
        ResolveReferences,
    };

    Pass current_pass_{Pass::CollectImports};
    ResolveResult result_;
    std::optional<std::string> module_name_;
    std::optional<SourceRange> module_range_;
    std::unordered_map<std::string, std::size_t> import_alias_index_;
    std::unordered_map<std::string, std::string> import_aliases_;
    std::unordered_map<std::size_t, std::vector<SymbolId>> type_alias_dependencies_;
    std::optional<SymbolId> current_type_alias_;
    std::unordered_set<std::size_t> reported_alias_cycle_nodes_;

    [[nodiscard]] std::string canonical_name_for(std::string_view local_name) const {
        if (!module_name_.has_value() || module_name_->empty()) {
            return std::string(local_name);
        }

        return *module_name_ + "::" + std::string(local_name);
    }

    [[nodiscard]] std::optional<SymbolId> register_symbol(SymbolNamespace name_space,
                                                          SymbolKind kind,
                                                          std::string_view local_name,
                                                          SourceRange range) {
        auto &index = result_.symbol_table.index(name_space);

        if (const auto existing = index.local_names.find(std::string(local_name));
            existing != index.local_names.end()) {
            const auto previous_symbol = result_.symbol_table.get(existing->second);
            result_.diagnostics.error("duplicate " + namespace_name(name_space) + " '" +
                                          std::string(local_name) + "'",
                                      range);

            if (previous_symbol.has_value()) {
                result_.diagnostics.note("previous declaration is here",
                                         previous_symbol->get().declaration_range);
            }

            return std::nullopt;
        }

        const auto symbol_id = SymbolId{result_.symbol_table.symbols_.size()};
        auto symbol = Symbol{
            .id = symbol_id,
            .name_space = name_space,
            .kind = kind,
            .local_name = std::string(local_name),
            .canonical_name = canonical_name_for(local_name),
            .declaration_range = range,
        };

        result_.symbol_table.symbols_.push_back(symbol);
        index.local_names.emplace(symbol.local_name, symbol_id);
        index.canonical_names.emplace(symbol.canonical_name, symbol_id);
        return symbol_id;
    }

    [[nodiscard]] std::optional<SymbolId>
    find_registered_symbol(SymbolNamespace name_space, std::string_view local_name) const {
        const auto &index = result_.symbol_table.index(name_space);
        if (const auto iter = index.local_names.find(std::string(local_name));
            iter != index.local_names.end()) {
            return iter->second;
        }

        return std::nullopt;
    }

    [[nodiscard]] std::string normalize_name(const ast::QualifiedName &name) const {
        if (name.segments.empty()) {
            return "";
        }

        if (name.segments.size() == 1) {
            return name.spelling();
        }

        if (const auto import_alias = import_aliases_.find(name.segments.front());
            import_alias != import_aliases_.end()) {
            std::vector<std::string> segments;
            segments.reserve(name.segments.size());

            const auto prefix = import_alias->second;
            std::size_t start = 0;
            std::size_t end = 0;
            while ((end = prefix.find("::", start)) != std::string::npos) {
                segments.push_back(prefix.substr(start, end - start));
                start = end + 2;
            }
            segments.push_back(prefix.substr(start));

            for (std::size_t index = 1; index < name.segments.size(); ++index) {
                segments.push_back(name.segments[index]);
            }

            return join_segments(segments);
        }

        return name.spelling();
    }

    [[nodiscard]] std::optional<SymbolId> lookup(SymbolNamespace name_space,
                                                 const ast::QualifiedName &name) const {
        const auto &index = result_.symbol_table.index(name_space);

        if (name.segments.size() == 1) {
            if (const auto local = index.local_names.find(name.spelling());
                local != index.local_names.end()) {
                return local->second;
            }
        }

        if (const auto canonical = index.canonical_names.find(name.spelling());
            canonical != index.canonical_names.end()) {
            return canonical->second;
        }

        const auto normalized_name = normalize_name(name);
        if (normalized_name == name.spelling()) {
            return std::nullopt;
        }

        if (const auto canonical = index.canonical_names.find(normalized_name);
            canonical != index.canonical_names.end()) {
            return canonical->second;
        }

        return std::nullopt;
    }

    [[nodiscard]] std::optional<SymbolId> resolve_reference(SymbolNamespace name_space,
                                                            const ast::QualifiedName &name,
                                                            ReferenceKind kind,
                                                            std::string_view expected_name) {
        const auto resolved = lookup(name_space, name);
        if (!resolved.has_value()) {
            result_.diagnostics.error(
                "unknown " + std::string(expected_name) + " '" + name.spelling() + "'", name.range);
            return std::nullopt;
        }

        result_.references.push_back(ResolvedReference{
            .kind = kind,
            .text = name.spelling(),
            .range = name.range,
            .target = *resolved,
        });
        return resolved;
    }

    [[nodiscard]] std::optional<SymbolId>
    resolve_callable_reference(const ast::QualifiedName &name) {
        const auto capability = lookup(SymbolNamespace::Capabilities, name);
        const auto predicate = lookup(SymbolNamespace::Predicates, name);

        if (capability.has_value() && predicate.has_value()) {
            result_.diagnostics.error("ambiguous callable '" + name.spelling() +
                                          "' matches both a capability and a predicate",
                                      name.range);

            if (const auto symbol = result_.symbol_table.get(*capability); symbol.has_value()) {
                result_.diagnostics.note("capability declaration is here",
                                         symbol->get().declaration_range);
            }

            if (const auto symbol = result_.symbol_table.get(*predicate); symbol.has_value()) {
                result_.diagnostics.note("predicate declaration is here",
                                         symbol->get().declaration_range);
            }

            return std::nullopt;
        }

        if (capability.has_value()) {
            result_.references.push_back(ResolvedReference{
                .kind = ReferenceKind::CallTarget,
                .text = name.spelling(),
                .range = name.range,
                .target = *capability,
            });
            return capability;
        }

        if (predicate.has_value()) {
            result_.references.push_back(ResolvedReference{
                .kind = ReferenceKind::CallTarget,
                .text = name.spelling(),
                .range = name.range,
                .target = *predicate,
            });
            return predicate;
        }

        result_.diagnostics.error("unknown callable '" + name.spelling() + "'", name.range);
        return std::nullopt;
    }

    void resolve_temporal_expr(const ast::TemporalExprSyntax &expr) {
        switch (expr.kind) {
        case ast::TemporalExprSyntaxKind::EmbeddedExpr:
            resolve_declaration_expr(*expr.expr);
            break;
        case ast::TemporalExprSyntaxKind::Called: {
            const auto name = make_single_segment_name(expr.name, expr.range);
            (void)resolve_reference(SymbolNamespace::Capabilities,
                                    name,
                                    ReferenceKind::TemporalCapability,
                                    "capability");
            break;
        }
        case ast::TemporalExprSyntaxKind::Unary:
            resolve_temporal_expr(*expr.first);
            break;
        case ast::TemporalExprSyntaxKind::Binary:
            resolve_temporal_expr(*expr.first);
            resolve_temporal_expr(*expr.second);
            break;
        case ast::TemporalExprSyntaxKind::InState:
        case ast::TemporalExprSyntaxKind::Running:
        case ast::TemporalExprSyntaxKind::Completed:
            break;
        }
    }

    void resolve_type(const ast::TypeSyntax &type) {
        switch (type.kind) {
        case ast::TypeSyntaxKind::Named: {
            const auto resolved = resolve_reference(
                SymbolNamespace::Types, *type.name, ReferenceKind::TypeName, "type");

            if (resolved.has_value() && current_type_alias_.has_value()) {
                const auto symbol = result_.symbol_table.get(*resolved);
                if (symbol.has_value() && symbol->get().kind == SymbolKind::TypeAlias) {
                    type_alias_dependencies_[current_type_alias_->value].push_back(*resolved);
                }
            }
            break;
        }
        case ast::TypeSyntaxKind::Optional:
        case ast::TypeSyntaxKind::List:
        case ast::TypeSyntaxKind::Set:
            resolve_type(*type.first);
            break;
        case ast::TypeSyntaxKind::Map:
            resolve_type(*type.first);
            resolve_type(*type.second);
            break;
        case ast::TypeSyntaxKind::Unit:
        case ast::TypeSyntaxKind::Bool:
        case ast::TypeSyntaxKind::Int:
        case ast::TypeSyntaxKind::Float:
        case ast::TypeSyntaxKind::String:
        case ast::TypeSyntaxKind::BoundedString:
        case ast::TypeSyntaxKind::UUID:
        case ast::TypeSyntaxKind::Timestamp:
        case ast::TypeSyntaxKind::Duration:
        case ast::TypeSyntaxKind::Decimal:
            break;
        }
    }

    void resolve_declaration_expr(const ast::ExprSyntax &expr) {
        switch (expr.kind) {
        case ast::ExprSyntaxKind::Some:
        case ast::ExprSyntaxKind::Unary:
        case ast::ExprSyntaxKind::Group:
            resolve_declaration_expr(*expr.first);
            break;
        case ast::ExprSyntaxKind::Binary:
        case ast::ExprSyntaxKind::IndexAccess:
            resolve_declaration_expr(*expr.first);
            resolve_declaration_expr(*expr.second);
            break;
        case ast::ExprSyntaxKind::Call:
            (void)resolve_callable_reference(*expr.qualified_name);
            for (const auto &item : expr.items) {
                resolve_declaration_expr(*item);
            }
            break;
        case ast::ExprSyntaxKind::StructLiteral: {
            (void)resolve_reference(
                SymbolNamespace::Types, *expr.qualified_name, ReferenceKind::TypeName, "type");
            for (const auto &field : expr.struct_fields) {
                resolve_declaration_expr(*field->value);
            }
            break;
        }
        case ast::ExprSyntaxKind::ListLiteral:
        case ast::ExprSyntaxKind::SetLiteral:
            for (const auto &item : expr.items) {
                resolve_declaration_expr(*item);
            }
            break;
        case ast::ExprSyntaxKind::MapLiteral:
            for (const auto &entry : expr.map_entries) {
                resolve_declaration_expr(*entry->key);
                resolve_declaration_expr(*entry->value);
            }
            break;
        case ast::ExprSyntaxKind::MemberAccess:
            resolve_declaration_expr(*expr.first);
            break;
        case ast::ExprSyntaxKind::QualifiedValue: {
            if (const auto resolved = lookup(SymbolNamespace::Consts, *expr.qualified_name);
                resolved.has_value()) {
                result_.references.push_back(ResolvedReference{
                    .kind = ReferenceKind::ConstValue,
                    .text = expr.qualified_name->spelling(),
                    .range = expr.qualified_name->range,
                    .target = *resolved,
                });
                break;
            }

            if (expr.qualified_name->segments.size() > 1) {
                const auto owner = owner_name_of(*expr.qualified_name);
                (void)resolve_reference(
                    SymbolNamespace::Types, owner, ReferenceKind::QualifiedValueOwnerType, "type");
            }
            break;
        }
        case ast::ExprSyntaxKind::BoolLiteral:
        case ast::ExprSyntaxKind::IntegerLiteral:
        case ast::ExprSyntaxKind::FloatLiteral:
        case ast::ExprSyntaxKind::DecimalLiteral:
        case ast::ExprSyntaxKind::StringLiteral:
        case ast::ExprSyntaxKind::DurationLiteral:
        case ast::ExprSyntaxKind::NoneLiteral:
        case ast::ExprSyntaxKind::Path:
            break;
        }
    }

    void resolve_block_types(const ast::BlockSyntax &block) {
        for (const auto &statement : block.statements) {
            switch (statement->kind) {
            case ast::StatementSyntaxKind::Let:
                if (statement->let_stmt->type) {
                    resolve_type(*statement->let_stmt->type);
                }
                break;
            case ast::StatementSyntaxKind::If:
                resolve_block_types(*statement->if_stmt->then_block);
                if (statement->if_stmt->else_block) {
                    resolve_block_types(*statement->if_stmt->else_block);
                }
                break;
            case ast::StatementSyntaxKind::Assign:
            case ast::StatementSyntaxKind::Return:
            case ast::StatementSyntaxKind::Assert:
            case ast::StatementSyntaxKind::Expr:
            case ast::StatementSyntaxKind::Goto:
                break;
            }
        }
    }

    void resolve_block_exprs(const ast::BlockSyntax &block) {
        for (const auto &statement : block.statements) {
            switch (statement->kind) {
            case ast::StatementSyntaxKind::Let:
                resolve_declaration_expr(*statement->let_stmt->initializer);
                break;
            case ast::StatementSyntaxKind::Assign:
                resolve_declaration_expr(*statement->assign_stmt->value);
                break;
            case ast::StatementSyntaxKind::If:
                resolve_declaration_expr(*statement->if_stmt->condition);
                resolve_block_exprs(*statement->if_stmt->then_block);
                if (statement->if_stmt->else_block) {
                    resolve_block_exprs(*statement->if_stmt->else_block);
                }
                break;
            case ast::StatementSyntaxKind::Return:
                resolve_declaration_expr(*statement->return_stmt->value);
                break;
            case ast::StatementSyntaxKind::Assert:
                resolve_declaration_expr(*statement->assert_stmt->condition);
                break;
            case ast::StatementSyntaxKind::Expr:
                resolve_declaration_expr(*statement->expr_stmt->expr);
                break;
            case ast::StatementSyntaxKind::Goto:
                break;
            }
        }
    }

    void detect_type_alias_cycles() {
        enum class VisitState {
            Unvisited,
            Visiting,
            Completed,
        };

        std::vector<VisitState> states(result_.symbol_table.symbols().size(),
                                       VisitState::Unvisited);
        std::vector<SymbolId> stack;

        std::function<void(SymbolId)> visit_alias = [&](SymbolId symbol_id) {
            states[symbol_id.value] = VisitState::Visiting;
            stack.push_back(symbol_id);

            const auto dependency_iter = type_alias_dependencies_.find(symbol_id.value);
            if (dependency_iter != type_alias_dependencies_.end()) {
                for (const auto dependency : dependency_iter->second) {
                    if (states[dependency.value] == VisitState::Visiting) {
                        report_alias_cycle(stack, dependency);
                        continue;
                    }

                    if (states[dependency.value] == VisitState::Unvisited) {
                        visit_alias(dependency);
                    }
                }
            }

            stack.pop_back();
            states[symbol_id.value] = VisitState::Completed;
        };

        for (const auto &symbol : result_.symbol_table.symbols()) {
            if (symbol.kind != SymbolKind::TypeAlias) {
                continue;
            }

            if (states[symbol.id.value] == VisitState::Unvisited) {
                visit_alias(symbol.id);
            }
        }
    }

    void report_alias_cycle(const std::vector<SymbolId> &stack, SymbolId entry) {
        const auto cycle_begin = std::find(stack.begin(), stack.end(), entry);
        if (cycle_begin == stack.end()) {
            return;
        }

        std::vector<SymbolId> cycle(cycle_begin, stack.end());
        cycle.push_back(entry);

        bool should_emit = false;
        for (const auto symbol_id : cycle) {
            should_emit = reported_alias_cycle_nodes_.insert(symbol_id.value).second || should_emit;
        }

        if (!should_emit) {
            return;
        }

        std::ostringstream builder;
        builder << "type alias cycle detected: ";

        for (std::size_t index = 0; index < cycle.size(); ++index) {
            if (index != 0) {
                builder << " -> ";
            }

            const auto symbol = result_.symbol_table.get(cycle[index]);
            builder << (symbol.has_value() ? symbol->get().local_name : "<alias>");
        }

        const auto message = builder.str();

        for (std::size_t index = 0; index + 1 < cycle.size(); ++index) {
            if (const auto symbol = result_.symbol_table.get(cycle[index]); symbol.has_value()) {
                result_.diagnostics.error(message, symbol->get().declaration_range);
            }
        }
    }
};

} // namespace detail

MaybeCRef<Symbol> SymbolTable::get(SymbolId id) const {
    if (id.value >= symbols_.size()) {
        return std::nullopt;
    }

    return std::cref(symbols_[id.value]);
}

MaybeCRef<Symbol> SymbolTable::find_local(SymbolNamespace name_space, std::string_view name) const {
    const auto &name_index = index(name_space);
    if (const auto iter = name_index.local_names.find(std::string(name));
        iter != name_index.local_names.end()) {
        return get(iter->second);
    }

    return std::nullopt;
}

MaybeCRef<Symbol> SymbolTable::find_canonical(SymbolNamespace name_space,
                                              std::string_view name) const {
    const auto &name_index = index(name_space);
    if (const auto iter = name_index.canonical_names.find(std::string(name));
        iter != name_index.canonical_names.end()) {
        return get(iter->second);
    }

    return std::nullopt;
}

const SymbolTable::NamespaceIndex &SymbolTable::index(SymbolNamespace name_space) const {
    switch (name_space) {
    case SymbolNamespace::Types:
        return type_symbols_;
    case SymbolNamespace::Consts:
        return const_symbols_;
    case SymbolNamespace::Capabilities:
        return capability_symbols_;
    case SymbolNamespace::Predicates:
        return predicate_symbols_;
    case SymbolNamespace::Agents:
        return agent_symbols_;
    case SymbolNamespace::Workflows:
        return workflow_symbols_;
    }

    return type_symbols_;
}

SymbolTable::NamespaceIndex &SymbolTable::index(SymbolNamespace name_space) {
    switch (name_space) {
    case SymbolNamespace::Types:
        return type_symbols_;
    case SymbolNamespace::Consts:
        return const_symbols_;
    case SymbolNamespace::Capabilities:
        return capability_symbols_;
    case SymbolNamespace::Predicates:
        return predicate_symbols_;
    case SymbolNamespace::Agents:
        return agent_symbols_;
    case SymbolNamespace::Workflows:
        return workflow_symbols_;
    }

    return type_symbols_;
}

MaybeCRef<ResolvedReference> ResolveResult::find_reference(ReferenceKind kind,
                                                           SourceRange range) const {
    for (const auto &reference : references) {
        if (reference.kind == kind && reference.range.begin_offset == range.begin_offset &&
            reference.range.end_offset == range.end_offset) {
            return std::cref(reference);
        }
    }

    return std::nullopt;
}

ResolveResult Resolver::resolve(const ast::Program &program) const {
    detail::ResolverPass pass;
    return pass.run(program);
}

} // namespace ahfl
