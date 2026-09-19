#include "compiler/backends/smv/smv_printer.hpp"

namespace ahfl {

using namespace smv;
using namespace smv_detail;

void SmvPrinter::index_declarations(const ir::Program &program) {
    program_ = &program;
    index_.rebuild(program);
    contracts_.clear();
    workflows_.clear();
    state_variables_.clear();
    observation_variables_.clear();
    called_observation_symbols_.clear();
    embedded_observation_symbols_.clear();
    symbol_mappings_.clear();
    defines_.clear();
    assignments_.clear();
    specs_.clear();

    // RFC 0027 P6/P7/P8 (KR6.13-F): one handler per Decl alternative, generated
    // from the X-list decl_nodes.def. Only contract and workflow declarations
    // carry the clauses this index consumes; every other declaration names
    // INDEX_DECL_LEAF (an explicit, named no-op — the same behavior the unnamed
    // catch-all had). The explicit enumeration turns a NEW declaration node into
    // a COMPILE ERROR here until it is classified (CLAUDE.md Principle 5).
#define INDEX_DECL_LEAF(Name) [](const ir::Name &) {},
#define INDEX_ContractDecl(Name)                                                                \
    [&](const ir::Name &contract) { contracts_.push_back(std::cref(contract)); },
#define INDEX_WorkflowDecl(Name)                                                                \
    [&](const ir::Name &workflow) { workflows_.push_back(std::cref(workflow)); },
#define INDEX_ModuleDecl(Name) INDEX_DECL_LEAF(Name)
#define INDEX_ImportDecl(Name) INDEX_DECL_LEAF(Name)
#define INDEX_ConstDecl(Name) INDEX_DECL_LEAF(Name)
#define INDEX_TypeAliasDecl(Name) INDEX_DECL_LEAF(Name)
#define INDEX_StructDecl(Name) INDEX_DECL_LEAF(Name)
#define INDEX_EnumDecl(Name) INDEX_DECL_LEAF(Name)
#define INDEX_CapabilityDecl(Name) INDEX_DECL_LEAF(Name)
#define INDEX_PredicateDecl(Name) INDEX_DECL_LEAF(Name)
#define INDEX_AgentDecl(Name) INDEX_DECL_LEAF(Name)
#define INDEX_FlowDecl(Name) INDEX_DECL_LEAF(Name)
#define INDEX_FnDecl(Name) INDEX_DECL_LEAF(Name)
#define INDEX_TraitDecl(Name) INDEX_DECL_LEAF(Name)
#define INDEX_ImplDecl(Name) INDEX_DECL_LEAF(Name)
#define INDEX_InstanceDecl(Name) INDEX_DECL_LEAF(Name)
#define HANDLE_DECL_NODE(Name) INDEX_##Name(Name)
    for (const auto &declaration : program.declarations) {
        std::visit(
            Overloaded{
#include "ahfl/compiler/ir/decl_nodes.def"
            },
            declaration);
    }
#undef HANDLE_DECL_NODE
#undef INDEX_ModuleDecl
#undef INDEX_ImportDecl
#undef INDEX_ConstDecl
#undef INDEX_TypeAliasDecl
#undef INDEX_StructDecl
#undef INDEX_EnumDecl
#undef INDEX_CapabilityDecl
#undef INDEX_PredicateDecl
#undef INDEX_AgentDecl
#undef INDEX_ContractDecl
#undef INDEX_FlowDecl
#undef INDEX_WorkflowDecl
#undef INDEX_FnDecl
#undef INDEX_TraitDecl
#undef INDEX_ImplDecl
#undef INDEX_InstanceDecl
#undef INDEX_DECL_LEAF
}

void SmvPrinter::index_observations(const ir::Program &program) {
    for (const auto &observation : ir::formal_observations(program)) {
        std::visit(
            Overloaded{
                [&](const ir::CalledCapabilityObservation &value) {
                    called_observation_symbols_.emplace(
                        called_observation_key(value.agent, value.capability), observation.symbol);
                    // Attach the capability declaration's source range when we
                    // can resolve it, so a counterexample can point at the
                    // capability call site.  Falls back to a description-only
                    // mapping (no source offsets) when the capability is not
                    // found — never fabricates a range.
                    const auto capability = find_capability(value.capability);
                    if (capability.has_value()) {
                        add_symbol_mapping(
                            observation.symbol,
                            with_source("agent " + value.agent + " called " + value.capability,
                                        capability->get().provenance.source_path,
                                        capability->get().provenance.source_range));
                    } else {
                        add_symbol_mapping(observation.symbol,
                                           "agent " + value.agent + " called " + value.capability);
                    }
                },
                [&](const ir::EmbeddedBoolObservation &value) {
                    if (!embedded_observation_requires_variable(value.scope)) {
                        return;
                    }

                    observation_variables_.push_back(observation.symbol + " : boolean;");
                    embedded_observation_symbols_.emplace(
                        embedded_observation_key(value.scope.kind,
                                                 value.scope.owner,
                                                 value.scope.clause_index,
                                                 value.scope.atom_index),
                        observation.symbol);
                    add_symbol_mapping(
                        observation.symbol,
                        "observation " + smv::observation_scope_kind_key(value.scope.kind) + " " +
                            value.scope.owner + "[" + std::to_string(value.scope.clause_index) +
                            "]" + observation_source_suffix(value.scope));
                },
            },
            observation.node);
    }
}

MaybeCRef<ir::AgentDecl> SmvPrinter::find_agent(std::string_view canonical_name) const {
    if (const auto *agent = index_.find_agent(canonical_name); agent != nullptr) {
        return std::cref(*agent);
    }

    return std::nullopt;
}

bool SmvPrinter::agent_has_contract(const ir::AgentDecl &agent) const {
    const auto agent_name = ir::symbol_canonical_name(agent.symbol_ref, agent.name);
    return std::ranges::any_of(contracts_, [&](const auto &contract) {
        return ir::symbol_canonical_name(contract.get().target_ref) == agent_name;
    });
}

std::unordered_map<std::string, SmvWorkflowNodeInfo>
SmvPrinter::build_workflow_node_map(const ir::WorkflowDecl &workflow) const {
    std::unordered_map<std::string, SmvWorkflowNodeInfo> nodes;

    for (const auto &node : workflow.nodes) {
        const auto agent = find_agent(ir::symbol_canonical_name(node.target_ref));
        if (!agent.has_value()) {
            continue;
        }

        nodes.emplace(node.name,
                      SmvWorkflowNodeInfo{
                          .node = std::cref(node),
                          .agent = std::cref(agent->get()),
                      });
    }

    return nodes;
}

MaybeCRef<ir::FlowDecl> SmvPrinter::find_flow(std::string_view canonical_name) const {
    if (const auto *flow = index_.find_flow_for_agent(canonical_name); flow != nullptr) {
        return std::cref(*flow);
    }

    return std::nullopt;
}

MaybeCRef<ir::CapabilityDecl> SmvPrinter::find_capability(std::string_view canonical_name) const {
    if (const auto *capability = index_.find_capability(canonical_name); capability != nullptr) {
        return std::cref(*capability);
    }

    const auto match = std::ranges::find_if(index_.capabilities(), [&](const auto *capability) {
        return capability_name_matches(capability->name, canonical_name);
    });
    if (match != index_.capabilities().end()) {
        return std::cref(**match);
    }

    return std::nullopt;
}

void SmvPrinter::add_symbol_mapping(const std::string &symbol, const std::string &description) {
    const auto mapping = symbol + " => " + description;
    if (std::ranges::find(symbol_mappings_, mapping) == symbol_mappings_.end()) {
        symbol_mappings_.push_back(mapping);
    }
}

std::string SmvPrinter::with_source(std::string description,
                                    std::string_view source_path,
                                    const ir::SourceRangeOpt &range) const {
    description += source_suffix(source_path, range);
    return description;
}

std::string SmvPrinter::contract_clause_source_suffix(std::string_view owner,
                                                      std::size_t clause_index) const {
    for (const auto &contract : contracts_) {
        if (ir::symbol_canonical_name(contract.get().target_ref) != owner ||
            clause_index >= contract.get().clauses.size()) {
            continue;
        }

        const auto &clause = contract.get().clauses[clause_index];
        return source_suffix(contract.get().provenance.source_path, clause.source_range);
    }

    return {};
}

std::string SmvPrinter::workflow_clause_source_suffix(std::string_view owner,
                                                      ir::FormalObservationScopeKind kind,
                                                      std::size_t clause_index) const {
    for (const auto &workflow : workflows_) {
        if (workflow.get().name != owner) {
            continue;
        }

        if (kind == ir::FormalObservationScopeKind::WorkflowSafetyClause &&
            clause_index < workflow.get().safety.size()) {
            return source_suffix(workflow.get().provenance.source_path,
                                 workflow.get().safety[clause_index]->source_range);
        }

        if (kind == ir::FormalObservationScopeKind::WorkflowLivenessClause &&
            clause_index < workflow.get().liveness.size()) {
            return source_suffix(workflow.get().provenance.source_path,
                                 workflow.get().liveness[clause_index]->source_range);
        }
    }

    return {};
}

std::string SmvPrinter::observation_source_suffix(const ir::FormalObservationScope &scope) const {
    if (scope.kind == ir::FormalObservationScopeKind::ContractClause) {
        return contract_clause_source_suffix(scope.owner, scope.clause_index);
    }

    return workflow_clause_source_suffix(scope.owner, scope.kind, scope.clause_index);
}

const ir::Expr *SmvPrinter::find_embedded_expr_by_atom(const ir::TemporalExpr &expr,
                                                       std::size_t target_atom,
                                                       std::size_t &current_atom) const {
    return std::visit(
        Overloaded{
            [&](const ir::EmbeddedTemporalExpr &value) -> const ir::Expr * {
                if (current_atom == target_atom) {
                    return value.expr.get();
                }
                ++current_atom;
                return nullptr;
            },
            [&](const ir::TemporalUnaryExpr &value) -> const ir::Expr * {
                return find_embedded_expr_by_atom(*value.operand, target_atom, current_atom);
            },
            [&](const ir::TemporalBinaryExpr &value) -> const ir::Expr * {
                if (const auto *lhs =
                        find_embedded_expr_by_atom(*value.lhs, target_atom, current_atom);
                    lhs != nullptr) {
                    return lhs;
                }
                return find_embedded_expr_by_atom(*value.rhs, target_atom, current_atom);
            },
            // RFC 0027 P6/P7/P8 (KR6.13-F): one handler per TemporalExprNode
            // alternative, generated from temporal_nodes.def. Only embedded /
            // unary / binary atoms nest an embedded expression, so those three
            // route above; the called / in_state / running / completed atoms
            // carry none and name FIND_ATOM_LEAF (explicit, named no-op — the
            // same nullptr the unnamed catch-all returned). The enumeration turns
            // a NEW temporal node into a COMPILE ERROR here until it is routed.
#define FIND_ATOM_LEAF(Name)                                                                    \
    [](const ir::Name &) -> const ir::Expr * { return nullptr; },
#define FIND_EmbeddedTemporalExpr(Name)
#define FIND_TemporalUnaryExpr(Name)
#define FIND_TemporalBinaryExpr(Name)
#define FIND_CalledTemporalExpr(Name) FIND_ATOM_LEAF(Name)
#define FIND_InStateTemporalExpr(Name) FIND_ATOM_LEAF(Name)
#define FIND_RunningTemporalExpr(Name) FIND_ATOM_LEAF(Name)
#define FIND_CompletedTemporalExpr(Name) FIND_ATOM_LEAF(Name)
#define HANDLE_TEMPORAL_NODE(Name) FIND_##Name(Name)
#include "ahfl/compiler/ir/temporal_nodes.def"
#undef HANDLE_TEMPORAL_NODE
#undef FIND_EmbeddedTemporalExpr
#undef FIND_TemporalUnaryExpr
#undef FIND_TemporalBinaryExpr
#undef FIND_CalledTemporalExpr
#undef FIND_InStateTemporalExpr
#undef FIND_RunningTemporalExpr
#undef FIND_CompletedTemporalExpr
#undef FIND_ATOM_LEAF
        },
        expr.node);
}

const ir::Expr *
SmvPrinter::find_embedded_observation_expr(const ir::FormalObservationScope &scope) const {
    if (scope.kind == ir::FormalObservationScopeKind::ContractClause) {
        for (const auto &contract : contracts_) {
            if (ir::symbol_canonical_name(contract.get().target_ref) != scope.owner ||
                scope.clause_index >= contract.get().clauses.size()) {
                continue;
            }

            const auto &clause = contract.get().clauses[scope.clause_index];
            if (const auto *expr = std::get_if<ir::ExprRef>(&clause.value);
                expr != nullptr && scope.atom_index == 0) {
                return expr->get();
            }

            if (const auto *temporal = std::get_if<ir::TemporalExprPtr>(&clause.value);
                temporal != nullptr) {
                std::size_t current_atom = 0;
                return find_embedded_expr_by_atom(**temporal, scope.atom_index, current_atom);
            }
        }
        return nullptr;
    }

    for (const auto &workflow : workflows_) {
        if (workflow.get().name != scope.owner) {
            continue;
        }

        if (scope.kind == ir::FormalObservationScopeKind::WorkflowSafetyClause &&
            scope.clause_index < workflow.get().safety.size()) {
            std::size_t current_atom = 0;
            return find_embedded_expr_by_atom(
                *workflow.get().safety[scope.clause_index], scope.atom_index, current_atom);
        }

        if (scope.kind == ir::FormalObservationScopeKind::WorkflowLivenessClause &&
            scope.clause_index < workflow.get().liveness.size()) {
            std::size_t current_atom = 0;
            return find_embedded_expr_by_atom(
                *workflow.get().liveness[scope.clause_index], scope.atom_index, current_atom);
        }
    }

    return nullptr;
}

bool SmvPrinter::embedded_observation_requires_variable(
    const ir::FormalObservationScope &scope) const {
    const auto *expr = find_embedded_observation_expr(scope);
    return expr == nullptr || !render_bounded_expr(*expr).has_value();
}

bool SmvPrinter::handler_calls_capability(const ir::StateHandler::Summary &summary,
                                          std::string_view capability_name) const {
    return std::ranges::any_of(summary.called_targets, [&](const auto &target) {
        return capability_name_matches(target, capability_name);
    });
}

std::string SmvPrinter::render_workflow_node_state_guard(const ir::WorkflowDecl &workflow,
                                                         const ir::WorkflowNode &node,
                                                         const ir::StateHandler &handler) const {
    return "(" + workflow_node_running_var(workflow, node.name) + " & (" +
           workflow_node_state_var(workflow, node.name) + " = " + handler.state_name + "))";
}

std::vector<std::string> SmvPrinter::collect_called_guards(std::string_view agent_name,
                                                           std::string_view capability_name) const {
    std::vector<std::string> guards;

    for (const auto &workflow : workflows_) {
        for (const auto &node : workflow.get().nodes) {
            const auto target = ir::symbol_canonical_name(node.target_ref);
            if (target != agent_name) {
                continue;
            }

            const auto flow = find_flow(target);
            if (!flow.has_value()) {
                continue;
            }

            for (const auto &handler : flow->get().state_handlers) {
                const auto *summary =
                    program_ == nullptr
                        ? nullptr
                        : ir::find_state_handler_summary(*program_, flow->get(), handler);
                if (summary != nullptr && handler_calls_capability(*summary, capability_name)) {
                    guards.push_back(
                        render_workflow_node_state_guard(workflow.get(), node, handler));
                }
            }
        }
    }

    if (!guards.empty()) {
        return guards;
    }

    const auto agent = find_agent(agent_name);
    const auto flow = find_flow(agent_name);
    if (!agent.has_value() || !flow.has_value() || !agent_has_contract(agent->get())) {
        return guards;
    }

    for (const auto &handler : flow->get().state_handlers) {
        const auto *summary = program_ == nullptr
                                  ? nullptr
                                  : ir::find_state_handler_summary(*program_, flow->get(), handler);
        if (summary != nullptr && handler_calls_capability(*summary, capability_name)) {
            guards.push_back("(" + agent_state_var(agent->get()) + " = " + handler.state_name +
                             ")");
        }
    }

    return guards;
}

} // namespace ahfl
