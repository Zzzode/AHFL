#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <random>

namespace ahfl::testing {

enum class AstNodeKind {
    Agent,
    Workflow,
    Capability,
    State,
    Transition,
    Struct,
    Field,
    Expression
};

struct GeneratedNode {
    AstNodeKind kind;
    std::string name;
    std::vector<GeneratedNode> children;
    std::vector<std::pair<std::string, std::string>> attributes;
};

struct GeneratorConfig {
    std::size_t max_depth = 5;
    std::size_t max_children = 4;
    std::size_t max_states = 6;
    std::size_t max_transitions = 10;
    uint64_t seed = 42;
};

class AstGenerator {
public:
    explicit AstGenerator(GeneratorConfig config = {});

    [[nodiscard]] GeneratedNode generate_agent();
    [[nodiscard]] GeneratedNode generate_workflow();
    [[nodiscard]] GeneratedNode generate_struct();
    [[nodiscard]] std::string generate_identifier();
    [[nodiscard]] std::string generate_type();

    void reseed(uint64_t seed);
    [[nodiscard]] std::size_t generation_count() const;

private:
    GeneratorConfig config_;
    std::mt19937_64 rng_;
    std::size_t count_ = 0;

    [[nodiscard]] GeneratedNode generate_state();
    [[nodiscard]] GeneratedNode generate_transition(const std::vector<std::string>& states);
    [[nodiscard]] GeneratedNode generate_field();
};

// Utility: convert generated AST to AHFL source text (approximate)
[[nodiscard]] std::string to_ahfl_source(const GeneratedNode& node);

} // namespace ahfl::testing
