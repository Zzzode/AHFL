#pragma once

// RFC 0026 KR6.5 E4-B2-B1a: the integrity-only durable-resume payload STORE core.
// This is the Linux, POSIX-filesystem transaction layer that publishes, loads, and
// tombstones the B0 artifact codecs (slot / commit_manifest / generation pointer)
// as immutable per-generation directories committed by an atomic pointer swap.
//
// It is INTEGRITY-ONLY (inherited from B0): every artifact is HMAC-authenticated
// under a caller-supplied key + key_id, but nothing is encrypted at rest and there
// is NO rollback protection (an actor with filesystem write access can replay a
// whole older authenticated generation). The store enforces guarantees == 0 on
// every published record; it provides NO confidentiality / KMS / keyring / lease /
// at-most-once side-effect (those are future B2-D / KMS gates). It has NO production
// caller yet (the first is the future B2-D host); this slice ships the store + unit
// evidence only. The B1b cross-process SIGKILL/reopen worker is a SEPARATE slice.
//
// Durability model (single host, Linux only): each publish stages a private
// `gen-<M>.stage` directory, fsyncs every artifact file and the stage directory,
// atomically publishes it to an immutable `gen-<M>` via `renameat2(RENAME_NOREPLACE)`
// + parent fsync, then flips the checkpoint `pointer` via O_EXCL temp + `renameat` +
// parent fsync. The pointer swap is the single atomic commit point. A crash before
// the pointer swap leaves the old generation live (or NotFound for a first publish);
// a crash after it leaves the new generation live. Writers serialize through a
// `flock(LOCK_EX)` on a per-checkpoint lock file (auto-released by the kernel on
// process death, including SIGKILL); readers are lock-free and take a single
// authenticated pointer read as their snapshot linearization point. Concurrent
// writers use an `expected_current_generation` CAS: exactly one commits N+1.
//
// HONEST LIMITS: SIGKILL leaves the kernel page cache intact, so the crash tests
// prove atomic-snapshot / reopen ordering, NOT power-loss fsync durability. The
// store is Linux-only (guarded `#ifdef __linux__`); every operation returns
// `UnsupportedPlatform` elsewhere. Durable filesystems are an explicit allowlist
// (EXT-family / XFS / Btrfs); tmpfs / NFS / SMB / FUSE / overlay / unknown are
// rejected with `UnsupportedFilesystem`.

#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <variant>
#include <vector>

#include "ahfl/compiler/ir/core_ir.hpp"                 // ir::core::CoreWorkflowId
#include "runtime/engine/core_wasm_resume_record.hpp"   // CoreWasmResumeRecord, PayloadSlotId
#include "runtime/engine/payload_store_codec.hpp"       // PayloadStoreError, ResumeCheckpointId

