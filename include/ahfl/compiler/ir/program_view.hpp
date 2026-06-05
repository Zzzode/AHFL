#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "ahfl/compiler/ir/ir.hpp"

namespace ahfl::ir {

class ProgramIndex {
  public:
    ProgramIndex() = default;
    explicit ProgramIndex(const Program &program);

    void rebuild(const Program &program);

    [[nodiscard]] const Program *program() const noexcept {
        return program_;
    }
    [[nodiscard]] const AgentDecl *find_agent(std::string_view canonical_name) const;
    [[nodiscard]] const FlowDecl *find_flow_for_agent(std::string_view canonical_name) const;
    [[nodiscard]] const WorkflowDecl *find_workflow(std::string_view canonical_name) const;
    [[nodiscard]] const CapabilityDecl *find_capability(std::string_view canonical_name) const;
    [[nodiscard]] const StructDecl *find_struct(std::string_view canonical_name) const;
    [[nodiscard]] const EnumDecl *find_enum(std::string_view canonical_name) const;

    [[nodiscard]] const std::vector<const AgentDecl *> &agents() const noexcept {
        return agents_in_order_;
    }
    [[nodiscard]] const std::vector<const FlowDecl *> &flows() const noexcept {
        return flows_in_order_;
    }
    [[nodiscard]] const std::vector<const WorkflowDecl *> &workflows() const noexcept {
        return workflows_in_order_;
    }
    [[nodiscard]] const std::vector<const CapabilityDecl *> &capabilities() const noexcept {
        return capabilities_in_order_;
    }
    [[nodiscard]] const std::vector<const StructDecl *> &structs() const noexcept {
        return structs_in_order_;
    }
    [[nodiscard]] const std::vector<const EnumDecl *> &enums() const noexcept {
        return enums_in_order_;
    }

    [[nodiscard]] std::vector<const WorkflowNode *>
    topological_order(const WorkflowDecl &workflow) const;
    [[nodiscard]] bool has_unresolved_or_cyclic_dependencies(const WorkflowDecl &workflow) const;

  private:
    const Program *program_{nullptr};
    std::unordered_map<std::string, const AgentDecl *> agents_;
    std::unordered_map<std::string, const FlowDecl *> flows_by_agent_;
    std::unordered_map<std::string, const WorkflowDecl *> workflows_;
    std::unordered_map<std::string, const CapabilityDecl *> capabilities_;
    std::unordered_map<std::string, const StructDecl *> structs_;
    std::unordered_map<std::string, const EnumDecl *> enums_;
    std::vector<const AgentDecl *> agents_in_order_;
    std::vector<const FlowDecl *> flows_in_order_;
    std::vector<const WorkflowDecl *> workflows_in_order_;
    std::vector<const CapabilityDecl *> capabilities_in_order_;
    std::vector<const StructDecl *> structs_in_order_;
    std::vector<const EnumDecl *> enums_in_order_;

    template <typename T>
    [[nodiscard]] static const T *find_in(const std::unordered_map<std::string, const T *> &index,
                                          std::string_view canonical_name) {
        const auto found = index.find(std::string(canonical_name));
        return found == index.end() ? nullptr : found->second;
    }
};

class ProgramView {
  public:
    explicit ProgramView(const Program &program);

    [[nodiscard]] const Program &program() const noexcept {
        return program_;
    }
    [[nodiscard]] const ProgramIndex &index() const noexcept {
        return index_;
    }

    [[nodiscard]] const AgentDecl *find_agent(std::string_view canonical_name) const {
        return index_.find_agent(canonical_name);
    }
    [[nodiscard]] const FlowDecl *find_flow_for_agent(std::string_view canonical_name) const {
        return index_.find_flow_for_agent(canonical_name);
    }
    [[nodiscard]] const WorkflowDecl *find_workflow(std::string_view canonical_name) const {
        return index_.find_workflow(canonical_name);
    }
    [[nodiscard]] const CapabilityDecl *find_capability(std::string_view canonical_name) const {
        return index_.find_capability(canonical_name);
    }
    [[nodiscard]] const StructDecl *find_struct(std::string_view canonical_name) const {
        return index_.find_struct(canonical_name);
    }
    [[nodiscard]] const EnumDecl *find_enum(std::string_view canonical_name) const {
        return index_.find_enum(canonical_name);
    }
    [[nodiscard]] const std::vector<const AgentDecl *> &agents() const noexcept {
        return index_.agents();
    }
    [[nodiscard]] const std::vector<const FlowDecl *> &flows() const noexcept {
        return index_.flows();
    }
    [[nodiscard]] const std::vector<const WorkflowDecl *> &workflows() const noexcept {
        return index_.workflows();
    }
    [[nodiscard]] const std::vector<const CapabilityDecl *> &capabilities() const noexcept {
        return index_.capabilities();
    }

  private:
    const Program &program_;
    ProgramIndex index_;
};

} // namespace ahfl::ir
