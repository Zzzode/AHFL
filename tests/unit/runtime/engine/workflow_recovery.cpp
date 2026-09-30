#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "runtime/engine/workflow_recovery.hpp"
#include "runtime/value/value_json.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace {

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

// WH-4b: the append-only `node` memo coordinate round-trips when set (wasm
// whole-workflow memo) and stays nullopt when absent (evaluator per-node memo).
TEST_CASE("workflow recovery memo node coordinate round-trips present and absent") {
    const auto path = unique_store_path();
    WorkflowRecoveryStore store(path);

    WorkflowRecoverySnapshot snap{
        .workflow = WorkflowId{5},
        .checkpoint = CheckpointId{9},
    };
    SuspendedNodeState suspended{
        .node = WorkflowNodeId{2},
        .agent = AgentId{1},
        .pending_cap_id = 3,
        .pending_ordinal = 1,
    };
    // Entry 0: wasm-lane entry WITH a node coordinate (a completed node's call).
    suspended.memo.push_back(CapabilityMemoEntry{
        .ordinal = 0,
        .cap_id = 4,
        .arg_hash = 111,
        .result = make_string("from-node-0"),
        .node = WorkflowNodeId{0},
    });
    // Entry 1: evaluator-lane entry WITHOUT a node coordinate (nullopt == the
    // suspended node).
    suspended.memo.push_back(CapabilityMemoEntry{
        .ordinal = 1,
        .cap_id = 5,
        .arg_hash = 222,
        .result = make_string("from-suspended-node"),
    });
    snap.suspended = std::move(suspended);
    REQUIRE(store.save(snap).has_value());

    const auto loaded = store.load();
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->suspended.has_value());
    REQUIRE(loaded->suspended->memo.size() == 2);
    CHECK(loaded->suspended->memo[0].node.has_value());
    if (loaded->suspended->memo[0].node.has_value()) {
        CHECK(*loaded->suspended->memo[0].node == WorkflowNodeId{0});
    }
    CHECK_FALSE(loaded->suspended->memo[1].node.has_value());
    std::filesystem::remove_all(path.parent_path());
}

namespace {

// Helper: build a v2 snapshot with a single memo entry carrying `result`.
[[nodiscard]] WorkflowRecoverySnapshot memo_snapshot(ahfl::runtime::Value result,
                                                     std::optional<bool> present = std::nullopt) {
    WorkflowRecoverySnapshot snap{
        .workflow = WorkflowId{5},
        .checkpoint = CheckpointId{9},
    };
    SuspendedNodeState suspended{
        .node = WorkflowNodeId{1},
        .agent = AgentId{2},
        .node_input = make_string("input"),
        .pending_cap_id = 3,
        .pending_ordinal = 1,
    };
    CapabilityMemoEntry entry{
        .ordinal = 0,
        .cap_id = 4,
        .arg_hash = 123,
        .result = std::move(result),
    };
    if (present.has_value()) {
        entry.result_present = present;
    }
    suspended.memo.push_back(std::move(entry));
    snap.suspended = std::move(suspended);
    return snap;
}

} // namespace

