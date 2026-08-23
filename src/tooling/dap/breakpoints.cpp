#include "tooling/dap/breakpoints.hpp"
#include <algorithm>

namespace ahfl::dap {

int BreakpointManager::add_breakpoint(Breakpoint bp) {
    bp.id = next_id_++;
    breakpoints_.push_back(std::move(bp));
    return breakpoints_.back().id;
}

bool BreakpointManager::remove_breakpoint(int id) {
    auto it = std::find_if(breakpoints_.begin(), breakpoints_.end(), [id](const Breakpoint &bp) {
        return bp.id == id;
    });
    if (it != breakpoints_.end()) {
        breakpoints_.erase(it);
        return true;
    }
    return false;
}

void BreakpointManager::clear_all() {
    breakpoints_.clear();
}

std::optional<Breakpoint> BreakpointManager::get_breakpoint(int id) const {
    auto it = std::find_if(breakpoints_.begin(), breakpoints_.end(), [id](const Breakpoint &bp) {
        return bp.id == id;
    });
    if (it != breakpoints_.end()) {
        return *it;
    }
    return std::nullopt;
}

std::vector<Breakpoint> BreakpointManager::all_breakpoints() const {
    return breakpoints_;
}

size_t BreakpointManager::count() const {
    return breakpoints_.size();
}

std::vector<BreakpointHit>
BreakpointManager::check_state_breakpoints(const std::string &agent_id,
                                           const std::string &state) const {
    std::vector<BreakpointHit> hits;
    for (const auto &bp : breakpoints_) {
        if (bp.enabled && bp.kind == BreakpointKind::State && bp.condition == state) {
            hits.push_back({.breakpoint_id = bp.id,
                            .agent_id = agent_id,
                            .current_state = state,
                            .description = "State breakpoint hit: " + state});
        }
    }
    return hits;
}

std::vector<BreakpointHit>
BreakpointManager::check_capability_breakpoints(const std::string &agent_id,
                                                const std::string &capability) const {
    std::vector<BreakpointHit> hits;
    for (const auto &bp : breakpoints_) {
        if (bp.enabled && bp.kind == BreakpointKind::Capability && bp.condition == capability) {
            hits.push_back({.breakpoint_id = bp.id,
                            .agent_id = agent_id,
                            .current_state = "",
                            .description = "Capability breakpoint hit: " + capability});
        }
    }
    return hits;
}

void BreakpointManager::set_breakable_lines(BreakableLineSet lines) {
    breakable_lines_ = std::move(lines);
}

bool BreakpointManager::is_line_breakable(const std::string &file, const int line) const {
    return breakable_lines_.contains({file, line});
}

std::vector<BreakpointHit>
BreakpointManager::check_line_breakpoints(const std::string &file, const int line) const {
    std::vector<BreakpointHit> hits;
    for (const auto &bp : breakpoints_) {
        if (bp.enabled && bp.kind == BreakpointKind::Line && bp.source_file == file &&
            bp.line == line) {
            hits.push_back({.breakpoint_id = bp.id,
                            .agent_id = "",
                            .current_state = "",
                            .description = "Line breakpoint hit: " + file + ":" +
                                           std::to_string(line)});
        }
    }
    return hits;
}

} // namespace ahfl::dap
