#include "runtime/engine/payload_store.hpp"

#include "base/support/sha256.hpp"
#include "runtime/engine/core_wasm_resume_record.hpp"
#include "runtime/engine/payload_store_codec.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <variant>
#include <vector>

#include <sys/stat.h> // mkfifo (POSIX; the test is Linux-gated like the store)

// RFC 0026 KR6.5 E4-B2-B1a permanent regression for the integrity-only durable-resume
// payload STORE core (publish / load / mark_consumed + observer). Hand-rolled
// check()/main(). Linux-only; on a non-Linux platform or a filesystem outside the
// durable allowlist the store's open() returns UnsupportedPlatform /
// UnsupportedFilesystem and this test SKIPs (ctest exit 77). Coverage:
//   * publish -> load round-trip; exact observer phase order (Available + Consumed);
//   * P0-T1 real concurrent CAS (one winner / one GenerationMismatch); reader-during-
//     publish sees old before PointerRenamed and new after (two blocking observers);
//   * P0-T2 three real orphans (pointer.tmp / gen-M.stage / complete-unreferenced
//     gen-M) rebuilt not adopted; unknown child and known-name symlink child both ->
//     CommitInterrupted and kept;
//   * P0-T3 FS gates: unsafe wf symlink / group-writable -> StateMismatch; pointer
//     FIFO / pointer hardlink / immutable hardlink rejected; st_dev submount SKIP;
//   * P0-T4 cross-artifact tamper via re-signed fixtures (record entry_id / gen /
//     guarantees, manifest record_len, slot payload_len, record digest break);
//     Consumed prior-admission: corrupt live-Available, corrupt previous manifest
//     after Consumed, previous namespace/state mismatch, consumed_generation != N-1;
//   * P0-T5 caps / sentinels with NO stage/pointer/gen advance on failure;
//   * P1-T6 CAS priority (stale expected + live Consumed -> GenerationMismatch),
//     publish-over-Consumed on a hit -> Consumed, absent expected>0 no dirs, absent
//     load/mark_consumed -> NotFound no dirs, wrong key/key_id, structural no-echo.
// NO production caller (first is the future B2-D host). B1b SIGKILL worker is separate.

namespace fs = std::filesystem;

namespace {

using namespace ahfl::runtime::payload_store;
using ahfl::ir::core::CoreCapabilityId;
using ahfl::ir::core::CoreWorkflowId;
using ahfl::ir::core::CoreWorkflowNodeId;
using ahfl::runtime::core_wasm_resume::CoreWasmResumeRecord;
using ahfl::runtime::core_wasm_resume::DigestHex;
using ahfl::runtime::core_wasm_resume::InvocationOrdinal;
using ahfl::runtime::core_wasm_resume::NodeKind;
using ahfl::runtime::core_wasm_resume::PayloadSlotId;
using ahfl::runtime::core_wasm_resume::ResumeMemoEntry;
using ahfl::runtime::core_wasm_resume::ResumeNode;
using ahfl::runtime::core_wasm_resume::ResumePendingEntry;

int g_failures = 0;

void check(bool ok, std::string_view name) {
    if (!ok) {
        ++g_failures;
        std::cerr << "FAIL: " << name << "\n";
    }
}

std::vector<std::uint8_t> test_key() { return std::vector<std::uint8_t>(32, 0x2b); }

std::array<std::uint8_t, 16> test_key_id() {
    std::array<std::uint8_t, 16> id{};
    for (std::size_t i = 0; i < id.size(); ++i) {
        id[i] = static_cast<std::uint8_t>(i + 1);
    }
    return id;
}

DigestHex hex_of(char fill) {
    DigestHex d{};
    d.fill(fill);
    return d;
}

CoreWasmResumeRecord make_record(CoreWorkflowId wf) {
    CoreWasmResumeRecord r;
    r.format_version = 1;
    r.guarantees = 0;
    r.module_sha256 = hex_of('a');
    r.wire_schema_sha256 = hex_of('b');
    r.exec_manifest_sha256 = hex_of('c');
    r.entry_id = wf;
    r.suspended_node_id = CoreWorkflowNodeId{41};
    r.resume_state = ahfl::runtime::core_wasm_resume::ResumeState::Suspended;

    ResumeNode n0;
    n0.workflow_node_id = CoreWorkflowNodeId{40};
    n0.schedule_pos = 0;
    n0.node_kind = NodeKind::Identity;

    ResumeNode n1;
    n1.workflow_node_id = CoreWorkflowNodeId{41};
    n1.schedule_pos = 1;
    n1.node_kind = NodeKind::Capability;
    ResumeMemoEntry m0;
    m0.invocation_ordinal = InvocationOrdinal{0};
    m0.capability = CoreCapabilityId{0};
    m0.source_symbol = 0;
    m0.arg_hash = 0x1122334455667788ULL;
    m0.result_slot = PayloadSlotId{5};
    n1.memo.push_back(m0);
    ResumePendingEntry p;
    p.invocation_ordinal = InvocationOrdinal{1};
    p.capability = CoreCapabilityId{3};
    p.source_symbol = 900;
    p.arg_hash = 0xdeadbeefULL;
    n1.pending = p;

    r.nodes.push_back(n0);
    r.nodes.push_back(n1);
    r.auth_header.alg_version = 1;
    r.auth_header.key_id = test_key_id();
    return r;
}

const std::vector<std::uint8_t> kSlot5Payload = {0xde, 0xad, 0xbe, 0xef};
std::vector<Slot> slots_5() { return {Slot{PayloadSlotId{5}, kSlot5Payload}}; }

void nuke(const fs::path &p) {
    std::error_code ec;
    fs::remove_all(p, ec);
}

std::string h8(std::uint32_t v) {
    char b[9];
    std::snprintf(b, sizeof(b), "%08x", v);
    return std::string(b, 8);
}
std::string h16(std::uint64_t v) {
    char b[17];
    std::snprintf(b, sizeof(b), "%016llx", static_cast<unsigned long long>(v));
    return std::string(b, 16);
}
fs::path ckpt_path(const fs::path &root, CoreWorkflowId wf, ResumeCheckpointId ckpt) {
    return root / ("wf-" + h8(wf.value)) / ("ckpt-" + h16(ckpt.value));
}
fs::path gen_path(const fs::path &root, CoreWorkflowId wf, ResumeCheckpointId ckpt,
                  std::uint64_t gen) {
    return ckpt_path(root, wf, ckpt) / ("gen-" + h16(gen));
}

[[nodiscard]] bool write_file(const fs::path &p, std::span<const std::uint8_t> bytes) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out.write(reinterpret_cast<const char *>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    out.flush();
    return static_cast<bool>(out);
}

std::vector<std::uint8_t> read_file(const fs::path &p) {
    std::ifstream in(p, std::ios::binary);
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in),
                                     std::istreambuf_iterator<char>());
}

