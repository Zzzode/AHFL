#include "ahfl/testing/ast_generator.hpp"

#include <algorithm>
#include <array>
#include <sstream>

namespace ahfl::testing {

namespace {

constexpr std::array<const char*, 12> kIdentifiers = {
    "agent_1", "node_a", "node_b", "handler_x",
    "state_idle", "state_active", "state_done", "state_error",
    "field_name", "field_value", "cap_read", "cap_write"
};

constexpr std::array<const char*, 6> kTypes = {
    "String", "Int", "Bool", "Float", "List", "Map"
};

constexpr std::array<const char*, 8> kStateNames = {
    "idle", "running", "waiting", "completed",
    "failed", "paused", "active", "terminated"
};

} // namespace

AstGenerator::AstGenerator(GeneratorConfig config)
    : config_(config), rng_(config.seed) {}

void AstGenerator::reseed(uint64_t seed) {
    rng_.seed(seed);
    config_.seed = seed;
}

std::size_t AstGenerator::generation_count() const {
    return count_;
}

std::string AstGenerator::generate_identifier() {
    auto dist = std::uniform_int_distribution<std::size_t>(0, kIdentifiers.size() - 1);
    auto idx = dist(rng_);
    std::string base{kIdentifiers[idx]};
    base += "_" + std::to_string(count_);
    return base;
}

std::string AstGenerator::generate_type() {
    auto dist = std::uniform_int_distribution<std::size_t>(0, kTypes.size() - 1);
    return std::string{kTypes[dist(rng_)]};
}

GeneratedNode AstGenerator::generate_state() {
    auto dist = std::uniform_int_distribution<std::size_t>(0, kStateNames.size() - 1);
    GeneratedNode node;
    node.kind = AstNodeKind::State;
    node.name = std::string{kStateNames[dist(rng_)]} + "_" + std::to_string(count_);
    ++count_;
    return node;
}

GeneratedNode AstGenerator::generate_transition(const std::vector<std::string>& states) {
    GeneratedNode node;
    node.kind = AstNodeKind::Transition;
    if (states.size() >= 2) {
        auto dist = std::uniform_int_distribution<std::size_t>(0, states.size() - 1);
        auto from_idx = dist(rng_);
        auto to_idx = dist(rng_);
        while (to_idx == from_idx && states.size() > 1) {
            to_idx = dist(rng_);
        }
        node.name = states[from_idx] + " -> " + states[to_idx];
        node.attributes.emplace_back("from", states[from_idx]);
        node.attributes.emplace_back("to", states[to_idx]);
    } else if (!states.empty()) {
        node.name = states[0] + " -> " + states[0];
        node.attributes.emplace_back("from", states[0]);
        node.attributes.emplace_back("to", states[0]);
    }
    ++count_;
    return node;
}

GeneratedNode AstGenerator::generate_field() {
    GeneratedNode node;
    node.kind = AstNodeKind::Field;
    node.name = generate_identifier();
    node.attributes.emplace_back("type", generate_type());
    ++count_;
    return node;
}

GeneratedNode AstGenerator::generate_agent() {
    GeneratedNode agent;
    agent.kind = AstNodeKind::Agent;
    agent.name = "Agent_" + std::to_string(count_);
    ++count_;

    // Generate states (at least 1)
    auto state_dist = std::uniform_int_distribution<std::size_t>(1, config_.max_states);
    auto num_states = state_dist(rng_);

    std::vector<std::string> state_names;
    for (std::size_t i = 0; i < num_states; ++i) {
        auto state = generate_state();
        state_names.push_back(state.name);
        agent.children.push_back(std::move(state));
    }

    // Generate transitions
    auto trans_dist = std::uniform_int_distribution<std::size_t>(0, config_.max_transitions);
    auto num_transitions = trans_dist(rng_);
    for (std::size_t i = 0; i < num_transitions; ++i) {
        agent.children.push_back(generate_transition(state_names));
    }

    return agent;
}

GeneratedNode AstGenerator::generate_workflow() {
    GeneratedNode workflow;
    workflow.kind = AstNodeKind::Workflow;
    workflow.name = "Workflow_" + std::to_string(count_);
    ++count_;

    auto child_dist = std::uniform_int_distribution<std::size_t>(1, config_.max_children);
    auto num_children = child_dist(rng_);

    for (std::size_t i = 0; i < num_children; ++i) {
        GeneratedNode node;
        node.kind = AstNodeKind::Expression;
        node.name = generate_identifier();
        node.attributes.emplace_back("type", generate_type());
        ++count_;
        workflow.children.push_back(std::move(node));
    }

    return workflow;
}

GeneratedNode AstGenerator::generate_struct() {
    GeneratedNode s;
    s.kind = AstNodeKind::Struct;
    s.name = "Struct_" + std::to_string(count_);
    ++count_;

    // Generate fields (at least 1)
    auto field_dist = std::uniform_int_distribution<std::size_t>(1, config_.max_children);
    auto num_fields = field_dist(rng_);

    for (std::size_t i = 0; i < num_fields; ++i) {
        s.children.push_back(generate_field());
    }

    return s;
}

std::string to_ahfl_source(const GeneratedNode& node) {
    std::ostringstream out;

    switch (node.kind) {
        case AstNodeKind::Agent:
            out << "agent " << node.name << " {\n";
            for (const auto& child : node.children) {
                out << "  " << to_ahfl_source(child) << "\n";
            }
            out << "}\n";
            break;

        case AstNodeKind::Workflow:
            out << "workflow " << node.name << " {\n";
            for (const auto& child : node.children) {
                out << "  " << to_ahfl_source(child) << "\n";
            }
            out << "}\n";
            break;

        case AstNodeKind::State:
            out << "state " << node.name << ";";
            break;

        case AstNodeKind::Transition:
            out << "transition " << node.name << ";";
            break;

        case AstNodeKind::Struct:
            out << "struct " << node.name << " {\n";
            for (const auto& child : node.children) {
                out << "  " << to_ahfl_source(child) << "\n";
            }
            out << "}\n";
            break;

        case AstNodeKind::Field:
            if (!node.attributes.empty()) {
                out << node.name << ": " << node.attributes[0].second << ";";
            } else {
                out << node.name << ": Unknown;";
            }
            break;

        case AstNodeKind::Expression:
            if (!node.attributes.empty()) {
                out << "let " << node.name << ": " << node.attributes[0].second << ";";
            } else {
                out << "let " << node.name << ";";
            }
            break;

        case AstNodeKind::Capability:
            out << "capability " << node.name << ";";
            break;
    }

    return out.str();
}

} // namespace ahfl::testing
