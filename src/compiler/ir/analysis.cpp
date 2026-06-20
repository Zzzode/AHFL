#include "ahfl/compiler/ir/analysis.hpp"

#include "ahfl/compiler/ir/identity.hpp"
#include "ahfl/compiler/ir/lowering.hpp"
#include "ahfl/base/support/overloaded.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <variant>
#include <vector>

namespace ahfl::ir {

namespace {

constexpr std::array<DerivedAnalysisKind, 3> kAllDerivedAnalysisKinds{
    DerivedAnalysisKind::StateHandlerSummaries,
    DerivedAnalysisKind::WorkflowExprSummaries,
    DerivedAnalysisKind::FormalObservations,
};

[[nodiscard]] bool requires_state_handler_summaries(const Program &program) {
    return std::any_of(
        program.declarations.begin(), program.declarations.end(), [](const auto &decl) {
            const auto *flow = std::get_if<FlowDecl>(&decl);
            return flow != nullptr && !flow->state_handlers.empty();
        });
}

[[nodiscard]] bool requires_workflow_expr_summaries(const Program &program) {
    return std::any_of(program.declarations.begin(),
                       program.declarations.end(),
                       [](const auto &decl) { return std::holds_alternative<WorkflowDecl>(decl); });
}

[[nodiscard]] bool has_required_analysis_entries(const Program &program,
                                                 DerivedAnalysisKind kind) noexcept {
    switch (kind) {
    case DerivedAnalysisKind::StateHandlerSummaries:
        return !requires_state_handler_summaries(program) ||
               !program.analyses.state_handler_summaries.empty();
    case DerivedAnalysisKind::WorkflowExprSummaries:
        return !requires_workflow_expr_summaries(program) ||
               !program.analyses.workflow_return_summaries.empty();
    case DerivedAnalysisKind::FormalObservations:
        return true;
    }
    return false;
}

[[nodiscard]] bool paths_equal(const Path &lhs, const Path &rhs) {
    return lhs.root_kind == rhs.root_kind && lhs.root_name == rhs.root_name &&
           lhs.members == rhs.members;
}

[[nodiscard]] bool workflow_value_reads_equal(const WorkflowValueRead &lhs,
                                              const WorkflowValueRead &rhs) {
    return lhs.kind == rhs.kind && lhs.root_name == rhs.root_name && lhs.members == rhs.members;
}

template <typename T> void push_unique_value(std::vector<T> &values, const T &value) {
    for (const auto &existing : values) {
        if (existing == value) {
            return;
        }
    }
    values.push_back(value);
}

void push_unique_path(std::vector<Path> &values, const Path &value) {
    for (const auto &existing : values) {
        if (paths_equal(existing, value)) {
            return;
        }
    }
    values.push_back(value);
}

void push_unique_workflow_value_read(std::vector<WorkflowValueRead> &values,
                                     const WorkflowValueRead &value) {
    for (const auto &existing : values) {
        if (workflow_value_reads_equal(existing, value)) {
            return;
        }
    }
    values.push_back(value);
}

void collect_called_targets_from_expr(const Expr &expr, std::vector<std::string> &called_targets) {
    std::visit(Overloaded{
                   [](const NoneLiteralExpr &) {},
                   [](const BoolLiteralExpr &) {},
                   [](const IntegerLiteralExpr &) {},
                   [](const FloatLiteralExpr &) {},
                   [](const DecimalLiteralExpr &) {},
                   [](const StringLiteralExpr &) {},
                   [](const DurationLiteralExpr &) {},
                   [&](const SomeExpr &value) {
                       collect_called_targets_from_expr(*value.value, called_targets);
                   },
                   [](const PathExpr &) {},
                   [](const QualifiedValueExpr &) {},
                   [&](const CallExpr &value) {
                       push_unique_value(called_targets, value.callee);
                       for (const auto &argument : value.arguments) {
                           collect_called_targets_from_expr(*argument, called_targets);
                       }
                   },
                   [&](const StructLiteralExpr &value) {
                       for (const auto &field : value.fields) {
                           collect_called_targets_from_expr(*field.value, called_targets);
                       }
                   },
                   [&](const ListLiteralExpr &value) {
                       for (const auto &item : value.items) {
                           collect_called_targets_from_expr(*item, called_targets);
                       }
                   },
                   [&](const SetLiteralExpr &value) {
                       for (const auto &item : value.items) {
                           collect_called_targets_from_expr(*item, called_targets);
                       }
                   },
                   [&](const MapLiteralExpr &value) {
                       for (const auto &entry : value.entries) {
                           collect_called_targets_from_expr(*entry.key, called_targets);
                           collect_called_targets_from_expr(*entry.value, called_targets);
                       }
                   },
                   [&](const UnaryExpr &value) {
                       collect_called_targets_from_expr(*value.operand, called_targets);
                   },
                   [&](const BinaryExpr &value) {
                       collect_called_targets_from_expr(*value.lhs, called_targets);
                       collect_called_targets_from_expr(*value.rhs, called_targets);
                   },
                   [&](const MemberAccessExpr &value) {
                       collect_called_targets_from_expr(*value.base, called_targets);
                   },
                   [&](const IndexAccessExpr &value) {
                       collect_called_targets_from_expr(*value.base, called_targets);
                       collect_called_targets_from_expr(*value.index, called_targets);
                   },
               },
               expr.node);
}

void merge_flow_summary(StateHandler::Summary &target, const StateHandler::Summary &other) {
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

[[nodiscard]] StateHandler::Summary summarize_block(const Block &block);

[[nodiscard]] StateHandler::Summary summarize_statement(const Statement &statement) {
    return std::visit(
        Overloaded{
            [](const LetStatement &value) {
                StateHandler::Summary summary;
                collect_called_targets_from_expr(*value.initializer, summary.called_targets);
                return summary;
            },
            [](const AssignStatement &value) {
                StateHandler::Summary summary;
                push_unique_path(summary.assigned_paths, value.target);
                collect_called_targets_from_expr(*value.value, summary.called_targets);
                return summary;
            },
            [](const IfStatement &value) {
                StateHandler::Summary summary;
                collect_called_targets_from_expr(*value.condition, summary.called_targets);

                const auto then_summary = summarize_block(*value.then_block);
                merge_flow_summary(summary, then_summary);

                StateHandler::Summary else_summary;
                if (value.else_block) {
                    else_summary = summarize_block(*value.else_block);
                    merge_flow_summary(summary, else_summary);
                }

                summary.may_fallthrough = !value.else_block || then_summary.may_fallthrough ||
                                          else_summary.may_fallthrough;
                return summary;
            },
            [](const GotoStatement &value) {
                StateHandler::Summary summary;
                summary.goto_targets.push_back(value.target_state);
                summary.may_fallthrough = false;
                return summary;
            },
            [](const ReturnStatement &value) {
                StateHandler::Summary summary;
                if (value.value) {
                    collect_called_targets_from_expr(*value.value, summary.called_targets);
                }
                summary.may_return = true;
                summary.may_fallthrough = false;
                return summary;
            },
            [](const AssertStatement &value) {
                StateHandler::Summary summary;
                collect_called_targets_from_expr(*value.condition, summary.called_targets);
                summary.assert_count = 1;
                return summary;
            },
            [](const ExprStatement &value) {
                StateHandler::Summary summary;
                collect_called_targets_from_expr(*value.expr, summary.called_targets);
                return summary;
            },
        },
        statement.node);
}

[[nodiscard]] StateHandler::Summary summarize_block(const Block &block) {
    StateHandler::Summary summary;
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

void collect_workflow_value_reads(const Expr &expr,
                                  const std::vector<std::string> &workflow_node_names,
                                  std::vector<WorkflowValueRead> &reads) {
    std::visit(Overloaded{
                   [](const NoneLiteralExpr &) {},
                   [](const BoolLiteralExpr &) {},
                   [](const IntegerLiteralExpr &) {},
                   [](const FloatLiteralExpr &) {},
                   [](const DecimalLiteralExpr &) {},
                   [](const StringLiteralExpr &) {},
                   [](const DurationLiteralExpr &) {},
                   [&](const SomeExpr &value) {
                       collect_workflow_value_reads(*value.value, workflow_node_names, reads);
                   },
                   [&](const PathExpr &value) {
                       if (value.path.root_kind == PathRootKind::Input) {
                           push_unique_workflow_value_read(
                               reads,
                               WorkflowValueRead{
                                   .kind = WorkflowValueSourceKind::WorkflowInput,
                                   .root_name = value.path.root_name,
                                   .members = value.path.members,
                               });
                           return;
                       }

                       if (value.path.root_kind != PathRootKind::Identifier) {
                           return;
                       }

                       if (std::find(workflow_node_names.begin(),
                                     workflow_node_names.end(),
                                     value.path.root_name) == workflow_node_names.end()) {
                           return;
                       }

                       push_unique_workflow_value_read(
                           reads,
                           WorkflowValueRead{
                               .kind = WorkflowValueSourceKind::WorkflowNodeOutput,
                               .root_name = value.path.root_name,
                               .members = value.path.members,
                           });
                   },
                   [](const QualifiedValueExpr &) {},
                   [&](const CallExpr &value) {
                       for (const auto &argument : value.arguments) {
                           collect_workflow_value_reads(*argument, workflow_node_names, reads);
                       }
                   },
                   [&](const StructLiteralExpr &value) {
                       for (const auto &field : value.fields) {
                           collect_workflow_value_reads(*field.value, workflow_node_names, reads);
                       }
                   },
                   [&](const ListLiteralExpr &value) {
                       for (const auto &item : value.items) {
                           collect_workflow_value_reads(*item, workflow_node_names, reads);
                       }
                   },
                   [&](const SetLiteralExpr &value) {
                       for (const auto &item : value.items) {
                           collect_workflow_value_reads(*item, workflow_node_names, reads);
                       }
                   },
                   [&](const MapLiteralExpr &value) {
                       for (const auto &entry : value.entries) {
                           collect_workflow_value_reads(*entry.key, workflow_node_names, reads);
                           collect_workflow_value_reads(*entry.value, workflow_node_names, reads);
                       }
                   },
                   [&](const UnaryExpr &value) {
                       collect_workflow_value_reads(*value.operand, workflow_node_names, reads);
                   },
                   [&](const BinaryExpr &value) {
                       collect_workflow_value_reads(*value.lhs, workflow_node_names, reads);
                       collect_workflow_value_reads(*value.rhs, workflow_node_names, reads);
                   },
                   [&](const MemberAccessExpr &value) {
                       collect_workflow_value_reads(*value.base, workflow_node_names, reads);
                   },
                   [&](const IndexAccessExpr &value) {
                       collect_workflow_value_reads(*value.base, workflow_node_names, reads);
                       collect_workflow_value_reads(*value.index, workflow_node_names, reads);
                   },
               },
               expr.node);
}

[[nodiscard]] WorkflowExprSummary
summarize_workflow_expr(const Expr &expr, const std::vector<std::string> &workflow_node_names) {
    WorkflowExprSummary summary;
    collect_workflow_value_reads(expr, workflow_node_names, summary.reads);
    return summary;
}

[[nodiscard]] std::string flow_target_name(const FlowDecl &flow) {
    return std::string(symbol_canonical_name(flow.target_ref));
}

[[nodiscard]] std::string workflow_name(const WorkflowDecl &workflow) {
    return std::string(symbol_canonical_name(workflow.symbol_ref, workflow.name));
}

[[nodiscard]] std::size_t handler_index(const FlowDecl &flow, const StateHandler &handler) {
    for (std::size_t index = 0; index < flow.state_handlers.size(); ++index) {
        if (&flow.state_handlers[index] == &handler) {
            return index;
        }
    }
    return flow.state_handlers.size();
}

[[nodiscard]] std::size_t node_index(const WorkflowDecl &workflow, const WorkflowNode &node) {
    for (std::size_t index = 0; index < workflow.nodes.size(); ++index) {
        if (&workflow.nodes[index] == &node) {
            return index;
        }
    }
    return workflow.nodes.size();
}

} // namespace

std::span<const DerivedAnalysisKind> all_derived_analysis_kinds() noexcept {
    return kAllDerivedAnalysisKinds;
}

std::vector<DerivedAnalysisKind> all_derived_analysis_kinds_vector() {
    return std::vector<DerivedAnalysisKind>(kAllDerivedAnalysisKinds.begin(),
                                            kAllDerivedAnalysisKinds.end());
}

void mark_derived_analyses_stale(Program &program) {
    ++program.analysis_revision;
}

bool has_fresh_derived_analyses(const Program &program,
                                std::span<const DerivedAnalysisKind> required) noexcept {
    if (required.empty()) {
        return true;
    }
    if (program.phase == ProgramPhase::Lowered) {
        return false;
    }
    if (program.analyses.source_program_revision != program.analysis_revision) {
        return false;
    }
    return std::all_of(required.begin(), required.end(), [&](DerivedAnalysisKind kind) {
        return has_required_analysis_entries(program, kind);
    });
}

const StateHandler::Summary *find_state_handler_summary(const Program &program,
                                                        const FlowDecl &flow,
                                                        const StateHandler &handler) {
    const auto target = flow_target_name(flow);
    const auto index = handler_index(flow, handler);
    for (const auto &entry : program.analyses.state_handler_summaries) {
        if (entry.flow_target == target && entry.handler_index == index &&
            entry.state_name == handler.state_name) {
            return &entry.summary;
        }
    }
    return nullptr;
}

const WorkflowExprSummary *find_workflow_node_input_summary(const Program &program,
                                                            const WorkflowDecl &workflow,
                                                            const WorkflowNode &node) {
    const auto owner = workflow_name(workflow);
    const auto index = node_index(workflow, node);
    for (const auto &entry : program.analyses.workflow_node_input_summaries) {
        if (entry.workflow_name == owner && entry.node_index == index &&
            entry.node_name == node.name) {
            return &entry.summary;
        }
    }
    return nullptr;
}

const WorkflowExprSummary *find_workflow_return_summary(const Program &program,
                                                        const WorkflowDecl &workflow) {
    const auto owner = workflow_name(workflow);
    for (const auto &entry : program.analyses.workflow_return_summaries) {
        if (entry.workflow_name == owner) {
            return &entry.summary;
        }
    }
    return nullptr;
}

const std::vector<FormalObservation> &formal_observations(const Program &program) noexcept {
    return program.analyses.formal_observations;
}

void recompute_state_handler_summaries(Program &program) {
    program.analyses.state_handler_summaries.clear();
    for (auto &declaration : program.declarations) {
        auto *flow = std::get_if<FlowDecl>(&declaration);
        if (flow == nullptr) {
            continue;
        }

        const auto target = flow_target_name(*flow);
        for (std::size_t index = 0; index < flow->state_handlers.size(); ++index) {
            const auto &handler = flow->state_handlers[index];
            program.analyses.state_handler_summaries.push_back(StateHandlerSummaryAnalysis{
                .flow_target = target,
                .state_name = handler.state_name,
                .handler_index = index,
                .summary = summarize_block(handler.body),
            });
        }
    }
}

void recompute_workflow_expr_summaries(Program &program) {
    program.analyses.workflow_node_input_summaries.clear();
    program.analyses.workflow_return_summaries.clear();
    for (auto &declaration : program.declarations) {
        auto *workflow = std::get_if<WorkflowDecl>(&declaration);
        if (workflow == nullptr) {
            continue;
        }

        std::vector<std::string> node_names;
        node_names.reserve(workflow->nodes.size());
        for (const auto &node : workflow->nodes) {
            node_names.push_back(node.name);
        }

        const auto owner = workflow_name(*workflow);
        for (std::size_t index = 0; index < workflow->nodes.size(); ++index) {
            const auto &node = workflow->nodes[index];
            program.analyses.workflow_node_input_summaries.push_back(
                WorkflowNodeExprSummaryAnalysis{
                    .workflow_name = owner,
                    .node_name = node.name,
                    .node_index = index,
                    .summary = node.input ? summarize_workflow_expr(*node.input, node_names)
                                          : WorkflowExprSummary{},
                });
        }

        program.analyses.workflow_return_summaries.push_back(WorkflowReturnExprSummaryAnalysis{
            .workflow_name = owner,
            .summary = workflow->return_value
                           ? summarize_workflow_expr(*workflow->return_value, node_names)
                           : WorkflowExprSummary{},
        });
    }
}

void recompute_formal_observations(Program &program) {
    program.analyses.formal_observations = ahfl::collect_formal_observations(program);
}

void recompute_derived_analyses(Program &program, ProgramPhase phase) {
    recompute_state_handler_summaries(program);
    recompute_workflow_expr_summaries(program);
    recompute_formal_observations(program);
    program.analyses.source_program_revision = program.analysis_revision;
    program.phase = phase;
}

void ensure_derived_analyses(Program &program,
                             std::span<const DerivedAnalysisKind> required,
                             ProgramPhase phase) {
    if (has_fresh_derived_analyses(program, required)) {
        return;
    }
    recompute_derived_analyses(program, phase);
}

} // namespace ahfl::ir
