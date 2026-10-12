#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "base/json/json_value.hpp"
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

struct StoreFixture {
    std::filesystem::path path{unique_store_path()};
    WorkflowRecoveryStore store{path};
    ~StoreFixture() {
        std::filesystem::remove_all(path.parent_path());
    }
    void write(std::string_view text) const {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << text;
    }
    [[nodiscard]] std::string read() const {
        std::ifstream input(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(input), {});
    }
};

[[nodiscard]] WorkflowRecoverySnapshot snapshot(std::string output) {
    WorkflowRecoverySnapshot value{.workflow = WorkflowId{2}, .checkpoint = CheckpointId{7}};
    value.completed_nodes.push_back(RecoveredNodeState{
        .node = WorkflowNodeId{0}, .agent = AgentId{3}, .output = make_string(std::move(output))});
    value.completed_nodes.push_back(
        RecoveredNodeState{.node = WorkflowNodeId{1}, .agent = AgentId{4}, .output = std::nullopt});
    return value;
}

[[nodiscard]] WorkflowRecoverySnapshot memo_snapshot(Value value, bool present = true) {
    WorkflowRecoverySnapshot result{.workflow = WorkflowId{2}, .checkpoint = CheckpointId{7}};
    SuspendedNodeState pending{.node = WorkflowNodeId{1},
                               .agent = AgentId{2},
                               .node_input = make_string("input"),
                               .pending_cap_id = 9,
                               .pending_ordinal = 1};
    pending.memo.push_back(
        CapabilityMemoEntry{.ordinal = 0,
                            .cap_id = 4,
                            .arg_hash = 123,
                            .result = InMemoryMemoResult{std::move(value), present},
                            .node = WorkflowNodeId{0}});
    result.suspended = std::move(pending);
    return result;
}

[[nodiscard]] std::string memo_document(std::string_view fields) {
    return std::string(
               R"({"format":"ahfl.workflow-recovery","workflow_id":2,"checkpoint_id":7,"completed_nodes":[],"suspended":{"node_id":1,"agent_id":2,"pending_cap_id":9,"pending_ordinal":1,"node_input":null,"memo":[{)") +
           std::string(fields) + "}]}}";
}

constexpr std::string_view kMemoFields =
    R"("ordinal":0,"cap_id":4,"arg_hash":"123","node_id":0,"result_wire_json":"5","result_present":true)";

void check_invalid(const StoreFixture &fixture, std::string_view text) {
    fixture.write(text);
    const auto loaded = fixture.store.load();
    REQUIRE_FALSE(loaded.has_value());
    CHECK(loaded.error() == WorkflowRecoveryError::InvalidSnapshot);
}
} // namespace

TEST_CASE("workflow recovery store round-trips strong IDs and values") {
    StoreFixture fixture;
    REQUIRE(fixture.store.save(snapshot("committed")));
    const auto loaded = fixture.store.load();
    REQUIRE(loaded);
    CHECK(loaded->workflow == WorkflowId{2});
    CHECK(loaded->checkpoint == CheckpointId{7});
    REQUIRE(loaded->completed_nodes.size() == 2);
    CHECK(loaded->completed_nodes[0].node == WorkflowNodeId{0});
    CHECK(loaded->completed_nodes[0].agent == AgentId{3});
    REQUIRE(loaded->completed_nodes[0].output);
    CHECK(value_to_json(*loaded->completed_nodes[0].output) == "\"committed\"");
    CHECK_FALSE(loaded->completed_nodes[1].output);
    auto parsed = ahfl::json::parse_json(fixture.read());
    REQUIRE(parsed);
    CHECK((*parsed)->get("format")->as_string() == kWorkflowRecoveryFormat);
    CHECK((*parsed)->get("suspended")->kind == ahfl::json::Kind::Null);
    CHECK((*parsed)->object_fields.size() == 5);
}

TEST_CASE("workflow recovery store preserves committed snapshot on atomic write failure") {
    StoreFixture fixture;
    CHECK(fixture.store.load().error() == WorkflowRecoveryError::Missing);
    REQUIRE(fixture.store.save(snapshot("old")));
    {
        std::ofstream partial(ahfl::support::atomic_temporary_path(fixture.path));
        partial << "{\"format\":\"partial";
    }
    const auto failed = fixture.store.save(
        snapshot("new"),
        ahfl::support::AtomicReplaceOptions{
            .before_commit = [](const std::filesystem::path &, const std::filesystem::path &) {
                return false;
            }});
    REQUIRE_FALSE(failed);
    CHECK(failed.error() == WorkflowRecoveryError::WriteFailed);
    const auto loaded = fixture.store.load();
    REQUIRE(loaded);
    CHECK(value_to_json(*loaded->completed_nodes[0].output) == "\"old\"");
}

