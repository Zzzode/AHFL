#include "ahfl/compiler/ir/program_view.hpp"

#include "ahfl/compiler/ir/identity.hpp"
#include "base/support/overloaded.hpp"

#include <cstddef>
#include <unordered_set>
#include <variant>

namespace ahfl::ir {

ProgramIndex::ProgramIndex(const Program &program) {
    rebuild(program);
}

void ProgramIndex::rebuild(const Program &program) {
    program_ = &program;
    agents_.clear();
    flows_by_agent_.clear();
    workflows_.clear();
    capabilities_.clear();
    structs_.clear();
    enums_.clear();
    agents_in_order_.clear();
    flows_in_order_.clear();
    workflows_in_order_.clear();
    capabilities_in_order_.clear();
    structs_in_order_.clear();
    enums_in_order_.clear();

    for (const auto &declaration : program.declarations) {
        std::visit(
            Overloaded{
                [&](const AgentDecl &agent) {
                    agents_.emplace(
                        std::string(symbol_canonical_name(agent.symbol_ref, agent.name)), &agent);
                    agents_in_order_.push_back(&agent);
                },
                [&](const FlowDecl &flow) {
                    flows_by_agent_.emplace(std::string(symbol_canonical_name(flow.target_ref)),
                                            &flow);
                    flows_in_order_.push_back(&flow);
                },
                [&](const WorkflowDecl &workflow) {
                    workflows_.emplace(
                        std::string(symbol_canonical_name(workflow.symbol_ref, workflow.name)),
                        &workflow);
                    workflows_in_order_.push_back(&workflow);
                },
                [&](const CapabilityDecl &capability) {
                    capabilities_.emplace(
                        std::string(symbol_canonical_name(capability.symbol_ref, capability.name)),
                        &capability);
                    capabilities_in_order_.push_back(&capability);
                },
                [&](const StructDecl &structure) {
                    structs_.emplace(
                        std::string(symbol_canonical_name(structure.symbol_ref, structure.name)),
                        &structure);
                    structs_in_order_.push_back(&structure);
                },
                [&](const EnumDecl &enumeration) {
                    enums_.emplace(std::string(symbol_canonical_name(enumeration.symbol_ref,
                                                                     enumeration.name)),
                                   &enumeration);
                    enums_in_order_.push_back(&enumeration);
                },
                [&](const auto &) {},
            },
            declaration);
    }
}

const AgentDecl *ProgramIndex::find_agent(std::string_view canonical_name) const {
    return find_in(agents_, canonical_name);
}

const FlowDecl *ProgramIndex::find_flow_for_agent(std::string_view canonical_name) const {
    return find_in(flows_by_agent_, canonical_name);
}

const WorkflowDecl *ProgramIndex::find_workflow(std::string_view canonical_name) const {
    return find_in(workflows_, canonical_name);
}

const CapabilityDecl *ProgramIndex::find_capability(std::string_view canonical_name) const {
    return find_in(capabilities_, canonical_name);
}

const StructDecl *ProgramIndex::find_struct(std::string_view canonical_name) const {
    return find_in(structs_, canonical_name);
}

const EnumDecl *ProgramIndex::find_enum(std::string_view canonical_name) const {
    return find_in(enums_, canonical_name);
}

std::vector<const WorkflowNode *>
ProgramIndex::topological_order(const WorkflowDecl &workflow) const {
    std::unordered_map<std::string, std::size_t> remaining_dependencies;
    remaining_dependencies.reserve(workflow.nodes.size());
    for (const auto &node : workflow.nodes) {
        remaining_dependencies.emplace(node.name, node.after.size());
    }

    std::vector<const WorkflowNode *> ready;
    ready.reserve(workflow.nodes.size());
    for (const auto &node : workflow.nodes) {
        if (node.after.empty()) {
            ready.push_back(&node);
        }
    }

    std::vector<const WorkflowNode *> order;
    order.reserve(workflow.nodes.size());
    std::unordered_set<std::string> processed;
    processed.reserve(workflow.nodes.size());

    for (std::size_t index = 0; index < ready.size(); ++index) {
        const auto *node = ready[index];
        if (!processed.insert(node->name).second) {
            continue;
        }
        order.push_back(node);

        for (const auto &candidate : workflow.nodes) {
            if (processed.contains(candidate.name)) {
                continue;
            }

            bool unlocked = false;
            for (const auto &dependency : candidate.after) {
                if (dependency != node->name) {
                    continue;
                }
                auto &remaining = remaining_dependencies[candidate.name];
                if (remaining > 0U) {
                    remaining -= 1U;
                }
                unlocked = true;
            }

            if (unlocked && remaining_dependencies[candidate.name] == 0U) {
                ready.push_back(&candidate);
            }
        }
    }

    return order;
}

bool ProgramIndex::has_unresolved_or_cyclic_dependencies(const WorkflowDecl &workflow) const {
    return topological_order(workflow).size() != workflow.nodes.size();
}

ProgramView::ProgramView(const Program &program) : program_(program), index_(program) {}

} // namespace ahfl::ir
