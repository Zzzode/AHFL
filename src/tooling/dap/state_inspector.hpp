#pragma once
#include <string>
#include <unordered_map>
#include <vector>

namespace ahfl::dap {

struct Variable {
    std::string name;
    std::string value;
    std::string type;
    int variables_reference = 0; // 0 = no children
};

struct Scope {
    std::string name; // "Input", "Context", "Output"
    std::vector<Variable> variables;
};

struct StackFrame {
    int id = 0;
    std::string name;
    std::string source_file;
    int line = 0;
    std::string agent_id;
    std::string state_name;
};

class StateInspector {
  public:
    void set_agent_state(const std::string &agent_id,
                         const std::string &state,
                         const std::unordered_map<std::string, std::string> &context);

    void set_input(const std::string &agent_id,
                   const std::unordered_map<std::string, std::string> &input);
    void set_output(const std::string &agent_id,
                    const std::unordered_map<std::string, std::string> &output);

    [[nodiscard]] std::vector<Scope> get_scopes(const std::string &agent_id) const;
    [[nodiscard]] std::vector<StackFrame> get_stack_frames() const;
    [[nodiscard]] std::vector<Variable> get_variables(const std::string &agent_id,
                                                      const std::string &scope_name) const;

    void clear();

  private:
    struct AgentDebugState {
        std::string current_state;
        std::unordered_map<std::string, std::string> context;
        std::unordered_map<std::string, std::string> input;
        std::unordered_map<std::string, std::string> output;
    };

    std::unordered_map<std::string, AgentDebugState> agents_;
};

} // namespace ahfl::dap