DigestHex digest_of(std::span<const std::uint8_t> bytes) {
    const std::string hex = ahfl::support::sha256_hex(bytes);
    DigestHex d{};
    std::copy(hex.begin(), hex.end(), d.begin());
    return d;
}

AuthHeader auth(std::uint64_t gen) {
    AuthHeader h;
    h.alg_version = 1;
    h.key_id = test_key_id();
    h.generation = gen;
    return h;
}

// Hand-write a fully digest-consistent Available generation to disk (bypassing the
// store) so a re-signed tamper isolates a single admit cross-check. Returns false if
// any fixture write / encode fails (so a broken fixture never masquerades as a store
// gate failure). One owning payload per slot must outlive this call.
[[nodiscard]] bool write_generation(const fs::path &root, CoreWorkflowId wf,
                                    ResumeCheckpointId ckpt, std::uint64_t gen,
                                    std::vector<Slot> slots,
                                    void (*record_tamper)(CoreWasmResumeRecord &),
                                    void (*manifest_tamper)(CommitManifest &)) {
    const auto key = test_key();
    const fs::path gp = gen_path(root, wf, ckpt, gen);
    std::error_code ec;
    fs::create_directories(gp, ec);
    if (ec) {
        return false;
    }

    CoreWasmResumeRecord record = make_record(wf);
    record.auth_header.generation = gen;
    if (record_tamper != nullptr) {
        record_tamper(record);
    }
    auto record_bytes = ahfl::runtime::core_wasm_resume::encode_and_authenticate(record, key);
    if (!record_bytes.ok()) {
        return false;
    }
    if (!write_file(gp / "record", *record_bytes.bytes)) {
        return false;
    }

    CommitManifest manifest;
    manifest.wf = wf;
    manifest.ckpt = ckpt;
    manifest.generation = gen;
    manifest.auth = auth(gen);
    AvailableBody body;
    body.record_sha256 = digest_of(*record_bytes.bytes);
    body.record_len = record_bytes.bytes->size();
    for (const Slot &s : slots) {
        SlotArtifact art;
        art.wf = wf;
        art.ckpt = ckpt;
        art.generation = gen;
        art.slot = s.slot;
        art.payload.assign(s.payload.begin(), s.payload.end());
        art.auth = auth(gen);
        auto sb = encode_slot(art, key);
        if (!sb.has_value() || !write_file(gp / ("slot-" + h16(s.slot.value)), *sb)) {
            return false;
        }
        body.slots.push_back(SlotMeta{s.slot, digest_of(*sb), s.payload.size()});
    }
    manifest.state = std::move(body);
    if (manifest_tamper != nullptr) {
        manifest_tamper(manifest);
    }
    auto manifest_bytes = encode_manifest(manifest, key);
    if (!manifest_bytes.has_value() || !write_file(gp / "manifest", *manifest_bytes)) {
        return false;
    }

    GenerationPointer pointer;
    pointer.wf = wf;
    pointer.ckpt = ckpt;
    pointer.current_generation = gen;
    pointer.manifest_sha256 = digest_of(*manifest_bytes);
    pointer.auth = auth(gen);
    auto pointer_bytes = encode_pointer(pointer, key);
    if (!pointer_bytes.has_value() ||
        !write_file(ckpt_path(root, wf, ckpt) / "pointer", *pointer_bytes)) {
        return false;
    }
    return true;
}

struct BlockSync {
    std::atomic<bool> reached{false};
    std::atomic<bool> may_continue{false};
    StorePhase block_at{StorePhase::GenerationRenamed};
};

void blocking_observer(StorePhase phase, void *ctx) noexcept {
    auto *s = static_cast<BlockSync *>(ctx);
    if (phase == s->block_at) {
        s->reached.store(true);
        while (!s->may_continue.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
    }
}

void phase_recorder(StorePhase phase, void *ctx) noexcept {
    static_cast<std::vector<StorePhase> *>(ctx)->push_back(phase);
}

StoreLimits big_limits() { return StoreLimits{1u << 20, 1u << 20}; }

std::expected<IntegrityPayloadStore, PayloadStoreError> open_plain(const fs::path &work) {
    StoreOptions o;
    o.limits = big_limits();
    return IntegrityPayloadStore::open(work, o);
}

// Fail-fast open for a case that is NOT testing open() itself: an unexpected open
// failure aborts the test honestly rather than letting a broken fixture masquerade
// as a store gate failure.
IntegrityPayloadStore must_open(const fs::path &work) {
    auto store = open_plain(work);
    if (!store.has_value()) {
        std::cerr << "FATAL: store open failed in setup\n";
        std::exit(2);
    }
    return std::move(*store);
}

// Fail-fast open with explicit options (observer wiring); same honest-abort contract.
IntegrityPayloadStore must_open_opts(const fs::path &work, StoreOptions o) {
    auto store = IntegrityPayloadStore::open(work, o);
    if (!store.has_value()) {
        std::cerr << "FATAL: store open (opts) failed in setup\n";
        std::exit(2);
    }
    return std::move(*store);
}

const CoreWorkflowId kWf{7};
const ResumeCheckpointId kCkpt{3};

} // namespace

