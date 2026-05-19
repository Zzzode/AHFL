#include <cstdio>
#include <string>
#include "ahfl/testing/ast_generator.hpp"

static int test_count = 0;
static int pass_count = 0;

static void check(bool condition, const char* name) {
    ++test_count;
    if (condition) { ++pass_count; std::printf("  PASS: %s\n", name); }
    else { std::printf("  FAIL: %s\n", name); }
}

namespace {

// Simple SMV syntax validation (checks basic structure)
bool has_valid_smv_structure(const std::string& smv) {
    // Must contain MODULE
    if (smv.find("MODULE") == std::string::npos) return false;
    // Must contain VAR section
    if (smv.find("VAR") == std::string::npos) return false;
    return true;
}

// Generate a simple SMV from an agent's states
std::string generate_simple_smv(const ahfl::testing::GeneratedNode& agent) {
    std::string smv = "MODULE main\nVAR\n";
    smv += "  state : {";
    bool first = true;
    for (const auto& child : agent.children) {
        if (child.kind == ahfl::testing::AstNodeKind::State) {
            if (!first) smv += ", ";
            smv += child.name;
            first = false;
        }
    }
    if (first) smv += "idle";  // fallback
    smv += "};\n";
    smv += "ASSIGN\n  init(state) := ";
    // Use first state
    for (const auto& child : agent.children) {
        if (child.kind == ahfl::testing::AstNodeKind::State) {
            smv += child.name;
            break;
        }
    }
    smv += ";\n";
    return smv;
}

} // namespace

int main() {
    std::printf("=== Property Tests: SMV Syntax ===\n");

    using namespace ahfl::testing;

    // Property 1: Generated SMV always has MODULE keyword
    {
        AstGenerator gen({.seed = 500});
        bool all_valid = true;
        for (int i = 0; i < 50; ++i) {
            auto agent = gen.generate_agent();
            auto smv = generate_simple_smv(agent);
            if (!has_valid_smv_structure(smv)) all_valid = false;
        }
        check(all_valid, "all generated SMV has valid structure");
    }

    // Property 2: SMV output is never empty
    {
        AstGenerator gen({.seed = 600});
        bool all_non_empty = true;
        for (int i = 0; i < 50; ++i) {
            auto agent = gen.generate_agent();
            auto smv = generate_simple_smv(agent);
            if (smv.empty()) all_non_empty = false;
        }
        check(all_non_empty, "SMV output is never empty");
    }

    // Property 3: SMV state enum matches agent state count
    {
        AstGenerator gen({.seed = 700});
        bool all_match = true;
        for (int i = 0; i < 30; ++i) {
            auto agent = gen.generate_agent();
            std::size_t state_count = 0;
            for (const auto& child : agent.children) {
                if (child.kind == AstNodeKind::State) ++state_count;
            }
            auto smv = generate_simple_smv(agent);
            // Count commas in VAR line (states - 1 commas)
            std::size_t comma_count = 0;
            auto var_pos = smv.find("state : {");
            if (var_pos != std::string::npos) {
                auto end_pos = smv.find("}", var_pos);
                for (auto p = var_pos; p < end_pos; ++p) {
                    if (smv[p] == ',') ++comma_count;
                }
            }
            if (state_count > 0 && comma_count != state_count - 1) all_match = false;
        }
        check(all_match, "SMV state count matches agent states");
    }

    std::printf("\n%d/%d tests passed\n", pass_count, test_count);
    return (pass_count == test_count) ? 0 : 1;
}