// RFC 0026 C2b stage3 (P0-14/16): a NativeOnly memo result of each "lossy" shape
// (schema-free load would drop its type) round-trips its EXACT wire spelling
// through the ExactSidecar. Load reconstructs source=ExactSidecar with the
// authoritative_json preserved verbatim; the native `result` projection may be
// lossy but is never the authority.
TEST_CASE("workflow recovery memo sidecar preserves exact wire spelling") {
    auto roundtrip_wire = [](ahfl::runtime::Value v) -> std::string {
        const auto path = unique_store_path();
        WorkflowRecoveryStore store(path);
        const std::string expected_wire = value_to_json(v);
        REQUIRE(store.save(memo_snapshot(std::move(v))).has_value());
        const auto loaded = store.load();
        REQUIRE(loaded.has_value());
        REQUIRE(loaded->suspended.has_value());
        REQUIRE(loaded->suspended->memo.size() == 1);
        const auto &e = loaded->suspended->memo[0];
        CHECK(e.source == PersistedMemoResultSource::ExactSidecar);
        REQUIRE(e.authoritative_json.has_value());
        REQUIRE(e.result_present.has_value());
        CHECK(*e.result_present == true);
        std::filesystem::remove_all(path.parent_path());
        return *e.authoritative_json;
    };

    CHECK(roundtrip_wire(make_decimal("1.23")) == "\"1.23\"");
    CHECK(roundtrip_wire(make_duration("5s")) == "\"5s\"");
    CHECK(roundtrip_wire(make_float(1.0)) == "1.0"); // integral Float preserved in sidecar
    CHECK(roundtrip_wire(make_option_some(make_int(7))) == "7");
    CHECK(roundtrip_wire(make_option_none()) == "null");
    // Set / Map / Unit exact spellings.
    {
        std::vector<ahfl::runtime::Value> items;
        items.push_back(make_int(1));
        items.push_back(make_int(2));
        CHECK(roundtrip_wire(make_set(std::move(items))) == "[1,2]");
    }
    {
        std::vector<std::pair<ahfl::runtime::Value, ahfl::runtime::Value>> entries;
        entries.emplace_back(make_string("k"), make_string("v"));
        CHECK(roundtrip_wire(make_map(std::move(entries))) == R"({"k":"v"})");
    }
    CHECK(roundtrip_wire(make_unit()) == "null");
}

// P0-19: a NativeOnly present bare NoneValue is the established valueless-success
// compat case; save normalizes it to presence=false (no caller mutation), the
// sidecar is JSON null, and load reconstructs ExactSidecar with result_present=false.
TEST_CASE("workflow recovery normalizes valueless NoneValue to presence=false on save") {
    const auto path = unique_store_path();
    WorkflowRecoveryStore store(path);
    auto snap = memo_snapshot(make_none(), /*present=*/true);
    REQUIRE(store.save(snap).has_value());
    // Caller's in-memory snapshot is NOT mutated by save.
    REQUIRE(snap.suspended->memo[0].result_present.has_value());
    CHECK(*snap.suspended->memo[0].result_present == true);

    const auto loaded = store.load();
    REQUIRE(loaded.has_value());
    const auto &e = loaded->suspended->memo[0];
    CHECK(e.source == PersistedMemoResultSource::ExactSidecar);
    REQUIRE(e.result_present.has_value());
    CHECK(*e.result_present == false);
    REQUIRE(e.authoritative_json.has_value());
    CHECK(*e.authoritative_json == "null");
    std::filesystem::remove_all(path.parent_path());
}

// P0-19 fail-closed: a NativeOnly entry declared result_present=false whose result
// is neither NoneValue nor UnitValue (wire != null) must fail the save.
TEST_CASE("workflow recovery rejects presence=false with a non-null result") {
    const auto path = unique_store_path();
    WorkflowRecoveryStore store(path);
    CHECK_FALSE(store.save(memo_snapshot(make_int(7), /*present=*/false)).has_value());
    CHECK_FALSE(store.save(memo_snapshot(make_string("x"), /*present=*/false)).has_value());
    // A UnitValue (wire null) with presence=false IS allowed.
    CHECK(store.save(memo_snapshot(make_unit(), /*present=*/false)).has_value());
    std::filesystem::remove_all(path.parent_path());
}

// P0-20: a legal rich shape whose schema-free projection FAILS (a Map with a
// reserved-marker key) must still LOAD from a real save; source=ExactSidecar with
// authoritative_json preserved, and the native `result` is only a None placeholder
// (never the trust authority).
TEST_CASE("workflow recovery loads a Map with a reserved-marker key via sidecar") {
    const auto path = unique_store_path();
    WorkflowRecoveryStore store(path);
    std::vector<std::pair<ahfl::runtime::Value, ahfl::runtime::Value>> entries;
    entries.emplace_back(make_string("_timestamp"), make_string("ordinary"));
    entries.emplace_back(make_string("plain"), make_string("value"));
    auto map_value = make_map(std::move(entries));
    const std::string expected_wire = value_to_json(map_value);
    REQUIRE(store.save(memo_snapshot(std::move(map_value))).has_value());

    const auto loaded = store.load();
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->suspended.has_value());
    REQUIRE(loaded->suspended->memo.size() == 1);
    const auto &e = loaded->suspended->memo[0];
    CHECK(e.source == PersistedMemoResultSource::ExactSidecar);
    REQUIRE(e.authoritative_json.has_value());
    CHECK(*e.authoritative_json == expected_wire);
    REQUIRE(e.result_present.has_value());
    CHECK(*e.result_present == true);
    // Schema-free projection of {"_timestamp":"ordinary",...} misclassifies as a
    // Timestamp marker and fails -> compat placeholder is a bare NoneValue.
    CHECK(std::holds_alternative<NoneValue>(e.result.node));
    std::filesystem::remove_all(path.parent_path());
}