namespace ahfl::runtime::payload_store {

// A transaction milestone reached AFTER the corresponding syscall SUCCEEDS. The
// observer (below) is invoked at each milestone for test instrumentation only; it
// cannot cancel or alter the transaction. A Consumed publish emits the applicable
// subsequence (no RecordFileFsynced / SlotFilesFsynced). First-create milestones
// fire only when this call actually created the directory (not on an EEXIST race).
enum class StorePhase : std::uint8_t {
    WorkflowDirParentFsynced,
    CheckpointDirParentFsynced,
    StageDirCreated,
    RecordFileFsynced,
    SlotFilesFsynced,
    ManifestFileFsynced,
    StageDirFsynced,
    GenerationRenamed,
    CheckpointDirFsyncedAfterGeneration,
    PointerFileFsynced,
    PointerRenamed,
    CheckpointDirFsyncedAfterPointer,
};

// Test-only, exception-free observation hook. Observes / may block at a milestone;
// it can neither return a value nor influence whether the transaction commits. The
// `context` lifetime is caller-owned. Default null = no observation.
using TransactionObserver = void (*)(StorePhase phase, void *context) noexcept;

// Caller-supplied conservative byte caps for the two attacker-influenced artifact
// classes. The manifest and pointer caps are derived internally (the manifest from
// a checked grammar formula over `max_record_artifact_bytes`; the pointer is the
// fixed grammar maximum). A zero or internally-overflowing cap fails `open`.
struct StoreLimits {
    std::uint64_t max_record_artifact_bytes{0};
    std::uint64_t max_slot_artifact_bytes{0};
};

struct StoreOptions {
    StoreLimits limits{};
    TransactionObserver observer{nullptr};
    void *observer_context{nullptr};
};

// One slot to publish: a distinct payload-slot id and its opaque payload bytes. The
// caller supplies a DISTINCT set (a ledger may reference the same slot id from
// multiple coordinates, but the store stores one artifact per distinct id); the
// store canonically sorts by id and rejects duplicate / missing / extra ids against
// the record-derived expected set with `SlotSetMismatch`.
struct Slot {
    PayloadSlotId slot{};
    std::span<const std::uint8_t> payload{};
};

// A loaded slot: the distinct id and its owned, authenticated payload bytes.
struct ResolvedSlot {
    PayloadSlotId slot{};
    std::vector<std::uint8_t> payload{};
};

// The result of loading a live Available generation: its number, the fully decoded +
// authenticated A1 record (so the B2-D host need not decode it again), and the owned
// authenticated slot payloads.
struct ResolvedAvailable {
    std::uint64_t generation{0};
    core_wasm_resume::CoreWasmResumeRecord record{};
    std::vector<ResolvedSlot> slots{};
};

// The result of loading a live Consumed tombstone: its number and the previous
// Available generation it consumed.
struct ResolvedConsumed {
    std::uint64_t generation{0};
    std::uint64_t consumed_generation{0};
};

// A live generation is either Available or Consumed. Absence of any published
// generation is reported as `std::unexpected(PayloadStoreError::NotFound)`, never a
// variant arm.
using ResolvedGeneration = std::variant<ResolvedAvailable, ResolvedConsumed>;

// A two-phase, no-TOCTOU handle over a live Available generation (B2-D D1a). The
// atomic `load()` admits the record AND all slots together, which cannot express the
// B2-D admission order (record HMAC -> digests -> coordinates must run and pass
// BEFORE slot admission). `open_snapshot` performs PHASE 1 only -- it pins the live
// generation directory fd and authenticates the pointer, manifest, and A1 record --
// and returns this handle exposing narrow `record()` / `generation()` views for the
// caller's digest + coordinate gates. Only after those gates pass does the caller
// call `std::move(snapshot).admit_slots(...)` for PHASE 2: it re-borrows the key,
// cross-checks it against the snapshot's non-secret key_id/generation authority, and
// admits the exact `{entry_input_slot} U memo result slots` set on the SAME pinned
// fd (never reopening the generation by name). The handle holds only NON-secret
// authenticated metadata plus the pinned fd; it NEVER stores key bytes and exposes
// no raw fd / manifest / pointer accessor. Move-only: an explicit noexcept move
// transfers the fd and leaves the source with fd = -1; the destructor closes it
// exactly once. `admit_slots` is ONE-SHOT: it consumes the pin (fd -> local RAII,
// member fd -> -1) before any work, so it runs at most once whether it succeeds or
// fails; a second call on a consumed/moved-from handle fails closed with
// `StateMismatch`.
class ResumeSnapshot {
  public:
    ResumeSnapshot(const ResumeSnapshot &) = delete;
    ResumeSnapshot &operator=(const ResumeSnapshot &) = delete;
    ResumeSnapshot(ResumeSnapshot &&other) noexcept;
    ResumeSnapshot &operator=(ResumeSnapshot &&other) noexcept;
    ~ResumeSnapshot();

    // Phase-1 views for the caller's digest + coordinate gates.
    [[nodiscard]] const core_wasm_resume::CoreWasmResumeRecord &record() const noexcept;
    [[nodiscard]] std::uint64_t generation() const noexcept;

    // The AUTHENTICATED checkpoint namespace this snapshot belongs to. Both
    // values come from the HMAC-authenticated commit manifest, whose (wf, ckpt)
    // `read_live_pointer` cross-checks against the open_snapshot arguments
    // (CrossCheckpointRejected on any mismatch), so they -- never a caller
    // re-supply -- are the namespace SSOT for downstream token authorities.
    [[nodiscard]] ir::core::CoreWorkflowId workflow_id() const noexcept;
    [[nodiscard]] ResumeCheckpointId checkpoint_id() const noexcept;

    // Phase 2 (one-shot; consumes the pin). Admits the exact distinct slot set on
    // the pinned generation fd after re-borrowing + cross-checking `(key_id, key)`.
    // Returns the full `ResolvedAvailable` (generation + record + slots) moved out.
    // A second call on a consumed/moved-from snapshot returns `StateMismatch`.
    [[nodiscard]] std::expected<ResolvedAvailable, PayloadStoreError>
    admit_slots(std::span<const std::uint8_t, 16> expected_key_id,
                std::span<const std::uint8_t> key) &&;

  private:
    ResumeSnapshot() noexcept = default;
    friend class IntegrityPayloadStore;

    int gen_dir_fd_{-1};                              // pinned generation-dir fd
    std::uint64_t root_dev_{0};                       // read_artifact st_dev guard
    StoreLimits limits_{};                            // max_slot_artifact_bytes, etc.
    payload_store::GenerationPointer pointer_{};      // authenticated, non-secret
    payload_store::CommitManifest manifest_{};        // authenticated, non-secret (has generation)
    std::uint64_t manifest_artifact_size_{0};         // actual manifest length read in phase 1
    core_wasm_resume::CoreWasmResumeRecord record_{}; // authenticated, non-secret
};

// The integrity-only durable-resume payload store. Move-only; owns a pinned
// directory fd for the trusted root. All identity is derived from typed ids into
// fixed-width lowercase-hex path components under that root fd (openat + O_NOFOLLOW),
// so no caller-supplied path or name ever participates in resolution below the root.
class IntegrityPayloadStore {
  public:
    IntegrityPayloadStore(const IntegrityPayloadStore &) = delete;
    IntegrityPayloadStore &operator=(const IntegrityPayloadStore &) = delete;
    IntegrityPayloadStore(IntegrityPayloadStore &&other) noexcept;
    IntegrityPayloadStore &operator=(IntegrityPayloadStore &&other) noexcept;
    ~IntegrityPayloadStore();

