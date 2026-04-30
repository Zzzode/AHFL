#include "ahfl/backends/native_json.hpp"

#include <algorithm>
#include <cstddef>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "ahfl/support/json.hpp"

namespace ahfl {

namespace {

template <typename... Ts> struct Overloaded : Ts... {
    using Ts::operator()...;
};

template <typename... Ts> Overloaded(Ts...) -> Overloaded<Ts...>;

[[nodiscard]] std::string_view executable_kind_name(handoff::ExecutableKind kind) {
    switch (kind) {
    case handoff::ExecutableKind::Agent:
        return "agent";
    case handoff::ExecutableKind::Workflow:
        return "workflow";
    }

    return "invalid";
}

[[nodiscard]] bool has_provenance(const ir::DeclarationProvenance &provenance) {
    return !provenance.module_name.empty() || !provenance.source_path.empty();
}

[[nodiscard]] std::string_view
workflow_node_start_condition_name(handoff::WorkflowNodeStartConditionKind kind) {
    switch (kind) {
    case handoff::WorkflowNodeStartConditionKind::Immediate:
        return "immediate";
    case handoff::WorkflowNodeStartConditionKind::AfterDependenciesCompleted:
        return "after_dependencies_completed";
    }

    return "invalid";
}

[[nodiscard]] std::string_view
workflow_node_completion_condition_name(handoff::WorkflowNodeCompletionConditionKind kind) {
    switch (kind) {
    case handoff::WorkflowNodeCompletionConditionKind::TargetReachedFinalState:
        return "target_reached_final_state";
    }

    return "invalid";
}

[[nodiscard]] std::string_view policy_obligation_kind_name(handoff::PolicyObligationKind kind) {
    switch (kind) {
    case handoff::PolicyObligationKind::Requires:
        return "requires";
    case handoff::PolicyObligationKind::Ensures:
        return "ensures";
    case handoff::PolicyObligationKind::Invariant:
        return "invariant";
    case handoff::PolicyObligationKind::Forbid:
        return "forbid";
    case handoff::PolicyObligationKind::WorkflowSafety:
        return "workflow_safety";
    case handoff::PolicyObligationKind::WorkflowLiveness:
        return "workflow_liveness";
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

class NativeJsonPrinter final : private PrettyJsonWriter {
  public:
    explicit NativeJsonPrinter(std::ostream &out) : PrettyJsonWriter(out) {}

    void print(const handoff::Package &package) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(handoff::kFormatVersion); });
            field("metadata", [&]() { print_metadata(package.metadata, 1); });
            field("executable_targets", [&]() {
                print_array(1, [&](const auto &item) {
                    for (const auto &target : package.executable_targets) {
                        item([&]() { print_executable_target(target, 2); });
                    }
                });
            });
            field("capability_binding_slots", [&]() {
                print_array(1, [&](const auto &item) {
                    for (const auto &slot : package.capability_binding_slots) {
                        item([&]() { print_capability_binding_slot(slot, 2); });
                    }
                });
            });
            field("policy_obligations", [&]() {
                print_array(1, [&](const auto &item) {
                    for (const auto &obligation : package.policy_obligations) {
                        item([&]() { print_policy_obligation(obligation, 2); });
                    }
                });
            });
            field("formal_observations", [&]() {
                print_array(1, [&](const auto &item) {
                    for (const auto &observation : package.formal_observations) {
                        item([&]() { print_formal_observation(observation, 2); });
                    }
                });
            });
        });
        out_ << '\n';
    }

  private:
    void print_provenance(const ir::DeclarationProvenance &provenance, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("module_name", [&]() { write_string(provenance.module_name); });
            field("source_path", [&]() { write_string(provenance.source_path); });
        });
    }

    void print_executable_ref(const handoff::ExecutableRef &target, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("kind", [&]() { write_string(executable_kind_name(target.kind)); });
            field("canonical_name", [&]() { write_string(target.canonical_name); });
        });
    }

    void print_workflow_value_summary(const ir::WorkflowExprSummary &summary, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("reads", [&]() {
                print_array(indent_level + 1, [&](const auto &item) {
                    for (const auto &read : summary.reads) {
                        item([&]() {
                            print_object(indent_level + 2, [&](const auto &read_field) {
                                read_field("kind", [&]() {
                                    write_string(workflow_value_source_kind_name(read.kind));
                                });
                                read_field("root_name", [&]() { write_string(read.root_name); });
                                read_field("members", [&]() {
                                    print_array(indent_level + 3, [&](const auto &member_item) {
                                        for (const auto &member : read.members) {
                                            member_item([&]() { write_string(member); });
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

    void print_workflow_execution_graph(const handoff::WorkflowExecutionGraph &graph,
                                        int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("entry_nodes", [&]() {
                print_array(indent_level + 1, [&](const auto &item) {
                    for (const auto &node_name : graph.entry_nodes) {
                        item([&]() { write_string(node_name); });
                    }
                });
            });
            field("dependency_edges", [&]() {
                print_array(indent_level + 1, [&](const auto &item) {
                    for (const auto &edge : graph.dependency_edges) {
                        item([&]() {
                            print_object(indent_level + 2, [&](const auto &edge_field) {
                                edge_field("from_node", [&]() { write_string(edge.from_node); });
                                edge_field("to_node", [&]() { write_string(edge.to_node); });
                            });
                        });
                    }
                });
            });
        });
    }

    void print_workflow_node_lifecycle(const handoff::WorkflowNodeLifecycleSummary &lifecycle,
                                       int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("start_condition", [&]() {
                write_string(workflow_node_start_condition_name(lifecycle.start_condition));
            });
            field("completion_condition", [&]() {
                write_string(
                    workflow_node_completion_condition_name(lifecycle.completion_condition));
            });
            field("completion_latched",
                  [&]() { out_ << (lifecycle.completion_latched ? "true" : "false"); });
            field("target_initial_state", [&]() { write_string(lifecycle.target_initial_state); });
            field("target_final_states", [&]() {
                print_array(indent_level + 1, [&](const auto &item) {
                    for (const auto &state : lifecycle.target_final_states) {
                        item([&]() { write_string(state); });
                    }
                });
            });
        });
    }

    void print_metadata(const handoff::PackageMetadata &metadata, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("identity", [&]() {
                if (!metadata.identity.has_value()) {
                    out_ << "null";
                    return;
                }

                print_object(indent_level + 1, [&](const auto &identity_field) {
                    identity_field("format_version",
                                   [&]() { write_string(metadata.identity->format_version); });
                    identity_field("name", [&]() { write_string(metadata.identity->name); });
                    identity_field("version", [&]() { write_string(metadata.identity->version); });
                });
            });
            field("entry_target", [&]() {
                if (!metadata.entry_target.has_value()) {
                    out_ << "null";
                    return;
                }

                print_executable_ref(*metadata.entry_target, indent_level + 1);
            });
            field("export_targets", [&]() {
                print_array(indent_level + 1, [&](const auto &item) {
                    for (const auto &target : metadata.export_targets) {
                        item([&]() { print_executable_ref(target, indent_level + 2); });
                    }
                });
            });
            field("capability_binding_keys", [&]() {
                std::vector<std::pair<std::string, std::string>> items;
                items.reserve(metadata.capability_binding_keys.size());
                for (const auto &[capability_name, binding_key] :
                     metadata.capability_binding_keys) {
                    items.emplace_back(capability_name, binding_key);
                }
                std::sort(items.begin(), items.end());

                print_array(indent_level + 1, [&](const auto &item) {
                    for (const auto &[capability_name, binding_key] : items) {
                        item([&]() {
                            print_object(indent_level + 2, [&](const auto &binding_field) {
                                binding_field("capability_name",
                                              [&]() { write_string(capability_name); });
                                binding_field("binding_key", [&]() { write_string(binding_key); });
                            });
                        });
                    }
                });
            });
        });
    }

    void print_agent_executable(const handoff::AgentExecutable &agent, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("kind", [&]() { write_string("agent"); });
            if (has_provenance(agent.provenance)) {
                field("provenance",
                      [&]() { print_provenance(agent.provenance, indent_level + 1); });
            }
            field("canonical_name", [&]() { write_string(agent.canonical_name); });
            field("input_type", [&]() { write_string(agent.input_type); });
            field("context_type", [&]() { write_string(agent.context_type); });
            field("output_type", [&]() { write_string(agent.output_type); });
            field("states", [&]() {
                print_array(indent_level + 1, [&](const auto &item) {
                    for (const auto &state : agent.states) {
                        item([&]() { write_string(state); });
                    }
                });
            });
            field("initial_state", [&]() { write_string(agent.initial_state); });
            field("final_states", [&]() {
                print_array(indent_level + 1, [&](const auto &item) {
                    for (const auto &state : agent.final_states) {
                        item([&]() { write_string(state); });
                    }
                });
            });
            field("capabilities", [&]() {
                print_array(indent_level + 1, [&](const auto &item) {
                    for (const auto &capability : agent.capabilities) {
                        item([&]() { write_string(capability); });
                    }
                });
            });
        });
    }

    void print_workflow_executable(const handoff::WorkflowExecutable &workflow, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("kind", [&]() { write_string("workflow"); });
            if (has_provenance(workflow.provenance)) {
                field("provenance",
                      [&]() { print_provenance(workflow.provenance, indent_level + 1); });
            }
            field("canonical_name", [&]() { write_string(workflow.canonical_name); });
            field("input_type", [&]() { write_string(workflow.input_type); });
            field("output_type", [&]() { write_string(workflow.output_type); });
            field("execution_graph", [&]() {
                print_workflow_execution_graph(workflow.execution_graph, indent_level + 1);
            });
            field("nodes", [&]() {
                print_array(indent_level + 1, [&](const auto &item) {
                    for (const auto &node : workflow.nodes) {
                        item([&]() {
                            print_object(indent_level + 2, [&](const auto &node_field) {
                                node_field("name", [&]() { write_string(node.name); });
                                node_field("target", [&]() { write_string(node.target); });
                                node_field("input_summary", [&]() {
                                    print_workflow_value_summary(node.input_summary,
                                                                 indent_level + 3);
                                });
                                node_field("after", [&]() {
                                    print_array(indent_level + 3, [&](const auto &after_item) {
                                        for (const auto &dependency : node.after) {
                                            after_item([&]() { write_string(dependency); });
                                        }
                                    });
                                });
                                node_field("lifecycle", [&]() {
                                    print_workflow_node_lifecycle(node.lifecycle, indent_level + 3);
                                });
                            });
                        });
                    }
                });
            });
            field("safety_clause_count", [&]() { out_ << workflow.safety_clause_count; });
            field("liveness_clause_count", [&]() { out_ << workflow.liveness_clause_count; });
            field("return_summary", [&]() {
                print_workflow_value_summary(workflow.return_summary, indent_level + 1);
            });
        });
    }

    void print_executable_target(const handoff::ExecutableTarget &target, int indent_level) {
        std::visit(Overloaded{
                       [&](const handoff::AgentExecutable &agent) {
                           print_agent_executable(agent, indent_level);
                       },
                       [&](const handoff::WorkflowExecutable &workflow) {
                           print_workflow_executable(workflow, indent_level);
                       },
                   },
                   target);
    }

    void print_capability_binding_slot(const handoff::CapabilityBindingSlot &slot,
                                       int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("capability_name", [&]() { write_string(slot.capability_name); });
            field("binding_key", [&]() { write_string(slot.binding_key); });
            field("required_by_targets", [&]() {
                print_array(indent_level + 1, [&](const auto &item) {
                    for (const auto &target : slot.required_by_targets) {
                        item([&]() { print_executable_ref(target, indent_level + 2); });
                    }
                });
            });
        });
    }

    void print_policy_obligation(const handoff::PolicyObligation &obligation, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("owner_target",
                  [&]() { print_executable_ref(obligation.owner_target, indent_level + 1); });
            field("kind", [&]() { write_string(policy_obligation_kind_name(obligation.kind)); });
            field("clause_index", [&]() { out_ << obligation.clause_index; });
            field("observation_symbols", [&]() {
                print_array(indent_level + 1, [&](const auto &item) {
                    for (const auto &symbol : obligation.observation_symbols) {
                        item([&]() { write_string(symbol); });
                    }
                });
            });
        });
    }

    void print_formal_observation(const ir::FormalObservation &observation, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("symbol", [&]() { write_string(observation.symbol); });
            std::visit(
                Overloaded{
                    [&](const ir::CalledCapabilityObservation &value) {
                        field("kind", [&]() { write_string("called_capability"); });
                        field("agent", [&]() { write_string(value.agent); });
                        field("capability", [&]() { write_string(value.capability); });
                    },
                    [&](const ir::EmbeddedBoolObservation &value) {
                        field("kind", [&]() { write_string("embedded_bool_expr"); });
                        field("scope", [&]() {
                            print_object(indent_level + 1, [&](const auto &scope_field) {
                                scope_field("kind", [&]() {
                                    write_string(
                                        formal_observation_scope_kind_name(value.scope.kind));
                                });
                                scope_field("owner", [&]() { write_string(value.scope.owner); });
                                scope_field("clause_index",
                                            [&]() { out_ << value.scope.clause_index; });
                                scope_field("atom_index",
                                            [&]() { out_ << value.scope.atom_index; });
                            });
                        });
                    },
                },
                observation.node);
        });
    }
};

} // namespace

void print_package_native_json(const handoff::Package &package, std::ostream &out) {
    NativeJsonPrinter(out).print(package);
}

void print_program_native_json(const ir::Program &program, std::ostream &out) {
    print_package_native_json(handoff::lower_package(program), out);
}

void print_program_native_json(const ir::Program &program,
                               const handoff::PackageMetadata &metadata,
                               std::ostream &out) {
    print_package_native_json(handoff::lower_package(program, metadata), out);
}

} // namespace ahfl