// P0-20 nested case: a Map whose VALUE is itself a rich shape (a Decimal, which
// schema-free load turns into a String, plus a reserved-marker key) must still
// round-trip its exact wire spelling through the sidecar and load successfully.
TEST_CASE("workflow recovery loads a Map with a nested rich value via sidecar") {
    const auto path = unique_store_path();
    WorkflowRecoveryStore store(path);
    std::vector<std::pair<ahfl::runtime::Value, ahfl::runtime::Value>> inner_entries;
    inner_entries.emplace_back(make_string("amount"), make_decimal("9.99"));
    auto inner_map = make_map(std::move(inner_entries));
    std::vector<std::pair<ahfl::runtime::Value, ahfl::runtime::Value>> entries;
    entries.emplace_back(make_string("_enum"), std::move(inner_map));
    auto map_value = make_map(std::move(entries));
    const std::string expected_wire = value_to_json(map_value);
    REQUIRE(store.save(memo_snapshot(std::move(map_value))).has_value());

    const auto loaded = store.load();
    REQUIRE(loaded.has_value());
    const auto &e = loaded->suspended->memo[0];
    CHECK(e.source == PersistedMemoResultSource::ExactSidecar);
    REQUIRE(e.authoritative_json.has_value());
    CHECK(*e.authoritative_json == expected_wire);
    REQUIRE(e.result_present.has_value());
    CHECK(*e.result_present == true);
    std::filesystem::remove_all(path.parent_path());
}

// State-gate: a persisted memo whose sidecar fields are internally inconsistent
// fails closed at load. result_present without result_wire_json, and a
// result_wire_json whose bytes disagree with the legacy `result` projection.
TEST_CASE("workflow recovery rejects inconsistent sidecar fields on load") {
    auto load_raw = [](const std::string &memo_result_fields) {
        const auto path = unique_store_path();
        WorkflowRecoveryStore store(path);
        std::filesystem::create_directories(path.parent_path());
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << R"({"schema":"ahfl.workflow-recovery.v2","workflow_id":5,"checkpoint_id":9,)"
                  R"("completed_nodes":[],"suspended":{"node_id":1,"agent_id":2,)"
                  R"("pending_cap_id":3,"pending_ordinal":1,"node_input":"input","memo":[{)"
                  R"("ordinal":0,"cap_id":4,"arg_hash":"123",)"
               << memo_result_fields << "}]}}";
        output.close();
        auto loaded = store.load();
        std::filesystem::remove_all(path.parent_path());
        return loaded.has_value();
    };

    // presence bit present but no wire sidecar -> inconsistent (partial sidecar).
    CHECK_FALSE(load_raw(R"("result":5,"result_present":true)"));
    // wire sidecar present but no presence bit -> inconsistent.
    CHECK_FALSE(load_raw(R"("result":5,"result_wire_json":"5")"));
    // consistency equation violated: legacy result 5 vs sidecar "6".
    CHECK_FALSE(load_raw(R"("result":5,"result_wire_json":"6","result_present":true)"));
    // presence=false but sidecar not null.
    CHECK_FALSE(load_raw(R"("result":5,"result_wire_json":"5","result_present":false)"));
    // A consistent ExactSidecar loads.
    CHECK(load_raw(R"("result":5,"result_wire_json":"5","result_present":true)"));
    // A bare LegacyV2 (no sidecar fields) loads.
    CHECK(load_raw(R"("result":5)"));
}

