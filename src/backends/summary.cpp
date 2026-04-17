#include "ahfl/backends/summary.hpp"

#include <cstddef>
#include <ostream>
#include <string_view>
#include <variant>

namespace ahfl {

namespace {

template <typename... Ts> struct Overloaded : Ts... {
    using Ts::operator()...;
};

template <typename... Ts> Overloaded(Ts...) -> Overloaded<Ts...>;

struct SummaryStats {
    std::size_t declarations_with_provenance{0};
    std::size_t modules{0};
    std::size_t imports{0};
    std::size_t consts{0};
    std::size_t type_aliases{0};
    std::size_t structs{0};
    std::size_t enums{0};
    std::size_t capabilities{0};
    std::size_t predicates{0};
    std::size_t agents{0};
    std::size_t contracts{0};
    std::size_t flows{0};
    std::size_t workflows{0};
    std::size_t called_observations{0};
    std::size_t embedded_observations{0};
    std::size_t flow_handlers{0};
    std::size_t flow_goto_targets{0};
    std::size_t flow_returning_handlers{0};
    std::size_t flow_fallthrough_handlers{0};
    std::size_t flow_assigned_paths{0};
    std::size_t flow_called_targets{0};
    std::size_t flow_asserts{0};
    std::size_t workflow_nodes{0};
    std::size_t workflow_input_reads{0};
    std::size_t workflow_input_reads_from_input{0};
    std::size_t workflow_input_reads_from_nodes{0};
    std::size_t workflow_return_reads{0};
    std::size_t workflow_return_reads_from_input{0};
    std::size_t workflow_return_reads_from_nodes{0};
};

[[nodiscard]] bool has_provenance(const ir::DeclarationProvenance &provenance) {
    return !provenance.module_name.empty() || !provenance.source_path.empty();
}

void accumulate_workflow_reads(const ir::WorkflowExprSummary &summary,
                               std::size_t &total_reads,
                               std::size_t &workflow_input_reads,
                               std::size_t &workflow_node_output_reads) {
    total_reads += summary.reads.size();

    for (const auto &read : summary.reads) {
        switch (read.kind) {
        case ir::WorkflowValueSourceKind::WorkflowInput:
            ++workflow_input_reads;
            break;
        case ir::WorkflowValueSourceKind::WorkflowNodeOutput:
            ++workflow_node_output_reads;
            break;
        }
    }
}

[[nodiscard]] SummaryStats summarize_program(const ir::Program &program) {
    SummaryStats stats;

    for (const auto &declaration : program.declarations) {
        std::visit(
            Overloaded{
                [&](const ir::ModuleDecl &value) {
                    stats.modules += 1;
                    stats.declarations_with_provenance += has_provenance(value.provenance) ? 1 : 0;
                },
                [&](const ir::ImportDecl &value) {
                    stats.imports += 1;
                    stats.declarations_with_provenance += has_provenance(value.provenance) ? 1 : 0;
                },
                [&](const ir::ConstDecl &value) {
                    stats.consts += 1;
                    stats.declarations_with_provenance += has_provenance(value.provenance) ? 1 : 0;
                },
                [&](const ir::TypeAliasDecl &value) {
                    stats.type_aliases += 1;
                    stats.declarations_with_provenance += has_provenance(value.provenance) ? 1 : 0;
                },
                [&](const ir::StructDecl &value) {
                    stats.structs += 1;
                    stats.declarations_with_provenance += has_provenance(value.provenance) ? 1 : 0;
                },
                [&](const ir::EnumDecl &value) {
                    stats.enums += 1;
                    stats.declarations_with_provenance += has_provenance(value.provenance) ? 1 : 0;
                },
                [&](const ir::CapabilityDecl &value) {
                    stats.capabilities += 1;
                    stats.declarations_with_provenance += has_provenance(value.provenance) ? 1 : 0;
                },
                [&](const ir::PredicateDecl &value) {
                    stats.predicates += 1;
                    stats.declarations_with_provenance += has_provenance(value.provenance) ? 1 : 0;
                },
                [&](const ir::AgentDecl &value) {
                    stats.agents += 1;
                    stats.declarations_with_provenance += has_provenance(value.provenance) ? 1 : 0;
                },
                [&](const ir::ContractDecl &value) {
                    stats.contracts += 1;
                    stats.declarations_with_provenance += has_provenance(value.provenance) ? 1 : 0;
                },
                [&](const ir::FlowDecl &value) {
                    stats.flows += 1;
                    stats.declarations_with_provenance += has_provenance(value.provenance) ? 1 : 0;
                    for (const auto &handler : value.state_handlers) {
                        stats.flow_handlers += 1;
                        stats.flow_goto_targets += handler.summary.goto_targets.size();
                        stats.flow_returning_handlers += handler.summary.may_return ? 1 : 0;
                        stats.flow_fallthrough_handlers += handler.summary.may_fallthrough ? 1 : 0;
                        stats.flow_assigned_paths += handler.summary.assigned_paths.size();
                        stats.flow_called_targets += handler.summary.called_targets.size();
                        stats.flow_asserts += handler.summary.assert_count;
                    }
                },
                [&](const ir::WorkflowDecl &value) {
                    stats.workflows += 1;
                    stats.declarations_with_provenance += has_provenance(value.provenance) ? 1 : 0;
                    stats.workflow_nodes += value.nodes.size();
                    for (const auto &node : value.nodes) {
                        accumulate_workflow_reads(node.input_summary,
                                                  stats.workflow_input_reads,
                                                  stats.workflow_input_reads_from_input,
                                                  stats.workflow_input_reads_from_nodes);
                    }
                    accumulate_workflow_reads(value.return_summary,
                                              stats.workflow_return_reads,
                                              stats.workflow_return_reads_from_input,
                                              stats.workflow_return_reads_from_nodes);
                },
            },
            declaration);
    }

    for (const auto &observation : program.formal_observations) {
        std::visit(
            Overloaded{
                [&](const ir::CalledCapabilityObservation &) { stats.called_observations += 1; },
                [&](const ir::EmbeddedBoolObservation &) { stats.embedded_observations += 1; },
            },
            observation.node);
    }

    return stats;
}

void line(std::ostream &out, int indent_level, std::string_view text) {
    out << std::string(static_cast<std::size_t>(indent_level) * 2, ' ') << text << '\n';
}

[[nodiscard]] std::string bool_name(bool value) {
    return value ? "true" : "false";
}

} // namespace

void print_program_summary(const ir::Program &program, std::ostream &out) {
    const auto stats = summarize_program(program);

    out << "ahfl.summary.v1\n";
    line(out, 0, "ir_format " + program.format_version);
    line(out, 0, "project_aware " + bool_name(stats.declarations_with_provenance != 0));
    line(out, 0, "declarations " + std::to_string(program.declarations.size()));
    line(out,
         0,
         "declarations_with_provenance " + std::to_string(stats.declarations_with_provenance));

    line(out, 0, "declaration_kinds {");
    line(out, 1, "module " + std::to_string(stats.modules));
    line(out, 1, "import " + std::to_string(stats.imports));
    line(out, 1, "const " + std::to_string(stats.consts));
    line(out, 1, "type_alias " + std::to_string(stats.type_aliases));
    line(out, 1, "struct " + std::to_string(stats.structs));
    line(out, 1, "enum " + std::to_string(stats.enums));
    line(out, 1, "capability " + std::to_string(stats.capabilities));
    line(out, 1, "predicate " + std::to_string(stats.predicates));
    line(out, 1, "agent " + std::to_string(stats.agents));
    line(out, 1, "contract " + std::to_string(stats.contracts));
    line(out, 1, "flow " + std::to_string(stats.flows));
    line(out, 1, "workflow " + std::to_string(stats.workflows));
    line(out, 0, "}");

    line(out, 0, "formal_observations {");
    line(out, 1, "total " + std::to_string(program.formal_observations.size()));
    line(out, 1, "called_capability " + std::to_string(stats.called_observations));
    line(out, 1, "embedded_bool_expr " + std::to_string(stats.embedded_observations));
    line(out, 0, "}");

    line(out, 0, "flow_summaries {");
    line(out, 1, "handlers " + std::to_string(stats.flow_handlers));
    line(out, 1, "total_goto_targets " + std::to_string(stats.flow_goto_targets));
    line(out, 1, "handlers_with_return " + std::to_string(stats.flow_returning_handlers));
    line(out, 1, "handlers_with_fallthrough " + std::to_string(stats.flow_fallthrough_handlers));
    line(out, 1, "total_assigned_paths " + std::to_string(stats.flow_assigned_paths));
    line(out, 1, "total_called_targets " + std::to_string(stats.flow_called_targets));
    line(out, 1, "total_asserts " + std::to_string(stats.flow_asserts));
    line(out, 0, "}");

    line(out, 0, "workflow_summaries {");
    line(out, 1, "workflows " + std::to_string(stats.workflows));
    line(out, 1, "nodes " + std::to_string(stats.workflow_nodes));
    line(out, 1, "total_input_reads " + std::to_string(stats.workflow_input_reads));
    line(out,
         1,
         "input_reads_workflow_input " + std::to_string(stats.workflow_input_reads_from_input));
    line(out,
         1,
         "input_reads_workflow_node_output " +
             std::to_string(stats.workflow_input_reads_from_nodes));
    line(out, 1, "total_return_reads " + std::to_string(stats.workflow_return_reads));
    line(out,
         1,
         "return_reads_workflow_input " + std::to_string(stats.workflow_return_reads_from_input));
    line(out,
         1,
         "return_reads_workflow_node_output " +
             std::to_string(stats.workflow_return_reads_from_nodes));
    line(out, 0, "}");
}

void emit_program_summary(const ast::Program &program,
                          const ResolveResult &resolve_result,
                          const TypeCheckResult &type_check_result,
                          std::ostream &out) {
    print_program_summary(lower_program_ir(program, resolve_result, type_check_result), out);
}

void emit_program_summary(const SourceGraph &graph,
                          const ResolveResult &resolve_result,
                          const TypeCheckResult &type_check_result,
                          std::ostream &out) {
    print_program_summary(lower_program_ir(graph, resolve_result, type_check_result), out);
}

} // namespace ahfl
