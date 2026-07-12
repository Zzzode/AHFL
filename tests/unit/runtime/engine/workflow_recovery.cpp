#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "runtime/engine/workflow_recovery.hpp"
#include "runtime/evaluator/value_json.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>

namespace {

using namespace ahfl::evaluator;
using namespace ahfl::runtime;

[[nodiscard]] std::filesystem::path unique_store_path() {
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("ahfl-workflow-recovery-" + std::to_string(suffix)) / "snapshot.json";
}

[[nodiscard]] WorkflowRecoverySnapshot snapshot(std::string output) {
    WorkflowRecoverySnapshot value{
        .workflow = WorkflowId{2},
        .checkpoint = CheckpointId{7},
    };
    value.completed_nodes.push_back(RecoveredNodeState{
        .node = WorkflowNodeId{0},
        .agent = AgentId{3},
        .output = make_string(std::move(output)),
    });
    value.completed_nodes.push_back(RecoveredNodeState{
        .node = WorkflowNodeId{1},
        .agent = AgentId{4},
        .output = std::nullopt,
    });
    return value;
}

} // namespace

TEST_CASE("workflow recovery store round-trips strong IDs and values") {
    const auto path = unique_store_path();
    WorkflowRecoveryStore store(path);
    REQUIRE(store.save(snapshot("committed")).has_value());

    const auto loaded = store.load();
    REQUIRE(loaded.has_value());
    CHECK(loaded->workflow == WorkflowId{2});
    CHECK(loaded->checkpoint == CheckpointId{7});
    REQUIRE(loaded->completed_nodes.size() == 2);
    CHECK(loaded->completed_nodes[0].node == WorkflowNodeId{0});
    CHECK(loaded->completed_nodes[0].agent == AgentId{3});
    REQUIRE(loaded->completed_nodes[0].output.has_value());
    CHECK(value_to_json(*loaded->completed_nodes[0].output) == "\"committed\"");
    CHECK_FALSE(loaded->completed_nodes[1].output.has_value());

    std::filesystem::remove_all(path.parent_path());
}

TEST_CASE("workflow recovery store ignores partial temp and preserves committed snapshot") {
    const auto path = unique_store_path();
    WorkflowRecoveryStore store(path);
    REQUIRE(store.save(snapshot("old")).has_value());

    {
        std::ofstream partial(ahfl::support::atomic_temporary_path(path),
                              std::ios::binary | std::ios::trunc);
        partial << "{\"schema\":\"partial";
    }
    auto loaded = store.load();
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->completed_nodes[0].output.has_value());
    CHECK(value_to_json(*loaded->completed_nodes[0].output) == "\"old\"");

    const auto failed = store.save(
        snapshot("new"),
        ahfl::support::AtomicReplaceOptions{
            .before_commit = [](const std::filesystem::path &,
                                const std::filesystem::path &) { return false; },
        });
    REQUIRE_FALSE(failed.has_value());
    loaded = store.load();
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->completed_nodes[0].output.has_value());
    CHECK(value_to_json(*loaded->completed_nodes[0].output) == "\"old\"");

    std::filesystem::remove_all(path.parent_path());
}

TEST_CASE("workflow recovery store rejects unknown and legacy schemas") {
    const auto path = unique_store_path();
    WorkflowRecoveryStore store(path);
    std::filesystem::create_directories(path.parent_path());

    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << R"({"schema":"ahfl.workflow-recovery.v2","workflow_id":0,)"
                  R"("checkpoint_id":0,"completed_nodes":[]})";
    }
    auto loaded = store.load();
    REQUIRE_FALSE(loaded.has_value());
    CHECK(loaded.error() == WorkflowRecoveryError::InvalidSnapshot);

    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << R"({"format_version":"ahfl.workflow-recovery.v0","workflow":"legacy",)"
                  R"("checkpoint":"name-based","nodes":[]})";
    }
    loaded = store.load();
    REQUIRE_FALSE(loaded.has_value());
    CHECK(loaded.error() == WorkflowRecoveryError::InvalidSnapshot);

    std::filesystem::remove_all(path.parent_path());
}