// Integral Float from an OLD (LegacyV2) snapshot: the disk byte is a bare int and
// the raw substring is preserved as authoritative_json for the legacy decoder.
TEST_CASE("workflow recovery preserves legacy integral-float authoritative bytes") {
    const auto path = unique_store_path();
    WorkflowRecoveryStore store(path);
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << R"({"schema":"ahfl.workflow-recovery.v2","workflow_id":5,"checkpoint_id":9,)"
              R"("completed_nodes":[],"suspended":{"node_id":1,"agent_id":2,)"
              R"("pending_cap_id":3,"pending_ordinal":1,"node_input":"input","memo":[{)"
              R"("ordinal":0,"cap_id":4,"arg_hash":"123","result":1}]}})";
    output.close();
    const auto loaded = store.load();
    REQUIRE(loaded.has_value());
    const auto &e = loaded->suspended->memo[0];
    CHECK(e.source == PersistedMemoResultSource::LegacyV2);
    REQUIRE(e.authoritative_json.has_value());
    CHECK(*e.authoritative_json == "1");
    CHECK_FALSE(e.result_present.has_value());
    std::filesystem::remove_all(path.parent_path());
}

// RFC 0026 C2b stage3 (P0-16): the two-field consistency check requires
//   raw legacy `result` bytes == serialize_json(parse(sidecar))
// This CHARACTERIZES the generic serializer's full deterministic canonicalization
// (NOT merely 1.0 -> 1): an ExactSidecar loads iff its legacy `result` is exactly
// the canonical re-serialization of its sidecar wire. Each positive uses a
// non-canonical sidecar spelling paired with its canonical legacy form; a tamper
// negative pairs a sidecar with a legacy form that is NOT its canonical projection.
TEST_CASE("workflow recovery consistency check characterizes serializer canonicalization") {
    // Load a v2 snapshot with one ExactSidecar memo entry whose legacy `result`
    // bytes are `legacy_result` and whose sidecar wire is `sidecar_wire` (embedded
    // as a JSON string via escaping). Returns whether the snapshot loaded.
    auto load_sidecar = [](std::string_view legacy_result,
                           std::string_view sidecar_wire) -> bool {
        // JSON-escape the sidecar wire so it is a valid JSON string literal.
        std::string escaped;
        for (const char c : sidecar_wire) {
            if (c == '"' || c == '\\') {
                escaped.push_back('\\');
            }
            escaped.push_back(c);
        }
        const auto path = unique_store_path();
        WorkflowRecoveryStore store(path);
        std::filesystem::create_directories(path.parent_path());
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << R"({"schema":"ahfl.workflow-recovery.v2","workflow_id":5,"checkpoint_id":9,)"
                  R"("completed_nodes":[],"suspended":{"node_id":1,"agent_id":2,)"
                  R"("pending_cap_id":3,"pending_ordinal":1,"node_input":"input","memo":[{)"
                  R"("ordinal":0,"cap_id":4,"arg_hash":"123","result":)"
               << legacy_result << R"(,"result_wire_json":")" << escaped
               << R"(","result_present":true}]}})";
        output.close();
        auto loaded = store.load();
        std::filesystem::remove_all(path.parent_path());
        return loaded.has_value();
    };

    // Positives: legacy `result` == serialize_json(parse(sidecar)) for a variety of
    // deterministic canonicalizations the serializer applies.
    CHECK(load_sidecar("-0", "-0.0"));                 // negative-zero float -> -0
    CHECK(load_sidecar("1000", "1e3"));                // lowercase exponent -> 1000
    CHECK(load_sidecar("1000", "1E3"));                // uppercase exponent -> 1000
    CHECK(load_sidecar("1", "1.0"));                   // integral float -> 1
    CHECK(load_sidecar("2.5", "2.5"));                 // already canonical
    // String escaping IS canonicalized: the sidecar wire is the U+0041 escape
    // spelling; the serializer re-emits it as the plain character, so the canonical
    // legacy form is "A". Build the sidecar bytes numerically so no transport /
    // patch layer can eat the escape sequence: 0x22 0x5c u 0 0 4 1 0x22.
    std::string escaped_a;
    escaped_a.push_back(static_cast<char>(0x22));
    escaped_a.push_back(static_cast<char>(0x5c));
    escaped_a += "u0041";
    escaped_a.push_back(static_cast<char>(0x22));
    CHECK(escaped_a.size() == 8);
    CHECK(escaped_a[1] == static_cast<char>(0x5c));
    CHECK(load_sidecar(R"json("A")json", escaped_a));
    // Nested Float canonicalizes recursively inside Struct / List / Enum payload.
    // NOTE: this same 1.0 -> 1 projection also governs a compact Option::Some(Float
    // {1.0}) (Option Some encodes as its inner compact value); the actual recursive
    // Option recovery under a binding is proven by the runtime resume matrix.
    CHECK(load_sidecar(R"({"_type":"T","f":1})", R"({"_type":"T","f":1.0})"));
    CHECK(load_sidecar("[1,2.5]", "[1.0,2.5]"));
    CHECK(load_sidecar(R"({"_enum":"E","_variant":"V","_payload":[1]})",
                       R"({"_enum":"E","_variant":"V","_payload":[1.0]})"));
    // Object field order is PRESERVED (not sorted): the canonical form keeps input
    // order, so a same-order legacy result matches.
    CHECK(load_sidecar(R"({"b":1,"a":2})", R"({"b":1,"a":2})"));

    // Tamper negatives: legacy `result` is NOT the canonical projection of the
    // sidecar -> the two fields have drifted -> fail closed.
    CHECK_FALSE(load_sidecar("1.0", "1.0"));           // legacy should be canonical `1`
    CHECK_FALSE(load_sidecar("1000", "1e4"));          // canonical is 10000, not 1000
    CHECK_FALSE(load_sidecar(R"({"a":2,"b":1})", R"({"b":1,"a":2})")); // order differs
    CHECK_FALSE(load_sidecar(R"({"_type":"T","f":1.0})",
                             R"({"_type":"T","f":1.0})")); // nested float not canonicalized
}

