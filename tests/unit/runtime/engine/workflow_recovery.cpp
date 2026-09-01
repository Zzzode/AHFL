#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "runtime/engine/workflow_recovery.hpp"
#include "runtime/evaluator/value_json.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>

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
        output << R"({"schema":"ahfl.workflow-recovery.v99","workflow_id":0,)"
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

// RFC 0026 C2b P0-10: a snapshot whose completed-node output carries an ambiguous
// number (a high-bit unsigned magnitude, or an integer token beyond uint64) must
// fail closed on load via the direct-DOM decode, not silently degrade.
TEST_CASE("workflow recovery rejects a snapshot with an ambiguous numeric output") {
    SUBCASE("high-bit unsigned integer output") {
        const auto path = unique_store_path();
        WorkflowRecoveryStore store(path);
        std::filesystem::create_directories(path.parent_path());
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << R"({"schema":"ahfl.workflow-recovery.v1","workflow_id":0,"checkpoint_id":0,)"
                  R"("completed_nodes":[{"node_id":0,"agent_id":0,)"
                  R"("output":18446744073709551615}]})";
        output.close();
        const auto loaded = store.load();
        REQUIRE_FALSE(loaded.has_value());
        CHECK(loaded.error() == WorkflowRecoveryError::InvalidSnapshot);
        std::filesystem::remove_all(path.parent_path());
    }

    SUBCASE("integer-token-fallback output") {
        const auto path = unique_store_path();
        WorkflowRecoveryStore store(path);
        std::filesystem::create_directories(path.parent_path());
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << R"({"schema":"ahfl.workflow-recovery.v1","workflow_id":0,"checkpoint_id":0,)"
                  R"("completed_nodes":[{"node_id":0,"agent_id":0,)"
                  R"("output":99999999999999999999999}]})";
        output.close();
        const auto loaded = store.load();
        REQUIRE_FALSE(loaded.has_value());
        CHECK(loaded.error() == WorkflowRecoveryError::InvalidSnapshot);
        std::filesystem::remove_all(path.parent_path());
    }

    SUBCASE("negative integer-token-fallback output") {
        const auto path = unique_store_path();
        WorkflowRecoveryStore store(path);
        std::filesystem::create_directories(path.parent_path());
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << R"({"schema":"ahfl.workflow-recovery.v1","workflow_id":0,"checkpoint_id":0,)"
                  R"("completed_nodes":[{"node_id":0,"agent_id":0,)"
                  R"("output":-99999999999999999999999}]})";
        output.close();
        const auto loaded = store.load();
        REQUIRE_FALSE(loaded.has_value());
        CHECK(loaded.error() == WorkflowRecoveryError::InvalidSnapshot);
        std::filesystem::remove_all(path.parent_path());
    }
}

// P0-10: a hand-written snapshot whose output is a genuine float literal (1.0)
// loads as a FloatValue — the direct-DOM decode preserves the source kind and
// does NOT launder it into an Int via a serialize->reparse.
TEST_CASE("workflow recovery direct-DOM load preserves a float output") {
    const auto path = unique_store_path();
    WorkflowRecoveryStore store(path);
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << R"({"schema":"ahfl.workflow-recovery.v1","workflow_id":0,"checkpoint_id":0,)"
              R"("completed_nodes":[{"node_id":0,"agent_id":0,"output":1.0},)"
              R"({"node_id":1,"agent_id":0,"output":42}]})";
    output.close();
    const auto loaded = store.load();
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->completed_nodes.size() == 2);
    REQUIRE(loaded->completed_nodes[0].output.has_value());
    CHECK(std::holds_alternative<FloatValue>(loaded->completed_nodes[0].output->node));
    REQUIRE(loaded->completed_nodes[1].output.has_value());
    CHECK(std::holds_alternative<IntValue>(loaded->completed_nodes[1].output->node));
    std::filesystem::remove_all(path.parent_path());
}

// P0-10 byte-compat boundary: a REAL store.save of a FloatValue{1.0} still writes
// the integral float as bare `1` (plan A does not change the generic float
// formatter). Because the persisted byte is a bare integer, a subsequent load
// reconstructs it as an IntValue — this is the pre-existing v1/v2 snapshot byte
// contract, NOT a full float round-trip, and is intentionally out of scope for
// this change.
TEST_CASE("workflow recovery save keeps the integral-Float byte baseline (bare 1)") {
    const auto path = unique_store_path();
    WorkflowRecoveryStore store(path);

    WorkflowRecoverySnapshot snap{
        .workflow = WorkflowId{2},
        .checkpoint = CheckpointId{7},
    };
    snap.completed_nodes.push_back(RecoveredNodeState{
        .node = WorkflowNodeId{0},
        .agent = AgentId{0},
        .output = make_float(1.0),
    });
    REQUIRE(store.save(snap).has_value());

    std::ifstream in(path, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());
    CHECK(text.find("\"output\":1") != std::string::npos); // bare integer
    CHECK(text.find("1.0") == std::string::npos);          // NOT 1.0

    // Consequently the round-trip reconstructs an IntValue (byte-compat boundary).
    const auto loaded = store.load();
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->completed_nodes[0].output.has_value());
    CHECK(std::holds_alternative<IntValue>(loaded->completed_nodes[0].output->node));
    std::filesystem::remove_all(path.parent_path());
}

// P0-10 no-regression: a real v2 snapshot (suspended record + memo) with ordinary
// signed-int / string / float values round-trips through save/load unchanged.
TEST_CASE("workflow recovery v2 round-trips with suspended record and memo") {
    const auto path = unique_store_path();
    WorkflowRecoveryStore store(path);

    WorkflowRecoverySnapshot snap{
        .workflow = WorkflowId{5},
        .checkpoint = CheckpointId{9},
    };
    snap.completed_nodes.push_back(RecoveredNodeState{
        .node = WorkflowNodeId{0},
        .agent = AgentId{1},
        .output = make_int(7),
    });
    SuspendedNodeState suspended{
        .node = WorkflowNodeId{1},
        .agent = AgentId{2},
        .node_input = make_string("input"),
        .pending_cap_id = 3,
        .pending_ordinal = 1,
    };
    suspended.memo.push_back(CapabilityMemoEntry{
        .ordinal = 0,
        .cap_id = 4,
        .arg_hash = 123,
        .result = make_float(2.5),
    });
    snap.suspended = std::move(suspended);
    REQUIRE(store.save(snap).has_value());

    const auto loaded = store.load();
    REQUIRE(loaded.has_value());
    CHECK(loaded->workflow == WorkflowId{5});
    REQUIRE(loaded->suspended.has_value());
    CHECK(loaded->suspended->node == WorkflowNodeId{1});
    CHECK(loaded->suspended->pending_cap_id == 3);
    REQUIRE(loaded->suspended->node_input.has_value());
    CHECK(value_to_json(*loaded->suspended->node_input) == "\"input\"");
    REQUIRE(loaded->suspended->memo.size() == 1);
    CHECK(loaded->suspended->memo[0].ordinal == 0);
    CHECK(loaded->suspended->memo[0].arg_hash == 123);
    CHECK(std::holds_alternative<FloatValue>(loaded->suspended->memo[0].result.node));
    std::filesystem::remove_all(path.parent_path());
}
