// RFC 0026 KR6.5 E4-B2-B1b: cross-process crash worker for the integrity-only
// durable-resume payload store. A multi-mode binary driven by the Python harness
// tests/scripts/payload_store_crash_smoke.py. It exercises the LANDED B1a store
// (src/runtime/engine/payload_store.hpp) across real process boundaries: a `crash-*`
// mode installs the store's TransactionObserver, parks at a chosen post-syscall
// StorePhase after writing a rendezvous marker, and can only terminate via the
// parent's SIGKILL; other modes publish/consume/load or run a stdin-gated CAS race.
//
// This is a TEST binary only (no production caller). The rendezvous marker is a
// process-visible signal, NOT store durability evidence. SIGKILL proves post-process-
// death visibility (the page cache survives), NOT power-loss durability. The store's
// own artifacts live under <case-dir>/store; the marker lives under <case-dir>/control
// so control-plane files never appear inside the store root.

#include "runtime/engine/payload_store.hpp"

#include "runtime/engine/core_wasm_resume_record.hpp"
#include "runtime/engine/payload_store_codec.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#if defined(__linux__)
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {

namespace ps = ahfl::runtime::payload_store;
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

// Exit codes (harness-decidable).
constexpr int kOk = 0;
constexpr int kUsage = 2;
constexpr int kUnexpected = 3;
constexpr int kNotFound = 4;
constexpr int kGenerationMismatch = 5;
constexpr int kUnsupported = 77;

// Fixed fixture identity (matches the B1a unit test).
std::vector<std::uint8_t> test_key() { return std::vector<std::uint8_t>(32, 0x2b); }
std::array<std::uint8_t, 16> test_key_id() {
    std::array<std::uint8_t, 16> id{};
    for (std::size_t i = 0; i < id.size(); ++i) {
        id[i] = static_cast<std::uint8_t>(i + 1);
    }
    return id;
}
constexpr CoreWorkflowId kWf{7};
constexpr ps::ResumeCheckpointId kCkpt{3};

DigestHex hex_of(char fill) {
    DigestHex d{};
    d.fill(fill);
    return d;
}

// A restricted payload-tag enum -> fixed distinct payload bytes, so the harness can
// distinguish OLD / NEW / rebuilt content deterministically. slot id is always 5.
std::optional<std::vector<std::uint8_t>> payload_for_tag(std::string_view tag) {
    if (tag == "baseline") {
        return std::vector<std::uint8_t>{0x00, 0x01, 0x02, 0x03};
    }
    if (tag == "orphan") {
        return std::vector<std::uint8_t>{0x0a, 0x0b, 0x0c, 0x0d};
    }
    if (tag == "real") {
        return std::vector<std::uint8_t>{0xde, 0xad, 0xbe, 0xef};
    }
    if (tag == "race-a") {
        return std::vector<std::uint8_t>{0xaa, 0xaa, 0xaa, 0xaa};
    }
    if (tag == "race-b") {
        return std::vector<std::uint8_t>{0xbb, 0xbb, 0xbb, 0xbb};
    }
    return std::nullopt;
}

CoreWasmResumeRecord make_record() {
    CoreWasmResumeRecord r;
    r.format_version = 1;
    r.guarantees = 0;
    r.module_sha256 = hex_of('a');
    r.wire_schema_sha256 = hex_of('b');
    r.exec_manifest_sha256 = hex_of('c');
    r.entry_id = kWf;
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

std::filesystem::path store_root(const std::filesystem::path &case_dir) {
    return case_dir / "store";
}
std::filesystem::path marker_path(const std::filesystem::path &case_dir) {
    return case_dir / "control" / "fault-ready.json";
}

// Pre-open shape check on <case-dir>/store so a genuinely-missing / non-directory /
// symlink / wrong-owner-or-mode / unstat-able store root is a SETUP error (exit 3),
// never a platform/FS SKIP. Only after this passes may IntegrityPayloadStore::open
// run, and only ITS UnsupportedPlatform/UnsupportedFilesystem may map to 77. Returns
// true iff the store root is a real, safe, store-owned directory.
[[nodiscard]] bool store_shape_ok(const std::filesystem::path &case_dir) {
#if defined(__linux__)
    struct stat st{};
    if (::lstat(store_root(case_dir).c_str(), &st) != 0) {
        return false; // ENOENT / unstat-able
    }
    if (S_ISLNK(st.st_mode) || !S_ISDIR(st.st_mode)) {
        return false; // symlink or not-a-directory
    }
    if (st.st_uid != ::geteuid() || (st.st_mode & (S_IWGRP | S_IWOTH)) != 0) {
        return false; // wrong owner / group-other writable
    }
    return true;
#else
    (void)case_dir;
    return true; // non-Linux: open() returns UnsupportedPlatform regardless
#endif
}

// Map an exit for a publish/consume result.
int result_exit(const std::expected<std::uint64_t, ps::PayloadStoreError> &r) {
    if (r.has_value()) {
        return kOk;
    }
    switch (r.error()) {
    case ps::PayloadStoreError::NotFound:
        return kNotFound;
    case ps::PayloadStoreError::GenerationMismatch:
        return kGenerationMismatch;
    case ps::PayloadStoreError::UnsupportedPlatform:
    case ps::PayloadStoreError::UnsupportedFilesystem:
        return kUnsupported;
    default:
        return kUnexpected;
    }
}

ps::StoreOptions base_options() {
    ps::StoreOptions o;
    o.limits = ps::StoreLimits{1u << 20, 1u << 20};
    return o;
}

std::string to_hex(std::span<const std::uint8_t> bytes) {
    static const char *k = "0123456789abcdef";
    std::string s;
    s.reserve(bytes.size() * 2);
    for (std::uint8_t b : bytes) {
        s.push_back(k[b >> 4]);
        s.push_back(k[b & 0x0f]);
    }
    return s;
}

#if defined(__linux__)

// Write the rendezvous marker's TEMP file: O_CREAT|O_EXCL + write_all + fsync +
// close. The temp is published atomically by a later rename (see crash_observer), so
// the final path only ever appears complete. NOT store durability evidence. Returns
// false on any failure so the caller _exit(3)s (parent detects a premature exit).
bool write_marker_tmp(const std::filesystem::path &tmp, const std::string &json) noexcept {
    const int fd = ::open(tmp.c_str(), O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0600);
    if (fd < 0) {
        return false;
    }
    const char *p = json.data();
    std::size_t remaining = json.size();
    while (remaining > 0) {
        const ssize_t n = ::write(fd, p, remaining);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            ::close(fd);
            return false;
        }
        if (n == 0) {
            ::close(fd);
            return false;
        }
        p += n;
        remaining -= static_cast<std::size_t>(n);
    }
    if (::fsync(fd) != 0) {
        ::close(fd);
        return false;
    }
    return ::close(fd) == 0;
}

// Observer context: the phase to park at + marker paths + rendezvous JSON fields.
struct CrashContext {
    ps::StorePhase block_at{};
    std::string marker_tmp;
    std::string marker_final;
    std::string json;
};

void crash_observer(ps::StorePhase phase, void *ctx) noexcept {
    auto *c = static_cast<CrashContext *>(ctx);
    if (phase != c->block_at) {
        return;
    }
    // 1) write the complete marker to a temp file (excl+fsync+close).
    if (!write_marker_tmp(c->marker_tmp, c->json)) {
        std::_Exit(kUnexpected);
    }
    // 2) flush FAULT_READY to stdout BEFORE publishing the marker.
    std::cout << "FAULT_READY\n" << std::flush;
    // 3) atomically publish the marker via rename -> the parent only ever observes a
    //    complete final marker, and only after FAULT_READY was flushed.
    if (::rename(c->marker_tmp.c_str(), c->marker_final.c_str()) != 0) {
        std::_Exit(kUnexpected);
    }
    // 4) park until the parent's SIGKILL.
    for (;;) {
        ::pause();
    }
}

std::optional<ps::StorePhase> phase_from_name(std::string_view name) {
    using P = ps::StorePhase;
    if (name == "WorkflowDirParentFsynced") return P::WorkflowDirParentFsynced;
    if (name == "CheckpointDirParentFsynced") return P::CheckpointDirParentFsynced;
    if (name == "StageDirCreated") return P::StageDirCreated;
    if (name == "RecordFileFsynced") return P::RecordFileFsynced;
    if (name == "SlotFilesFsynced") return P::SlotFilesFsynced;
    if (name == "ManifestFileFsynced") return P::ManifestFileFsynced;
    if (name == "StageDirFsynced") return P::StageDirFsynced;
    if (name == "GenerationRenamed") return P::GenerationRenamed;
    if (name == "CheckpointDirFsyncedAfterGeneration") {
        return P::CheckpointDirFsyncedAfterGeneration;
    }
    if (name == "PointerFileFsynced") return P::PointerFileFsynced;
    if (name == "PointerRenamed") return P::PointerRenamed;
    if (name == "CheckpointDirFsyncedAfterPointer") return P::CheckpointDirFsyncedAfterPointer;
    return std::nullopt;
}

#endif // __linux__

// ---- normal modes ----------------------------------------------------------

int mode_probe(const std::filesystem::path &case_dir) {
    if (!store_shape_ok(case_dir)) {
        return kUnexpected; // missing / non-dir / symlink / unsafe store root != SKIP
    }
    auto store = ps::IntegrityPayloadStore::open(store_root(case_dir), base_options());
    if (store.has_value()) {
        return kOk;
    }
    if (store.error() == ps::PayloadStoreError::UnsupportedPlatform ||
        store.error() == ps::PayloadStoreError::UnsupportedFilesystem) {
        return kUnsupported;
    }
    return kUnexpected;
}

int mode_publish(const std::filesystem::path &case_dir, std::uint64_t expected,
                 std::string_view tag) {
    auto payload = payload_for_tag(tag);
    if (!payload.has_value()) {
        return kUsage;
    }
    auto store = ps::IntegrityPayloadStore::open(store_root(case_dir), base_options());
    if (!store.has_value()) {
        return result_exit(std::unexpected(store.error()));
    }
    std::vector<ps::Slot> slots = {ps::Slot{PayloadSlotId{5}, *payload}};
    auto r = store->publish_available(kWf, kCkpt, expected, make_record(), slots,
                                      std::span<const std::uint8_t, 16>(test_key_id()), test_key());
    return result_exit(r);
}

int mode_consume(const std::filesystem::path &case_dir, std::uint64_t expected) {
    auto store = ps::IntegrityPayloadStore::open(store_root(case_dir), base_options());
    if (!store.has_value()) {
        return result_exit(std::unexpected(store.error()));
    }
    auto r = store->mark_consumed(kWf, kCkpt, expected,
                                  std::span<const std::uint8_t, 16>(test_key_id()), test_key());
    return result_exit(r);
}

int mode_load(const std::filesystem::path &case_dir) {
    if (!store_shape_ok(case_dir)) {
        return kUnexpected; // missing/unsafe store root is a setup error, never SKIP
    }
    auto store = ps::IntegrityPayloadStore::open(store_root(case_dir), base_options());
    if (!store.has_value()) {
        if (store.error() == ps::PayloadStoreError::UnsupportedPlatform ||
            store.error() == ps::PayloadStoreError::UnsupportedFilesystem) {
            return kUnsupported;
        }
        return kUnexpected;
    }
    auto r = store->load(kWf, kCkpt, std::span<const std::uint8_t, 16>(test_key_id()), test_key());
    if (!r.has_value()) {
        if (r.error() == ps::PayloadStoreError::NotFound) {
            std::cout << "{\"state\":\"notfound\"}\n" << std::flush;
            return kNotFound;
        }
        return kUnexpected;
    }
    if (std::holds_alternative<ps::ResolvedAvailable>(*r)) {
        const auto &av = std::get<ps::ResolvedAvailable>(*r);
        std::string hex;
        for (const auto &s : av.slots) {
            if (s.slot.value == 5) {
                hex = to_hex(s.payload);
            }
        }
        std::cout << "{\"state\":\"available\",\"generation\":" << av.generation
                  << ",\"slot_payload_hex\":\"" << hex << "\"}\n"
                  << std::flush;
        return kOk;
    }
    const auto &cs = std::get<ps::ResolvedConsumed>(*r);
    std::cout << "{\"state\":\"consumed\",\"generation\":" << cs.generation
              << ",\"consumed_generation\":" << cs.consumed_generation << "}\n"
              << std::flush;
    return kOk;
}

// stdin-gated CAS worker: prints CAS_READY, blocks on one stdin line, then publishes.
int mode_race_publish(const std::filesystem::path &case_dir, std::uint64_t expected,
                      std::string_view tag) {
    auto payload = payload_for_tag(tag);
    if (!payload.has_value()) {
        return kUsage;
    }
    auto store = ps::IntegrityPayloadStore::open(store_root(case_dir), base_options());
    if (!store.has_value()) {
        return result_exit(std::unexpected(store.error()));
    }
    std::cout << "CAS_READY\n" << std::flush;
    std::string line;
    std::getline(std::cin, line); // released by the parent simultaneously
    std::vector<ps::Slot> slots = {ps::Slot{PayloadSlotId{5}, *payload}};
    auto r = store->publish_available(kWf, kCkpt, expected, make_record(), slots,
                                      std::span<const std::uint8_t, 16>(test_key_id()), test_key());
    return result_exit(r);
}

#if defined(__linux__)
int mode_crash(const std::filesystem::path &case_dir, bool consume, std::string_view phase_name,
               std::uint64_t expected, std::string_view tag) {
    auto phase = phase_from_name(phase_name);
    if (!phase.has_value()) {
        return kUsage;
    }
    std::vector<std::uint8_t> payload;
    if (!consume) {
        auto p = payload_for_tag(tag);
        if (!p.has_value()) {
            return kUsage;
        }
        payload = std::move(*p);
    }
    CrashContext ctx;
    ctx.block_at = *phase;
    ctx.marker_final = marker_path(case_dir).string();
    ctx.marker_tmp = ctx.marker_final + ".tmp";
    ctx.json = std::string("{\"operation\":\"") + (consume ? "consume" : "publish") +
               "\",\"phase\":\"" + std::string(phase_name) +
               "\",\"expected_generation\":" + std::to_string(expected) + "}";
    ps::StoreOptions o = base_options();
    o.observer = &crash_observer;
    o.observer_context = &ctx;
    auto store = ps::IntegrityPayloadStore::open(store_root(case_dir), o);
    if (!store.has_value()) {
        if (store.error() == ps::PayloadStoreError::UnsupportedPlatform ||
            store.error() == ps::PayloadStoreError::UnsupportedFilesystem) {
            return kUnsupported;
        }
        return kUnexpected;
    }
    if (consume) {
        auto r = store->mark_consumed(kWf, kCkpt, expected,
                                      std::span<const std::uint8_t, 16>(test_key_id()), test_key());
        return result_exit(r); // reached only if the phase was never hit
    }
    std::vector<ps::Slot> slots = {ps::Slot{PayloadSlotId{5}, payload}};
    auto r = store->publish_available(kWf, kCkpt, expected, make_record(), slots,
                                      std::span<const std::uint8_t, 16>(test_key_id()), test_key());
    return result_exit(r);
}
#endif // __linux__

} // namespace