namespace {

// Build a v2 snapshot with a single, fully-specified memo entry (every trust-state
// field controlled by the caller) so a hand-built illegal aggregate reaches the
// real save gate. Unlike memo_snapshot(), this does NOT default the source /
// authoritative_json / presence — the caller sets them to model each illegal state.
[[nodiscard]] WorkflowRecoverySnapshot snapshot_with_entry(CapabilityMemoEntry entry) {
    WorkflowRecoverySnapshot snap{
        .workflow = WorkflowId{5},
        .checkpoint = CheckpointId{9},
    };
    SuspendedNodeState suspended{
        .node = WorkflowNodeId{1},
        .agent = AgentId{2},
        .node_input = make_string("input"),
        .pending_cap_id = 3,
        .pending_ordinal = 1,
    };
    suspended.memo.push_back(std::move(entry));
    snap.suspended = std::move(suspended);
    return snap;
}

} // namespace

// RFC 0026 C2b stage3 (P0-18) SAVE gate: CapabilityMemoEntry is a public aggregate,
// so the three-state well-formedness is enforced fail-closed at the real save entry
// point (not merely by a construction helper). Each structurally-illegal state must
// abort the save. Table-driven to avoid per-case boilerplate.
TEST_CASE("workflow recovery save fails closed on ill-formed memo trust state") {
    const auto path = unique_store_path();
    WorkflowRecoveryStore store(path);
    auto save_ok = [&](CapabilityMemoEntry entry) -> bool {
        return store.save(snapshot_with_entry(std::move(entry))).has_value();
    };
    // A minimal valid Int result for the native projection slot.
    auto base = []() { return CapabilityMemoEntry{.ordinal = 0, .cap_id = 4, .arg_hash = 123}; };

    // NativeOnly must have authoritative_json ABSENT and result_present SET.
    {
        auto e = base();
        e.result = make_int(5);
        e.source = PersistedMemoResultSource::NativeOnly;
        e.authoritative_json = "5"; // ILLEGAL: NativeOnly carries no sidecar
        e.result_present = true;
        CHECK_FALSE(save_ok(std::move(e)));
    }
    {
        auto e = base();
        e.result = make_int(5);
        e.source = PersistedMemoResultSource::NativeOnly;
        e.authoritative_json = std::nullopt;
        e.result_present = std::nullopt; // ILLEGAL: NativeOnly presence must be set
        CHECK_FALSE(save_ok(std::move(e)));
    }
    // LegacyV2 must have authoritative_json PRESENT (non-empty) and presence ABSENT.
    {
        auto e = base();
        e.source = PersistedMemoResultSource::LegacyV2;
        e.authoritative_json = std::nullopt; // ILLEGAL: absent string
        e.result_present = std::nullopt;
        CHECK_FALSE(save_ok(std::move(e)));
    }
    {
        auto e = base();
        e.source = PersistedMemoResultSource::LegacyV2;
        e.authoritative_json = ""; // ILLEGAL: empty string
        e.result_present = std::nullopt;
        CHECK_FALSE(save_ok(std::move(e)));
    }
    {
        auto e = base();
        e.source = PersistedMemoResultSource::LegacyV2;
        e.authoritative_json = "{not json"; // ILLEGAL: unparseable authoritative bytes
        e.result_present = std::nullopt;
        CHECK_FALSE(save_ok(std::move(e)));
    }
    {
        auto e = base();
        e.source = PersistedMemoResultSource::LegacyV2;
        e.authoritative_json = "5";
        e.result_present = true; // ILLEGAL: LegacyV2 presence must be absent
        CHECK_FALSE(save_ok(std::move(e)));
    }
    // ExactSidecar must have authoritative_json PRESENT (non-empty) and presence SET.
    {
        auto e = base();
        e.source = PersistedMemoResultSource::ExactSidecar;
        e.authoritative_json = std::nullopt; // ILLEGAL: absent string
        e.result_present = true;
        CHECK_FALSE(save_ok(std::move(e)));
    }
    {
        auto e = base();
        e.source = PersistedMemoResultSource::ExactSidecar;
        e.authoritative_json = ""; // ILLEGAL: empty string
        e.result_present = true;
        CHECK_FALSE(save_ok(std::move(e)));
    }
    {
        auto e = base();
        e.source = PersistedMemoResultSource::ExactSidecar;
        e.authoritative_json = "{not json"; // ILLEGAL: unparseable authoritative bytes
        e.result_present = true;
        CHECK_FALSE(save_ok(std::move(e)));
    }
    {
        auto e = base();
        e.source = PersistedMemoResultSource::ExactSidecar;
        e.authoritative_json = "5";
        e.result_present = std::nullopt; // ILLEGAL: ExactSidecar presence must be set
        CHECK_FALSE(save_ok(std::move(e)));
    }
    // ExactSidecar presence=false + non-null wire: STRUCTURALLY well-formed, but the
    // presence/wire semantic gate at save rejects it (a distinct SAVE negative — the
    // existing load negative is not a save proof).
    {
        auto e = base();
        e.source = PersistedMemoResultSource::ExactSidecar;
        e.authoritative_json = "5"; // non-null wire
        e.result_present = false;   // ILLEGAL: presence=false must spell null
        CHECK_FALSE(save_ok(std::move(e)));
    }

    // --- Positive controls (avoid "all-reject is green") -------------------------
    // Each source's WELL-FORMED state must save AND reload to its EXPECTED post-save
    // source (NativeOnly upgrades to ExactSidecar; Legacy/Exact stay put), so the
    // save-side write_memo_result branch for Legacy/Exact is exercised end-to-end,
    // not just the reject paths. `Value` is move-only (Map/Set hold unique_ptr), so
    // extract only the copyable trust-state fields from the reloaded entry.
    struct ReloadedState {
        PersistedMemoResultSource source;
        std::optional<std::string> authoritative_json;
        std::optional<bool> result_present;
    };
    auto save_and_reload = [&](CapabilityMemoEntry entry) -> std::optional<ReloadedState> {
        const auto ctrl_path = unique_store_path();
        WorkflowRecoveryStore ctrl(ctrl_path);
        if (!ctrl.save(snapshot_with_entry(std::move(entry))).has_value()) {
            std::filesystem::remove_all(ctrl_path.parent_path());
            return std::nullopt;
        }
        auto loaded = ctrl.load();
        std::optional<ReloadedState> out;
        if (loaded.has_value() && loaded->suspended.has_value() &&
            loaded->suspended->memo.size() == 1) {
            const auto &e = loaded->suspended->memo[0];
            out = ReloadedState{e.source, e.authoritative_json, e.result_present};
        }
        std::filesystem::remove_all(ctrl_path.parent_path());
        return out;
    };
    // NativeOnly valid: Int result, no sidecar, presence set -> reloads as an
    // ExactSidecar (save upgrades a NativeOnly to the append-only sidecar form; the
    // detailed NativeOnly->ExactSidecar behavior is covered by the sidecar cases).
    {
        auto e = base();
        e.result = make_int(5);
        e.source = PersistedMemoResultSource::NativeOnly;
        e.authoritative_json = std::nullopt;
        e.result_present = true;
        auto reloaded = save_and_reload(std::move(e));
        REQUIRE(reloaded.has_value());
        CHECK(reloaded->source == PersistedMemoResultSource::ExactSidecar);
        REQUIRE(reloaded->authoritative_json.has_value());
        CHECK(*reloaded->authoritative_json == "5");
        REQUIRE(reloaded->result_present.has_value());
        CHECK(*reloaded->result_present == true);
    }
    // LegacyV2 valid: authoritative="1", presence nullopt -> the save re-emits ONLY
    // the legacy `result` (no sidecar / no presence upgrade), so it reloads STILL as
    // LegacyV2 with authoritative_json="1" and presence UNKNOWN.
    {
        auto e = base();
        e.source = PersistedMemoResultSource::LegacyV2;
        e.authoritative_json = "1";
        e.result_present = std::nullopt;
        auto reloaded = save_and_reload(std::move(e));
        REQUIRE(reloaded.has_value());
        CHECK(reloaded->source == PersistedMemoResultSource::LegacyV2);
        REQUIRE(reloaded->authoritative_json.has_value());
        CHECK(*reloaded->authoritative_json == "1");
        CHECK_FALSE(reloaded->result_present.has_value());
    }
    // ExactSidecar valid: authoritative="1", presence=true -> reloads STILL as
    // ExactSidecar with the wire + presence preserved.
    {
        auto e = base();
        e.source = PersistedMemoResultSource::ExactSidecar;
        e.authoritative_json = "1";
        e.result_present = true;
        auto reloaded = save_and_reload(std::move(e));
        REQUIRE(reloaded.has_value());
        CHECK(reloaded->source == PersistedMemoResultSource::ExactSidecar);
        REQUIRE(reloaded->authoritative_json.has_value());
        CHECK(*reloaded->authoritative_json == "1");
        REQUIRE(reloaded->result_present.has_value());
        CHECK(*reloaded->result_present == true);
    }
    std::filesystem::remove_all(path.parent_path());
}

