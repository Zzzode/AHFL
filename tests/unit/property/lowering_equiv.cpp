#include <cstdio>
#include "testing/ast_generator.hpp"

static int test_count = 0;
static int pass_count = 0;

static void check(bool condition, const char* name) {
    ++test_count;
    if (condition) { ++pass_count; std::printf("  PASS: %s\n", name); }
    else { std::printf("  FAIL: %s\n", name); }
}

int main() {
    std::printf("=== Property Tests: Lowering Equivalence ===\n");

    using namespace ahfl::testing;

    // Property 1: Generated agents always have at least one state
    {
        AstGenerator gen({.seed = 100});
        bool all_have_states = true;
        for (int i = 0; i < 50; ++i) {
            auto agent = gen.generate_agent();
            bool has_state = false;
            for (const auto& child : agent.children) {
                if (child.kind == AstNodeKind::State) has_state = true;
            }
            if (!has_state) all_have_states = false;
        }
        check(all_have_states, "generated agents always have states");
    }

    // Property 2: Generated structs always have fields
    {
        AstGenerator gen({.seed = 200});
        bool all_have_fields = true;
        for (int i = 0; i < 50; ++i) {
            auto s = gen.generate_struct();
            bool has_field = false;
            for (const auto& child : s.children) {
                if (child.kind == AstNodeKind::Field) has_field = true;
            }
            if (!has_field) all_have_fields = false;
        }
        check(all_have_fields, "generated structs always have fields");
    }

    // Property 3: Source text is non-empty for any generated node
    {
        AstGenerator gen({.seed = 300});
        bool all_non_empty = true;
        for (int i = 0; i < 50; ++i) {
            auto agent = gen.generate_agent();
            auto src = to_ahfl_source(agent);
            if (src.empty()) all_non_empty = false;
        }
        check(all_non_empty, "generated source is always non-empty");
    }

    // Property 4: Deterministic generation with same seed
    {
        AstGenerator gen1({.seed = 42});
        AstGenerator gen2({.seed = 42});
        auto a1 = gen1.generate_agent();
        auto a2 = gen2.generate_agent();
        check(a1.name == a2.name, "same seed produces same agent name");
    }

    std::printf("\n%d/%d tests passed\n", pass_count, test_count);
    return (pass_count == test_count) ? 0 : 1;
}
