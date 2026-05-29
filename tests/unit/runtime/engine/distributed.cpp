#include "runtime/engine/distributed.hpp"
#include <cstdio>

static int test_count = 0;
static int pass_count = 0;

static void check(bool condition, const char* name) {
    test_count++;
    if (condition) {
        pass_count++;
    } else {
        std::printf("FAIL: %s\n", name);
    }
}

int main() {
    // Test 1: Region management
    {
        ahfl::runtime::DistributedScheduler sched;
        ahfl::runtime::RegionConfig region;
        region.region_id = "us-east-1";
        region.nodes.push_back({"node-1", "grpc://host1:50051", "us-east-1", 1});
        region.nodes.push_back({"node-2", "grpc://host2:50051", "us-east-1", 0});
        sched.add_region(std::move(region));

        check(sched.region_count() == 1, "region added");
        check(sched.total_node_count() == 2, "nodes counted correctly");
    }

    // Test 2: State snapshot creation and serialization
    {
        ahfl::runtime::DistributedScheduler sched;
        auto snapshot = sched.create_snapshot("agent_1", "Processing",
            {{"key1", "val1"}, {"key2", "val2"}});

        check(snapshot.agent_id == "agent_1", "snapshot agent id");
        check(snapshot.current_state == "Processing", "snapshot state");
        check(snapshot.context_values.size() == 2, "snapshot context size");

        auto serialized = sched.serialize_snapshot(snapshot);
        check(!serialized.empty(), "serialization produces output");
    }

    // Test 3: Deserialize round-trip
    {
        ahfl::runtime::DistributedScheduler sched;
        auto original = sched.create_snapshot("agent_x", "Done", {{"result", "ok"}});
        auto serialized = sched.serialize_snapshot(original);
        auto restored = sched.deserialize_snapshot(serialized);

        check(restored.has_value(), "deserialization succeeds");
        check(restored->agent_id == "agent_x", "round-trip agent id");
        check(restored->current_state == "Done", "round-trip state");
        check(restored->context_values["result"] == "ok", "round-trip context");
    }

    // Test 4: Checkpoint stub
    {
        ahfl::runtime::DistributedScheduler sched;
        auto snapshot = sched.create_snapshot("agent_2", "Init", {});
        auto checkpoint = sched.checkpoint(snapshot);

        check(checkpoint.status == ahfl::runtime::CheckpointStatus::Created, "checkpoint created");
        check(!checkpoint.checkpoint_id.empty(), "checkpoint has id");
    }

    std::printf("%d/%d tests passed\n", pass_count, test_count);
    return (pass_count == test_count) ? 0 : 1;
}
