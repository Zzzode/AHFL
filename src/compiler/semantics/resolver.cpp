#include "ahfl/compiler/semantics/resolver.hpp"

#include "ahfl/base/support/diagnostics.hpp"
#include "ahfl/compiler/frontend/frontend.hpp"

#include "ahfl/base/support/overloaded.hpp"

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

constexpr std::string_view kStdPreludeModule = "std::prelude";

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
    case SymbolNamespace::Functions:
        return "function";
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

[[nodiscard]] std::size_t hash_mix(std::size_t seed, std::size_t value) noexcept {
    seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
    return seed;
}

} // namespace

namespace detail {

class ResolverPass final {
  public:
    [[nodiscard]] ResolveResult run(const ast::Program &program) {
        run_program(program);

        detect_type_alias_cycles();
        return std::move(result_);
    }

    [[nodiscard]] ResolveResult run(const SourceGraph &graph) {
        source_graph_mode_ = true;
        source_graph_ = &graph;

        run_source_graph_pass(graph, Pass::CollectImports);
        run_source_graph_pass(graph, Pass::RegisterSymbols);
        run_source_graph_pass(graph, Pass::ResolveReferences);

        detect_type_alias_cycles();
        return std::move(result_);
    }

    void visit(const ast::Decl &node) {
        switch (node.kind) {
        case ast::NodeKind::ModuleDecl:
            visit(static_cast<const ast::ModuleDecl &>(node));
            return;
        case ast::NodeKind::ImportDecl:
            visit(static_cast<const ast::ImportDecl &>(node));
            return;
        case ast::NodeKind::ConstDecl:
            visit(static_cast<const ast::ConstDecl &>(node));
            return;
        case ast::NodeKind::TypeAliasDecl:
            visit(static_cast<const ast::TypeAliasDecl &>(node));
            return;
        case ast::NodeKind::StructDecl:
            visit(static_cast<const ast::StructDecl &>(node));
            return;
        case ast::NodeKind::EnumDecl:
            visit(static_cast<const ast::EnumDecl &>(node));
            return;
        case ast::NodeKind::CapabilityDecl:
            visit(static_cast<const ast::CapabilityDecl &>(node));
            return;
        case ast::NodeKind::PredicateDecl:
            visit(static_cast<const ast::PredicateDecl &>(node));
            return;
        case ast::NodeKind::AgentDecl:
            visit(static_cast<const ast::AgentDecl &>(node));
            return;
        case ast::NodeKind::ContractDecl:
            visit(static_cast<const ast::ContractDecl &>(node));
            return;
        case ast::NodeKind::FlowDecl:
            visit(static_cast<const ast::FlowDecl &>(node));
            return;
        case ast::NodeKind::WorkflowDecl:
            visit(static_cast<const ast::WorkflowDecl &>(node));
            return;
        case ast::NodeKind::FnDecl:
            visit(static_cast<const ast::FnDecl &>(node));
            return;
        case ast::NodeKind::TraitDecl:
            visit(static_cast<const ast::TraitDecl &>(node));
            return;
        case ast::NodeKind::ImplDecl:
            visit(static_cast<const ast::ImplDecl &>(node));
            return;
        case ast::NodeKind::Program:
            emit_error("unexpected program node in declarations list", current_source_, node.range);
            return;
        }
    }

    void visit(const ast::ModuleDecl &node) {
        if (current_pass_ != Pass::CollectImports) {
            return;
        }

        if (source_graph_mode_ && current_source_id_.has_value()) {
            if (module_name_.has_value() && node.name->spelling() != *module_name_) {
                emit_error("source unit module boundary does not match graph owner",
                           current_source_,
                           node.range);
            }
            return;
        }

        const auto module_name = node.name->spelling();
        if (!module_name_.has_value()) {
            module_name_ = module_name;
            module_range_ = node.range;
            return;
        }

        emit_error("multiple module declarations are not supported in one source file",
                   current_source_,
                   node.range);
        emit_note("first module declaration is here", current_source_, module_range_);
    }

    void visit(const ast::ImportDecl &node) {
        if (current_pass_ != Pass::CollectImports) {
            return;
        }

        if (node.alias.empty()) {
            result_.add_import(ImportBinding{
                .alias = "",
                .target_module = node.path->spelling(),
                .source_id = current_source_id_,
                .declaration_range = node.range,
            });
            return;
        }

        if (const auto existing = import_alias_index_.find(node.alias);
            existing != import_alias_index_.end()) {
            emit_error("duplicate import alias '" + node.alias + "'", current_source_, node.range);
            const auto &prev_import = result_.imports()[existing->second];
            if (auto src = source_unit_for(*prev_import.source_id); src.has_value()) {
                emit_note(
                    "previous import alias is here", &src->get(), prev_import.declaration_range);
            }
            return;
        }

        import_alias_index_.emplace(node.alias, result_.imports().size());
        import_aliases_.emplace(node.alias, node.path->spelling());
        result_.add_import(ImportBinding{
            .alias = node.alias,
            .target_module = node.path->spelling(),
            .source_id = current_source_id_,
            .declaration_range = node.range,
        });
    }