int main(int argc, char **argv) {
    if (argc < 2) {
        std::cerr << "usage: payload_store_tests <work-dir>\n";
        return 2;
    }
    const fs::path base = fs::path(argv[1]);
    const auto key = test_key();
    const auto key_id = test_key_id();
    const std::span<const std::uint8_t, 16> id_span(key_id);
    const CoreWorkflowId wf = kWf;
    const ResumeCheckpointId ckpt = kCkpt;

    // FS/platform gate probe.
    {
        const fs::path probe_dir = base / "probe";
        nuke(probe_dir);
        std::error_code ec;
        fs::create_directories(probe_dir, ec);
        auto probe = open_plain(probe_dir);
        if (!probe.has_value()) {
            if (probe.error() == PayloadStoreError::UnsupportedPlatform ||
                probe.error() == PayloadStoreError::UnsupportedFilesystem) {
                std::cerr << "SKIP: payload store requires a Linux durable filesystem\n";
                nuke(probe_dir);
                return 77;
            }
            std::cerr << "FAIL: unexpected open error\n";
            return 1;
        }
        nuke(probe_dir);
    }

    auto fresh = [&](std::string_view name) {
        const fs::path w = base / name;
        nuke(w);
        std::error_code ec;
        fs::create_directories(w, ec);
        return w;
    };

    // ---- publish -> load round-trip + Available observer phase order ----------
    {
        const fs::path work = fresh("roundtrip");
        std::vector<StorePhase> phases;
        StoreOptions o;
        o.limits = big_limits();
        o.observer = &phase_recorder;
        o.observer_context = &phases;
        auto store = must_open_opts(work, o);
        auto gen = store.publish_available(wf, ckpt, 0, make_record(wf), slots_5(), id_span, key);
        check(gen.has_value() && *gen == 1, "roundtrip.first_gen_is_1");
        auto loaded = store.load(wf, ckpt, id_span, key);
        check(loaded.has_value() && std::holds_alternative<ResolvedAvailable>(*loaded),
              "roundtrip.is_available");
        if (loaded.has_value() && std::holds_alternative<ResolvedAvailable>(*loaded)) {
            const auto &av = std::get<ResolvedAvailable>(*loaded);
            check(av.generation == 1, "roundtrip.gen_1");
            check(av.slots.size() == 1 && av.slots[0].slot.value == 5 &&
                      av.slots[0].payload == kSlot5Payload,
                  "roundtrip.slot_payload");
            check(av.record.entry_id.value == 7, "roundtrip.record_entry_id");
        }
        const std::vector<StorePhase> expect = {
            StorePhase::WorkflowDirParentFsynced,     StorePhase::CheckpointDirParentFsynced,
            StorePhase::StageDirCreated,              StorePhase::RecordFileFsynced,
            StorePhase::SlotFilesFsynced,             StorePhase::ManifestFileFsynced,
            StorePhase::StageDirFsynced,              StorePhase::GenerationRenamed,
            StorePhase::CheckpointDirFsyncedAfterGeneration, StorePhase::PointerFileFsynced,
            StorePhase::PointerRenamed,               StorePhase::CheckpointDirFsyncedAfterPointer,
        };
        check(phases == expect, "roundtrip.observer_phase_order");
        nuke(work);
    }

    // ---- Consumed observer phase subsequence (no record/slot phases) ----------
    {
        const fs::path work = fresh("consumed-phases");
        {
            auto store = must_open(work);
            check(store.publish_available(wf, ckpt, 0, make_record(wf), slots_5(), id_span, key)
                      .has_value(),
                  "consumed_phases.setup_publish");
        }
        std::vector<StorePhase> phases;
        StoreOptions o;
        o.limits = big_limits();
        o.observer = &phase_recorder;
        o.observer_context = &phases;
        auto store = must_open_opts(work, o);
        auto c = store.mark_consumed(wf, ckpt, 1, id_span, key);
        check(c.has_value() && *c == 2, "consumed_phases.mark_ok");
        const std::vector<StorePhase> expect = {
            StorePhase::StageDirCreated,              StorePhase::ManifestFileFsynced,
            StorePhase::StageDirFsynced,              StorePhase::GenerationRenamed,
            StorePhase::CheckpointDirFsyncedAfterGeneration, StorePhase::PointerFileFsynced,
            StorePhase::PointerRenamed,               StorePhase::CheckpointDirFsyncedAfterPointer,
        };
        check(phases == expect, "consumed_phases.subsequence");
        nuke(work);
    }

    // ---- P0-T1a real concurrent CAS: one winner, one GenerationMismatch ------
    {
        const fs::path work = fresh("cas-concurrent");
        std::atomic<int> ready{0};
        std::atomic<bool> go{false};
        std::atomic<int> wins{0};
        std::atomic<int> gen_mismatch{0};
        auto worker = [&]() {
            auto store = must_open(work);
            ready.fetch_add(1);
            while (!go.load()) {
                std::this_thread::sleep_for(std::chrono::microseconds{50});
            }
            auto r = store.publish_available(wf, ckpt, 0, make_record(wf), slots_5(), id_span, key);
            if (r.has_value()) {
                ++wins;
            } else if (r.error() == PayloadStoreError::GenerationMismatch) {
                ++gen_mismatch;
            }
        };
        std::thread t1(worker);
        std::thread t2(worker);
        while (ready.load() < 2) {
            std::this_thread::sleep_for(std::chrono::microseconds{50});
        }
        go.store(true);
        t1.join();
        t2.join();
        check(wins.load() == 1 && gen_mismatch.load() == 1, "cas.exactly_one_winner");
        nuke(work);
    }

    // ---- P0-T1b reader sees old before GenerationRenamed, new after PointerRenamed
    auto reader_case = [&](std::string_view name, StorePhase block_at,
                           std::uint64_t expect_during) {
        const fs::path work = fresh(name);
        {
            auto store = must_open(work);
            auto g = store.publish_available(wf, ckpt, 0, make_record(wf), slots_5(), id_span, key);
            check(g.has_value() && *g == 1, "reader.pre_gen_1");
        }
        BlockSync sync;
        sync.block_at = block_at;
        StoreOptions wo;
        wo.limits = big_limits();
        wo.observer = &blocking_observer;
        wo.observer_context = &sync;
        auto writer_store = must_open_opts(work, wo);
        std::thread writer([&]() {
            auto r = writer_store.publish_available(wf, ckpt, 1, make_record(wf), slots_5(),
                                                    id_span, key);
            check(r.has_value() && *r == 2, "reader.writer_gen_2");
        });
        while (!sync.reached.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        std::uint64_t during = 0;
        bool during_ok = false;
        {
            auto reader = must_open(work);
            auto l = reader.load(wf, ckpt, id_span, key);
            if (l.has_value() && std::holds_alternative<ResolvedAvailable>(*l)) {
                during = std::get<ResolvedAvailable>(*l).generation;
                during_ok = true;
            }
        }
        sync.may_continue.store(true);
        writer.join();
        check(during_ok && during == expect_during, name);
        nuke(work);
    };
    reader_case("reader-before-swap", StorePhase::GenerationRenamed, 1); // pointer still 1
    // block right after the pointer rename (commit point) but BEFORE the trailing
    // checkpoint dir fsync -> a fresh reader must already see the new generation.
    reader_case("reader-after-swap", StorePhase::PointerRenamed, 2);
    // NOTE: the pointer st_nlink==0 acceptance branch (an already-open old inode whose
    // name was atomically replaced by rename) is exercised by CODE-PATH REVIEW ONLY.
    // Deterministically parking a load between its pointer open() and fstat() would need
    // a future dedicated read-side seam (NOT the B1b transaction observer / SIGKILL
    // worker, which also cannot wedge that window). This slice adds no such read-side
    // hook, so the branch is NOT asserted by a permanent test and is not claimed covered.

    // ---- P0-T2 orphans -------------------------------------------------------
    {
        // residual pointer.tmp
        {
            const fs::path work = fresh("orphan-pointer-tmp");
            auto store = must_open(work);
            check(store.publish_available(wf, ckpt, 0, make_record(wf), slots_5(), id_span, key)
                      .has_value(),
                  "orphan.ptmp_pre");
            const std::vector<std::uint8_t> junk = {0x00, 0x01};
            check(write_file(ckpt_path(work, wf, ckpt) / "pointer.tmp", junk),
                  "orphan.ptmp_fixture");
            auto g = store.publish_available(wf, ckpt, 1, make_record(wf), slots_5(), id_span, key);
            check(g.has_value() && *g == 2, "orphan.pointer_tmp_rebuilt");
            check(!fs::exists(ckpt_path(work, wf, ckpt) / "pointer.tmp"),
                  "orphan.pointer_tmp_removed");
            nuke(work);
        }
        // residual gen-<M>.stage with known children
        {
            const fs::path work = fresh("orphan-stage");
            auto store = must_open(work);
            check(store.publish_available(wf, ckpt, 0, make_record(wf), slots_5(), id_span, key)
                      .has_value(),
                  "orphan.stage_pre");
            const fs::path stage = ckpt_path(work, wf, ckpt) / ("gen-" + h16(2) + ".stage");
            std::error_code ec;
            fs::create_directories(stage, ec);
            const std::vector<std::uint8_t> junk = {0x00};
            check(write_file(stage / "record", junk) && write_file(stage / "manifest", junk),
                  "orphan.stage_fixture");
            auto g = store.publish_available(wf, ckpt, 1, make_record(wf), slots_5(), id_span, key);
            check(g.has_value() && *g == 2, "orphan.stage_rebuilt");
            nuke(work);
        }
        // complete-unreferenced final gen-<M> (pointer stays N=1) rebuilt not adopted
        {
            const fs::path work = fresh("orphan-final");
            auto store = must_open(work);
            check(store.publish_available(wf, ckpt, 0, make_record(wf), slots_5(), id_span, key)
                      .has_value(),
                  "orphan.final_pre");
            // Save the real gen-1 pointer bytes.
            const std::vector<std::uint8_t> pointer1 =
                read_file(ckpt_path(work, wf, ckpt) / "pointer");
            check(!pointer1.empty(), "orphan.final_saved_pointer");
            // Hand-write an unreferenced complete gen-2 with a MARKER payload.
            const std::vector<std::uint8_t> marker(4, 0x99);
            std::vector<Slot> marker_slots = {Slot{PayloadSlotId{5}, marker}};
            check(write_generation(work, wf, ckpt, 2, marker_slots, nullptr, nullptr),
                  "orphan.final_marker_fixture");
            // write_generation rewrote the pointer to gen 2; restore pointer to gen 1.
            check(write_file(ckpt_path(work, wf, ckpt) / "pointer", pointer1),
                  "orphan.final_restore_pointer");
            // Now expected=1 publish: the store must clean the unreferenced gen-2 and
            // publish its OWN gen-2 with the real caller payload (not the marker).
            auto g = store.publish_available(wf, ckpt, 1, make_record(wf), slots_5(), id_span, key);
            check(g.has_value() && *g == 2, "orphan.final_rebuilt");
            auto l = store.load(wf, ckpt, id_span, key);
            check(l.has_value() && std::holds_alternative<ResolvedAvailable>(*l) &&
                      std::get<ResolvedAvailable>(*l).slots.size() == 1 &&
                      std::get<ResolvedAvailable>(*l).slots[0].payload == kSlot5Payload,
                  "orphan.final_not_adopted");
            nuke(work);
        }
        // unknown child -> CommitInterrupted, kept
        {
            const fs::path work = fresh("orphan-unknown");
            auto store = must_open(work);
            check(store.publish_available(wf, ckpt, 0, make_record(wf), slots_5(), id_span, key)
                      .has_value(),
                  "orphan.unknown_pre");
            const fs::path stage = ckpt_path(work, wf, ckpt) / ("gen-" + h16(2) + ".stage");
            std::error_code ec;
            fs::create_directories(stage, ec);
            const std::vector<std::uint8_t> junk = {0x00};
            check(write_file(stage / "bogus", junk), "orphan.unknown_fixture");
            auto g = store.publish_available(wf, ckpt, 1, make_record(wf), slots_5(), id_span, key);
            check(!g.has_value() && g.error() == PayloadStoreError::CommitInterrupted,
                  "orphan.unknown_child_fails_closed");
            check(fs::exists(stage / "bogus"), "orphan.unknown_child_kept");
            nuke(work);
        }
        // known-name SYMLINK child -> CommitInterrupted, kept
        {
            const fs::path work = fresh("orphan-symlink-child");
            auto store = must_open(work);
            check(store.publish_available(wf, ckpt, 0, make_record(wf), slots_5(), id_span, key)
                      .has_value(),
                  "orphan.symlink_pre");
            const fs::path stage = ckpt_path(work, wf, ckpt) / ("gen-" + h16(2) + ".stage");
            std::error_code ec;
            fs::create_directories(stage, ec);
            fs::create_symlink("/etc/hostname", stage / "record", ec); // known name, symlink
            if (!ec) {
                auto g =
                    store.publish_available(wf, ckpt, 1, make_record(wf), slots_5(), id_span, key);
                check(!g.has_value() && g.error() == PayloadStoreError::CommitInterrupted,
                      "orphan.symlink_child_fails_closed");
                check(fs::is_symlink(stage / "record"), "orphan.symlink_child_kept");
            } else {
                std::cerr << "SKIP-CASE: cannot symlink (orphan.symlink_child)\n";
            }
            nuke(work);
        }
    }

    // ---- P0-T3 FS gates -----------------------------------------------------
    {
        // unsafe wf symlink -> StateMismatch
        {
            const fs::path work = fresh("fs-symlink-wf");
            std::error_code ec;
            fs::create_directories(work / "elsewhere", ec);
            fs::create_directory_symlink("elsewhere", work / ("wf-" + h8(wf.value)), ec);
            if (!ec) {
                auto store = must_open(work);
                auto l = store.load(wf, ckpt, id_span, key);
                check(!l.has_value() && l.error() == PayloadStoreError::StateMismatch,
                      "fs.symlink_wf_statemismatch");
            } else {
                std::cerr << "SKIP-CASE: cannot symlink (fs.symlink_wf)\n";
            }
            nuke(work);
        }
        // group-writable wf -> StateMismatch
        {
            const fs::path work = fresh("fs-groupwrite-wf");
            auto store = must_open(work);
            check(store.publish_available(wf, ckpt, 0, make_record(wf), slots_5(), id_span, key)
                      .has_value(),
                  "fs.groupwrite_pre");
            const fs::path wfdir = work / ("wf-" + h8(wf.value));
            std::error_code ec;
            fs::permissions(wfdir, fs::perms::group_write, fs::perm_options::add, ec);
            if (!ec) {
                auto l = store.load(wf, ckpt, id_span, key);
                check(!l.has_value() && l.error() == PayloadStoreError::StateMismatch,
                      "fs.groupwrite_wf_statemismatch");
            } else {
                std::cerr << "SKIP-CASE: cannot chmod (fs.groupwrite_wf)\n";
            }
            nuke(work);
        }
        // pointer FIFO -> reject
        {
            const fs::path work = fresh("fs-fifo-pointer");
            auto store = must_open(work);
            check(store.publish_available(wf, ckpt, 0, make_record(wf), slots_5(), id_span, key)
                      .has_value(),
                  "fs.fifo_pre");
            const fs::path ptr = ckpt_path(work, wf, ckpt) / "pointer";
            fs::remove(ptr);
            if (::mkfifo(ptr.c_str(), 0600) == 0) {
                auto l = store.load(wf, ckpt, id_span, key);
                check(!l.has_value() && l.error() == PayloadStoreError::StateMismatch,
                      "fs.fifo_pointer_rejected");
            } else {
                std::cerr << "SKIP-CASE: cannot mkfifo (fs.fifo_pointer)\n";
            }
            nuke(work);
        }
        // pointer hardlink nlink>1 -> reject
        {
            const fs::path work = fresh("fs-hardlink-pointer");
            auto store = must_open(work);
            check(store.publish_available(wf, ckpt, 0, make_record(wf), slots_5(), id_span, key)
                      .has_value(),
                  "fs.hlptr_pre");
            std::error_code ec;
            fs::create_hard_link(ckpt_path(work, wf, ckpt) / "pointer",
                                 ckpt_path(work, wf, ckpt) / "pointer.hl", ec);
            if (!ec) {
                auto l = store.load(wf, ckpt, id_span, key);
                check(!l.has_value() && l.error() == PayloadStoreError::StateMismatch,
                      "fs.hardlink_pointer_rejected");
            } else {
                std::cerr << "SKIP-CASE: cannot hardlink (fs.hardlink_pointer)\n";
            }
            nuke(work);
        }
        // immutable record hardlink -> reject
        {
            const fs::path work = fresh("fs-hardlink-record");
            auto store = must_open(work);
            check(store.publish_available(wf, ckpt, 0, make_record(wf), slots_5(), id_span, key)
                      .has_value(),
                  "fs.hlrec_pre");
            std::error_code ec;
            fs::create_hard_link(gen_path(work, wf, ckpt, 1) / "record",
                                 gen_path(work, wf, ckpt, 1) / "record.hl", ec);
            if (!ec) {
                auto l = store.load(wf, ckpt, id_span, key);
                check(!l.has_value() && l.error() == PayloadStoreError::StateMismatch,
                      "fs.hardlink_record_rejected");
            } else {
                std::cerr << "SKIP-CASE: cannot hardlink (fs.hardlink_record)\n";
            }
            nuke(work);
        }
        std::cerr << "NOTE: st_dev submount gate is exercised by code path only; a real "
                     "cross-filesystem mount under root needs privilege and is not asserted.\n";
    }

    // ---- P0-T4 cross-artifact tamper via re-signed fixtures -------------------
    auto tamper_case = [&](std::string_view name, void (*rt)(CoreWasmResumeRecord &),
                           void (*mt)(CommitManifest &)) {
        const fs::path work = fresh(name);
        check(write_generation(work, wf, ckpt, 1, slots_5(), rt, mt), "tamper.fixture");
        auto store = must_open(work);
        auto l = store.load(wf, ckpt, id_span, key);
        check(!l.has_value() && l.error() == PayloadStoreError::StateMismatch, name);
        nuke(work);
    };
    tamper_case("tamper-entry-id",
                [](CoreWasmResumeRecord &r) { r.entry_id = CoreWorkflowId{8}; }, nullptr);
    tamper_case("tamper-record-gen",
                [](CoreWasmResumeRecord &r) { r.auth_header.generation = 99; }, nullptr);
    tamper_case("tamper-guarantees", [](CoreWasmResumeRecord &r) { r.guarantees = 0x1; }, nullptr);
    tamper_case("tamper-record-len", nullptr,
                [](CommitManifest &m) { std::get<AvailableBody>(m.state).record_len += 1; });
    tamper_case("tamper-slot-len", nullptr, [](CommitManifest &m) {
        std::get<AvailableBody>(m.state).slots[0].payload_len += 1;
    });
    {
        // single-artifact record digest break (rewrite record without fixing manifest)
        const fs::path work = fresh("tamper-record-digest");
        check(write_generation(work, wf, ckpt, 1, slots_5(), nullptr, nullptr),
              "tamper.digest_fix");
        CoreWasmResumeRecord other = make_record(wf);
        other.auth_header.generation = 1;
        other.nodes.back().memo[0].arg_hash = 0x5555555555555555ULL;
        auto ob = ahfl::runtime::core_wasm_resume::encode_and_authenticate(other, key);
        check(ob.ok() && write_file(gen_path(work, wf, ckpt, 1) / "record", *ob.bytes),
              "tamper.digest_rewrite");
        auto store = must_open(work);
        auto l = store.load(wf, ckpt, id_span, key);
        check(!l.has_value() && l.error() == PayloadStoreError::StateMismatch,
              "tamper.record_digest_break");
        nuke(work);
    }
    {
        // mark_consumed rejects a corrupt live Available (manifest record_len wrong).
        const fs::path work = fresh("tamper-consume-live");
        check(write_generation(work, wf, ckpt, 1, slots_5(), nullptr,
                               [](CommitManifest &m) {
                                   std::get<AvailableBody>(m.state).record_len += 1;
                               }),
              "tamper.consume_fixture");
        auto store = must_open(work);
        auto c = store.mark_consumed(wf, ckpt, 1, id_span, key);
        check(!c.has_value() && c.error() == PayloadStoreError::StateMismatch,
              "tamper.consume_rejects_corrupt_live");
        nuke(work);
    }
    {
        // Consumed prior-admission: after a valid Consumed, corrupt the PREVIOUS
        // Available manifest -> load rejects.
        const fs::path work = fresh("tamper-consumed-prev");
        {
            auto store = must_open(work);
            check(store.publish_available(wf, ckpt, 0, make_record(wf), slots_5(), id_span, key)
                      .has_value(),
                  "tamper.cprev_pub");
            check(store.mark_consumed(wf, ckpt, 1, id_span, key).has_value(), "tamper.cprev_mark");
        }
        // Corrupt the previous (gen-1) manifest bytes (a single flipped byte breaks
        // its authentication; the Consumed tombstone still binds its old digest).
        const fs::path prev_manifest = gen_path(work, wf, ckpt, 1) / "manifest";
        auto bytes = read_file(prev_manifest);
        check(!bytes.empty(), "tamper.cprev_read");
        bytes[0] ^= 0xff;
        check(write_file(prev_manifest, bytes), "tamper.cprev_write");
        auto store = must_open(work);
        auto l = store.load(wf, ckpt, id_span, key);
        check(!l.has_value() && l.error() == PayloadStoreError::StateMismatch,
              "tamper.consumed_prev_corrupt");
        nuke(work);
    }
    // P0-5 (a/b/c): re-signed Consumed tombstone with a bad binding -> load rejects.
    // Helper: publish a valid Available gen-1, then hand-write a Consumed gen-2
    // manifest (via the given tamper) + a consistent pointer to gen-2.
    auto write_consumed = [&](const fs::path &work,
                              void (*tamper)(ConsumedBody &, CommitManifest &)) {
        {
            auto store = must_open(work);
            if (!store.publish_available(wf, ckpt, 0, make_record(wf), slots_5(), id_span, key)
                     .has_value()) {
                return false;
            }
        }
        CommitManifest m;
        m.wf = wf;
        m.ckpt = ckpt;
        m.generation = 2;
        m.auth = auth(2);
        ConsumedBody cb;
        cb.consumed_generation = 1;
        // Bind the real gen-1 manifest digest.
        cb.consumed_manifest_sha256 =
            digest_of(read_file(gen_path(work, wf, ckpt, 1) / "manifest"));
        m.state = cb;
        if (tamper != nullptr) {
            tamper(std::get<ConsumedBody>(m.state), m);
        }
        auto mb = encode_manifest(m, key);
        if (!mb.has_value()) {
            return false;
        }
        const fs::path gp = gen_path(work, wf, ckpt, 2);
        std::error_code ec;
        fs::create_directories(gp, ec);
        if (!write_file(gp / "manifest", *mb)) {
            return false;
        }
        GenerationPointer p;
        p.wf = wf;
        p.ckpt = ckpt;
        p.current_generation = 2;
        p.manifest_sha256 = digest_of(*mb);
        p.auth = auth(2);
        auto pb = encode_pointer(p, key);
        return pb.has_value() && write_file(ckpt_path(work, wf, ckpt) / "pointer", *pb);
    };
    {
        // (c) consumed_generation != current-1: bind gen 0 (encoder needs 1<=cg<gen,
        // so use a valid-but-wrong cg... cg must be < 2 and >=1, so the only encodable
        // value is 1; to exercise "!= current-1" we instead make current generation 3
        // while consumed_generation stays 1). Simpler: tamper the manifest generation
        // to 3 but keep pointer/consumed consistent so B0 admits, B1 rejects cg+1!=gen.
        const fs::path work = fresh("consumed-bad-cg");
        auto ok = write_consumed(work, [](ConsumedBody &cb, CommitManifest &m) {
            m.generation = 3;      // pointer will bind gen 3
            m.auth.generation = 3; // keep body==auth
            cb.consumed_generation = 1; // 1 + 1 != 3 -> B1 StateMismatch
        });
        check(ok, "consumed_cg.fixture");
        // The pointer above was written for generation 2; rewrite it for generation 3.
        if (ok) {
            CommitManifest m;
            m.wf = wf;
            m.ckpt = ckpt;
            m.generation = 3;
            m.auth = auth(3);
            ConsumedBody cb;
            cb.consumed_generation = 1;
            cb.consumed_manifest_sha256 =
                digest_of(read_file(gen_path(work, wf, ckpt, 1) / "manifest"));
            m.state = cb;
            auto mb = encode_manifest(m, key);
            const fs::path gp = gen_path(work, wf, ckpt, 3);
            std::error_code ec;
            fs::create_directories(gp, ec);
            check(mb.has_value() && write_file(gp / "manifest", *mb), "consumed_cg.gen3_manifest");
            GenerationPointer p;
            p.wf = wf;
            p.ckpt = ckpt;
            p.current_generation = 3;
            p.manifest_sha256 = digest_of(*mb);
            p.auth = auth(3);
            auto pb = encode_pointer(p, key);
            check(pb.has_value() && write_file(ckpt_path(work, wf, ckpt) / "pointer", *pb),
                  "consumed_cg.pointer3");
            auto store = must_open(work);
            auto l = store.load(wf, ckpt, id_span, key);
            check(!l.has_value() && l.error() == PayloadStoreError::StateMismatch,
                  "consumed_cg.rejected");
        }
        nuke(work);
    }
    {
        // (a) previous manifest namespace mismatch: rewrite the gen-1 manifest with a
        // different wf (re-signed), and rebind the Consumed tombstone to its new digest.
        const fs::path work = fresh("consumed-prev-ns");
        {
            auto store = must_open(work);
            check(store.publish_available(wf, ckpt, 0, make_record(wf), slots_5(), id_span, key)
                      .has_value(),
                  "consumed_ns.pub");
        }
        // Re-sign the gen-1 manifest with wf=8 (namespace mismatch) but keep its own
        // internal consistency; record/slots still reference wf=7 so admit will fail on
        // the previous-manifest namespace check.
        CommitManifest prev;
        prev.wf = CoreWorkflowId{8}; // wrong namespace
        prev.ckpt = ckpt;
        prev.generation = 1;
        prev.auth = auth(1);
        AvailableBody body;
        auto rec_bytes = read_file(gen_path(work, wf, ckpt, 1) / "record");
        body.record_sha256 = digest_of(rec_bytes);
        body.record_len = rec_bytes.size();
        auto slot_bytes = read_file(gen_path(work, wf, ckpt, 1) / ("slot-" + h16(5)));
        body.slots.push_back(
            SlotMeta{PayloadSlotId{5}, digest_of(slot_bytes), kSlot5Payload.size()});
        prev.state = std::move(body);
        auto prev_mb = encode_manifest(prev, key);
        check(prev_mb.has_value() &&
                  write_file(gen_path(work, wf, ckpt, 1) / "manifest", *prev_mb),
              "consumed_ns.prev_manifest");
        // Consumed gen-2 binds the new (wrong-namespace) prev digest.
        CommitManifest m;
        m.wf = wf;
        m.ckpt = ckpt;
        m.generation = 2;
        m.auth = auth(2);
        ConsumedBody cb;
        cb.consumed_generation = 1;
        cb.consumed_manifest_sha256 = digest_of(*prev_mb);
        m.state = cb;
        auto mb = encode_manifest(m, key);
        const fs::path gp2 = gen_path(work, wf, ckpt, 2);
        std::error_code ec;
        fs::create_directories(gp2, ec);
        check(mb.has_value() && write_file(gp2 / "manifest", *mb), "consumed_ns.tomb_manifest");
        GenerationPointer p;
        p.wf = wf;
        p.ckpt = ckpt;
        p.current_generation = 2;
        p.manifest_sha256 = digest_of(*mb);
        p.auth = auth(2);
        auto pb = encode_pointer(p, key);
        check(pb.has_value() && write_file(ckpt_path(work, wf, ckpt) / "pointer", *pb),
              "consumed_ns.pointer");
        auto store = must_open(work);
        auto l = store.load(wf, ckpt, id_span, key);
        check(!l.has_value() && l.error() == PayloadStoreError::StateMismatch,
              "consumed_ns.rejected");
        nuke(work);
    }
    {
        // (b) previous manifest state != Available: rebind the Consumed tombstone to a
        // previous generation whose manifest is itself Consumed.
        const fs::path work = fresh("consumed-prev-state");
        {
            auto store = must_open(work);
            check(store.publish_available(wf, ckpt, 0, make_record(wf), slots_5(), id_span, key)
                      .has_value(),
                  "consumed_state.pub");
            check(store.mark_consumed(wf, ckpt, 1, id_span, key).has_value(),
                  "consumed_state.mark");
        }
        // Now gen-2 is a Consumed manifest. Hand-write a gen-3 Consumed tombstone that
        // (wrongly) consumes gen-2 (which is Consumed, not Available).
        CommitManifest m;
        m.wf = wf;
        m.ckpt = ckpt;
        m.generation = 3;
        m.auth = auth(3);
        ConsumedBody cb;
        cb.consumed_generation = 2;
        cb.consumed_manifest_sha256 =
            digest_of(read_file(gen_path(work, wf, ckpt, 2) / "manifest"));
        m.state = cb;
        auto mb = encode_manifest(m, key);
        const fs::path gp3 = gen_path(work, wf, ckpt, 3);
        std::error_code ec;
        fs::create_directories(gp3, ec);
        check(mb.has_value() && write_file(gp3 / "manifest", *mb), "consumed_state.tomb");
        GenerationPointer p;
        p.wf = wf;
        p.ckpt = ckpt;
        p.current_generation = 3;
        p.manifest_sha256 = digest_of(*mb);
        p.auth = auth(3);
        auto pb = encode_pointer(p, key);
        check(pb.has_value() && write_file(ckpt_path(work, wf, ckpt) / "pointer", *pb),
              "consumed_state.pointer");
        auto store = must_open(work);
        auto l = store.load(wf, ckpt, id_span, key);
        check(!l.has_value() && l.error() == PayloadStoreError::StateMismatch,
              "consumed_state.rejected");
        nuke(work);
    }

    // ---- P1-6 create-path existing-but-unsafe + lock hardlink -> StateMismatch -
    {
        // publish expected=0 over an existing group-writable wf -> StateMismatch.
        const fs::path work = fresh("create-groupwrite-wf");
        std::error_code ec;
        fs::create_directories(work / ("wf-" + h8(wf.value)), ec);
        fs::permissions(work / ("wf-" + h8(wf.value)), fs::perms::group_write,
                        fs::perm_options::add, ec);
        if (!ec) {
            auto store = must_open(work);
            auto r = store.publish_available(wf, ckpt, 0, make_record(wf), slots_5(), id_span, key);
            check(!r.has_value() && r.error() == PayloadStoreError::StateMismatch,
                  "create.groupwrite_wf_statemismatch");
        } else {
            std::cerr << "SKIP-CASE: cannot chmod (create.groupwrite_wf)\n";
        }
        nuke(work);
    }
    {
        // publish expected=0 over an existing wf symlink -> StateMismatch.
        const fs::path work = fresh("create-symlink-wf");
        std::error_code ec;
        fs::create_directories(work / "elsewhere", ec);
        fs::create_directory_symlink("elsewhere", work / ("wf-" + h8(wf.value)), ec);
        if (!ec) {
            auto store = must_open(work);
            auto r = store.publish_available(wf, ckpt, 0, make_record(wf), slots_5(), id_span, key);
            check(!r.has_value() && r.error() == PayloadStoreError::StateMismatch,
                  "create.symlink_wf_statemismatch");
        } else {
            std::cerr << "SKIP-CASE: cannot symlink (create.symlink_wf)\n";
        }
        nuke(work);
    }
    {
        // hardlinked pointer.lock -> StateMismatch (not WriteFailed).
        const fs::path work = fresh("lock-hardlink");
        auto store = must_open(work);
        check(store.publish_available(wf, ckpt, 0, make_record(wf), slots_5(), id_span, key)
                  .has_value(),
              "lock.pre");
        const fs::path lock = ckpt_path(work, wf, ckpt) / "pointer.lock";
        std::error_code ec;
        fs::create_hard_link(lock, ckpt_path(work, wf, ckpt) / "pointer.lock.hl", ec);
        if (!ec) {
            auto r = store.publish_available(wf, ckpt, 1, make_record(wf), slots_5(), id_span, key);
            check(!r.has_value() && r.error() == PayloadStoreError::StateMismatch,
                  "lock.hardlink_statemismatch");
        } else {
            std::cerr << "SKIP-CASE: cannot hardlink (lock.hardlink)\n";
        }
        nuke(work);
    }

    // ---- P0-T5 caps / sentinels: NO stage/pointer/gen advance on failure ------
    // A failed publish may legitimately create the wf/ckpt directories and the lock
    // (expected==0 create path); the contract is only that NO pointer / pointer.tmp /
    // gen-* / gen-*.stage advanced.
    auto assert_no_advance = [&](const fs::path &work) {
        const fs::path cp = ckpt_path(work, wf, ckpt);
        if (!fs::exists(cp)) {
            return true; // nothing created at all
        }
        if (fs::exists(cp / "pointer") || fs::exists(cp / "pointer.tmp")) {
            return false;
        }
        for (const auto &ent : fs::directory_iterator(cp)) {
            const std::string n = ent.path().filename().string();
            if (n.rfind("gen-", 0) == 0) {
                return false; // any gen-* or gen-*.stage
            }
        }
        return true;
    };
    {
        const fs::path work = fresh("cap-slot");
        StoreOptions o;
        o.limits = StoreLimits{1u << 20, 8};
        auto store = must_open_opts(work, o);
        auto r = store.publish_available(wf, ckpt, 0, make_record(wf), slots_5(), id_span, key);
        check(!r.has_value() && r.error() == PayloadStoreError::SizeCapExceeded,
              "cap.slot_exceeded");
        check(assert_no_advance(work), "cap.slot_no_advance");
        nuke(work);
    }
    {
        const fs::path work = fresh("cap-record");
        StoreOptions o;
        o.limits = StoreLimits{16, 1u << 20};
        auto store = must_open_opts(work, o);
        auto r = store.publish_available(wf, ckpt, 0, make_record(wf), slots_5(), id_span, key);
        check(!r.has_value() && r.error() == PayloadStoreError::SizeCapExceeded,
              "cap.record_exceeded");
        check(assert_no_advance(work), "cap.record_no_advance");
        nuke(work);
    }
    {
        const fs::path work = fresh("cap-overflow");
        StoreOptions o;
        o.limits = StoreLimits{std::numeric_limits<std::uint64_t>::max(), 1u << 20};
        auto store = IntegrityPayloadStore::open(work, o);
        check(!store.has_value() && store.error() == PayloadStoreError::SizeCapExceeded,
              "cap.formula_overflow");
        nuke(work);
    }
    {
        const fs::path work = fresh("cap-zero");
        StoreOptions o;
        o.limits = StoreLimits{0, 0};
        auto store = IntegrityPayloadStore::open(work, o);
        check(!store.has_value() && store.error() == PayloadStoreError::SizeCapExceeded,
              "cap.zero_limits");
        nuke(work);
    }
    {
        const fs::path work = fresh("sentinel-expected");
        auto store = must_open(work);
        auto r = store.publish_available(wf, ckpt, std::numeric_limits<std::uint64_t>::max(),
                                          make_record(wf), slots_5(), id_span, key);
        check(!r.has_value() && r.error() == PayloadStoreError::GenerationMismatch,
              "sentinel.expected_uint64max");
        check(assert_no_advance(work), "sentinel.expected_no_advance");
        nuke(work);
    }
    {
        const fs::path work = fresh("sentinel-ids");
        auto store = must_open(work);
        auto bad_wf = store.publish_available(CoreWorkflowId{CoreWorkflowId::kInvalid}, ckpt, 0,
                                               make_record(wf), slots_5(), id_span, key);
        check(!bad_wf.has_value() && bad_wf.error() == PayloadStoreError::Malformed,
              "sentinel.invalid_wf");
        auto bad_ckpt = store.publish_available(
            wf, ResumeCheckpointId{ResumeCheckpointId::kInvalid}, 0, make_record(wf), slots_5(),
            id_span, key);
        check(!bad_ckpt.has_value() && bad_ckpt.error() == PayloadStoreError::Malformed,
              "sentinel.invalid_ckpt");
        std::vector<Slot> bad_slot = {Slot{PayloadSlotId{PayloadSlotId::kInvalid}, kSlot5Payload}};
        auto bs = store.publish_available(wf, ckpt, 0, make_record(wf), bad_slot, id_span, key);
        check(!bs.has_value() && bs.error() == PayloadStoreError::Malformed,
              "sentinel.invalid_slot");
        check(assert_no_advance(work), "sentinel.ids_no_advance");
        nuke(work);
    }

    // ---- P0-T3(set) distinct-slot missing / extra / duplicate ---------------
    {
        const fs::path work = fresh("slot-set");
        auto store = must_open(work);
        auto miss = store.publish_available(wf, ckpt, 0, make_record(wf), {}, id_span, key);
        check(!miss.has_value() && miss.error() == PayloadStoreError::SlotSetMismatch,
              "set.missing_rejected");
        std::vector<Slot> extra = {Slot{PayloadSlotId{5}, kSlot5Payload},
                                   Slot{PayloadSlotId{9}, kSlot5Payload}};
        auto ex = store.publish_available(wf, ckpt, 0, make_record(wf), extra, id_span, key);
        check(!ex.has_value() && ex.error() == PayloadStoreError::SlotSetMismatch,
              "set.extra_rejected");
        std::vector<Slot> dup = {Slot{PayloadSlotId{5}, kSlot5Payload},
                                 Slot{PayloadSlotId{5}, kSlot5Payload}};
        auto d = store.publish_available(wf, ckpt, 0, make_record(wf), dup, id_span, key);
        check(!d.has_value() && d.error() == PayloadStoreError::SlotSetMismatch,
              "set.duplicate_rejected");
        check(assert_no_advance(work), "set.no_advance");
        nuke(work);
    }

    // ---- P1-T6 CAS priority, Consumed, absent, key/key_id --------------------
    {
        const fs::path work = fresh("consumed-priority");
        auto store = must_open(work);
        check(store.publish_available(wf, ckpt, 0, make_record(wf), slots_5(), id_span, key)
                  .has_value(),
              "cpri.pub");
        auto c = store.mark_consumed(wf, ckpt, 1, id_span, key);
        check(c.has_value() && *c == 2, "cpri.mark_ok");
        auto stale = store.publish_available(wf, ckpt, 0, make_record(wf), slots_5(), id_span, key);
        check(!stale.has_value() && stale.error() == PayloadStoreError::GenerationMismatch,
              "cpri.stale_expected_gen_mismatch");
        auto over = store.publish_available(wf, ckpt, 2, make_record(wf), slots_5(), id_span, key);
        check(!over.has_value() && over.error() == PayloadStoreError::Consumed,
              "cpri.publish_over_hit");
        auto cc = store.mark_consumed(wf, ckpt, 2, id_span, key);
        check(!cc.has_value() && cc.error() == PayloadStoreError::Consumed, "cpri.consume_over");
        auto l = store.load(wf, ckpt, id_span, key);
        check(l.has_value() && std::holds_alternative<ResolvedConsumed>(*l), "cpri.load_consumed");
        nuke(work);
    }
    {
        const fs::path work = fresh("absent");
        auto store = must_open(work);
        const CoreWorkflowId absent_wf{999};
        auto l = store.load(absent_wf, ckpt, id_span, key);
        check(!l.has_value() && l.error() == PayloadStoreError::NotFound, "absent.load_notfound");
        auto c = store.mark_consumed(absent_wf, ckpt, 0, id_span, key);
        check(!c.has_value() && c.error() == PayloadStoreError::NotFound,
              "absent.consume_notfound");
        auto pe = store.publish_available(wf, ckpt, 3, make_record(wf), slots_5(), id_span, key);
        check(!pe.has_value() && pe.error() == PayloadStoreError::GenerationMismatch,
              "absent.publish_expected_gt0");
        check(!fs::exists(work / ("wf-" + h8(absent_wf.value))), "absent.no_dir_created");
        check(!fs::exists(work / ("wf-" + h8(wf.value))), "absent.publish_no_dir_created");
        nuke(work);
    }
    {
        const fs::path work = fresh("key-mismatch");
        auto store = must_open(work);
        check(store.publish_available(wf, ckpt, 0, make_record(wf), slots_5(), id_span, key)
                  .has_value(),
              "key.pub");
        auto bad_id = key_id;
        bad_id[0] ^= 0xff;
        auto wrong_id = store.load(wf, ckpt, std::span<const std::uint8_t, 16>(bad_id), key);
        check(!wrong_id.has_value() && wrong_id.error() == PayloadStoreError::KeyIdMismatch,
              "key.wrong_id");
        std::vector<std::uint8_t> bad_key(32, 0x2c);
        auto wrong_key = store.load(wf, ckpt, id_span, bad_key);
        check(!wrong_key.has_value() && wrong_key.error() == PayloadStoreError::IntegrityFailed,
              "key.wrong_key");
        static_assert(std::is_enum_v<PayloadStoreError>, "error type must be a bare no-echo enum");
        nuke(work);
    }

    if (g_failures == 0) {
        std::cout << "payload_store: all checks passed\n";
        return 0;
    }
    std::cerr << "payload_store: " << g_failures << " failure(s)\n";
    return 1;
}
