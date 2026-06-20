#pragma once
#include <optional>
#include <string>
#include <vector>

namespace ahfl::dap {

enum class BreakpointKind {
    State,      // Break on entering a state
    Capability, // Break on capability invocation
    Line        // Source line breakpoint
};

struct Breakpoint {
    int id = 0;
    BreakpointKind kind = BreakpointKind::Line;
    std::string source_file;
    int line = 0;
    std::string condition; // state name or capability name
    bool enabled = true;
    bool verified = false;
};

struct BreakpointHit {
    int breakpoint_id = 0;
    std::string agent_id;
    std::string current_state;
    std::string description;
};

class BreakpointManager {
  public:
    int add_breakpoint(Breakpoint bp);
    bool remove_breakpoint(int id);
    void clear_all();

    [[nodiscard]] std::optional<Breakpoint> get_breakpoint(int id) const;
    [[nodiscard]] std::vector<Breakpoint> all_breakpoints() const;
    [[nodiscard]] size_t count() const;

    [[nodiscard]] std::vector<BreakpointHit>
    check_state_breakpoints(const std::string &agent_id, const std::string &state) const;

    [[nodiscard]] std::vector<BreakpointHit>
    check_capability_breakpoints(const std::string &agent_id, const std::string &capability) const;

  private:
    std::vector<Breakpoint> breakpoints_;
    int next_id_ = 1;
};

} // namespace ahfl::dap
