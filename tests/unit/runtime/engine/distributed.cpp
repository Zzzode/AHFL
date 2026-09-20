// KR7.6 — Distributed scheduler soundness: content-addressed deterministic
// checkpoint ids, exact-match restore, strict snapshot parsing, fsync durability.
//
// Hand-rolled check()/main() matching its siblings (parallel_scheduler.cpp,
// sandbox.cpp). Every test that touches the filesystem points AHFL_CHECKPOINT_DIR
// at a UNIQUE per-test temp directory, so nothing is shared and stale state never
// leaks between cases. Coverage:
//   * content-addressed ids: a pure function of (agent_id, bytes) — equal content
//     serializes byte-identically regardless of context insertion order and yields
//     one id; different content in the same millisecond no longer overwrites;
//   * exact-id restore: a prefix id is rejected (never resolves to a longer id),
//     and list_checkpoints() returns the agent's ids in deterministic order;
//   * strict parse: unknown/missing/wrong-typed fields, non-string context
//     members, a missing V1 header, a swallowed timestamp, and trailing garbage
//     are all rejected — nothing is silently dropped or defaulted;
//   * durability: the durable atomic replace fsyncs before rename (and the parent
//     after), so a committed checkpoint is on disk, not just in the page cache.

#include "runtime/engine/distributed.hpp"

#include "base/support/atomic_file.hpp"
#include "base/support/sha256.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {

namespace fs = std::filesystem;

int g_tests = 0;
int g_failures = 0;

void check(bool condition, const char *name) {
    ++g_tests;
    if (!condition) {
        ++g_failures;
        std::printf("FAIL: %s\n", name);
    }
}

// A unique temp directory per test; removed by the caller.
[[nodiscard]] fs::path fresh_dir(std::string_view tag) {
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto dir = fs::temp_directory_path() /
                     ("ahfl-distributed-" + std::string(tag) + "-" + std::to_string(suffix));
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    return dir;
}

// scoped AHFL_CHECKPOINT_DIR; restores the previous value on destruction.
class ScopedCheckpointDir {
  public:
    explicit ScopedCheckpointDir(const fs::path &dir) {
        const char *previous = std::getenv("AHFL_CHECKPOINT_DIR");
        if (previous != nullptr) {
            previous_ = previous;
        }
        ::setenv("AHFL_CHECKPOINT_DIR", dir.c_str(), 1);
    }
    ~ScopedCheckpointDir() {
        if (previous_.has_value()) {
            ::setenv("AHFL_CHECKPOINT_DIR", previous_->c_str(), 1);
        } else {
            ::unsetenv("AHFL_CHECKPOINT_DIR");
        }
    }
    ScopedCheckpointDir(const ScopedCheckpointDir &) = delete;
    ScopedCheckpointDir &operator=(const ScopedCheckpointDir &) = delete;

  private:
    std::optional<std::string> previous_;
};