int main(int argc, char **argv) {
    if (argc < 3) {
        std::cerr << "usage: payload_store_worker <mode> <case-dir> [...]\n";
        return kUsage;
    }
    const std::string mode = argv[1];
    const std::filesystem::path case_dir = argv[2];

    if (mode == "probe" && argc == 3) {
        return mode_probe(case_dir);
    }
    if (mode == "publish" && argc == 5) {
        return mode_publish(case_dir, std::strtoull(argv[3], nullptr, 10), argv[4]);
    }
    if (mode == "consume" && argc == 4) {
        return mode_consume(case_dir, std::strtoull(argv[3], nullptr, 10));
    }
    if (mode == "load" && argc == 3) {
        return mode_load(case_dir);
    }
    if (mode == "race-publish" && argc == 5) {
        return mode_race_publish(case_dir, std::strtoull(argv[3], nullptr, 10), argv[4]);
    }
#if defined(__linux__)
    if (mode == "crash-publish" && argc == 6) {
        return mode_crash(case_dir, /*consume=*/false, argv[3], std::strtoull(argv[4], nullptr, 10),
                          argv[5]);
    }
    if (mode == "crash-consume" && argc == 5) {
        return mode_crash(case_dir, /*consume=*/true, argv[3], std::strtoull(argv[4], nullptr, 10),
                          "");
    }
#else
    // Non-Linux: the store is unsupported; crash modes fail closed uniformly.
    if (mode == "crash-publish" || mode == "crash-consume") {
        return kUnsupported;
    }
#endif
    return kUsage;
}
