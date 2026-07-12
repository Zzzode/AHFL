// End-to-end coverage for RFC 0002 if-let optional narrowing.
//
// The fixture is compiled from source, lowered to IR, executed through
// WorkflowRuntime, and checked only through the public runtime result.

#include "e2e_test_harness.hpp"
#include "runtime/engine/workflow_runtime.hpp"
#include "runtime/evaluator/value.hpp"

#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

using namespace ahfl::evaluator;
using namespace ahfl::runtime;
using namespace ahfl::tests::runtime_e2e;

[[nodiscard]] Value make_request(Value maybe) {
    return make_struct("InspectRequest", build_fields("maybe", std::move(maybe)));
}

[[nodiscard]] Value make_maybe_some(std::int64_t value) {
    std::vector<Value> payload;
    payload.push_back(make_int(value));
    return make_enum("runtime::if_let_e2e::Maybe", "Some", std::move(payload));
}

[[nodiscard]] Value make_maybe_none() {
    return make_enum("runtime::if_let_e2e::Maybe", "None");
}

void assert_inspect_response(const WorkflowResult &result,
                             TestStats &stats,
                             std::string_view case_name,
                             std::string_view expected_branch,
                             std::int64_t expected_value) {
    const auto prefix = std::string{"if_let."} + std::string{case_name};

    stats.check(result.status() == WorkflowStatus::Completed, prefix + ".status_completed");
    stats.check(!result.has_errors(), prefix + ".no_errors");
    stats.check(result.report.execution_order.size() == 1, prefix + ".exec_order_size");
    if (!result.report.execution_order.empty()) {
        const auto *node = result.metadata.node(result.report.execution_order.front());
        stats.check(node != nullptr && node->display_name == "inspect",
                    prefix + ".exec_order_name");
    }
    const auto *output = result.output();
    stats.check(output != nullptr, prefix + ".has_output");
    if (output == nullptr) {
        return;
    }

    const auto *response = std::get_if<StructValue>(&output->node);
    stats.check(response != nullptr, prefix + ".output_is_struct");
    if (response == nullptr) {
        return;
    }

    const auto *branch = struct_field(*response, "branch");
    stats.check(branch != nullptr, prefix + ".has_branch");
    if (branch != nullptr) {
        const auto *branch_value = std::get_if<StringValue>(&branch->node);
        stats.check(branch_value != nullptr && branch_value->value == expected_branch,
                    prefix + ".branch_matches");
    }

    const auto *value = struct_field(*response, "value");
    stats.check(value != nullptr, prefix + ".has_value_field");
    if (value != nullptr) {
        const auto *int_value = std::get_if<IntValue>(&value->node);
        stats.check(int_value != nullptr && int_value->value == expected_value,
                    prefix + ".value_matches");
    }
}

} // namespace

int main(int argc, char **argv) {
    if (argc != 2) {
        std::cerr << "usage: " << argv[0] << " <if_let_e2e.ahfl>\n";
        return 2;
    }

    const auto program = compile_ahfl_file(argv[1]);
    if (!program.has_value()) {
        std::cerr << "ERROR: failed to compile if-let E2E fixture\n";
        return 1;
    }

    TestStats stats;
    WorkflowRuntime runtime(*program);

    auto some_result =
        runtime.run("runtime::if_let_e2e::IfLetWorkflow", make_request(make_maybe_some(42)));
    if (some_result.has_errors()) {
        some_result.diagnostics.render(std::cerr);
    }
    assert_inspect_response(some_result, stats, "some", "some", 42);

    auto none_result =
        runtime.run("runtime::if_let_e2e::IfLetWorkflow", make_request(make_maybe_none()));
    if (none_result.has_errors()) {
        none_result.diagnostics.render(std::cerr);
    }
    assert_inspect_response(none_result, stats, "none", "none", 0);

    std::cout << stats.pass_count << "/" << stats.test_count << " tests passed\n";
    return stats.passed() ? 0 : 1;
}