    void visit(const ast::ConstDecl &node) {
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

    void visit(const ast::TypeAliasDecl &node) {
        if (current_pass_ == Pass::RegisterSymbols) {
            (void)register_symbol(
                SymbolNamespace::Types, SymbolKind::TypeAlias, node.name, node.range);
            return;
        }

        if (current_pass_ == Pass::ResolveReferences) {
            current_type_alias_ = find_registered_symbol(SymbolNamespace::Types, node.name);
            const auto previous_type_params = generic_type_params_;
            for (const auto &type_param : node.type_params) {
                generic_type_params_.insert(type_param->name);
            }
            for (const auto &type_param : node.type_params) {
                for (const auto &bound : type_param->bounds) {
                    resolve_type(*bound);
                }
            }
            resolve_type(*node.aliased_type);
            generic_type_params_ = previous_type_params;
            current_type_alias_.reset();
        }
    }

    void visit(const ast::StructDecl &node) {
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

    void visit(const ast::EnumDecl &node) {
        if (current_pass_ == Pass::RegisterSymbols) {
            (void)register_symbol(SymbolNamespace::Types, SymbolKind::Enum, node.name, node.range);
            return;
        }

        if (current_pass_ == Pass::ResolveReferences) {
            // Register type parameters so that variant payload types (e.g.
            // `List<T>` on a generic enum variant) can resolve their tparam
            // references during the same resolve-type walk.
            const auto previous_type_params = generic_type_params_;
            for (const auto &type_param : node.type_params) {
                generic_type_params_.insert(type_param->name);
            }
            for (const auto &type_param : node.type_params) {
                for (const auto &bound : type_param->bounds) {
                    resolve_type(*bound);
                }
            }
            // Resolve each variant's positional payload types. This handles
            // recursive ADT references (e.g. `JList(List<JsonValue>)`) as
            // long as the enum name itself was registered in the earlier
            // RegisterSymbols pass.
            for (const auto &variant : node.variants) {
                for (const auto &payload_type : variant->payload) {
                    resolve_type(*payload_type);
                }
            }
            if (node.where_clause) {
                for (const auto &constraint : node.where_clause->constraints) {
                    if (constraint->subject) {
                        resolve_type(*constraint->subject);
                    }
                    for (const auto &argument : constraint->arguments) {
                        resolve_type(*argument);
                    }
                    for (const auto &bound : constraint->bounds) {
                        resolve_type(*bound);
                    }
                }
            }
            generic_type_params_ = previous_type_params;
        }
    }

    void visit(const ast::CapabilityDecl &node) {
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

    void visit(const ast::PredicateDecl &node) {
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

    void visit(const ast::AgentDecl &node) {
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

    void visit(const ast::ContractDecl &node) {
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

    void visit(const ast::FlowDecl &node) {
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

    void visit(const ast::WorkflowDecl &node) {
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

    void visit(const ast::FnDecl &node) {
        // P2 (RFC §3.2.2): three-pass fn handling, mirroring the existing
        // capability/predicate/agent pattern:
        //   Pass::RegisterSymbols  — register the fn name in the Functions
        //                           namespace so call sites can resolve it.
        //   Pass::ResolveReferences — walk the signature (generic bounds,
        //                           param types, return type, where-clause
        //                           types) and, for a fn with a body, the
        //                           body's expressions so nested type / fn /
        //                           callable references are recorded. Effect
        //                           clause capability names are also resolved.
        // Closure-capture analysis (RFC §4) and where-clause bound evaluation
        // are deferred to the typecheck pass, which has the typed environment
        // needed to unify generic parameters.
        if (current_pass_ == Pass::RegisterSymbols) {
            (void)register_symbol(
                SymbolNamespace::Functions, SymbolKind::Function, node.name, node.range);
            return;
        }

        if (current_pass_ == Pass::ResolveReferences) {
            // P2: push generic type params into scope so T in fn id<T>(x: T)
            // resolves as an opaque type variable, not "unknown type".
            for (const auto &type_param : node.type_params) {
                generic_type_params_.insert(type_param->name);
            }

            resolve_fn_signature(node);

            // P2: clear generic type param scope after fn signature + body.
            generic_type_params_.clear();
        }
    }

    // P3 (RFC §3.2.2 / type-system §1.3 / §1.4): shared signature resolver
    // for a fn declaration, reused by top-level FnDecl and by impl-block
    // method bodies (which use the same ast::FnDecl node). Assumes the
    // caller has already pushed the fn's generic type params into
    // generic_type_params_ if it wants them treated as opaque.
    void resolve_fn_signature(const ast::FnDecl &node) {
        // --- Scope: add method-level generic type params to the resolver's
        // opaque-set so nested type references (like `U` in `Fn(T) -> U` or
        // `Option<U>`) are treated as type variables rather than looked up as
        // type-name references. Without this, the resolver emits "unknown
        // type U" diagnostics for every method-level type param because the
        // symbol isn't registered in the Types namespace (only impl-level
        // params are set by the ImplDecl handler upstream).
        //
        // This mirrors the FnDecl top-level handler at line 406-423:
        //   1. capture previous scope
        //   2. insert node.type_params
        //   3. walk signature / body
        //   4. restore
        const auto previous_method_type_params = generic_type_params_;
        for (const auto &type_param : node.type_params) {
            generic_type_params_.insert(type_param->name);
        }

        for (const auto &type_param : node.type_params) {
            for (const auto &bound : type_param->bounds) {
                resolve_type(*bound);
            }
        }

        for (const auto &param : node.params) {
            if (param->type) {
                resolve_type(*param->type);
            }
        }

        if (node.return_type) {
            resolve_type(*node.return_type);
        }

        if (node.where_clause) {
            for (const auto &constraint : node.where_clause->constraints) {
                if (constraint->subject) {
                    resolve_type(*constraint->subject);
                }
                for (const auto &argument : constraint->arguments) {
                    resolve_type(*argument);
                }
                for (const auto &bound : constraint->bounds) {
                    resolve_type(*bound);
                }
            }
        }

        resolve_effect_clause(node.effect_clause);

        if (node.body) {
            resolve_block_types(*node.body);
            resolve_block_exprs(*node.body);
        }

        // Restore previous scope (pop method-level type params).
        generic_type_params_ = previous_method_type_params;
    }

    // P3 (RFC §3.2.2 / type-system §1.3): walk one trait item, recording
    // type/callable references in its signature (for TraitFnItem) and in its
    // bounds/default (for AssocTypeItem). Self is treated as opaque by the
    // caller (visit(TraitDecl) pushes it).
    void resolve_trait_item(const ast::TraitItemSyntax &item) {
        if (item.kind == ast::TraitItemKind::Fn) {
            for (const auto &type_param : item.type_params) {
                for (const auto &bound : type_param->bounds) {
                    resolve_type(*bound);
                }
            }
            for (const auto &param : item.params) {
                if (param->type) {
                    resolve_type(*param->type);
                }
            }
            if (item.return_type) {
                resolve_type(*item.return_type);
            }
            if (item.where_clause) {
                for (const auto &constraint : item.where_clause->constraints) {
                    if (constraint->subject) {
                        resolve_type(*constraint->subject);
                    }
                    for (const auto &argument : constraint->arguments) {
                        resolve_type(*argument);
                    }
                    for (const auto &bound : constraint->bounds) {
                        resolve_type(*bound);
                    }
                }
            }
            resolve_effect_clause(item.effect_clause);
            return;
        }

        if (item.kind == ast::TraitItemKind::AssocType && item.assoc_type) {
            for (const auto &type_param : item.assoc_type->type_params) {
                for (const auto &bound : type_param->bounds) {
                    resolve_type(*bound);
                }
            }
            for (const auto &bound : item.assoc_type->bounds) {
                resolve_type(*bound);
            }
            if (item.assoc_type->default_type) {
                resolve_type(*item.assoc_type->default_type);
            }
            return;
        }

        // P3c.S1: resolve references inside associated-constant trait items
        // (type annotation + optional default value expression).
        if (item.kind == ast::TraitItemKind::AssocConst && item.assoc_const) {
            if (item.assoc_const->type) {
                resolve_type(*item.assoc_const->type);
            }
            // Expression-level resolution for assoc-const default values is
            // deferred to semantic analysis phase P3b.
            (void)item.assoc_const->default_value;
        }
    }

    void visit(const ast::TraitDecl &node) {
        // P3 (RFC §3.2.2 / type-system §1.3): a trait registers in the Types
        // namespace as SymbolKind::Trait so it is usable at both bound
        // positions (`T: Ord`) and impl positions (`impl Ord for T`). The
        // trait's method signatures, super-trait bounds, and associated-type
        // references are resolved in ResolveReferences; generic type params
        // (including the implicit Self) enter scope so they are opaque during
        // signature resolution.
        if (current_pass_ == Pass::RegisterSymbols) {
            (void)register_symbol(SymbolNamespace::Types, SymbolKind::Trait, node.name, node.range);
            return;
        }

        if (current_pass_ == Pass::ResolveReferences) {
            for (const auto &type_param : node.type_params) {
                generic_type_params_.insert(type_param->name);
            }
            // The implicit `Self` parameter names the impl's target type inside
            // trait items. P3b resolves Self to the impl target at typecheck;
            // at the resolver it is opaque.
            generic_type_params_.insert("Self");

            for (const auto &type_param : node.type_params) {
                for (const auto &bound : type_param->bounds) {
                    resolve_type(*bound);
                }
            }

            for (const auto &super_trait : node.super_traits) {
                resolve_type(*super_trait);
            }

            for (const auto &item : node.items) {
                resolve_trait_item(*item);
            }

            generic_type_params_.clear();
        }
    }

    void visit(const ast::ImplDecl &node) {
        // P3 (RFC §3.2.2 / type-system §1.4): an impl block has no
        // user-facing name, so nothing is registered in RegisterSymbols. The
        // impl's trait_ref (when present) resolves to a Trait symbol, the
        // target_type to a Types symbol, and each method body/where-clause
        // type is walked so nested references are recorded. Impl method
        // *names* are not registered as standalone Function symbols today:
        // they are reached through `Type::method` / `e.method` resolution at
        // typecheck, not as free callables.
        if (current_pass_ != Pass::ResolveReferences) {
            return;
        }

        for (const auto &type_param : node.type_params) {
            generic_type_params_.insert(type_param->name);
        }

        for (const auto &type_param : node.type_params) {
            for (const auto &bound : type_param->bounds) {
                resolve_type(*bound);
            }
        }

        if (node.trait_ref) {
            // R-10 (P3c pre-research TODO): trait-name lookup order today is
            //   (1) resolver-recorded TypeName reference (cross-module paths
            //       through `Module::Trait` + imports), then
            //   (2) local SymbolNamespace::Types lookup in the canonical-name
            //       index (TypeEnvironment trait_name_index_ is a downstream
            //       mirror of the resolver's write).
            // This matches how regular type references resolve, but the trait
            // system RFC additionally requires: (a) trait aliases / blanket
            // imports, (b) prelude trait shadowing rules, (c) when a trait
            // name collides with a type/type-alias of the same spelling in
            // Types namespace we prefer the Trait-symbol variant when the
            // reference appears in a bound (`T: Name`) or impl-header
            // (`impl Name for T`) position. All three items are deferred to
            // S4b; the current path is sufficient for P3c coherence and
            // diagnostic coverage.
            // trait_ref is a TypeSyntax (NamedType) — resolve the trait name.
            resolve_type(*node.trait_ref);
        }

        if (node.target_type) {
            resolve_type(*node.target_type);
        }

        if (node.where_clause) {
            for (const auto &constraint : node.where_clause->constraints) {
                if (constraint->subject) {
                    resolve_type(*constraint->subject);
                }
                for (const auto &argument : constraint->arguments) {
                    resolve_type(*argument);
                }
                for (const auto &bound : constraint->bounds) {
                    resolve_type(*bound);
                }
            }
        }

        for (const auto &method : node.methods) {
            resolve_fn_signature(*method);
        }

        for (const auto &assoc : node.assoc_items) {
            if (assoc->type) {
                resolve_type(*assoc->type);
            }
        }

        // P3c.S1: resolve associated constants inside impl blocks.
        for (const auto &assoc_const : node.const_items) {
            if (assoc_const->type) {
                resolve_type(*assoc_const->type);
            }
            // Expression-level resolution for impl assoc-const initializers
            // is deferred to semantic analysis phase P3b.
            (void)assoc_const->value;
        }

        generic_type_params_.clear();
    }

  private:
    enum class Pass {
        CollectImports,
        RegisterSymbols,
        ResolveReferences,
    };

    Pass current_pass_{Pass::CollectImports};
    ResolveResult result_;
    bool source_graph_mode_{false};
    const SourceGraph *source_graph_{nullptr};
    const SourceUnit *current_source_{nullptr};
    std::optional<SourceId> current_source_id_;
    std::optional<std::string> module_name_;
    std::optional<SourceRange> module_range_;
    std::unordered_map<std::string, std::size_t> import_alias_index_;
    std::unordered_map<std::string, std::string> import_aliases_;
    std::unordered_map<std::size_t, std::vector<SymbolId>> type_alias_dependencies_;
    std::optional<SymbolId> current_type_alias_;
    std::unordered_set<std::size_t> reported_alias_cycle_nodes_;
    // P2: generic type param names currently in scope (fn signature). When
    // resolving types inside a generic fn signature, these are opaque type
    // variables — not references to resolve. Cleared after fn signature.
    std::unordered_set<std::string> generic_type_params_;

    [[nodiscard]] std::string canonical_name_for(std::string_view local_name) const {
        if (!module_name_.has_value() || module_name_->empty()) {
            return std::string(local_name);
        }

        return *module_name_ + "::" + std::string(local_name);
    }

    void run_source_graph_pass(const SourceGraph &graph, Pass pass) {
        current_pass_ = pass;
        for (const auto &source : graph.sources) {
            const auto &program =
                require(source.program.get(), "source graph program must exist before resolution");
            enter_source(source);
            visit(program);
            leave_source();
        }
    }

    void visit(const ast::Program &program) {
        for (const auto &declaration : program.declarations) {
            visit(*declaration);
        }
    }

    void run_program(const ast::Program &program) {
        current_pass_ = Pass::CollectImports;
        visit(program);

        current_pass_ = Pass::RegisterSymbols;
        visit(program);

        current_pass_ = Pass::ResolveReferences;
        visit(program);
    }

    void enter_source(const SourceUnit &source) {
        current_source_ = &source;
        current_source_id_ = source.id;
        module_name_ = source.module_name;
        module_range_.reset();
        import_alias_index_.clear();
        import_aliases_.clear();

        for (std::size_t index = 0; index < result_.imports().size(); ++index) {
            const auto &binding = result_.imports()[index];
            if (binding.source_id != current_source_id_ || binding.alias.empty()) {
                continue;
            }

            import_alias_index_.emplace(binding.alias, index);
            import_aliases_.emplace(binding.alias, binding.target_module);
        }
    }

    void leave_source() {
        current_source_ = nullptr;
        current_source_id_.reset();
        module_name_.reset();
        module_range_.reset();
        import_alias_index_.clear();
        import_aliases_.clear();
    }

    [[nodiscard]] MaybeCRef<SourceUnit> source_unit_for(SourceId id) const {
        if (source_graph_ == nullptr) {
            return std::nullopt;
        }

        for (const auto &source : source_graph_->sources) {
            if (source.id == id) {
                return std::cref(source);
            }
        }

        return std::nullopt;
    }

    void emit_error(std::string message,
                    const SourceUnit *source,
                    std::optional<SourceRange> range = std::nullopt) {
        if (source != nullptr) {
            result_.diagnostics.error()
                .message(std::move(message))
                .range(range)
                .source(source->source)
                .emit();
        } else {
            result_.diagnostics.error().message(std::move(message)).range(range).emit();
        }
    }

    void emit_note(std::string message,
                   const SourceUnit *source,
                   std::optional<SourceRange> range = std::nullopt) {
        if (source != nullptr) {
            result_.diagnostics.note()
                .message(std::move(message))
                .range(range)
                .source(source->source)
                .emit();
        } else {
            result_.diagnostics.note().message(std::move(message)).range(range).emit();
        }
    }

    [[nodiscard]] std::optional<SymbolId> register_symbol(SymbolNamespace name_space,
                                                          SymbolKind kind,
                                                          std::string_view local_name,
                                                          SourceRange range) {
        auto &index = result_.symbol_table.index(name_space);
        auto &module_index = index.module_local_names[module_name_.value_or("")];

        if (const auto existing = module_index.find(std::string(local_name));
            existing != module_index.end()) {
            const auto previous_symbol = result_.symbol_table.get(existing->second);
            emit_error("duplicate " + namespace_name(name_space) + " '" + std::string(local_name) +
                           "'",
                       current_source_,
                       range);

            if (previous_symbol.has_value()) {
                if (auto src = source_unit_for(*previous_symbol->get().source_id);
                    src.has_value()) {
                    emit_note("previous declaration is here",
                              &src->get(),
                              previous_symbol->get().declaration_range);
                }
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
            .module_name = module_name_.value_or(""),
            .source_id = current_source_id_,
            .declaration_range = range,
        };

        if (const auto existing = index.canonical_names.find(symbol.canonical_name);
            existing != index.canonical_names.end()) {
            const auto previous_symbol = result_.symbol_table.get(existing->second);
            emit_error("duplicate " + namespace_name(name_space) + " '" + symbol.canonical_name +
                           "'",
                       current_source_,
                       range);

            if (previous_symbol.has_value()) {
                if (auto src = source_unit_for(*previous_symbol->get().source_id);
                    src.has_value()) {
                    emit_note("previous declaration is here",
                              &src->get(),
                              previous_symbol->get().declaration_range);
                }
            }

            return std::nullopt;
        }

        result_.symbol_table.symbols_.push_back(symbol);
        module_index.emplace(symbol.local_name, symbol_id);
        index.canonical_names.emplace(symbol.canonical_name, symbol_id);
        return symbol_id;
    }

    [[nodiscard]] std::optional<SymbolId>
    find_registered_symbol(SymbolNamespace name_space, std::string_view local_name) const {
        const auto &index = result_.symbol_table.index(name_space);
        if (const auto module_iter = index.module_local_names.find(module_name_.value_or(""));
            module_iter != index.module_local_names.end()) {
            if (const auto iter = module_iter->second.find(std::string(local_name));
                iter != module_iter->second.end()) {
                return iter->second;
            }
        }

        return std::nullopt;
    }

    [[nodiscard]] std::optional<SymbolId>
    lookup_prelude_symbol(const SymbolTable::NamespaceIndex &index,
                          const ast::QualifiedName &name) const {
        if (!source_graph_mode_ || name.segments.size() != 1 ||
            module_name_.value_or("") == kStdPreludeModule) {
            return std::nullopt;
        }

        if (const auto module_iter = index.module_local_names.find(std::string(kStdPreludeModule));
            module_iter != index.module_local_names.end()) {
            if (const auto local = module_iter->second.find(name.spelling());
                local != module_iter->second.end()) {
                return local->second;
            }
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
            if (const auto module_iter = index.module_local_names.find(module_name_.value_or(""));
                module_iter != index.module_local_names.end()) {
                if (const auto local = module_iter->second.find(name.spelling());
                    local != module_iter->second.end()) {
                    return local->second;
                }
            }
        }

        if (const auto prelude = lookup_prelude_symbol(index, name); prelude.has_value()) {
            return prelude;
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
            emit_error("unknown " + std::string(expected_name) + " '" + name.spelling() + "'",
                       current_source_,
                       name.range);
            return std::nullopt;
        }

        result_.add_reference(ResolvedReference{
            .kind = kind,
            .text = name.spelling(),
            .source_id = current_source_id_,
            .range = name.range,
            .target = *resolved,
        });
        return resolved;
    }

    [[nodiscard]] std::optional<SymbolId>
    resolve_callable_reference(const ast::QualifiedName &name) {
        const auto capability = lookup(SymbolNamespace::Capabilities, name);
        const auto predicate = lookup(SymbolNamespace::Predicates, name);
        const auto function = lookup(SymbolNamespace::Functions, name);

        if (capability.has_value() && predicate.has_value()) {
            emit_error("ambiguous callable '" + name.spelling() +
                           "' matches both a capability and a predicate",
                       current_source_,
                       name.range);

            if (const auto symbol = result_.symbol_table.get(*capability); symbol.has_value()) {
                if (auto src = source_unit_for(*symbol->get().source_id); src.has_value()) {
                    emit_note("capability declaration is here",
                              &src->get(),
                              symbol->get().declaration_range);
                }
            }

            if (const auto symbol = result_.symbol_table.get(*predicate); symbol.has_value()) {
                if (auto src = source_unit_for(*symbol->get().source_id); src.has_value()) {
                    emit_note("predicate declaration is here",
                              &src->get(),
                              symbol->get().declaration_range);
                }
            }

            return std::nullopt;
        }

        if (capability.has_value()) {
            result_.add_reference(ResolvedReference{
                .kind = ReferenceKind::CallTarget,
                .text = name.spelling(),
                .source_id = current_source_id_,
                .range = name.range,
                .target = *capability,
            });
            return capability;
        }

        if (predicate.has_value()) {
            result_.add_reference(ResolvedReference{
                .kind = ReferenceKind::CallTarget,
                .text = name.spelling(),
                .source_id = current_source_id_,
                .range = name.range,
                .target = *predicate,
            });
            return predicate;
        }

        // P2 (RFC §3.2.2): a fn call resolves to a Function symbol under a
        // dedicated ReferenceKind (FnCallTarget) so the typecheck
        // call-target-kind lookup — which keys off ReferenceKind::CallTarget
        // for capability/predicate — can recognise a fn call without an
        // extra namespace re-walk at every call site.
        if (function.has_value()) {
            result_.add_reference(ResolvedReference{
                .kind = ReferenceKind::FnCallTarget,
                .text = name.spelling(),
                .source_id = current_source_id_,
                .range = name.range,
                .target = *function,
            });
            return function;
        }

        // M3 (RFC §1.5): an enum variant constructor `EnumName::Variant(args)`.
        // When the callee is a multi-segment path whose owner names an Enum
        // type, register an EnumVariantConstructor reference (target = the enum
        // symbol) so the typecheck pass can validate the variant name and
        // payload arity, and build the variant value type. The variant itself
        // is not a standalone symbol (only the enum is), so this owner-only
        // reference is all the typecheck pass needs to recover the full
        // EnumTypeInfo.
        if (name.segments.size() > 1) {
            const auto owner = owner_name_of(name);
            if (const auto owner_id = lookup(SymbolNamespace::Types, owner); owner_id.has_value()) {
                const auto owner_symbol = result_.symbol_table.get(*owner_id);
                if (owner_symbol.has_value() && owner_symbol->get().kind == SymbolKind::Enum) {
                    result_.add_reference(ResolvedReference{
                        .kind = ReferenceKind::EnumVariantConstructor,
                        .text = name.spelling(),
                        .source_id = current_source_id_,
                        .range = name.range,
                        .target = *owner_id,
                    });
                    return owner_id;
                }
            }
        }

        // P2 callable values: an unresolved single-segment callee may be a local
        // binding whose type is Fn(...). The resolver has no value-binding type
        // environment, so defer the final unknown-callable diagnostic to
        // typecheck, where local bindings are available.
        if (name.segments.size() == 1) {
            return std::nullopt;
        }

        emit_error("unknown callable '" + name.spelling() + "'", current_source_, name.range);
        return std::nullopt;
    }

    // P2 (RFC §2): resolve the capability references named in an effect
    // clause (`effect ChargeCard + RefundCard`). Each name must resolve to a
    // declared capability; an unresolvable capability is diagnosed here so
    // the typecheck pass can trust the clause's capability list later.
    void resolve_effect_clause(const Owned<ast::EffectClauseSyntax> &clause) {
        if (!clause || clause->kind != ast::EffectClauseKind::Capability) {
            return;
        }

        for (const auto &capability_name : clause->capabilities) {
            (void)resolve_reference(SymbolNamespace::Capabilities,
                                    *capability_name,
                                    ReferenceKind::AgentCapability,
                                    "capability");
        }
    }

    void resolve_temporal_expr(const ast::TemporalExprSyntax &expr) {
        std::visit(Overloaded{
                       [&](const ast::EmbeddedTemporalExpr &e) {
                           if (e.expr) {
                               resolve_declaration_expr(*e.expr);
                           }
                       },
                       [&](const ast::CalledTemporalExpr &e) {
                           const auto name = make_single_segment_name(e.name, expr.range);
                           (void)resolve_reference(SymbolNamespace::Capabilities,
                                                   name,
                                                   ReferenceKind::TemporalCapability,
                                                   "capability");
                       },
                       [&](const ast::InStateTemporalExpr &) {
                           // No resolution needed for InState (state names resolved at typecheck
                       },
                       [&](const ast::RunningTemporalExpr &) {
                           // No resolution needed for Running (node names resolved at typecheck)
                       },
                       [&](const ast::CompletedTemporalExpr &) {
                           // No resolution needed for Completed (node names resolved at typecheck)
                       },
                       [&](const ast::UnaryTemporalExpr &e) {
                           if (e.operand) {
                               resolve_temporal_expr(*e.operand);
                           }
                       },
                       [&](const ast::BinaryTemporalExpr &e) {
                           if (e.lhs) {
                               resolve_temporal_expr(*e.lhs);
                           }
                           if (e.rhs) {
                               resolve_temporal_expr(*e.rhs);
                           }
                       },
                   },
                   expr.node);
    }

    void resolve_type(const ast::TypeSyntax &type) {
        std::visit(
            Overloaded{
                [&](const ast::NamedType &t) {
                    // P2: if this name is a generic type param in scope,
                    // skip resolution — it's an opaque type variable.
                    if (generic_type_params_.count(t.name->spelling()) > 0) {
                        // Still recurse into type args (they may reference
                        // outer scope types like T or U used inside a Fn).
                        for (const auto &arg : t.type_args) {
                            if (arg) {
                                resolve_type(*arg);
                            }
                        }
                        return;
                    }
                    // P5.5 (TypeSyntax desugaring): a bare single-segment
                    // name with generic arguments used to be handled by a
                    // dedicated AST node (OptionalType / ListType / etc.).
                    // Those nodes have been removed; the bare name now
                    // resolves like any other nominal type. When the stdlib
                    // is loaded, Optional/List/Set/Map are real enum/struct
                    // declarations in the symbol table and must produce a
                    // TypeName reference. resolve_std_container_type in
                    // type_resolver.cpp handles the fallback case where no
                    // such declaration exists (stdlib-less pipelines).
                    //
                    // P5.6a integration fix: the four recognised container
                    // names (Optional / List / Set / Map) are allowed to
                    // remain unresolved at the Resolver phase. The actual
                    // diagnostic (`stdlib container type unavailable`) is
                    // emitted later by the TypeResolver so stdlib-less
                    // pipelines surface a targeted message rather than a
                    // generic "unknown type".
                    if (t.name->segments.size() == 1 && !t.type_args.empty()) {
                        const auto &head = t.name->segments.front();
                        if (head == "Option" || head == "Optional" ||
                            head == "List" || head == "Set" || head == "Map") {
                            // Silent lookup first — only record a symbol
                            // reference if the symbol actually exists. Do not
                            // call resolve_reference() because it emits an
                            // "unknown type" diagnostic on miss.
                            const auto pre_existing =
                                lookup(SymbolNamespace::Types, *t.name);
                            if (!pre_existing.has_value()) {
                                for (const auto &arg : t.type_args) {
                                    if (arg) {
                                        resolve_type(*arg);
                                    }
                                }
                                return;
                            }
                            // Container symbol IS available: register the
                            // reference and type-alias dependency tracking.
                            result_.add_reference(ResolvedReference{
                                .kind = ReferenceKind::TypeName,
                                .text = t.name->spelling(),
                                .source_id = current_source_id_,
                                .range = t.name->range,
                                .target = *pre_existing,
                            });
                            if (current_type_alias_.has_value()) {
                                const auto symbol =
                                    result_.symbol_table.get(*pre_existing);
                                if (symbol.has_value() &&
                                    symbol->get().kind == SymbolKind::TypeAlias) {
                                    type_alias_dependencies_[current_type_alias_->value]
                                        .push_back(*pre_existing);
                                }
                            }
                            for (const auto &arg : t.type_args) {
                                if (arg) {
                                    resolve_type(*arg);
                                }
                            }
                            return;
                        }
                    }
                    const auto resolved = resolve_reference(
                        SymbolNamespace::Types, *t.name, ReferenceKind::TypeName, "type");

                    if (resolved.has_value() && current_type_alias_.has_value()) {
                        const auto symbol = result_.symbol_table.get(*resolved);
                        if (symbol.has_value() && symbol->get().kind == SymbolKind::TypeAlias) {
                            type_alias_dependencies_[current_type_alias_->value].push_back(
                                *resolved);
                        }
                    }
                    for (const auto &arg : t.type_args) {
                        if (arg) {
                            resolve_type(*arg);
                        }
                    }
                },
                [&](const ast::FnType &t) {
                    // P2 (RFC §3.3): first-class Fn type. Recurse into
                    // parameter types and the return type so named/App
                    // types inside a Fn signature (e.g. the Option<U> in
                    // `Fn(T) -> Option<U>`) are resolved like any other
                    // type position. The effect clause capabilities are
                    // resolved separately by resolve_effect_clause.
                    for (const auto &param : t.params) {
                        if (param) {
                            resolve_type(*param);
                        }
                    }
                    if (t.return_type) {
                        resolve_type(*t.return_type);
                    }
                },
                [&](const ast::AppType &t) {
                    // Resolve the base name (same as NamedType).
                    // P2: if this name is a generic type param in scope,
                    // skip resolution — it's an opaque type variable.
                    if (generic_type_params_.count(t.name->spelling()) == 0) {
                        (void)resolve_reference(
                            SymbolNamespace::Types, *t.name, ReferenceKind::TypeName, "type");
                    }
                    // Recurse into type arguments.
                    for (const auto &arg : t.arguments) {
                        if (arg) {
                            resolve_type(*arg);
                        }
                    }
                },
                [](const auto &) { /* leaf types: nothing to resolve */ },
            },
            type.node);
    }

    void resolve_declaration_expr(const ast::ExprSyntax &expr) {
        std::visit(Overloaded{
                       [&](const ast::UnaryExpr &e) { resolve_declaration_expr(*e.operand); },
                       [&](const ast::GroupExpr &e) { resolve_declaration_expr(*e.inner); },
                       [&](const ast::BinaryExpr &e) {
                           resolve_declaration_expr(*e.lhs);
                           resolve_declaration_expr(*e.rhs);
                       },
                       [&](const ast::IndexAccessExpr &e) {
                           resolve_declaration_expr(*e.base);
                           resolve_declaration_expr(*e.index);
                       },
                       [&](const ast::CallExpr &e) {
                           (void)resolve_callable_reference(*e.callee);
                           for (const auto &type_arg : e.type_args) {
                               resolve_type(*type_arg);
                           }
                           for (const auto &arg : e.arguments) {
                               resolve_declaration_expr(*arg);
                           }
                       },
                       [&](const ast::MethodCallExpr &e) {
                           resolve_declaration_expr(*e.receiver);
                           for (const auto &type_arg : e.type_args) {
                               resolve_type(*type_arg);
                           }
                           for (const auto &arg : e.arguments) {
                               resolve_declaration_expr(*arg);
                           }
                       },
                       [&](const ast::StructLiteralExpr &e) {
                           (void)resolve_reference(SymbolNamespace::Types,
                                                   *e.type_name,
                                                   ReferenceKind::TypeName,
                                                   "type");
                           for (const auto &field : e.fields) {
                               resolve_declaration_expr(*field->value);
                           }
                       },
                       [&](const ast::MemberAccessExpr &e) { resolve_declaration_expr(*e.base); },
                       [&](const ast::MatchExpr &e) {
                           // P1b (RFC §1.6): match is a regular expression,
                           // so the scrutinee and each arm's guard/body must be
                           // resolved too — otherwise qualified enum variants
                           // used as an arm body (e.g. `Some(_) => Option::None`)
                           // never get a QualifiedValueOwnerType reference and
                           // the typecheck pass reports UNKNOWN_QUALIFIED_VALUE.
                           resolve_declaration_expr(*e.scrutinee);
                           for (const auto &arm : e.arms) {
                               if (arm->guard) {
                                   resolve_declaration_expr(*arm->guard);
                               }
                               if (arm->body) {
                                   resolve_declaration_expr(*arm->body);
                               }
                           }
                       },
                       [&](const ast::LambdaExpr &e) {
                           // P2 (RFC §6): a closure body is an expression;
                           // resolve it so free-variable references inside the
                           // closure are recorded. Parameter type annotations
                           // are resolved by the fn typecheck pass which owns
                           // the typed environment for generic instantiation.
                           for (const auto &param : e.params) {
                               if (param->type) {
                                   resolve_type(*param->type);
                               }
                           }
                           if (e.body) {
                               resolve_declaration_expr(*e.body);
                           }
                       },
                       [&](const ast::QualifiedValueExpr &e) {
                           if (const auto resolved = lookup(SymbolNamespace::Consts, *e.name);
                               resolved.has_value()) {
                               result_.add_reference(ResolvedReference{
                                   .kind = ReferenceKind::ConstValue,
                                   .text = e.name->spelling(),
                                   .source_id = current_source_id_,
                                   .range = e.name->range,
                                   .target = *resolved,
                               });
                               return;
                           }

                           if (e.name->segments.size() > 1) {
                               const auto owner = owner_name_of(*e.name);
                               (void)resolve_reference(SymbolNamespace::Types,
                                                       owner,
                                                       ReferenceKind::QualifiedValueOwnerType,
                                                       "type");
                           }
                       },
                       [](const auto &) { /* leaf expressions: nothing to resolve */ },
                   },
                   expr.node);
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
                if (source_graph_ != nullptr) {
                    if (auto src = source_unit_for(*symbol->get().source_id); src.has_value()) {
                        emit_error(message, &src->get(), symbol->get().declaration_range);
                    }
                } else {
                    // Single-file mode - emit without source
                    emit_error(message, nullptr, symbol->get().declaration_range);
                }
            }
        }
    }
};

} // namespace detail

ResolveResult::ResolveResult(const ResolveResult &other)
    : symbol_table(other.symbol_table), diagnostics(other.diagnostics),
      references_(other.references_), imports_(other.imports_) {
    invalidate_reference_lookup_cache();
}

ResolveResult &ResolveResult::operator=(const ResolveResult &other) {
    if (this == &other) {
        return *this;
    }

    symbol_table = other.symbol_table;
    diagnostics = other.diagnostics;
    references_ = other.references_;
    imports_ = other.imports_;
    invalidate_reference_lookup_cache();
    return *this;
}

ResolveResult::ResolveResult(ResolveResult &&other) noexcept
    : symbol_table(std::move(other.symbol_table)), diagnostics(std::move(other.diagnostics)),
      references_(std::move(other.references_)), imports_(std::move(other.imports_)) {
    invalidate_reference_lookup_cache();
    other.invalidate_reference_lookup_cache();
}

ResolveResult &ResolveResult::operator=(ResolveResult &&other) noexcept {
    if (this == &other) {
        return *this;
    }

    symbol_table = std::move(other.symbol_table);
    diagnostics = std::move(other.diagnostics);
    references_ = std::move(other.references_);
    imports_ = std::move(other.imports_);
    invalidate_reference_lookup_cache();
    other.invalidate_reference_lookup_cache();
    return *this;
}

MaybeCRef<Symbol> SymbolTable::get(SymbolId id) const {
    if (id.value >= symbols_.size()) {
        return std::nullopt;
    }

    return std::cref(symbols_[id.value]);
}

MaybeCRef<Symbol> SymbolTable::find_local(SymbolNamespace name_space,
                                          std::string_view name,
                                          std::string_view module_name) const {
    const auto &name_index = index(name_space);
    if (!module_name.empty()) {
        if (const auto module_iter = name_index.module_local_names.find(std::string(module_name));
            module_iter != name_index.module_local_names.end()) {
            if (const auto iter = module_iter->second.find(std::string(name));
                iter != module_iter->second.end()) {
                return get(iter->second);
            }
        }

        return std::nullopt;
    }

    std::optional<SymbolId> unique_match;
    for (const auto &[owner_module, local_names] : name_index.module_local_names) {
        (void)owner_module;
        if (const auto iter = local_names.find(std::string(name)); iter != local_names.end()) {
            if (unique_match.has_value()) {
                return std::nullopt;
            }

            unique_match = iter->second;
        }
    }

    if (!unique_match.has_value()) {
        return std::nullopt;
    }

    return get(*unique_match);
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
    case SymbolNamespace::Functions:
        return function_symbols_;
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
    case SymbolNamespace::Functions:
        return function_symbols_;
    }

    return type_symbols_;
}

MaybeCRef<ResolvedReference> ResolveResult::find_reference(
    ReferenceKind kind, SourceRange range, std::optional<SourceId> source_id) const {
    ensure_reference_lookup_cache();

    if (source_id.has_value()) {
        const auto iter = reference_lookup_cache_.find(ReferenceLookupKey{
            .kind = kind,
            .begin_offset = range.begin_offset,
            .end_offset = range.end_offset,
            .source_id = source_id,
        });
        if (iter == reference_lookup_cache_.end()) {
            return std::nullopt;
        }

        return std::cref(references_[iter->second]);
    }

    const auto iter = reference_lookup_no_source_cache_.find(ReferenceLookupNoSourceKey{
        .kind = kind,
        .begin_offset = range.begin_offset,
        .end_offset = range.end_offset,
    });
    if (iter == reference_lookup_no_source_cache_.end()) {
        return std::nullopt;
    }

    return std::cref(references_[iter->second]);
}

void ResolveResult::add_reference(ResolvedReference reference) {
    references_.push_back(std::move(reference));
    invalidate_reference_lookup_cache();
}

void ResolveResult::add_import(ImportBinding binding) {
    imports_.push_back(std::move(binding));
}

std::size_t ResolveResult::ReferenceLookupKeyHash::operator()(
    const ResolveResult::ReferenceLookupKey &key) const noexcept {
    auto seed = std::hash<int>{}(static_cast<int>(key.kind));
    seed = hash_mix(seed, std::hash<std::size_t>{}(key.begin_offset));
    seed = hash_mix(seed, std::hash<std::size_t>{}(key.end_offset));
    seed = hash_mix(seed, std::hash<bool>{}(key.source_id.has_value()));
    if (key.source_id.has_value()) {
        seed = hash_mix(seed, std::hash<std::size_t>{}(key.source_id->value));
    }
    return seed;
}

std::size_t ResolveResult::ReferenceLookupNoSourceKeyHash::operator()(
    const ResolveResult::ReferenceLookupNoSourceKey &key) const noexcept {
    auto seed = std::hash<int>{}(static_cast<int>(key.kind));
    seed = hash_mix(seed, std::hash<std::size_t>{}(key.begin_offset));
    seed = hash_mix(seed, std::hash<std::size_t>{}(key.end_offset));
    return seed;
}

void ResolveResult::rebuild_reference_lookup_cache() const {
    reference_lookup_cache_.clear();
    reference_lookup_no_source_cache_.clear();

    reference_lookup_cache_.reserve(references_.size());
    reference_lookup_no_source_cache_.reserve(references_.size());

    for (std::size_t index = 0; index < references_.size(); ++index) {
        const auto &reference = references_[index];
        reference_lookup_cache_.try_emplace(
            ReferenceLookupKey{
                .kind = reference.kind,
                .begin_offset = reference.range.begin_offset,
                .end_offset = reference.range.end_offset,
                .source_id = reference.source_id,
            },
            index);
        reference_lookup_no_source_cache_.try_emplace(
            ReferenceLookupNoSourceKey{
                .kind = reference.kind,
                .begin_offset = reference.range.begin_offset,
                .end_offset = reference.range.end_offset,
            },
            index);
    }

    reference_lookup_cache_size_ = references_.size();
    reference_lookup_cache_data_ = references_.data();
    reference_lookup_cache_valid_ = true;
}

void ResolveResult::ensure_reference_lookup_cache() const {
    if (!reference_lookup_cache_valid_ || reference_lookup_cache_size_ != references_.size() ||
        reference_lookup_cache_data_ != references_.data()) {
        rebuild_reference_lookup_cache();
    }
}

void ResolveResult::invalidate_reference_lookup_cache() const noexcept {
    reference_lookup_cache_valid_ = false;
}

ResolveResult Resolver::resolve(const ast::Program &program) const {
    detail::ResolverPass pass;
    return pass.run(program);
}

ResolveResult Resolver::resolve(const SourceGraph &graph) const {
    detail::ResolverPass pass;
    return pass.run(graph);
}

} // namespace ahfl
