#pragma once
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ahfl::dap {

/// Hash for std::pair specializations, enabling pair-keyed unordered containers.
struct PairHash {
    template <typename T1, typename T2>
    [[nodiscard]] std::size_t operator()(const std::pair<T1, T2> &pair) const noexcept {
        const std::size_t h1 = std::hash<T1>{}(pair.first);
        const std::size_t h2 = std::hash<T2>{}(pair.second);
        return h1 ^ (h2 << 1U);
    }
};

/// A breakable source location: (file path, 1-based line).
using BreakableLine = std::pair<std::string, int>;
using BreakableLineSet = std::unordered_set<BreakableLine, PairHash>;

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

    /// Register the set of source locations that have a mappable IR
    /// declaration. Line breakpoints set on these locations verify as
    /// `verified: true` in the setBreakpoints response (RFC 0015 Slice 3).
    void set_breakable_lines(BreakableLineSet lines);

    [[nodiscard]] bool is_line_breakable(const std::string &file, int line) const;

    [[nodiscard]] std::vector<BreakpointHit>
    check_line_breakpoints(const std::string &file, int line) const;

  private:
    std::vector<Breakpoint> breakpoints_;
    BreakableLineSet breakable_lines_;
    int next_id_ = 1;
};

} // namespace ahfl::dap