// RFC 0026 C2b stage3 (P0 provenance laundering): the LegacyV2 save branch has no
// binding, so it must NOT re-canonicalize the legacy bytes. A hostile bare integer
// literal that parses as an out-of-range IntegerFallback would, under a parse +
// generic-serialize re-emit, become exponent/float spelling that re-parses as
// FloatSyntax — which the recovery-internal legacy Float decoder ACCEPTS. Re-saving
// an UNCONSUMED such entry would flip a trust-boundary reject into an accept. So a
// LegacyV2 entry re-saves ONLY when its bytes are already byte-stable under the
// deterministic serializer; anything that would canonicalize is fail-closed and must
// instead be upgraded through per-ordinal consumption (P0-13 dense prefix).
TEST_CASE("workflow recovery save rejects non-byte-stable LegacyV2 (no provenance laundering)") {
    // Return the save result's error (nullopt on success) so a negative can pin the
    // EXACT WorkflowRecoveryError — a WriteFailed / other error must not count as a
    // laundering rejection.
    auto legacy_save_error =
        [](const std::string &authoritative) -> std::optional<WorkflowRecoveryError> {
        const auto path = unique_store_path();
        WorkflowRecoveryStore store(path);
        CapabilityMemoEntry entry{.ordinal = 0, .cap_id = 4, .arg_hash = 123};
        entry.source = PersistedMemoResultSource::LegacyV2;
        entry.authoritative_json = authoritative;
        entry.result_present = std::nullopt;
        const auto result = store.save(snapshot_with_entry(std::move(entry)));
        std::filesystem::remove_all(path.parent_path());
        if (result.has_value()) {
            return std::nullopt;
        }
        return result.error();
    };
    // Laundering negatives: each parses but the serializer would REWRITE it, so save
    // must fail closed with InvalidSnapshot (not merely "not saved").
    for (const auto *laundering : {
             "99999999999999999999999",  // overflow +int -> IntegerFallback -> float
             "-99999999999999999999999", // overflow -int -> same laundering risk
             "1.0",                      // canonical is `1`
             "1e3",                      // canonical is `1000`
             "-0.0",                     // canonical is `-0`
         }) {
        const auto error = legacy_save_error(laundering);
        REQUIRE(error.has_value());
        CHECK(*error == WorkflowRecoveryError::InvalidSnapshot);
    }

    // Byte-stable positive control: `1` is already canonical, so it re-saves and
    // reloads STILL as LegacyV2 with presence UNKNOWN and wire "1".
    {
        const auto path = unique_store_path();
        WorkflowRecoveryStore store(path);
        CapabilityMemoEntry entry{.ordinal = 0, .cap_id = 4, .arg_hash = 123};
        entry.source = PersistedMemoResultSource::LegacyV2;
        entry.authoritative_json = "1";
        entry.result_present = std::nullopt;
        REQUIRE(store.save(snapshot_with_entry(std::move(entry))).has_value());
        const auto loaded = store.load();
        REQUIRE(loaded.has_value());
        REQUIRE(loaded->suspended.has_value());
        REQUIRE(loaded->suspended->memo.size() == 1);
        const auto &e = loaded->suspended->memo[0];
        CHECK(e.source == PersistedMemoResultSource::LegacyV2);
        REQUIRE(e.authoritative_json.has_value());
        CHECK(*e.authoritative_json == "1");
        CHECK_FALSE(e.result_present.has_value());
        std::filesystem::remove_all(path.parent_path());
    }
}

