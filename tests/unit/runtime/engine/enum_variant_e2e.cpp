// End-to-end coverage for RFC 0001 enum struct-variant payloads.
//
// The fixture is compiled from source, lowered to IR, executed through
// WorkflowRuntime, and checked only through the public runtime result.

#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/ir/ir.hpp"
#include "e2e_test_harness.hpp"
#include "runtime/engine/workflow_runtime.hpp"
#include "runtime/evaluator/value.hpp"

#include <iostream>
#include <string>
#include <variant>

namespace {

using namespace ahfl;
using namespace ahfl::evaluator;
using namespace ahfl::runtime;
using namespace ahfl::tests::runtime_e2e;

void assert_ticket_response(const WorkflowResult &result, TestStats &stats) {
    stats.check(result.status() == WorkflowStatus::Completed, "enum_variant.status_completed");
    stats.check(!result.has_errors(), "enum_variant.no_errors");
    stats.check(result.report.execution_order.size() == 1, "enum_variant.exec_order_size");
    if (!result.report.execution_order.empty()) {
        const auto *node = result.metadata.node(result.report.execution_order.front());
        stats.check(node != nullptr && node->display_name == "inspect",
                    "enum_variant.exec_order_name");
    }
    const auto *output = result.output();
    stats.check(output != nullptr, "enum_variant.has_output");
    if (output == nullptr) {
        return;
    }

    const auto *response = std::get_if<StructValue>(&output->node);
    stats.check(response != nullptr, "enum_variant.output_is_struct");
    if (response == nullptr) {
        return;
    }

    const auto *id = struct_field(*response, "id");
    stats.check(id != nullptr, "enum_variant.has_id");
    if (id != nullptr) {
        const auto *id_value = std::get_if<IntValue>(&id->node);
        stats.check(id_value != nullptr && id_value->value == 42, "enum_variant.id_from_payload");
    }

    const auto *owner = struct_field(*response, "owner");
    stats.check(owner != nullptr, "enum_variant.has_owner");
    if (owner != nullptr) {
        const auto *owner_value = std::get_if<StringValue>(&owner->node);
        stats.check(owner_value != nullptr && owner_value->value == "system",
                    "enum_variant.default_owner_materialized");
    }
}

} // namespace

int main(int argc, char **argv) {
    if (argc != 2) {
        std::cerr << "usage: " << argv[0] << " <enum_variant_e2e.ahfl>\n";
        return 2;
    }

    const auto program = compile_ahfl_file(argv[1]);
    if (!program.has_value()) {
        std::cerr << "ERROR: failed to compile enum variant E2E fixture\n";
        return 1;
    }

    WorkflowRuntime runtime(*program);
    auto result = runtime.run("runtime::enum_variant_e2e::TicketWorkflow",
                              make_struct("TicketRequest", build_fields("id", make_int(42))));
    if (result.has_errors()) {
        result.diagnostics.render(std::cerr);
    }

    TestStats stats;
    assert_ticket_response(result, stats);

    std::cout << stats.pass_count << "/" << stats.test_count << " tests passed\n";
    return stats.passed() ? 0 : 1;
}