[[nodiscard]] std::string read_bytes(const fs::path &path) {
    std::ifstream input(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
}

// Write raw bytes to `path`, ignoring the artifact content (used to plant foreign
// or corrupt snapshots under a chosen id).
void write_bytes(const fs::path &path, std::string_view bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

using ahfl::runtime::DistributedScheduler;
using ahfl::runtime::StateSnapshot;

// A fixed timestamp so a snapshot's bytes are a pure function of its content.
[[nodiscard]] StateSnapshot fixed_snapshot(std::string agent_id,
                                           std::string state,
                                           std::unordered_map<std::string, std::string> context) {
    StateSnapshot snapshot;
    snapshot.agent_id = std::move(agent_id);
    snapshot.current_state = std::move(state);
    snapshot.context_values = std::move(context);
    snapshot.timestamp =
        std::chrono::system_clock::time_point(std::chrono::milliseconds(1'700'000'000'123));
    return snapshot;
}

// ---------------------------------------------------------------------------
// 1. Content-addressed deterministic ids
// ---------------------------------------------------------------------------

void test_content_addressed_ids_are_deterministic() {
    DistributedScheduler sched;

    // Serialization ignores unordered_map iteration order: two maps with the same
    // pairs inserted in opposite order must serialize byte-identically.
    std::unordered_map<std::string, std::string> forward;
    forward["a"] = "1";
    forward["b"] = "2";
    forward["c"] = "3";
    std::unordered_map<std::string, std::string> reverse;
    reverse["c"] = "3";
    reverse["b"] = "2";
    reverse["a"] = "1";

    const std::string first = sched.serialize_snapshot(fixed_snapshot("agent", "S", forward));
    const std::string second = sched.serialize_snapshot(fixed_snapshot("agent", "S", reverse));
    check(first == second, "id.context_order_independent_bytes");

    const std::string id1 = ahfl::runtime::content_addressed_checkpoint_id("agent", first);
    const std::string id2 = ahfl::runtime::content_addressed_checkpoint_id("agent", second);
    check(id1 == id2, "id.equal_content_equal_id");

    // The id is <agent>_<16 lowercase hex>, nothing else.
    const std::string expected_prefix = "agent_";
    check(id1.starts_with(expected_prefix), "id.agent_prefix");
    check(id1.size() == expected_prefix.size() + 16, "id.digest_length");
    const std::string digest = ahfl::support::sha256_hex(first).substr(0, 16);
    check(id1 == expected_prefix + digest, "id.matches_sha256_prefix");
    check(ahfl::runtime::is_checkpoint_id_for(id1, "agent"), "id.shape_recognized");
    check(!ahfl::runtime::is_checkpoint_id_for(id1, "agen"), "id.rejects_shorter_agent");
    check(!ahfl::runtime::is_checkpoint_id_for("agent_" + digest.substr(1) + "X", "agent"),
          "id.rejects_non_hex_digest");

    // Different content yields a different id (no wall-clock dependence).
    const std::string other = sched.serialize_snapshot(fixed_snapshot("agent", "T", forward));
    check(other != first, "id.content_change_changes_bytes");
    check(ahfl::runtime::content_addressed_checkpoint_id("agent", other) != id1,
          "id.content_change_changes_id");
}

void test_checkpoint_id_stable_across_schedulers() {
    // Two independent scheduler instances (as two processes would be) derive the
    // same id for the same snapshot — the property the old wall-clock scheme broke.
    DistributedScheduler a;
    DistributedScheduler b;
    const auto snapshot = fixed_snapshot("replay_agent", "Waiting", {{"k", "v"}});
    check(ahfl::runtime::content_addressed_checkpoint_id(snapshot.agent_id,
                                                         a.serialize_snapshot(snapshot)) ==
              ahfl::runtime::content_addressed_checkpoint_id(snapshot.agent_id,
                                                             b.serialize_snapshot(snapshot)),
          "id.stable_across_instances");
}

void test_distinct_snapshots_do_not_overwrite() {
    const auto dir = fresh_dir("no-overwrite");
    ScopedCheckpointDir scoped(dir);
    DistributedScheduler sched;

    // Two different states, checkpointed back to back — under the old ms-stamped
    // id these could collide and the second would silently overwrite the first.
    const auto first = sched.checkpoint(fixed_snapshot("agent", "StateA", {}));
    const auto second = sched.checkpoint(fixed_snapshot("agent", "StateB", {}));
    check(first.status == ahfl::runtime::CheckpointStatus::Created, "overwrite.first_created");
    check(second.status == ahfl::runtime::CheckpointStatus::Created, "overwrite.second_created");
    check(first.checkpoint_id != second.checkpoint_id, "overwrite.distinct_ids");

    // Both survive, and each restores its own state.
    const auto restored_a = sched.restore(first.checkpoint_id);
    const auto restored_b = sched.restore(second.checkpoint_id);
    check(restored_a.has_value() && restored_a->current_state == "StateA",
          "overwrite.first_restores_state_a");
    check(restored_b.has_value() && restored_b->current_state == "StateB",
          "overwrite.second_restores_state_b");

    // Idempotence: re-checkpointing the SAME content is a no-op, not a second file.
    const auto again = sched.checkpoint(fixed_snapshot("agent", "StateA", {}));
    check(again.checkpoint_id == first.checkpoint_id, "overwrite.same_content_same_id");
    check(again.status == ahfl::runtime::CheckpointStatus::Created, "overwrite.idempotent_created");
    check(sched.list_checkpoints("agent").size() == 2, "overwrite.two_files_only");

    fs::remove_all(dir);
}

// ---------------------------------------------------------------------------
// 2. Exact-match restore
// ---------------------------------------------------------------------------

void test_restore_requires_exact_id() {
    const auto dir = fresh_dir("exact-restore");
    ScopedCheckpointDir scoped(dir);
    DistributedScheduler sched;

    // An agent whose id is a PREFIX of another: "a" is a starts_with prefix of
    // "ab_<digest>". Under the old prefix scan, restoring "a" could return ab's
    // snapshot. Exact-id restore must reject "a" outright.
    const auto ab = sched.checkpoint(fixed_snapshot("ab", "AbState", {}));
    check(ab.status == ahfl::runtime::CheckpointStatus::Created, "exact.ab_created");

    // "a" is itself a valid agent id but has no checkpoint of its own.
    const auto restore_a = sched.restore("a");
    check(!restore_a.has_value(), "exact.prefix_not_resolved");
    check(restore_a.error() == ahfl::runtime::CheckpointRestoreError::InvalidId,
          "exact.bare_prefix_is_invalid_id");

    // A full-length prefix (all-but-one digest char) is still not an exact id.
    const std::string truncated = ab.checkpoint_id.substr(0, ab.checkpoint_id.size() - 1);
    check(!sched.restore(truncated).has_value(), "exact.truncated_digest_rejected");

    // Path traversal / unsafe ids are rejected before any filesystem access.
    check(sched.restore("../../etc/passwd").error() ==
              ahfl::runtime::CheckpointRestoreError::InvalidId,
          "exact.traversal_rejected");
    check(sched.restore("ab_0000000000000000").error() ==
              ahfl::runtime::CheckpointRestoreError::NotFound,
          "exact.wellformed_but_absent_not_found");

    // Exactly the right id works.
    const auto ok = sched.restore(ab.checkpoint_id);
    check(ok.has_value() && ok->current_state == "AbState", "exact.exact_id_restores");

    fs::remove_all(dir);
}

void test_list_checkpoints_is_deterministic_and_scoped() {
    const auto dir = fresh_dir("list");
    ScopedCheckpointDir scoped(dir);
    DistributedScheduler sched;

    // Interleave two agents and several states.
    for (const char *state : {"S1", "S2", "S3"}) {
        check(sched.checkpoint(fixed_snapshot("scout", state, {})).status ==
                  ahfl::runtime::CheckpointStatus::Created,
              "list.scout_checkpoints_created");
    }
    (void)sched.checkpoint(fixed_snapshot("scout2", "Other", {}));

    auto listed = sched.list_checkpoints("scout");
    check(listed.size() == 3, "list.scoped_count");
    check(std::is_sorted(listed.begin(), listed.end()), "list.sorted_deterministic");
    for (const auto &id : listed) {
        check(ahfl::runtime::is_checkpoint_id_for(id, "scout"), "list.scoped_to_agent");
    }
    // Calling twice returns the same order (filesystem iteration order is not
    // exposed).
    check(sched.list_checkpoints("scout") == listed, "list.stable_across_calls");
    // The other agent's checkpoint is never listed under "scout".
    check(sched.list_checkpoints("scout").size() == 3, "list.other_agent_excluded");

    // An invalid agent id lists nothing rather than scanning the directory.
    check(sched.list_checkpoints("../x").empty(), "list.invalid_agent_empty");

    fs::remove_all(dir);
}

void test_path_component_validation() {
    // Safe: the ASCII set the id grammar allows.
    check(ahfl::runtime::is_safe_path_component("agent_1"), "path.accept_plain");
    check(ahfl::runtime::is_safe_path_component("A-b.c_9"), "path.accept_dot_dash");
    // Unsafe: empty, over-length, dot names, separators, and any other byte.
    check(!ahfl::runtime::is_safe_path_component(""), "path.reject_empty");
    check(!ahfl::runtime::is_safe_path_component("."), "path.reject_dot");
    check(!ahfl::runtime::is_safe_path_component(".."), "path.reject_dotdot");
    check(!ahfl::runtime::is_safe_path_component("../x"), "path.reject_traversal");
    check(!ahfl::runtime::is_safe_path_component("a/b"), "path.reject_slash");
    check(!ahfl::runtime::is_safe_path_component("a b"), "path.reject_space");
    check(!ahfl::runtime::is_safe_path_component("a\nb"), "path.reject_backslash");
    check(!ahfl::runtime::is_safe_path_component(std::string(129, 'a')), "path.reject_too_long");
    check(ahfl::runtime::is_safe_path_component(std::string(128, 'a')), "path.accept_max_len");
}

void test_describe_is_actionable() {
    using ahfl::runtime::describe;
    // Every enumerator maps to a non-empty, distinct, human-readable string (the
    // diagnostics contract: no bare "" and no two errors collapsed into one).
    const ahfl::runtime::SnapshotParseError parse_errors[] = {
        ahfl::runtime::SnapshotParseError::Empty,
        ahfl::runtime::SnapshotParseError::MalformedJson,
        ahfl::runtime::SnapshotParseError::NotAnObject,
        ahfl::runtime::SnapshotParseError::UnknownFormat,
        ahfl::runtime::SnapshotParseError::UnknownField,
        ahfl::runtime::SnapshotParseError::MissingField,
        ahfl::runtime::SnapshotParseError::WrongFieldType,
        ahfl::runtime::SnapshotParseError::InvalidAgentId,
        ahfl::runtime::SnapshotParseError::InvalidTimestamp,
        ahfl::runtime::SnapshotParseError::MalformedTextLine,
    };
    std::vector<std::string_view> texts;
    for (const auto error : parse_errors) {
        const std::string_view text = describe(error);
        check(!text.empty(), "describe.parse_non_empty");
        check(std::find(texts.begin(), texts.end(), text) == texts.end(),
              "describe.parse_distinct");
        texts.push_back(text);
    }
    const ahfl::runtime::CheckpointRestoreError restore_errors[] = {
        ahfl::runtime::CheckpointRestoreError::InvalidId,
        ahfl::runtime::CheckpointRestoreError::NotFound,
        ahfl::runtime::CheckpointRestoreError::ReadFailed,
        ahfl::runtime::CheckpointRestoreError::MalformedSnapshot,
        ahfl::runtime::CheckpointRestoreError::WrongContent,
        ahfl::runtime::CheckpointRestoreError::RemoteMalformedSnapshot,
    };
    std::vector<std::string_view> restore_texts;
    for (const auto error : restore_errors) {
        const std::string_view text = describe(error);
        check(!text.empty(), "describe.restore_non_empty");
        check(std::find(restore_texts.begin(), restore_texts.end(), text) == restore_texts.end(),
              "describe.restore_distinct");
        restore_texts.push_back(text);
    }
}

void test_checkpoint_rejects_invalid_agent_id() {
    const auto dir = fresh_dir("bad-agent");
    ScopedCheckpointDir scoped(dir);
    DistributedScheduler sched;

    const auto bad = sched.checkpoint(fixed_snapshot("../escape", "S", {}));
    check(bad.status == ahfl::runtime::CheckpointStatus::Failed, "agent.invalid_failed");
    check(!bad.error.empty(), "agent.invalid_has_error");

    const auto slash = sched.checkpoint(fixed_snapshot("a/b", "S", {}));
    check(slash.status == ahfl::runtime::CheckpointStatus::Failed, "agent.slash_failed");

    // Nothing was written.
    std::error_code ec;
    check(fs::is_empty(dir, ec), "agent.nothing_written");

    fs::remove_all(dir);
}

// ---------------------------------------------------------------------------
// 3. Strict snapshot parsing
// ---------------------------------------------------------------------------

void test_v2_round_trip_is_byte_identical() {
    DistributedScheduler sched;
    const auto original = fixed_snapshot("agent", "Processing", {{"k1", "v1"}, {"k2", "v2"}});
    const std::string serialized = sched.serialize_snapshot(original);

    const auto restored = sched.deserialize_snapshot(serialized);
    check(restored.has_value(), "v2.round_trip_ok");
    check(restored->agent_id == "agent", "v2.round_trip_agent");
    check(restored->current_state == "Processing", "v2.round_trip_state");
    check(restored->context_values.size() == 2, "v2.round_trip_context_size");
    check(restored->context_values.at("k1") == "v1", "v2.round_trip_context_value");
    check(restored->timestamp == original.timestamp, "v2.round_trip_timestamp");
    // Re-serializing the restored snapshot is byte-identical (deterministic).
    check(sched.serialize_snapshot(*restored) == serialized, "v2.re_serialize_identical");
}

void test_v2_rejects_malformed() {
    DistributedScheduler sched;

    const std::string good = sched.serialize_snapshot(fixed_snapshot("a", "S", {{"k", "v"}}));

    const auto reject =
        [&](std::string_view data, ahfl::runtime::SnapshotParseError expected, const char *name) {
            const auto parsed = sched.deserialize_snapshot(data);
            check(!parsed.has_value(), name);
            if (!parsed.has_value()) {
                check(parsed.error() == expected, name);
            }
        };

    // Empty / not JSON / not an object.
    reject("", ahfl::runtime::SnapshotParseError::Empty, "v2.reject_empty");
    // Neither a V1 header nor JSON-object shaped -> no recognized format.
    reject("not json at all",
           ahfl::runtime::SnapshotParseError::UnknownFormat,
           "v2.reject_no_recognized_format");
    // Object-shaped but syntactically broken JSON.
    reject(
        "{not json}", ahfl::runtime::SnapshotParseError::MalformedJson, "v2.reject_malformed_json");
    reject("[1,2,3]", ahfl::runtime::SnapshotParseError::NotAnObject, "v2.reject_not_object");

    // Trailing garbage after a complete object.
    reject(good + " trailing",
           ahfl::runtime::SnapshotParseError::MalformedJson,
           "v2.reject_trailing_garbage");

    // Unknown field (closed schema).
    reject(R"({"format":"AHFL_SNAPSHOT_V2","agent_id":"a","current_state":"S",)"
           R"("timestamp_ms":1,"extra":true})",
           ahfl::runtime::SnapshotParseError::UnknownField,
           "v2.reject_unknown_field");

    // Missing required field.
    reject(R"({"agent_id":"a","current_state":"S","timestamp_ms":1})",
           ahfl::runtime::SnapshotParseError::MissingField,
           "v2.reject_missing_format");
    reject(R"({"format":"AHFL_SNAPSHOT_V2","current_state":"S","timestamp_ms":1})",
           ahfl::runtime::SnapshotParseError::MissingField,
           "v2.reject_missing_agent");
    reject(R"({"format":"AHFL_SNAPSHOT_V2","agent_id":"a","timestamp_ms":1})",
           ahfl::runtime::SnapshotParseError::MissingField,
           "v2.reject_missing_state");
    reject(R"({"format":"AHFL_SNAPSHOT_V2","agent_id":"a","current_state":"S"})",
           ahfl::runtime::SnapshotParseError::MissingField,
           "v2.reject_missing_timestamp");

    // Wrong field type.
    reject(R"({"format":"AHFL_SNAPSHOT_V2","agent_id":7,"current_state":"S","timestamp_ms":1})",
           ahfl::runtime::SnapshotParseError::WrongFieldType,
           "v2.reject_agent_not_string");
    reject(R"({"format":"AHFL_SNAPSHOT_V2","agent_id":"a","current_state":true,)"
           R"("timestamp_ms":1})",
           ahfl::runtime::SnapshotParseError::WrongFieldType,
           "v2.reject_state_not_string");
    reject(R"({"format":"AHFL_SNAPSHOT_V2","agent_id":"a","current_state":"S",)"
           R"("timestamp_ms":"1"})",
           ahfl::runtime::SnapshotParseError::InvalidTimestamp,
           "v2.reject_timestamp_not_int");
    reject(R"({"format":"AHFL_SNAPSHOT_V2","agent_id":"a","current_state":"S",)"
           R"("timestamp_ms":-5})",
           ahfl::runtime::SnapshotParseError::InvalidTimestamp,
           "v2.reject_negative_timestamp");

    // Non-string context member is rejected, NOT silently dropped.
    reject(R"({"format":"AHFL_SNAPSHOT_V2","agent_id":"a","current_state":"S",)"
           R"("timestamp_ms":1,"context":{"good":"v","bad":7}})",
           ahfl::runtime::SnapshotParseError::WrongFieldType,
           "v2.reject_non_string_context");
    reject(R"({"format":"AHFL_SNAPSHOT_V2","agent_id":"a","current_state":"S",)"
           R"("timestamp_ms":1,"context":[1,2]})",
           ahfl::runtime::SnapshotParseError::WrongFieldType,
           "v2.reject_context_not_object");

    // Unknown format value.
    reject(R"({"format":"AHFL_SNAPSHOT_V3","agent_id":"a","current_state":"S","timestamp_ms":1})",
           ahfl::runtime::SnapshotParseError::UnknownFormat,
           "v2.reject_unknown_format");

    // Unsafe agent id inside the payload.
    reject(R"({"format":"AHFL_SNAPSHOT_V2","agent_id":"../x","current_state":"S",)"
           R"("timestamp_ms":1})",
           ahfl::runtime::SnapshotParseError::InvalidAgentId,
           "v2.reject_unsafe_agent_id");
}

void test_v1_strict_parsing() {
    DistributedScheduler sched;

    // A well-formed V1 snapshot parses.
    const std::string good =
        "AHFL_SNAPSHOT_V1\nagent_id=legacy\ncurrent_state=Done\ntimestamp=1700000000123\n"
        "ctx.result=ok\n";
    const auto restored = sched.deserialize_snapshot(good);
    check(restored.has_value(), "v1.accept_wellformed");
    check(restored.has_value() && restored->agent_id == "legacy", "v1.agent");
    check(restored.has_value() && restored->context_values.at("result") == "ok", "v1.context");

    const auto reject =
        [&](std::string_view data, ahfl::runtime::SnapshotParseError expected, const char *name) {
            const auto parsed = sched.deserialize_snapshot(data);
            check(!parsed.has_value() && parsed.error() == expected, name);
        };

    // Missing header (the old reader tolerated this by failing only on the first
    // line mismatch — but a body without any header used to slip through as a
    // parse that just found no fields).
    reject("agent_id=legacy\ncurrent_state=Done\ntimestamp=1\n",
           ahfl::runtime::SnapshotParseError::UnknownFormat,
           "v1.reject_missing_header");
    // Header present but a later line is not key=value.
    reject("AHFL_SNAPSHOT_V1\nagent_id=legacy\ncurrent_state=Done\ntimestamp=1\ngarbage\n",
           ahfl::runtime::SnapshotParseError::MalformedTextLine,
           "v1.reject_malformed_line");
    // Trailing garbage after a valid timestamp (the old std::stoll swallowed it).
    reject("AHFL_SNAPSHOT_V1\nagent_id=legacy\ncurrent_state=Done\ntimestamp=12x\n",
           ahfl::runtime::SnapshotParseError::InvalidTimestamp,
           "v1.reject_timestamp_garbage");
    // Non-numeric / negative timestamp.
    reject("AHFL_SNAPSHOT_V1\nagent_id=legacy\ncurrent_state=Done\ntimestamp=abc\n",
           ahfl::runtime::SnapshotParseError::InvalidTimestamp,
           "v1.reject_timestamp_nan");
    reject("AHFL_SNAPSHOT_V1\nagent_id=legacy\ncurrent_state=Done\ntimestamp=-9\n",
           ahfl::runtime::SnapshotParseError::InvalidTimestamp,
           "v1.reject_timestamp_negative");
    // Unknown key.
    reject("AHFL_SNAPSHOT_V1\nagent_id=legacy\ncurrent_state=Done\ntimestamp=1\nbogus=x\n",
           ahfl::runtime::SnapshotParseError::MalformedTextLine,
           "v1.reject_unknown_key");
    // Duplicate key.
    reject("AHFL_SNAPSHOT_V1\nagent_id=legacy\nagent_id=other\ncurrent_state=Done\n"
           "timestamp=1\n",
           ahfl::runtime::SnapshotParseError::MalformedTextLine,
           "v1.reject_duplicate_key");
    // Missing required fields.
    reject("AHFL_SNAPSHOT_V1\nagent_id=legacy\ncurrent_state=Done\n",
           ahfl::runtime::SnapshotParseError::MissingField,
           "v1.reject_missing_timestamp");
    reject("AHFL_SNAPSHOT_V1\ncurrent_state=Done\ntimestamp=1\n",
           ahfl::runtime::SnapshotParseError::MissingField,
           "v1.reject_missing_agent");
    reject("AHFL_SNAPSHOT_V1\nagent_id=legacy\ntimestamp=1\n",
           ahfl::runtime::SnapshotParseError::MissingField,
           "v1.reject_missing_state");
    // Unsafe agent id.
    reject("AHFL_SNAPSHOT_V1\nagent_id=../x\ncurrent_state=Done\ntimestamp=1\n",
           ahfl::runtime::SnapshotParseError::InvalidAgentId,
           "v1.reject_unsafe_agent");

    // CRLF line endings are tolerated (checked-out files), and only that.
    const auto crlf = sched.deserialize_snapshot(
        "AHFL_SNAPSHOT_V1\r\nagent_id=legacy\r\ncurrent_state=Done\r\ntimestamp=1\r\n");
    check(crlf.has_value(), "v1.accept_crlf");
}

void test_restore_reports_malformed_file() {
    const auto dir = fresh_dir("malformed-file");
    ScopedCheckpointDir scoped(dir);
    DistributedScheduler sched;

    // Write a well-formed id but corrupt bytes under it.
    const std::string id = "agent_0123456789abcdef";
    {
        std::ofstream out(dir / (id + ".snapshot"), std::ios::binary);
        out << R"({"format":"AHFL_SNAPSHOT_V2","agent_id":"agent",)";
    }
    const auto restored = sched.restore(id);
    check(!restored.has_value() &&
              restored.error() == ahfl::runtime::CheckpointRestoreError::MalformedSnapshot,
          "file.malformed_reported");

    fs::remove_all(dir);
}

// The three defects the KR7.6 fix-forward closes: a non-regular file at an id
// must not abort the process, a foreign-id file must not be restored/listed as
// this agent's, and a pre-epoch timestamp must round-trip (or be refused).
void test_restore_non_regular_file_is_not_thrown() {
#if defined(__unix__) || defined(__APPLE__)
    const auto dir = fresh_dir("non-regular");
    ScopedCheckpointDir scoped(dir);
    DistributedScheduler sched;

    // Repro A: a DIRECTORY under a well-formed `<id>.snapshot`. The old
    // istreambuf_iterator read threw std::__ios_failure out of restore().
    {
        const std::string id = "agent_0123456789abcdef";
        std::error_code ec;
        fs::create_directory(dir / (id + ".snapshot"), ec);
        check(!ec, "nonregular.dir_created");
        const auto restored = sched.restore(id);
        check(!restored.has_value(), "nonregular.directory_not_restored");
        check(!restored.has_value() &&
                  restored.error() == ahfl::runtime::CheckpointRestoreError::ReadFailed,
              "nonregular.directory_read_failed");
    }

    // Repro C: a SYMLINK to a directory under an id. O_NOFOLLOW refuses to follow
    // it, so it too is ReadFailed (never a followed read, never a throw).
    {
        const std::string id = "agent_fedcba9876543210";
        std::error_code ec;
        fs::create_directory(dir / "target_dir", ec);
        check(!ec, "nonregular.symlink_target_created");
        fs::create_symlink(dir / "target_dir", dir / (id + ".snapshot"), ec);
        check(!ec, "nonregular.symlink_created");
        const auto restored = sched.restore(id);
        check(!restored.has_value(), "nonregular.symlink_not_restored");
        check(!restored.has_value() &&
                  restored.error() == ahfl::runtime::CheckpointRestoreError::ReadFailed,
              "nonregular.symlink_read_failed");
    }

    // Repro B: checkpoint() of a snapshot whose content-addressed id names an
    // existing directory must fail closed, not abort. The id is derived from the
    // bytes, so re-derive it here and plant the directory first.
    {
        const auto snapshot = fixed_snapshot("agent", "DirCollision", {{"k", "v"}});
        const std::string serialized = sched.serialize_snapshot(snapshot);
        const std::string id =
            ahfl::runtime::content_addressed_checkpoint_id(snapshot.agent_id, serialized);
        std::error_code ec;
        fs::create_directory(dir / (id + ".snapshot"), ec);
        check(!ec, "nonregular.checkpoint_dir_planted");
        const auto result = sched.checkpoint(snapshot);
        check(result.status == ahfl::runtime::CheckpointStatus::Failed,
              "nonregular.checkpoint_failed");
        check(!result.error.empty(), "nonregular.checkpoint_has_error");
    }

    fs::remove_all(dir);
#endif
}

void test_restore_binds_id_to_content() {
    const auto dir = fresh_dir("content-binding");
    ScopedCheckpointDir scoped(dir);
    DistributedScheduler sched;

    // bob checkpoints; the bytes are copied to a FOREIGN well-formed id.
    const auto bob = sched.checkpoint(fixed_snapshot("bob", "BOB_STATE", {{"k", "v"}}));
    check(bob.status == ahfl::runtime::CheckpointStatus::Created, "bind.bob_created");
    const std::string foreign_id = "alice_0123456789abcdef";
    write_bytes(dir / (foreign_id + ".snapshot"), read_bytes(dir / (bob.checkpoint_id + ".snapshot")));

    // The digest half of the id is not decoration: the foreign id must not resolve
    // to bob's bytes.
    const auto stolen = sched.restore(foreign_id);
    check(!stolen.has_value(), "bind.foreign_id_not_restored");
    check(!stolen.has_value() &&
              stolen.error() == ahfl::runtime::CheckpointRestoreError::WrongContent,
          "bind.foreign_id_wrong_content");

    // Listing is scoped by the PAYLOAD's agent, not merely the filename prefix.
    check(sched.list_checkpoints("alice").empty(), "bind.foreign_not_listed_for_alice");
    check(sched.list_checkpoints("bob").size() == 1, "bind.bob_still_listed");

    // A same-id file whose bytes were tampered with is a checksum mismatch.
    const auto carol = sched.checkpoint(fixed_snapshot("carol", "CAROL_STATE", {}));
    check(carol.status == ahfl::runtime::CheckpointStatus::Created, "bind.carol_created");
    write_bytes(dir / (carol.checkpoint_id + ".snapshot"),
                R"({"format":"AHFL_SNAPSHOT_V2","agent_id":"carol","current_state":"TAMPERED",)"
                R"("timestamp_ms":1700000000123,"context":{}})");
    const auto tampered = sched.restore(carol.checkpoint_id);
    check(!tampered.has_value(), "bind.tampered_not_restored");
    check(!tampered.has_value() &&
              tampered.error() == ahfl::runtime::CheckpointRestoreError::WrongContent,
          "bind.tampered_wrong_content");

    // The honest id still restores exactly.
    const auto honest = sched.restore(bob.checkpoint_id);
    check(honest.has_value() && honest->current_state == "BOB_STATE", "bind.honest_restores");

    fs::remove_all(dir);
}

void test_timestamp_write_read_reconciled() {
    const auto dir = fresh_dir("timestamp");
    ScopedCheckpointDir scoped(dir);
    DistributedScheduler sched;

    // A pre-epoch timestamp is out of the accepted range: checkpoint() must REFUSE
    // it rather than write a file its own strict reader rejects.
    StateSnapshot negative = fixed_snapshot("agent", "PreEpoch", {});
    negative.timestamp = std::chrono::system_clock::time_point(std::chrono::milliseconds(-1));
    const auto refused = sched.checkpoint(negative);
    check(refused.status == ahfl::runtime::CheckpointStatus::Failed, "timestamp.negative_refused");
    check(!refused.error.empty(), "timestamp.negative_has_error");

    // A representable snapshot round-trips exactly, including a non-zero (but
    // non-default) epoch.
    StateSnapshot good = fixed_snapshot("agent", "Epoch", {});
    good.timestamp = std::chrono::system_clock::time_point(std::chrono::milliseconds(0));
    const auto good_result = sched.checkpoint(good);
    check(good_result.status == ahfl::runtime::CheckpointStatus::Created, "timestamp.epoch_created");
    const auto restored = sched.restore(good_result.checkpoint_id);
    check(restored.has_value() && restored->timestamp == good.timestamp,
          "timestamp.epoch_round_trips");

    // The extreme accepted bound: representable (no UB) and stable.
    DistributedScheduler plain;
    const std::string at_max =
        R"({"format":"AHFL_SNAPSHOT_V2","agent_id":"a","current_state":"S","timestamp_ms":9223372036854})";
    const auto parsed_max = plain.deserialize_snapshot(at_max);
    check(parsed_max.has_value(), "timestamp.max_accepted");
    // One past it is rejected in both V2 and V1, so no conversion can overflow.
    const std::string past_max =
        R"({"format":"AHFL_SNAPSHOT_V2","agent_id":"a","current_state":"S","timestamp_ms":9223372036855})";
    check(plain.deserialize_snapshot(past_max).error() ==
              ahfl::runtime::SnapshotParseError::InvalidTimestamp,
          "timestamp.past_max_rejected");
    // INT64_MAX is the UB case the fix closes: previously accepted and overflowed.
    const std::string int64_max =
        R"({"format":"AHFL_SNAPSHOT_V2","agent_id":"a","current_state":"S","timestamp_ms":9223372036854775807})";
    check(plain.deserialize_snapshot(int64_max).error() ==
              ahfl::runtime::SnapshotParseError::InvalidTimestamp,
          "timestamp.int64_max_rejected");
    const std::string v1_int64_max =
        "AHFL_SNAPSHOT_V1\nagent_id=a\ncurrent_state=S\ntimestamp=9223372036854775807\n";
    check(plain.deserialize_snapshot(v1_int64_max).error() ==
              ahfl::runtime::SnapshotParseError::InvalidTimestamp,
          "timestamp.v1_int64_max_rejected");

    fs::remove_all(dir);
}

// A minimal forked HTTP server answering every GET with a fixed body. Used to
// exercise the remote path of restore() without a real region. POSIX-only.
#if defined(__unix__) || defined(__APPLE__)
struct RemoteServer {
    pid_t pid = -1;
    std::string endpoint;

    static std::optional<RemoteServer> start(std::string_view body) {
        const int listener = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listener < 0) {
            return std::nullopt;
        }
        const int one = 1;
        ::setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        if (::bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0) {
            ::close(listener);
            return std::nullopt;
        }
        socklen_t length = sizeof(address);
        if (::getsockname(listener, reinterpret_cast<sockaddr *>(&address), &length) != 0 ||
            ::listen(listener, 8) != 0) {
            ::close(listener);
            return std::nullopt;
        }
        const int port = ntohs(address.sin_port);
        const pid_t pid = ::fork();
        if (pid < 0) {
            ::close(listener);
            return std::nullopt;
        }
        if (pid == 0) {
            // Child: serve one request at a time until killed by the parent.
            while (true) {
                const int connection = ::accept(listener, nullptr, nullptr);
                if (connection < 0) {
                    ::_exit(0);
                }
                char request[1024];
                const ssize_t ignored = ::read(connection, request, sizeof(request));
                (void)ignored;
                const std::string response =
                    "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
                    std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + std::string(body);
                ssize_t written = 0;
                while (static_cast<std::size_t>(written) < response.size()) {
                    const ssize_t n =
                        ::write(connection, response.data() + written,
                                response.size() - static_cast<std::size_t>(written));
                    if (n <= 0) {
                        break;
                    }
                    written += n;
                }
                ::close(connection);
            }
            ::_exit(0);
        }
        ::close(listener);
        return RemoteServer{pid, "http://127.0.0.1:" + std::to_string(port)};
    }

    ~RemoteServer() {
        if (pid > 0) {
            ::kill(pid, SIGKILL);
            ::waitpid(pid, nullptr, 0);
        }
    }
    RemoteServer(const RemoteServer &) = delete;
    RemoteServer &operator=(const RemoteServer &) = delete;
    RemoteServer(RemoteServer &&other) noexcept : pid(other.pid), endpoint(std::move(other.endpoint)) {
        other.pid = -1;
    }
    RemoteServer &operator=(RemoteServer &&other) noexcept {
        if (this != &other) {
            if (pid > 0) {
                ::kill(pid, SIGKILL);
                ::waitpid(pid, nullptr, 0);
            }
            pid = other.pid;
            endpoint = std::move(other.endpoint);
            other.pid = -1;
        }
        return *this;
    }

  private:
    RemoteServer(pid_t p, std::string ep) : pid(p), endpoint(std::move(ep)) {}
};
#endif

void test_remote_malformed_without_local_file() {
#if defined(__unix__) || defined(__APPLE__)
    const auto dir = fresh_dir("remote-malformed");
    ScopedCheckpointDir scoped(dir);
    DistributedScheduler sched;

    // The region answers 200 with a body that is not a snapshot, and no local file
    // exists. The error must name the REMOTE failure, not point the operator at a
    // local file that does not exist.
    auto server = RemoteServer::start(R"({"format":"NOT_A_SNAPSHOT"})");
    if (!server.has_value()) {
        std::printf("SKIP: remote-malformed (could not start local server)\n");
        fs::remove_all(dir);
        return;
    }
    ahfl::runtime::RegionConfig region;
    region.region_id = "test";
    region.nodes.push_back(ahfl::runtime::RemoteNodeSpec{
        .node_id = "n1", .endpoint = server->endpoint, .region = "test", .priority = 1});
    sched.add_region(std::move(region));

    const auto restored = sched.restore("agent_0123456789abcdef");
    check(!restored.has_value(), "remote.malformed_not_restored");
    check(!restored.has_value() &&
              restored.error() == ahfl::runtime::CheckpointRestoreError::RemoteMalformedSnapshot,
          "remote.malformed_reported_as_remote");

    // Conversely, with no region configured and no local file, the error is the
    // accurate NotFound — never the "local bytes failed the parse" wording.
    DistributedScheduler local_only;
    check(local_only.restore("agent_0123456789abcdef").error() ==
              ahfl::runtime::CheckpointRestoreError::NotFound,
          "remote.no_region_not_found");

    fs::remove_all(dir);
#endif
}

// ---------------------------------------------------------------------------
// 4. fsync durability gate on the atomic replace
// ---------------------------------------------------------------------------

void test_durable_replace_survives_and_commits() {
    const auto dir = fresh_dir("durable");
    const auto target = dir / "checkpoint.snapshot";

    ahfl::support::AtomicReplaceOptions durable_options;
    durable_options.durable = true;

    const auto first = ahfl::support::atomic_replace_text(target, "payload-one", durable_options);
    check(first.has_value(), "durable.first_commit");
    check(read_bytes(target) == "payload-one", "durable.first_bytes");
    check(!fs::exists(ahfl::support::atomic_temporary_path(target)), "durable.no_temp_left");

    // A second durable replace overwrites atomically.
    const auto second = ahfl::support::atomic_replace_text(target, "payload-two", durable_options);
    check(second.has_value(), "durable.second_commit");
    check(read_bytes(target) == "payload-two", "durable.second_bytes");

    // A durable replace whose before_commit aborts leaves the old data intact and
    // reports the interruption (durability must not skip the commit gate).
    const auto interrupted = ahfl::support::atomic_replace_text(
        target,
        "never-committed",
        ahfl::support::AtomicReplaceOptions{
            .before_commit = [](const fs::path &, const fs::path &) { return false; },
            .durable = true});
    check(!interrupted.has_value(), "durable.interrupt_fails");
    check(interrupted.error() == ahfl::support::AtomicReplaceError::CommitInterrupted,
          "durable.interrupt_error");
    check(read_bytes(target) == "payload-two", "durable.interrupt_preserves_old");

    fs::remove_all(dir);
}

void test_checkpoint_file_is_durably_committed() {
    const auto dir = fresh_dir("durable-checkpoint");
    ScopedCheckpointDir scoped(dir);
    DistributedScheduler sched;

    const auto result = sched.checkpoint(fixed_snapshot("agent", "Durable", {{"k", "v"}}));
    check(result.status == ahfl::runtime::CheckpointStatus::Created, "durable.checkpoint_created");
    const auto path = dir / (result.checkpoint_id + ".snapshot");
    check(fs::exists(path), "durable.checkpoint_file_exists");
    // No leftover .tmp: the rename committed and the temp is gone.
    check(!fs::exists(ahfl::support::atomic_temporary_path(path)), "durable.checkpoint_no_temp");
    // The bytes on disk are exactly what the serializer produced.
    const auto snapshot = fixed_snapshot("agent", "Durable", {{"k", "v"}});
    check(read_bytes(path) == sched.serialize_snapshot(snapshot), "durable.checkpoint_bytes");

    fs::remove_all(dir);
}

} // namespace

int main() {
    test_content_addressed_ids_are_deterministic();
    test_checkpoint_id_stable_across_schedulers();
    test_distinct_snapshots_do_not_overwrite();
    test_restore_requires_exact_id();
    test_list_checkpoints_is_deterministic_and_scoped();
    test_path_component_validation();
    test_describe_is_actionable();
    test_checkpoint_rejects_invalid_agent_id();
    test_v2_round_trip_is_byte_identical();
    test_v2_rejects_malformed();
    test_v1_strict_parsing();
    test_restore_reports_malformed_file();
    test_restore_non_regular_file_is_not_thrown();
    test_restore_binds_id_to_content();
    test_timestamp_write_read_reconciled();
    test_remote_malformed_without_local_file();
    test_durable_replace_survives_and_commits();
    test_checkpoint_file_is_durably_committed();

    if (g_failures == 0) {
        std::printf("distributed: %d checks passed\n", g_tests);
        return 0;
    }
    std::printf("distributed: %d/%d checks failed\n", g_failures, g_tests);
    return 1;
}
