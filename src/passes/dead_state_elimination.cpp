#include "ahfl/passes/dead_state_elimination.hpp"

#include "ahfl/ir/ir.hpp"

#include <algorithm>
#include <queue>
#include <string>
#include <unordered_set>
#include <variant>

namespace ahfl::passes {

bool DeadStateEliminationPass::run(ir::Program &program) {
    bool any_modified = false;

    for (auto &decl : program.declarations) {
        auto *agent = std::get_if<ir::AgentDecl>(&decl);
        if (agent == nullptr) {
            continue;
        }

        if (agent->states.empty() || agent->initial_state.empty()) {
            continue;
        }

        // BFS from initial_state through transitions
        std::unordered_set<std::string> reachable;
        std::queue<std::string> work;
        work.push(agent->initial_state);
        reachable.insert(agent->initial_state);

        while (!work.empty()) {
            auto current = work.front();
            work.pop();
            for (const auto &t : agent->transitions) {
                if (t.from_state == current && reachable.find(t.to_state) == reachable.end()) {
                    reachable.insert(t.to_state);
                    work.push(t.to_state);
                }
            }
        }

        // Also mark final states as reachable (they're targets)
        for (const auto &fs : agent->final_states) {
            reachable.insert(fs);
        }

        if (reachable.size() == agent->states.size()) {
            continue; // all states are reachable
        }

        // Remove unreachable states
        auto orig_size = agent->states.size();
        agent->states.erase(
            std::remove_if(agent->states.begin(), agent->states.end(),
                           [&](const std::string &s) { return reachable.find(s) == reachable.end(); }),
            agent->states.end());

        // Remove transitions involving unreachable states
        agent->transitions.erase(
            std::remove_if(agent->transitions.begin(), agent->transitions.end(),
                           [&](const ir::TransitionDecl &t) {
                               return reachable.find(t.from_state) == reachable.end() ||
                                      reachable.find(t.to_state) == reachable.end();
                           }),
            agent->transitions.end());

        if (agent->states.size() != orig_size) {
            any_modified = true;
        }
    }

    return any_modified;
}

} // namespace ahfl::passes