    // Open a store rooted at a pre-existing trusted directory. TRUST BOUNDARY: the
    // parent / prefix components of `root` are resolved by the ordinary kernel path
    // resolver at open time and are the CALLER's trusted boundary -- a symlink
    // anywhere in that prefix is outside this store's protection. This store
    // guarantees safety ONLY for the final `root` component (opened O_NOFOLLOW) and
    // for everything BELOW the pinned root directory fd (every descendant reached by
    // openat + O_NOFOLLOW, never by re-resolving a path). On Linux the root is opened
    // O_DIRECTORY|O_NOFOLLOW, validated (S_ISDIR, owned by the effective uid, not
    // group/other-writable) and its filesystem type is checked against the durable
    // allowlist; the manifest cap formula is validated for overflow. On any non-Linux
    // platform this returns UnsupportedPlatform.
    [[nodiscard]] static std::expected<IntegrityPayloadStore, PayloadStoreError>
    open(const std::filesystem::path &root, StoreOptions options);

    // Publish a new Available generation. CAS: `expected_current_generation` must
    // equal the live pointer's generation (0 requires no pointer yet); the store
    // assigns M = expected+1, SETS `record.auth_header = {alg 1, key_id, M}`,
    // REQUIRES `record.guarantees == 0`, encodes the record + slot artifacts + the
    // commit manifest, publishes the immutable generation, and swaps the pointer.
    // Returns the published generation M. Publishing over a live Consumed tombstone
    // does not resurrect it: it returns `Consumed`.
    [[nodiscard]] std::expected<std::uint64_t, PayloadStoreError>
    publish_available(ir::core::CoreWorkflowId wf, ResumeCheckpointId ckpt,
                      std::uint64_t expected_current_generation,
                      core_wasm_resume::CoreWasmResumeRecord record, std::span<const Slot> slots,
                      std::span<const std::uint8_t, 16> key_id, std::span<const std::uint8_t> key);

    // Tombstone the live Available generation. CAS as above; the live generation is
    // fully admitted first, then a manifest-only Consumed generation M = N+1 is
    // published binding consumed_generation = N and the live Available manifest
    // digest. Only Available -> Consumed is allowed; a live Consumed returns
    // `Consumed`. Returns the published generation M.
    [[nodiscard]] std::expected<std::uint64_t, PayloadStoreError>
    mark_consumed(ir::core::CoreWorkflowId wf, ResumeCheckpointId ckpt,
                  std::uint64_t expected_current_generation,
                  std::span<const std::uint8_t, 16> key_id, std::span<const std::uint8_t> key);

    // Load the live generation. Lock-free: a single authenticated pointer read is
    // the snapshot linearization point. Returns the fully authenticated Available or
    // Consumed result, or `NotFound` when no pointer exists.
    [[nodiscard]] std::expected<ResolvedGeneration, PayloadStoreError>
    load(ir::core::CoreWorkflowId wf, ResumeCheckpointId ckpt,
         std::span<const std::uint8_t, 16> key_id, std::span<const std::uint8_t> key);

    // Open a staged, two-phase snapshot of the live generation (B2-D D1a). PHASE 1:
    // lock-free single authenticated pointer read (the snapshot linearization point),
    // pins the live generation dir fd, and authenticates the pointer + manifest +
    // A1 record WITHOUT admitting any slot. A live Consumed tombstone returns
    // `Consumed` before any record admission; `NotFound` when no pointer exists. The
    // returned handle exposes `record()`/`generation()` for the caller's digest +
    // coordinate gates; the caller then calls `std::move(snapshot).admit_slots(...)`
    // for phase 2. `key` is BORROWED for phase-1 authentication and never stored.
    [[nodiscard]] std::expected<ResumeSnapshot, PayloadStoreError>
    open_snapshot(ir::core::CoreWorkflowId wf, ResumeCheckpointId ckpt,
                  std::span<const std::uint8_t, 16> key_id, std::span<const std::uint8_t> key);

  private:
    IntegrityPayloadStore() noexcept = default;

    int root_fd_{-1};
    std::uint64_t root_dev_{0}; // st_dev of the root; every descendant must match
    StoreLimits limits_{};
    std::uint64_t manifest_cap_{0}; // derived, checked at open
    TransactionObserver observer_{nullptr};
    void *observer_context_{nullptr};
};

} // namespace ahfl::runtime::payload_store