TEST_CASE("workflow recovery requires exact fields format and end of input") {
    StoreFixture fixture;
    constexpr std::string_view valid =
        R"({"format":"ahfl.workflow-recovery","workflow_id":0,"checkpoint_id":0,"completed_nodes":[],"suspended":null})";
    fixture.write(valid);
    REQUIRE(fixture.store.load());
    const std::string cases[] = {
        R"({"schema":"ahfl.workflow-recovery","workflow_id":0,"checkpoint_id":0,"completed_nodes":[],"suspended":null})",
        R"({"format":"unknown","workflow_id":0,"checkpoint_id":0,"completed_nodes":[],"suspended":null})",
        R"({"format":"ahfl.workflow-recovery","workflow_id":0,"checkpoint_id":0,"completed_nodes":[]})",
        R"({"format":"ahfl.workflow-recovery","workflow_id":0,"checkpoint_id":0,"completed_nodes":[],"suspended":null,"extra":0})",
        R"({"format":"ahfl.workflow-recovery","format":"ahfl.workflow-recovery","workflow_id":0,"checkpoint_id":0,"completed_nodes":[],"suspended":null})",
        R"({"format":1,"workflow_id":0,"checkpoint_id":0,"completed_nodes":[],"suspended":null})",
        R"({"format":"ahfl.workflow-recovery","workflow_id":-1,"checkpoint_id":0,"completed_nodes":[],"suspended":null})",
        R"({"format":"ahfl.workflow-recovery","workflow_id":1.0,"checkpoint_id":0,"completed_nodes":[],"suspended":null})",
        std::string(valid) + " false",
    };
    for (const auto &text : cases) {
        CAPTURE(text);
        check_invalid(fixture, text);
    }
}

TEST_CASE("workflow recovery direct DOM output preserves numeric provenance") {
    StoreFixture fixture;
    const auto document = [](std::string_view output) {
        return std::string(
                   R"({"format":"ahfl.workflow-recovery","workflow_id":0,"checkpoint_id":0,"completed_nodes":[{"node_id":0,"agent_id":0,"output":)") +
               std::string(output) + "}],\"suspended\":null}";
    };
    for (const auto text :
         {"18446744073709551615", "99999999999999999999999", "-99999999999999999999999"}) {
        check_invalid(fixture, document(text));
    }
    fixture.write(document("1.0"));
    auto loaded = fixture.store.load();
    REQUIRE(loaded);
    CHECK(std::holds_alternative<FloatValue>(loaded->completed_nodes[0].output->node));
    fixture.write(document("42"));
    loaded = fixture.store.load();
    REQUIRE(loaded);
    CHECK(std::holds_alternative<IntValue>(loaded->completed_nodes[0].output->node));
}

TEST_CASE("workflow recovery stores only exact memo wire bytes and explicit presence") {
    StoreFixture fixture;
    const auto roundtrip = [&](Value value, std::string_view expected) {
        REQUIRE(fixture.store.save(memo_snapshot(std::move(value))));
        const auto loaded = fixture.store.load();
        REQUIRE(loaded);
        REQUIRE(loaded->suspended);
        CHECK(loaded->suspended->node == WorkflowNodeId{1});
        CHECK(value_to_json(*loaded->suspended->node_input) == "\"input\"");
        REQUIRE(loaded->suspended->memo.size() == 1);
        const auto &entry = loaded->suspended->memo[0];
        CHECK(entry.node == WorkflowNodeId{0});
        CHECK(entry.arg_hash == 123);
        REQUIRE(std::holds_alternative<StoredMemoResult>(entry.result));
        const auto &stored = std::get<StoredMemoResult>(entry.result);
        CHECK(stored.present);
        CHECK(stored.result_wire_json == expected);
        CHECK(fixture.read().find("\"result\":") == std::string::npos);
    };
    roundtrip(make_decimal("1.23"), "\"1.23\"");
    roundtrip(make_duration("5s"), "\"5s\"");
    roundtrip(make_float(1.0), "1.0");
    roundtrip(make_option_some(make_int(7)), "7");
    roundtrip(make_option_none(), "null");
    roundtrip(make_unit(), "null");
    std::vector<Value> items;
    items.push_back(make_int(1));
    items.push_back(make_int(2));
    roundtrip(make_set(std::move(items)), "[1,2]");
    std::vector<std::pair<Value, Value>> entries;
    entries.emplace_back(make_string("_timestamp"), make_string("ordinary"));
    entries.emplace_back(make_string("nested"), make_decimal("1.23"));
    roundtrip(make_map(std::move(entries)), R"({"_timestamp":"ordinary","nested":"1.23"})");
}

TEST_CASE("workflow recovery preserves stored spelling without a duplicate projection") {
    StoreFixture fixture;
    auto snap = memo_snapshot(make_int(5));
    const std::string wire = R"({ "_timestamp" : "ordinary", "amount" : 1.000e0 })";
    snap.suspended->memo[0].result = StoredMemoResult{wire, true};
    REQUIRE(fixture.store.save(snap));
    auto loaded = fixture.store.load();
    REQUIRE(loaded);
    CHECK(std::get<StoredMemoResult>(loaded->suspended->memo[0].result).result_wire_json == wire);
    REQUIRE(fixture.store.save(*loaded));
    loaded = fixture.store.load();
    REQUIRE(loaded);
    CHECK(std::get<StoredMemoResult>(loaded->suspended->memo[0].result).result_wire_json == wire);
}

