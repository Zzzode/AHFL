#include "ahfl/compiler/semantics/validate.hpp"

#include "ahfl/compiler/frontend/frontend.hpp"

#include "ahfl/base/support/overloaded.hpp"

#include <algorithm>
#include <cassert>
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

struct ControlFlowSummary {
    bool may_fallthrough{true};
    bool saw_goto{false};
    bool saw_return{false};
};

[[nodiscard]] bool is_error_type(const Type &type) noexcept {
    return type.holds<types::ErrorT>();
}

[[nodiscard]] bool is_bool_type(const Type &type) noexcept {
    return type.holds<types::BoolT>();
}

[[nodiscard]] bool contains_name(const std::vector<std::string> &names, std::string_view name) {
    return std::find(names.begin(), names.end(), name) != names.end();
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

class ValidationPass final {
  public:
    ValidationPass(const ast::Program &program,
                   const ResolveResult &resolve_result,
                   const TypeCheckResult &type_check_result)
        : program_(&program), resolve_result_(resolve_result),
          type_check_result_(type_check_result) {}
    ValidationPass(const SourceGraph &graph,
                   const ResolveResult &resolve_result,
                   const TypeCheckResult &type_check_result)
        : graph_(&graph), resolve_result_(resolve_result), type_check_result_(type_check_result) {}

    [[nodiscard]] ValidationResult run() {
        index_declarations();
        check_agents();
        check_contracts();
        check_flows();
        check_workflows();
        // P4.S3: Walk every contract clause in the typed program even though
        // the ValidationPass does not yet enforce decreases-related rules.
        // This keeps the traversal gate explicit so S5b has an insertion point.
        walk_typed_contract_clauses();
        // P4.S5a / R-04: Fill decreases counters via a dedicated pass that only
        // increments on the ContractClauseKind::Decreases arm.
        count_contracts();
        // R-04 invariants: a missed traversal leaves counters at zero (S5b
        // negative-control safety net), and a wildcard clause is always a
        // total clause — wildcards are a strict subset of decreases.
        assert(result_.wildcard_decreases_clauses <= result_.total_decreases_clauses &&
               "wildcard decreases cannot exceed total decreases");
        assert((result_.total_decreases_clauses == 0 ||
                result_.total_decreases_clauses >= result_.wildcard_decreases_clauses) &&
               "decreases counters are zero or satisfy wildcard <= total");
        return std::move(result_);
    }

  private:
    const ast::Program *program_{nullptr};
    const SourceGraph *graph_{nullptr};
    const ResolveResult &resolve_result_;
    const TypeCheckResult &type_check_result_;
    ValidationResult result_;
    const SourceUnit *current_source_{nullptr};
    std::optional<SourceId> current_source_id_;
    std::string current_module_name_;

    std::unordered_map<std::size_t, std::reference_wrapper<const ast::AgentDecl>> agent_decls_;

    void index_program_declarations(const ast::Program &program) {
        for (const auto &declaration : program.declarations) {
            if (declaration->kind != ast::NodeKind::AgentDecl) {
                continue;
            }

            const auto &decl = static_cast<const ast::AgentDecl &>(*declaration);
            const auto symbol = find_local_here(SymbolNamespace::Agents, decl.name);
            if (!symbol.has_value()) {
                continue;
            }

            agent_decls_.emplace(symbol->get().id.value, std::cref(decl));
        }
    }

    void enter_source(const SourceUnit &source) {
        current_source_ = &source;
        current_source_id_ = source.id;
        current_module_name_ = source.module_name;
    }

    void leave_source() {
        current_source_ = nullptr;
        current_source_id_.reset();
        current_module_name_.clear();
    }

    [[nodiscard]] MaybeCRef<SourceUnit> source_unit_for(SourceId id) const {
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

    template <typename Fn> decltype(auto) with_symbol_context(SymbolId id, Fn &&fn) {
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
                if (const auto source = source_unit_for(*symbol->get().source_id);
                    source.has_value()) {
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

    [[nodiscard]] MaybeCRef<Symbol> find_local_here(SymbolNamespace name_space,
                                                    std::string_view name) const {
        if (!current_module_name_.empty()) {
            return resolve_result_.symbol_table.find_local(name_space, name, current_module_name_);
        }

        return resolve_result_.symbol_table.find_local(name_space, name);
    }

    [[nodiscard]] MaybeCRef<ResolvedReference> find_reference_here(ReferenceKind kind,
                                                                   SourceRange range) const {
        return resolve_result_.find_reference(kind, range, current_source_id_);
    }

    void validation_error_here(ErrorCode<DiagnosticCategory::Validation> code,
                               std::string message,
                               SourceRange range) {
        if (current_source_ != nullptr) {
            result_.diagnostics.error()
                .code(code)
                .message(std::move(message))
                .range(range)
                .source(current_source_->source)
                .emit();
        } else {
            result_.diagnostics.error().code(code).message(std::move(message)).range(range).emit();
        }
    }

    void invalid_state_here(std::string message, SourceRange range) {
        validation_error_here(error_codes::validation::InvalidState, std::move(message), range);
    }

    void invalid_temporal_here(std::string message, SourceRange range) {
        validation_error_here(
            error_codes::validation::InvalidTemporalFormula, std::move(message), range);
    }

    void invalid_workflow_here(std::string message, SourceRange range) {
        validation_error_here(
            error_codes::validation::InvalidWorkflowGraph, std::move(message), range);
    }

    void index_declarations() {
        if (graph_ != nullptr) {
            for (const auto &source : graph_->sources) {
                enter_source(source);
                index_program_declarations(require(
                    source.program.get(), "source graph program must exist before validate"));
                leave_source();
            }
            return;
        }

        index_program_declarations(require(program_, "validate program must exist"));
    }

    [[nodiscard]] MaybeCRef<Symbol> symbol_of(SymbolId id) const {
        return resolve_result_.symbol_table.get(id);
    }

    [[nodiscard]] MaybeCRef<ast::AgentDecl> agent_decl_of(SymbolId id) const {
        return find_decl_ref(agent_decls_, id);
    }

    void check_typed_temporal_expr(const ast::ExprSyntax &expr) {
        // Prefer NodeId-based lookup so overlapping ranges in temporal formulas
        // do not alias each other.
        const TypedExpr *expr_info =
            type_check_result_.typed_program.find_expr(expr.node_id, current_source_id_);
        if (expr_info == nullptr) {
            expr_info =
                type_check_result_.typed_program.find_expr_by_range(expr.range, current_source_id_);
        }
        if (expr_info == nullptr || expr_info->type == nullptr) {
            return;
        }

        if (!is_bool_type(*expr_info->type) && !is_error_type(*expr_info->type)) {
            invalid_temporal_here(
                messages::validation::TemporalEmbeddedExprMustBeBool.format_with(), expr.range);
        }

        if (!expr_info->is_pure) {
            invalid_temporal_here(
                messages::validation::TemporalEmbeddedExprMustBePure.format_with(), expr.range);
        }
    }

    void check_agents() {
        for (const auto &[id, decl] : agent_decls_) {
            with_symbol_context(SymbolId{id}, [&]() {
                std::unordered_set<std::string> states;
                std::unordered_set<std::string> final_states;
                std::unordered_set<std::string> capabilities;
                std::unordered_map<std::string, std::vector<std::string>> adjacency;

                for (const auto &state : decl.get().states) {
                    if (!states.insert(state).second) {
                        invalid_state_here(
                            messages::validation::DuplicateAgentState.format_with(state),
                            decl.get().states_range);
                    }
                }

                if (!states.contains(decl.get().initial_state)) {
                    invalid_state_here(messages::validation::InitialStateNotDeclared.format_with(
                                           decl.get().initial_state),
                                       decl.get().initial_state_range);
                }

                for (const auto &final_state : decl.get().final_states) {
                    if (!final_states.insert(final_state).second) {
                        invalid_state_here(
                            messages::validation::DuplicateFinalState.format_with(final_state),
                            decl.get().final_states_range);
                    }

                    if (!states.contains(final_state)) {
                        invalid_state_here(
                            messages::validation::FinalStateNotDeclared.format_with(final_state),
                            decl.get().final_states_range);
                    }
                }

                for (const auto &capability : decl.get().capabilities) {
                    if (!capabilities.insert(capability).second) {
                        validation_error_here(
                            error_codes::validation::DuplicateCapability,
                            messages::validation::DuplicateCapability.format_with(capability),
                            decl.get().capabilities_range);
                    }
                }

                for (const auto &transition : decl.get().transitions) {
                    if (!states.contains(transition->from_state)) {
                        invalid_state_here(
                            messages::validation::TransitionSourceNotDeclared.format_with(
                                transition->from_state),
                            transition->range);
                    }

                    if (!states.contains(transition->to_state)) {
                        invalid_state_here(
                            messages::validation::TransitionTargetNotDeclared.format_with(
                                transition->to_state),
                            transition->range);
                    }

                    if (final_states.contains(transition->from_state)) {
                        invalid_state_here(
                            messages::validation::FinalStateOutgoingTransition.format_with(
                                transition->from_state),
                            transition->range);
                    }

                    adjacency[transition->from_state].push_back(transition->to_state);
                }

                if (!states.contains(decl.get().initial_state)) {
                    return;
                }

                std::unordered_set<std::string> reachable;
                std::vector<std::string> worklist{decl.get().initial_state};
                reachable.insert(decl.get().initial_state);

                for (std::size_t index = 0; index < worklist.size(); ++index) {
                    const auto adjacency_iter = adjacency.find(worklist[index]);
                    if (adjacency_iter == adjacency.end()) {
                        continue;
                    }

                    for (const auto &next_state : adjacency_iter->second) {
                        if (reachable.insert(next_state).second) {
                            worklist.push_back(next_state);
                        }
                    }
                }

                for (const auto &state : decl.get().states) {
                    if (!reachable.contains(state)) {
                        invalid_state_here(messages::validation::UnreachableAgentState.format_with(
                                               state, decl.get().initial_state),
                                           decl.get().states_range);
                    }
                }
            });
        }
    }

    void check_contract_temporal_expr(const ast::TemporalExprSyntax &expr,
                                      const ast::AgentDecl &agent_decl,
                                      std::string_view agent_name) {
        std::visit(
            Overloaded{
                [&](const ast::EmbeddedTemporalExpr &e) {
                    if (e.expr) {
                        check_typed_temporal_expr(*e.expr);
                    }
                },
                [&](const ast::CalledTemporalExpr &) {
                    // Called is valid in contract context
                },
                [&](const ast::InStateTemporalExpr &e) {
                    const auto state =
                        std::find(agent_decl.states.begin(), agent_decl.states.end(), e.name);
                    if (state == agent_decl.states.end()) {
                        invalid_temporal_here(
                            messages::validation::UnknownContractState.format_with(e.name,
                                                                                   agent_name),
                            expr.range);
                    }
                },
                [&](const ast::RunningTemporalExpr &) {
                    invalid_temporal_here(messages::validation::RunningOnlyWorkflow.format_with(),
                                          expr.range);
                },
                [&](const ast::CompletedTemporalExpr &) {
                    invalid_temporal_here(messages::validation::CompletedOnlyWorkflow.format_with(),
                                          expr.range);
                },
                [&](const ast::UnaryTemporalExpr &e) {
                    if (e.operand) {
                        check_contract_temporal_expr(*e.operand, agent_decl, agent_name);
                    }
                },
                [&](const ast::BinaryTemporalExpr &e) {
                    if (e.lhs) {
                        check_contract_temporal_expr(*e.lhs, agent_decl, agent_name);
                    }
                    if (e.rhs) {
                        check_contract_temporal_expr(*e.rhs, agent_decl, agent_name);
                    }
                },
            },
            expr.node);
    }

    void check_contracts_in_program(const ast::Program &program) {
        for (const auto &declaration : program.declarations) {
            if (declaration->kind != ast::NodeKind::ContractDecl) {
                continue;
            }

            const auto &decl = static_cast<const ast::ContractDecl &>(*declaration);
            const auto target =
                find_reference_here(ReferenceKind::ContractTarget, decl.target->range);
            if (!target.has_value()) {
                continue;
            }

            const auto agent_decl = agent_decl_of(target->get().target);
            if (!agent_decl.has_value()) {
                continue;
            }

            const auto agent_symbol = symbol_of(target->get().target);
            const auto agent_name = agent_symbol.has_value() ? agent_symbol->get().canonical_name
                                                             : std::string("<agent>");

            for (const auto &clause : decl.clauses) {
                if (!clause->temporal_expr) {
                    continue;
                }

                check_contract_temporal_expr(*clause->temporal_expr, agent_decl->get(), agent_name);
            }
        }
    }

    void check_contracts() {
        if (graph_ != nullptr) {
            for (const auto &source : graph_->sources) {
                enter_source(source);
                check_contracts_in_program(require(
                    source.program.get(), "source graph program must exist before validate"));
                leave_source();
            }
            return;
        }

        check_contracts_in_program(require(program_, "validate program must exist"));
    }

    // R-04: explicit typed-program walk over every contract clause.
    // Today this is a no-op traversal whose sole purpose is to keep the
    // iteration surface visible for S5a. Decreases-specific validation
    // should be inserted here instead of being scattered into AST-level
    // check_contracts() above, because the decreases metadata lives on
    // TypedProgram/ContractClauseInfo after P4.S3 plumbing.
    void walk_typed_contract_clauses_in_program(const ast::Program &program) {
        (void)program;
        const auto &typed_decls = type_check_result_.typed_program.declarations;
        for (const auto &decl : typed_decls) {
            if (decl.kind != ast::NodeKind::ContractDecl) {
                continue;
            }
            const auto *contract_info = std::get_if<ContractTypeInfo>(&decl.payload);
            if (contract_info == nullptr) {
                continue;
            }
            for (const auto &clause : contract_info->clauses) {
                // TODO(P4.S5a): validate clause.decreases_{exprs,is_wildcard,range}
                // and cross-clause termination ranking rules here.
                (void)clause;
            }
        }
    }

    void walk_typed_contract_clauses() {
        if (graph_ != nullptr) {
            for (const auto &source : graph_->sources) {
                enter_source(source);
                walk_typed_contract_clauses_in_program(require(
                    source.program.get(), "source graph program must exist before validate"));
                leave_source();
            }
            return;
        }

        walk_typed_contract_clauses_in_program(
            require(program_, "validate program must exist"));
    }

    [[nodiscard]] bool has_transition(const ast::AgentDecl &agent_decl,
                                      std::string_view from_state,
                                      std::string_view to_state) const {
        for (const auto &transition : agent_decl.transitions) {
            if (transition->from_state == from_state && transition->to_state == to_state) {
                return true;
            }
        }

        return false;
    }

    [[nodiscard]] ControlFlowSummary analyze_statement(const ast::StatementSyntax &statement,
                                                       const ast::AgentDecl &agent_decl,
                                                       std::string_view handler_state,
                                                       bool is_final_handler) {
        switch (statement.kind) {
        case ast::StatementSyntaxKind::Let:
        case ast::StatementSyntaxKind::Assign:
        case ast::StatementSyntaxKind::Assert:
        case ast::StatementSyntaxKind::Expr:
            return ControlFlowSummary{};
        case ast::StatementSyntaxKind::Goto: {
            if (!has_transition(agent_decl, handler_state, statement.goto_stmt->target_state)) {
                invalid_state_here(messages::validation::IllegalGoto.format_with(
                                       handler_state, statement.goto_stmt->target_state),
                                   statement.goto_stmt->range);
            }

            return ControlFlowSummary{
                .may_fallthrough = false,
                .saw_goto = true,
            };
        }
        case ast::StatementSyntaxKind::Return:
            if (!is_final_handler) {
                invalid_state_here(
                    messages::validation::ReturnOnlyAllowedInFinalHandler.format_with(),
                    statement.return_stmt->range);
            }

            return ControlFlowSummary{
                .may_fallthrough = false,
                .saw_return = true,
            };
        case ast::StatementSyntaxKind::If: {
            const auto then_summary = analyze_block(
                *statement.if_stmt->then_block, agent_decl, handler_state, is_final_handler);
            const auto else_summary = statement.if_stmt->else_block
                                          ? analyze_block(*statement.if_stmt->else_block,
                                                          agent_decl,
                                                          handler_state,
                                                          is_final_handler)
                                          : ControlFlowSummary{};

            return ControlFlowSummary{
                .may_fallthrough = !statement.if_stmt->else_block || then_summary.may_fallthrough ||
                                   else_summary.may_fallthrough,
                .saw_goto = then_summary.saw_goto || else_summary.saw_goto,
                .saw_return = then_summary.saw_return || else_summary.saw_return,
            };
        }
        }

        return ControlFlowSummary{};
    }

    [[nodiscard]] ControlFlowSummary analyze_block(const ast::BlockSyntax &block,
                                                   const ast::AgentDecl &agent_decl,
                                                   std::string_view handler_state,
                                                   bool is_final_handler) {
        ControlFlowSummary summary;

        for (const auto &statement : block.statements) {
            if (!summary.may_fallthrough) {
                break;
            }

            const auto statement_summary =
                analyze_statement(*statement, agent_decl, handler_state, is_final_handler);
            summary.saw_goto = summary.saw_goto || statement_summary.saw_goto;
            summary.saw_return = summary.saw_return || statement_summary.saw_return;
            summary.may_fallthrough = statement_summary.may_fallthrough;
        }

        return summary;
    }

    void check_flows_in_program(const ast::Program &program) {
        for (const auto &declaration : program.declarations) {
            if (declaration->kind != ast::NodeKind::FlowDecl) {
                continue;
            }

            const auto &decl = static_cast<const ast::FlowDecl &>(*declaration);
            const auto target = find_reference_here(ReferenceKind::FlowTarget, decl.target->range);
            if (!target.has_value()) {
                continue;
            }

            const auto agent_decl = agent_decl_of(target->get().target);
            if (!agent_decl.has_value()) {
                continue;
            }

            std::unordered_set<std::string> known_states(agent_decl->get().states.begin(),
                                                         agent_decl->get().states.end());
            std::unordered_set<std::string> final_states(agent_decl->get().final_states.begin(),
                                                         agent_decl->get().final_states.end());
            std::unordered_map<std::string, std::reference_wrapper<const ast::StateHandlerSyntax>>
                handlers;

            for (const auto &handler : decl.state_handlers) {
                if (!known_states.contains(handler->state_name)) {
                    invalid_state_here(
                        messages::validation::FlowHandlerStateNotDeclared.format_with(
                            handler->state_name),
                        handler->range);
                    continue;
                }

                if (!handlers.emplace(handler->state_name, std::cref(*handler)).second) {
                    invalid_state_here(
                        messages::validation::DuplicateFlowHandler.format_with(handler->state_name),
                        handler->range);
                }
            }

            for (const auto &state : agent_decl->get().states) {
                if (handlers.contains(state)) {
                    continue;
                }

                if (final_states.contains(state)) {
                    invalid_state_here(
                        messages::validation::MissingFinalStateHandler.format_with(state),
                        decl.range);
                } else {
                    invalid_state_here(
                        messages::validation::MissingNonFinalStateHandler.format_with(state),
                        decl.range);
                }
            }

            for (const auto &handler : decl.state_handlers) {
                if (!known_states.contains(handler->state_name)) {
                    continue;
                }

                const bool is_final_handler = final_states.contains(handler->state_name);
                const auto summary = analyze_block(
                    *handler->body, agent_decl->get(), handler->state_name, is_final_handler);

                if (is_final_handler) {
                    if (summary.may_fallthrough || summary.saw_goto || !summary.saw_return) {
                        invalid_state_here(
                            messages::validation::FinalStateHandlerMustReturn.format_with(
                                handler->state_name),
                            handler->range);
                    }
                } else {
                    if (summary.may_fallthrough || summary.saw_return || !summary.saw_goto) {
                        invalid_state_here(
                            messages::validation::NonFinalStateHandlerMustGoto.format_with(
                                handler->state_name),
                            handler->range);
                    }
                }
            }
        }
    }

    void check_flows() {
        if (graph_ != nullptr) {
            for (const auto &source : graph_->sources) {
                enter_source(source);
                check_flows_in_program(require(source.program.get(),
                                               "source graph program must exist before validate"));
                leave_source();
            }
            return;
        }

        check_flows_in_program(require(program_, "validate program must exist"));
    }

    void
    check_workflow_temporal_expr(const ast::TemporalExprSyntax &expr,
                                 const std::unordered_map<std::string, SymbolId> &node_agent_ids) {
        std::visit(
            Overloaded{
                [&](const ast::EmbeddedTemporalExpr &e) {
                    if (e.expr) {
                        check_typed_temporal_expr(*e.expr);
                    }
                },
                [&](const ast::CalledTemporalExpr &) {
                    invalid_temporal_here(
                        messages::validation::CalledOnlyAgentContracts.format_with(), expr.range);
                },
                [&](const ast::InStateTemporalExpr &) {
                    invalid_temporal_here(
                        messages::validation::InStateOnlyAgentContracts.format_with(), expr.range);
                },
                [&](const ast::RunningTemporalExpr &e) {
                    if (!node_agent_ids.contains(e.name)) {
                        invalid_temporal_here(
                            messages::validation::UnknownWorkflowNode.format_with(e.name),
                            expr.range);
                    }
                },
                [&](const ast::CompletedTemporalExpr &e) {
                    const auto node = node_agent_ids.find(e.name);
                    if (node == node_agent_ids.end()) {
                        invalid_temporal_here(
                            messages::validation::UnknownWorkflowNode.format_with(e.name),
                            expr.range);
                        return;
                    }

                    if (!e.state_name.has_value()) {
                        return;
                    }

                    const auto agent_decl = agent_decl_of(node->second);
                    if (!agent_decl.has_value()) {
                        return;
                    }

                    if (!contains_name(agent_decl->get().final_states, *e.state_name)) {
                        invalid_temporal_here(
                            messages::validation::WorkflowCompletedStateNotFinal.format_with(
                                *e.state_name, e.name),
                            expr.range);
                    }
                },
                [&](const ast::UnaryTemporalExpr &e) {
                    if (e.operand) {
                        check_workflow_temporal_expr(*e.operand, node_agent_ids);
                    }
                },
                [&](const ast::BinaryTemporalExpr &e) {
                    if (e.lhs) {
                        check_workflow_temporal_expr(*e.lhs, node_agent_ids);
                    }
                    if (e.rhs) {
                        check_workflow_temporal_expr(*e.rhs, node_agent_ids);
                    }
                },
            },
            expr.node);
    }

    void check_workflows_in_program(const ast::Program &program) {
        for (const auto &declaration : program.declarations) {
            if (declaration->kind != ast::NodeKind::WorkflowDecl) {
                continue;
            }

            const auto &decl = static_cast<const ast::WorkflowDecl &>(*declaration);
            std::unordered_map<std::string, SymbolId> node_agent_ids;
            std::unordered_map<std::string, std::vector<std::string>> dependency_graph;
            std::unordered_set<std::string> node_names;

            for (const auto &node : decl.nodes) {
                if (!node_names.insert(node->name).second) {
                    invalid_workflow_here(
                        messages::validation::DuplicateWorkflowNode.format_with(node->name),
                        node->range);
                    continue;
                }

                const auto target =
                    find_reference_here(ReferenceKind::WorkflowNodeTarget, node->target->range);
                if (target.has_value()) {
                    node_agent_ids.emplace(node->name, target->get().target);
                }

                dependency_graph[node->name] = node->after;

                for (const auto &dependency : node->after) {
                    if (!node_names.contains(dependency) &&
                        std::find_if(decl.nodes.begin(),
                                     decl.nodes.end(),
                                     [&](const Owned<ast::WorkflowNodeDeclSyntax> &candidate) {
                                         return candidate->name == dependency;
                                     }) == decl.nodes.end()) {
                        invalid_workflow_here(
                            messages::validation::UnknownWorkflowDependency.format_with(dependency),
                            node->range);
                    }
                }
            }

            enum class VisitState {
                Unvisited,
                Visiting,
                Completed,
            };

            std::unordered_map<std::string, VisitState> visit_states;
            for (const auto &node_name : node_names) {
                visit_states.emplace(node_name, VisitState::Unvisited);
            }

            std::function<void(const std::string &)> visit_node =
                [&](const std::string &node_name) {
                    const auto state = visit_states.find(node_name);
                    if (state == visit_states.end()) {
                        return;
                    }

                    if (state->second == VisitState::Completed) {
                        return;
                    }

                    if (state->second == VisitState::Visiting) {
                        invalid_workflow_here(
                            messages::validation::WorkflowDependencyCycle.format_with(node_name),
                            decl.range);
                        return;
                    }

                    state->second = VisitState::Visiting;
                    const auto dependencies = dependency_graph.find(node_name);
                    if (dependencies != dependency_graph.end()) {
                        for (const auto &dependency : dependencies->second) {
                            if (!node_names.contains(dependency)) {
                                continue;
                            }

                            visit_node(dependency);
                        }
                    }

                    state->second = VisitState::Completed;
                };

            for (const auto &node_name : node_names) {
                visit_node(node_name);
            }

            for (const auto &formula : decl.safety) {
                check_workflow_temporal_expr(*formula, node_agent_ids);
            }

            for (const auto &formula : decl.liveness) {
                check_workflow_temporal_expr(*formula, node_agent_ids);
            }
        }
    }

    void check_workflows() {
        if (graph_ != nullptr) {
            for (const auto &source : graph_->sources) {
                enter_source(source);
                check_workflows_in_program(require(
                    source.program.get(), "source graph program must exist before validate"));
                leave_source();
            }
            return;
        }

        check_workflows_in_program(require(program_, "validate program must exist"));
    }

    // R-04 plumbing counter: explicitly enter the decreases branch of every
    // contract_clause and accumulate totals. Logic is intentionally separate
    // from the semantic checks so a missed traversal leaves counters at 0.
    void count_contracts_in_program(const ast::Program &program) {
        for (const auto &declaration : program.declarations) {
            if (declaration->kind != ast::NodeKind::ContractDecl) {
                continue;
            }

            const auto &decl = static_cast<const ast::ContractDecl &>(*declaration);
            for (const auto &clause : decl.clauses) {
                // Explicit switch arms — the default case intentionally does
                // NOT touch the counters, so any missed branch is visible as
                // an undercount during reverse verification in S5b.
                switch (clause->kind) {
                case ast::ContractClauseKind::Requires:
                case ast::ContractClauseKind::Ensures:
                case ast::ContractClauseKind::Invariant:
                case ast::ContractClauseKind::Forbid:
                    break;
                case ast::ContractClauseKind::Decreases:
                    ++result_.total_decreases_clauses;
                    if (clause->is_wildcard) {
                        ++result_.wildcard_decreases_clauses;
                    }
                    break;
                }
            }
        }
    }

    void count_contracts() {
        if (graph_ != nullptr) {
            for (const auto &source : graph_->sources) {
                enter_source(source);
                count_contracts_in_program(require(
                    source.program.get(), "source graph program must exist before validate"));
                leave_source();
            }
            return;
        }

        count_contracts_in_program(require(program_, "validate program must exist"));
    }
};

} // namespace

ValidationResult Validator::validate(const ast::Program &program,
                                     const ResolveResult &resolve_result,
                                     const TypeCheckResult &type_check_result) const {
    ValidationPass pass(program, resolve_result, type_check_result);
    return pass.run();
}

ValidationResult Validator::validate(const SourceGraph &graph,
                                     const ResolveResult &resolve_result,
                                     const TypeCheckResult &type_check_result) const {
    ValidationPass pass(graph, resolve_result, type_check_result);
    return pass.run();
}

} // namespace ahfl
