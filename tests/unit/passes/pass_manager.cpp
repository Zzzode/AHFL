#include "passes/pass_manager.hpp"
#include "ahfl/ir/ir.hpp"

#include <cassert>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>

namespace {

using namespace ahfl::passes;

/// A pass that never modifies the IR.
class NoOpPass final : public Pass {
  public:
    [[nodiscard]] std::string_view name() const override { return "NoOpPass"; }
    [[nodiscard]] bool run(ahfl::ir::Program & /*program*/) override { return false; }
};

/// A pass that always reports a modification.
class AlwaysModifyPass final : public Pass {
  public:
    [[nodiscard]] std::string_view name() const override { return "AlwaysModifyPass"; }
    [[nodiscard]] bool run(ahfl::ir::Program & /*program*/) override { return true; }
};

bool test_empty_pass_manager() {
    PassManager pm;
    ahfl::ir::Program prog;

    auto result = pm.run(prog);
    if (result.any_modified != false) return false;
    if (!result.executed.empty()) return false;
    return true;
}

bool test_noop_pass_returns_false() {
    PassManager pm;
    pm.add_pass(std::make_unique<NoOpPass>());

    ahfl::ir::Program prog;
    auto result = pm.run(prog);
    if (result.any_modified != false) return false;
    // The pass should still be listed as executed
    if (result.executed.size() != 1) return false;
    if (result.executed[0] != "NoOpPass") return false;
    return true;
}

bool test_modify_pass_returns_true() {
    PassManager pm;
    pm.add_pass(std::make_unique<AlwaysModifyPass>());

    ahfl::ir::Program prog;
    auto result = pm.run(prog);
    if (result.any_modified != true) return false;
    if (result.executed.size() != 1) return false;
    if (result.executed[0] != "AlwaysModifyPass") return false;
    return true;
}

bool test_create_default_pipeline() {
    auto pipeline = create_default_pipeline();
    if (!pipeline) return false;
    // The default pipeline should be non-null; we just verify it exists
    return true;
}

} // namespace

int main() {
    int failures = 0;
    auto run = [&](bool (*fn)(), const char *name) {
        if (!fn()) {
            std::cerr << "FAIL: " << name << "\n";
            ++failures;
        }
    };

    run(test_empty_pass_manager, "test_empty_pass_manager");
    run(test_noop_pass_returns_false, "test_noop_pass_returns_false");
    run(test_modify_pass_returns_true, "test_modify_pass_returns_true");
    run(test_create_default_pipeline, "test_create_default_pipeline");

    if (failures > 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cerr << "All tests passed\n";
    return 0;
}