TEST_CASE("workflow recovery load fails closed on malformed sidecar fields") {
    auto load_raw = [](const std::string &memo_result_fields) {
        const auto path = unique_store_path();
        WorkflowRecoveryStore store(path);
        std::filesystem::create_directories(path.parent_path());
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << R"({"schema":"ahfl.workflow-recovery.v2","workflow_id":5,"checkpoint_id":9,)"
                  R"("completed_nodes":[],"suspended":{"node_id":1,"agent_id":2,)"
                  R"("pending_cap_id":3,"pending_ordinal":1,"node_input":"input","memo":[{)"
                  R"("ordinal":0,"cap_id":4,"arg_hash":"123",)"
               << memo_result_fields << "}]}}";
        output.close();
        auto loaded = store.load();
        std::filesystem::remove_all(path.parent_path());
        return loaded.has_value();
    };

    // Sidecar string is EMPTY -> rejected.
    CHECK_FALSE(load_raw(R"("result":5,"result_wire_json":"","result_present":true)"));
    // Sidecar string is present + presence set + consistent-looking, but the sidecar
    // bytes are UNPARSEABLE JSON -> rejected.
    CHECK_FALSE(load_raw(R"("result":5,"result_wire_json":"{bad","result_present":true)"));
    // result_wire_json wrong type (number, not string) -> rejected.
    CHECK_FALSE(load_raw(R"("result":5,"result_wire_json":5,"result_present":true)"));
    // result_present wrong type (string, not bool) -> rejected.
    CHECK_FALSE(load_raw(R"("result":5,"result_wire_json":"5","result_present":"true")"));
    // Missing the required `result` field entirely -> rejected by the required-field
    // check (a LegacyV2 needs the raw `result` substring as its authority).
    CHECK_FALSE(load_raw(R"("result_wire_json":"5","result_present":true)"));
    CHECK_FALSE(load_raw(R"("filler":true)"));
}
