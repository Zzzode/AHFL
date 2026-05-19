#include "ahfl/dap/state_inspector.hpp"

namespace ahfl::dap {

void StateInspector::set_agent_state(
    const std::string& agent_id,
    const std::string& state,
    const std::unordered_map<std::string, std::string>& context) {
    agents_[agent_id].current_state = state;
    agents_[agent_id].context = context;
}

void StateInspector::set_input(const std::string& agent_id,
                                const std::unordered_map<std::string, std::string>& input) {
    agents_[agent_id].input = input;
}

void StateInspector::set_output(const std::string& agent_id,
                                 const std::unordered_map<std::string, std::string>& output) {
    agents_[agent_id].output = output;
}

std::vector<Scope> StateInspector::get_scopes(const std::string& agent_id) const {
    std::vector<Scope> scopes;
    auto it = agents_.find(agent_id);
    if (it == agents_.end()) return scopes;
    
    const auto& state = it->second;
    
    // Input scope
    Scope input_scope;
    input_scope.name = "Input";
    for (const auto& [k, v] : state.input) {
        input_scope.variables.push_back({k, v, "String", 0});
    }
    scopes.push_back(std::move(input_scope));
    
    // Context scope
    Scope context_scope;
    context_scope.name = "Context";
    for (const auto& [k, v] : state.context) {
        context_scope.variables.push_back({k, v, "String", 0});
    }
    scopes.push_back(std::move(context_scope));
    
    // Output scope
    Scope output_scope;
    output_scope.name = "Output";
    for (const auto& [k, v] : state.output) {
        output_scope.variables.push_back({k, v, "String", 0});
    }
    scopes.push_back(std::move(output_scope));
    
    return scopes;
}

std::vector<StackFrame> StateInspector::get_stack_frames() const {
    std::vector<StackFrame> frames;
    int id = 1;
    for (const auto& [agent_id, state] : agents_) {
        frames.push_back({
            .id = id++,
            .name = agent_id + "::" + state.current_state,
            .source_file = "",
            .line = 0,
            .agent_id = agent_id,
            .state_name = state.current_state
        });
    }
    return frames;
}

std::vector<Variable> StateInspector::get_variables(
    const std::string& agent_id, const std::string& scope_name) const {
    auto scopes = get_scopes(agent_id);
    for (const auto& scope : scopes) {
        if (scope.name == scope_name) {
            return scope.variables;
        }
    }
    return {};
}

void StateInspector::clear() {
    agents_.clear();
}

} // namespace ahfl::dap