TEST_CASE("workflow recovery keeps valueless success distinct from explicit Unit") {
    StoreFixture fixture;
    REQUIRE(fixture.store.save(memo_snapshot(make_none(), false)));
    auto loaded = fixture.store.load();
    REQUIRE(loaded);
    const auto &absent = std::get<StoredMemoResult>(loaded->suspended->memo[0].result);
    CHECK_FALSE(absent.present);
    CHECK(absent.result_wire_json == "null");
    REQUIRE(fixture.store.save(memo_snapshot(make_unit(), true)));
    loaded = fixture.store.load();
    REQUIRE(loaded);
    CHECK(std::get<StoredMemoResult>(loaded->suspended->memo[0].result).present);
    CHECK_FALSE(fixture.store.save(memo_snapshot(make_none(), true)));
    CHECK_FALSE(fixture.store.save(memo_snapshot(make_int(5), false)));
}

TEST_CASE("workflow recovery rejects malformed memo state before replacing durable snapshot") {
    StoreFixture fixture;
    REQUIRE(fixture.store.save(snapshot("old")));
    const auto saved = fixture.read();
    const StoredMemoResult invalid[] = {
        {"", true},
        {"{bad", true},
        {"1 trailing", true},
        {"5", false},
        {R"({"x":1,"x":2})", true},
    };
    for (const auto &state : invalid) {
        auto snap = memo_snapshot(make_int(5));
        snap.suspended->memo[0].result = state;
        CHECK_FALSE(memo_result_state_well_formed(snap.suspended->memo[0]));
        CHECK_FALSE(fixture.store.save(snap));
        CHECK(fixture.read() == saved);
    }
    auto invalid_node = memo_snapshot(make_int(5));
    invalid_node.suspended->memo[0].node = WorkflowNodeId{};
    CHECK_FALSE(fixture.store.save(invalid_node));
    CHECK(fixture.read() == saved);
}

TEST_CASE("workflow recovery rejects missing unknown duplicate or malformed memo fields") {
    StoreFixture fixture;
    fixture.write(memo_document(kMemoFields));
    REQUIRE(fixture.store.load());
    const std::string fields[] = {
        R"("ordinal":0,"cap_id":4,"arg_hash":"123","result_wire_json":"5","result_present":true)",
        R"("ordinal":0,"cap_id":4,"arg_hash":"123","node_id":0,"result":5)",
        std::string(kMemoFields) + ",\"result\":5",
        std::string(kMemoFields) + ",\"ordinal\":0",
        R"("ordinal":0,"cap_id":4,"arg_hash":"123x","node_id":0,"result_wire_json":"5","result_present":true)",
        R"("ordinal":0,"cap_id":4,"arg_hash":" 123","node_id":0,"result_wire_json":"5","result_present":true)",
        R"("ordinal":0,"cap_id":4,"arg_hash":"18446744073709551616","node_id":0,"result_wire_json":"5","result_present":true)",
        R"("ordinal":0,"cap_id":4,"arg_hash":"123","node_id":0,"result_wire_json":5,"result_present":true)",
        R"("ordinal":0,"cap_id":4,"arg_hash":"123","node_id":0,"result_wire_json":"5","result_present":"true")",
        R"("ordinal":0,"cap_id":4,"arg_hash":"123","node_id":0,"result_wire_json":"5","result_present":false)",
        R"("ordinal":0,"cap_id":4,"arg_hash":"123","node_id":0,"result_wire_json":"","result_present":true)",
    };
    for (const auto &text : fields) {
        CAPTURE(text);
        check_invalid(fixture, memo_document(text));
    }
}

TEST_CASE("workflow recovery validates coordinates by node and ordinal") {
    StoreFixture fixture;
    auto snap = memo_snapshot(make_int(5));
    snap.suspended->memo.push_back(
        CapabilityMemoEntry{.ordinal = 0,
                            .cap_id = 4,
                            .arg_hash = 456,
                            .result = InMemoryMemoResult{make_string("another node"), true},
                            .node = WorkflowNodeId{1}});
    REQUIRE(fixture.store.save(snap));
    auto loaded = fixture.store.load();
    REQUIRE(loaded);
    CHECK(loaded->suspended->memo.size() == 2);
    snap.suspended->memo[1].node = WorkflowNodeId{0};
    CHECK_FALSE(fixture.store.save(snap));
    snap.suspended->memo[1].node = WorkflowNodeId{1};
    snap.suspended->memo[1].ordinal = 1;
    CHECK_FALSE(fixture.store.save(snap));

    auto text = memo_document(kMemoFields);
    const auto array_end = text.find("}]}}");
    REQUIRE(array_end != std::string::npos);
    text.insert(array_end + 1, ",{ " + std::string(kMemoFields) + "}");
    check_invalid(fixture, text);
}
