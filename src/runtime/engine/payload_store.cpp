#include "runtime/engine/payload_store.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <utility>

#include "base/support/sha256.hpp"

#if defined(__linux__)
#include <cerrno>
#include <dirent.h>
#include <fcntl.h>
#include <linux/magic.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <unistd.h>
#endif

namespace ahfl::runtime::payload_store {
namespace {

using core_wasm_resume::CoreWasmResumeRecord;
using core_wasm_resume::PayloadSlotId;
using ir::core::CoreWorkflowId;

// A record's smallest per-memo footprint on the wire is one entry, but the record
// cap is a caller-supplied artifact byte budget; each distinct slot costs at least
// one memo reference. A memo entry is at least 5 ULEB bytes (A1 kMinMemoEntryBytes),
// so a record of R bytes references at most R/5 memo entries, hence at most R/5
// distinct slots. This is the checked bound used to size the manifest cap.
constexpr std::uint64_t kMinMemoEntryBytes = 5;

// The commit_manifest Available body fixed overhead (magic..slot_count) plus the
// tail, excluding the variable slot_meta array:
//   magic(6)+fmt(1)+wf(<=5)+ckpt(<=10)+generation(8)+state(1)+record_sha256(64)+
//   record_len(<=10 ULEB)+slot_count(<=10 ULEB)+auth_header(25)+tag(32).
constexpr std::uint64_t kManifestFixedOverhead = 6 + 1 + 5 + 10 + 8 + 1 + 64 + 10 + 10 + 25 + 32;
// Each SlotMeta on the wire: slot(<=10 ULEB)+slot_artifact_sha256(64)+payload_len(<=10 ULEB).
constexpr std::uint64_t kManifestPerSlotMeta = 10 + 64 + 10;

// Checked multiply/add helpers: return nullopt on overflow.
[[nodiscard]] std::optional<std::uint64_t> checked_mul(std::uint64_t a, std::uint64_t b) noexcept {
    if (a == 0 || b == 0) {
        return std::uint64_t{0};
    }
    if (a > std::numeric_limits<std::uint64_t>::max() / b) {
        return std::nullopt;
    }
    return a * b;
}

[[nodiscard]] std::optional<std::uint64_t> checked_add(std::uint64_t a, std::uint64_t b) noexcept {
    if (a > std::numeric_limits<std::uint64_t>::max() - b) {
        return std::nullopt;
    }
    return a + b;
}

// Derive the conservative manifest artifact byte cap from the record cap. Returns
// nullopt if the formula overflows u64 (caller maps to SizeCapExceeded at open).
[[nodiscard]] std::optional<std::uint64_t>
derive_manifest_cap(std::uint64_t max_record_artifact_bytes) noexcept {
    const std::uint64_t max_slots = max_record_artifact_bytes / kMinMemoEntryBytes;
    auto slots_bytes = checked_mul(max_slots, kManifestPerSlotMeta);
    if (!slots_bytes.has_value()) {
        return std::nullopt;
    }
    return checked_add(kManifestFixedOverhead, *slots_bytes);
}

} // namespace

#if defined(__linux__)
namespace {

// Move-only RAII guard for a POSIX file descriptor (mirrors process.cpp:FdGuard).
class FdGuard {
  public:
    FdGuard() noexcept = default;
    explicit FdGuard(int fd) noexcept : fd_(fd) {}
    FdGuard(const FdGuard &) = delete;
    FdGuard &operator=(const FdGuard &) = delete;
    FdGuard(FdGuard &&other) noexcept : fd_(other.fd_) { other.fd_ = -1; }
    FdGuard &operator=(FdGuard &&other) noexcept {
        if (this != &other) {
            reset();
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }
    ~FdGuard() { reset(); }

    [[nodiscard]] int get() const noexcept { return fd_; }
    [[nodiscard]] bool valid() const noexcept { return fd_ >= 0; }
    [[nodiscard]] int release() noexcept {
        const int fd = fd_;
        fd_ = -1;
        return fd;
    }
    void reset() noexcept {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

  private:
    int fd_{-1};
};

void notify(TransactionObserver observer, void *context, StorePhase phase) noexcept {
    if (observer != nullptr) {
        observer(phase, context);
    }
}

// Durable filesystem superblock magics. 0xEF53 is the shared ext2/ext3/ext4 magic --
// named EXT-family, not claimed as ext4 specifically.
[[nodiscard]] bool is_durable_fs_magic(decltype(statfs::f_type) t) noexcept {
    const auto magic = static_cast<unsigned long long>(t);
    return magic == static_cast<unsigned long long>(EXT4_SUPER_MAGIC) || // EXT-family 0xEF53
           magic == static_cast<unsigned long long>(XFS_SUPER_MAGIC) ||  // 0x58465342
           magic == static_cast<unsigned long long>(BTRFS_SUPER_MAGIC);  // 0x9123683E
}

// close() called EXACTLY once. On Linux a fd is released even when close() returns
// an error (except EBADF), so retrying on EINTR could close an unrelated fd that was
// concurrently reused. EINTR therefore means "already closed" (success); any other
// error is a genuine failure. fsync (before close) is the durability gate.
[[nodiscard]] bool close_checked(int fd) noexcept {
    if (::close(fd) != 0) {
        return errno == EINTR;
    }
    return true;
}

// write(2) loop handling EINTR and short writes (mirrors process.cpp:write_all). A
// write() returning 0 on a non-empty buffer makes no progress and is treated as a
// failure rather than looping forever.
[[nodiscard]] bool write_all(int fd, std::span<const std::uint8_t> data) noexcept {
    while (!data.empty()) {
        const ssize_t n = ::write(fd, data.data(), data.size());
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (n == 0) {
            return false; // no progress on a non-empty buffer
        }
        data = data.subspan(static_cast<std::size_t>(n));
    }
    return true;
}

// read(2) exactly `want` bytes into out. The initial allocation is guarded: an
// out-of-memory / over-length allocation for an untrusted artifact must never
// terminate the process, so it is caught and reported distinctly from an I/O / EOF
// failure. Returns: Ok, AllocFailed (map to SizeCapExceeded), or IoFailed (map to
// StateMismatch).
enum class ReadStatus { Ok, AllocFailed, IoFailed };

[[nodiscard]] ReadStatus read_exact(int fd, std::vector<std::uint8_t> &out,
                                    std::size_t want) noexcept {
    try {
        out.resize(want);
    } catch (...) {
        return ReadStatus::AllocFailed;
    }
    std::size_t got = 0;
    while (got < want) {
        const ssize_t n = ::read(fd, out.data() + got, want - got);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return ReadStatus::IoFailed;
        }
        if (n == 0) {
            return ReadStatus::IoFailed; // premature EOF
        }
        got += static_cast<std::size_t>(n);
    }
    return ReadStatus::Ok;
}

// fsync retrying on EINTR.
[[nodiscard]] bool fsync_checked(int fd) noexcept {
    while (::fsync(fd) != 0) {
        if (errno == EINTR) {
            continue;
        }
        return false;
    }
    return true;
}

// Fixed-width lowercase-hex of a u32 (8 chars) / u64 (16 chars). No caller string
// ever participates; every path component is generated from a typed id.
[[nodiscard]] std::string hex32(std::uint32_t v) {
    char buf[9];
    std::snprintf(buf, sizeof(buf), "%08x", v);
    return std::string(buf, 8);
}

[[nodiscard]] std::string hex64(std::uint64_t v) {
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(v));
    return std::string(buf, 16);
}

[[nodiscard]] std::string workflow_dir_name(CoreWorkflowId wf) { return "wf-" + hex32(wf.value); }
[[nodiscard]] std::string checkpoint_dir_name(ResumeCheckpointId ckpt) {
    return "ckpt-" + hex64(ckpt.value);
}
[[nodiscard]] std::string generation_dir_name(std::uint64_t gen) { return "gen-" + hex64(gen); }
[[nodiscard]] std::string generation_stage_name(std::uint64_t gen) {
    return "gen-" + hex64(gen) + ".stage";
}
[[nodiscard]] std::string slot_file_name(PayloadSlotId slot) {
    return "slot-" + hex64(slot.value);
}

} // namespace
#endif // __linux__

// The remaining implementation is Linux-only. On any other platform every operation
// fails closed with UnsupportedPlatform and the store holds no resources.
#if !defined(__linux__)

std::expected<IntegrityPayloadStore, PayloadStoreError>
IntegrityPayloadStore::open(const std::filesystem::path &, StoreOptions) {
    return std::unexpected(PayloadStoreError::UnsupportedPlatform);
}

std::expected<std::uint64_t, PayloadStoreError> IntegrityPayloadStore::publish_available(
    ir::core::CoreWorkflowId, ResumeCheckpointId, std::uint64_t, CoreWasmResumeRecord,
    std::span<const Slot>, std::span<const std::uint8_t, 16>, std::span<const std::uint8_t>) {
    return std::unexpected(PayloadStoreError::UnsupportedPlatform);
}

std::expected<std::uint64_t, PayloadStoreError> IntegrityPayloadStore::mark_consumed(
    ir::core::CoreWorkflowId, ResumeCheckpointId, std::uint64_t, std::span<const std::uint8_t, 16>,
    std::span<const std::uint8_t>) {
    return std::unexpected(PayloadStoreError::UnsupportedPlatform);
}

std::expected<ResolvedGeneration, PayloadStoreError>
IntegrityPayloadStore::load(ir::core::CoreWorkflowId, ResumeCheckpointId,
                            std::span<const std::uint8_t, 16>, std::span<const std::uint8_t>) {
    return std::unexpected(PayloadStoreError::UnsupportedPlatform);
}

#endif // !__linux__

#if defined(__linux__)
namespace {

// Structured open-existing outcome: distinguishes a genuinely absent directory
// (the openat itself failed ENOENT) from an existing-but-unsafe one (any post-open
// validation failure). A global-errno read after validation is unreliable, so the
// distinction is captured here.
enum class OpenDirStatus { Opened, Missing, Unsafe };
struct OpenDirResult {
    OpenDirStatus status{OpenDirStatus::Unsafe};
    FdGuard fd{};
};

// Open a directory child by name under a parent dirfd, O_NOFOLLOW (reject symlink),
// and verify S_ISDIR + same-device as the root + owned by the effective uid + not
// group/other-writable.
[[nodiscard]] OpenDirResult open_dir_existing(int parent_fd, const std::string &name,
                                             std::uint64_t root_dev) {
    const int fd = ::openat(parent_fd, name.c_str(),
                            O_DIRECTORY | O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) {
        return OpenDirResult{errno == ENOENT ? OpenDirStatus::Missing : OpenDirStatus::Unsafe, {}};
    }
    FdGuard guard(fd);
    struct stat st{};
    if (::fstat(fd, &st) != 0 || !S_ISDIR(st.st_mode) ||
        static_cast<std::uint64_t>(st.st_dev) != root_dev || st.st_uid != ::geteuid() ||
        (st.st_mode & (S_IWGRP | S_IWOTH)) != 0) {
        return OpenDirResult{OpenDirStatus::Unsafe, {}};
    }
    return OpenDirResult{OpenDirStatus::Opened, std::move(guard)};
}

// Thin wrapper for callers that only need "opened-and-safe or not" (any Missing /
// Unsafe collapses to nullopt).
[[nodiscard]] std::optional<FdGuard> open_dir_checked(int parent_fd, const std::string &name,
                                                     std::uint64_t root_dev) {
    OpenDirResult r = open_dir_existing(parent_fd, name, root_dev);
    if (r.status != OpenDirStatus::Opened) {
        return std::nullopt;
    }
    return std::move(r.fd);
}

// Read+authenticate a regular artifact file by name under a dirfd. Opens
// O_RDONLY|O_NONBLOCK|O_NOFOLLOW|O_CLOEXEC (O_NONBLOCK prevents a FIFO open blocking),
// same-fd fstat requires S_ISREG + same-device + euid owner + not group/other-writable
// + st_nlink (see kind) + size <= cap, then reads exactly the file into `out` and
// confirms EOF (a concurrent grow after fstat must not be read as a whole artifact).
// Returns a PayloadStoreError on any failure. Only a Pointer ENOENT maps to NotFound;
// every other missing/atypical artifact is StateMismatch.
enum class ArtifactReadKind { Immutable, Pointer };

[[nodiscard]] std::expected<std::vector<std::uint8_t>, PayloadStoreError>
read_artifact(int dir_fd, const std::string &name, std::uint64_t root_dev, std::uint64_t cap,
              ArtifactReadKind kind) {
    const int fd = ::openat(dir_fd, name.c_str(),
                            O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) {
        if (errno == ENOENT) {
            return std::unexpected(kind == ArtifactReadKind::Pointer
                                       ? PayloadStoreError::NotFound
                                       : PayloadStoreError::StateMismatch);
        }
        return std::unexpected(PayloadStoreError::StateMismatch);
    }
    FdGuard guard(fd);
    struct stat st{};
    if (::fstat(fd, &st) != 0) {
        return std::unexpected(PayloadStoreError::StateMismatch);
    }
    if (!S_ISREG(st.st_mode) || static_cast<std::uint64_t>(st.st_dev) != root_dev) {
        return std::unexpected(PayloadStoreError::StateMismatch);
    }
    if (st.st_uid != ::geteuid() || (st.st_mode & (S_IWGRP | S_IWOTH)) != 0) {
        return std::unexpected(PayloadStoreError::StateMismatch);
    }
    // A concurrent pointer rename can leave an already-open old inode at nlink 0;
    // the pointer therefore accepts 0 or 1 and rejects > 1. Immutable artifacts
    // require exactly 1.
    if (kind == ArtifactReadKind::Immutable) {
        if (st.st_nlink != 1) {
            return std::unexpected(PayloadStoreError::StateMismatch);
        }
    } else {
        if (st.st_nlink > 1) {
            return std::unexpected(PayloadStoreError::StateMismatch);
        }
    }
    if (st.st_size < 0 ||
        static_cast<std::uint64_t>(st.st_size) > std::numeric_limits<std::size_t>::max() ||
        static_cast<std::uint64_t>(st.st_size) > cap) {
        return std::unexpected(PayloadStoreError::SizeCapExceeded);
    }
    std::vector<std::uint8_t> bytes;
    switch (read_exact(fd, bytes, static_cast<std::size_t>(st.st_size))) {
    case ReadStatus::Ok:
        break;
    case ReadStatus::AllocFailed:
        return std::unexpected(PayloadStoreError::SizeCapExceeded);
    case ReadStatus::IoFailed:
        return std::unexpected(PayloadStoreError::StateMismatch);
    }
    // Confirm EOF: any byte beyond the fstat size means the file grew concurrently
    // (or fstat was stale); it is not the exact whole artifact.
    std::uint8_t extra = 0;
    while (true) {
        const ssize_t n = ::read(fd, &extra, 1);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return std::unexpected(PayloadStoreError::StateMismatch);
        }
        if (n > 0) {
            return std::unexpected(PayloadStoreError::StateMismatch);
        }
        break; // n == 0 => EOF as expected
    }
    return bytes;
}

// Write one artifact into a stage dirfd via O_CREAT|O_EXCL, write_all, fsync, close.
[[nodiscard]] bool write_artifact(int stage_fd, const std::string &name,
                                  std::span<const std::uint8_t> bytes) noexcept {
    const int fd = ::openat(stage_fd, name.c_str(),
                            O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) {
        return false;
    }
    bool ok = write_all(fd, bytes) && fsync_checked(fd);
    ok = close_checked(fd) && ok;
    return ok;
}

// SHA-256 (lowercase hex) of a whole artifact, materialized into a DigestHex.
[[nodiscard]] std::optional<DigestHex> artifact_digest_hex(std::span<const std::uint8_t> bytes) {
    std::string hex;
    try {
        hex = support::sha256_hex(bytes);
    } catch (const std::length_error &) {
        return std::nullopt;
    }
    if (hex.size() != 64) {
        return std::nullopt;
    }
    DigestHex out{};
    std::copy(hex.begin(), hex.end(), out.begin());
    return out;
}

} // namespace
#endif // __linux__

#if defined(__linux__)

std::expected<IntegrityPayloadStore, PayloadStoreError>
IntegrityPayloadStore::open(const std::filesystem::path &root, StoreOptions options) {
    if (options.limits.max_record_artifact_bytes == 0 ||
        options.limits.max_slot_artifact_bytes == 0) {
        return std::unexpected(PayloadStoreError::SizeCapExceeded);
    }
    const auto manifest_cap = derive_manifest_cap(options.limits.max_record_artifact_bytes);
    if (!manifest_cap.has_value()) {
        return std::unexpected(PayloadStoreError::SizeCapExceeded);
    }
    // Open the final root component O_NOFOLLOW; the parent prefix is the caller's
    // trusted boundary (resolved by the ordinary resolver, documented in the header).
    const int fd = ::open(root.c_str(), O_DIRECTORY | O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) {
        return std::unexpected(PayloadStoreError::UnsupportedFilesystem);
    }
    FdGuard guard(fd);
    struct stat st{};
    if (::fstat(fd, &st) != 0 || !S_ISDIR(st.st_mode) || st.st_uid != ::geteuid() ||
        (st.st_mode & (S_IWGRP | S_IWOTH)) != 0) {
        return std::unexpected(PayloadStoreError::UnsupportedFilesystem);
    }
    struct statfs sfs{};
    if (::fstatfs(fd, &sfs) != 0 || !is_durable_fs_magic(sfs.f_type)) {
        return std::unexpected(PayloadStoreError::UnsupportedFilesystem);
    }
    IntegrityPayloadStore store;
    store.root_fd_ = guard.release();
    store.root_dev_ = static_cast<std::uint64_t>(st.st_dev);
    store.limits_ = options.limits;
    store.manifest_cap_ = *manifest_cap;
    store.observer_ = options.observer;
    store.observer_context_ = options.observer_context;
    return store;
}

// ---- shared publish + admission internals ----------------------------------
namespace {

// Create a directory child under a parent dirfd if absent, handling a concurrent
// mkdir race (EEXIST). Sets `created` to whether THIS call created it (only the
// creator fsyncs the parent). On failure returns the error: a mkdir/open I/O failure
// -> WriteFailed; an EEXIST entry that is not a safe store-owned directory (symlink /
// wrong owner / group-other-writable / cross-device) -> StateMismatch.
[[nodiscard]] std::expected<FdGuard, PayloadStoreError>
ensure_dir(int parent_fd, const std::string &name, std::uint64_t root_dev, bool &created) {
    created = false;
    if (::mkdirat(parent_fd, name.c_str(), 0700) == 0) {
        created = true;
        auto opened = open_dir_existing(parent_fd, name, root_dev);
        if (opened.status != OpenDirStatus::Opened) {
            return std::unexpected(PayloadStoreError::WriteFailed); // we just made it
        }
        return std::move(opened.fd);
    }
    if (errno != EEXIST) {
        return std::unexpected(PayloadStoreError::WriteFailed); // mkdir I/O failure
    }
    // Lost the create race (or it pre-existed): open + validate. An existing-but-unsafe
    // descendant is StateMismatch; a Missing here means a concurrent removal -> retry
    // would be racy, so treat as WriteFailed (rare, transient).
    auto opened = open_dir_existing(parent_fd, name, root_dev);
    switch (opened.status) {
    case OpenDirStatus::Opened:
        return std::move(opened.fd);
    case OpenDirStatus::Unsafe:
        return std::unexpected(PayloadStoreError::StateMismatch);
    case OpenDirStatus::Missing:
        return std::unexpected(PayloadStoreError::WriteFailed);
    }
    return std::unexpected(PayloadStoreError::WriteFailed);
}

// Open (creating if needed) the wf/ckpt checkpoint directory under the root fd,
// fsyncing each parent that this process actually created. Returns the checkpoint
// dirfd or a PayloadStoreError (existing-but-unsafe descendant -> StateMismatch).
[[nodiscard]] std::expected<FdGuard, PayloadStoreError>
open_checkpoint_dir(int root_fd, std::uint64_t root_dev, CoreWorkflowId wf, ResumeCheckpointId ckpt,
                    TransactionObserver observer, void *ctx) {
    bool wf_created = false;
    auto wf_dir = ensure_dir(root_fd, workflow_dir_name(wf), root_dev, wf_created);
    if (!wf_dir.has_value()) {
        return std::unexpected(wf_dir.error());
    }
    if (wf_created) {
        if (!fsync_checked(root_fd)) {
            return std::unexpected(PayloadStoreError::WriteFailed);
        }
        notify(observer, ctx, StorePhase::WorkflowDirParentFsynced);
    }
    bool ckpt_created = false;
    auto ckpt_dir = ensure_dir(wf_dir->get(), checkpoint_dir_name(ckpt), root_dev, ckpt_created);
    if (!ckpt_dir.has_value()) {
        return std::unexpected(ckpt_dir.error());
    }
    if (ckpt_created) {
        if (!fsync_checked(wf_dir->get())) {
            return std::unexpected(PayloadStoreError::WriteFailed);
        }
        notify(observer, ctx, StorePhase::CheckpointDirParentFsynced);
    }
    return std::move(*ckpt_dir);
}

// Open (WITHOUT creating) the existing wf/ckpt checkpoint directory. `missing_error`
// is returned when either level is genuinely absent (openat ENOENT); an existing but
// unsafe directory is always StateMismatch. Never creates directories or emits phases.
[[nodiscard]] std::expected<FdGuard, PayloadStoreError>
open_existing_checkpoint_dir(int root_fd, std::uint64_t root_dev, CoreWorkflowId wf,
                             ResumeCheckpointId ckpt, PayloadStoreError missing_error) {
    OpenDirResult wf_dir = open_dir_existing(root_fd, workflow_dir_name(wf), root_dev);
    if (wf_dir.status == OpenDirStatus::Missing) {
        return std::unexpected(missing_error);
    }
    if (wf_dir.status == OpenDirStatus::Unsafe) {
        return std::unexpected(PayloadStoreError::StateMismatch);
    }
    OpenDirResult ckpt_dir =
        open_dir_existing(wf_dir.fd.get(), checkpoint_dir_name(ckpt), root_dev);
    if (ckpt_dir.status == OpenDirStatus::Missing) {
        return std::unexpected(missing_error);
    }
    if (ckpt_dir.status == OpenDirStatus::Unsafe) {
        return std::unexpected(PayloadStoreError::StateMismatch);
    }
    return std::move(ckpt_dir.fd);
}

// Acquire the exclusive per-checkpoint lock. Opens ckpt/pointer.lock (creating it),
// validates it as a store-owned regular file, then blocking flock(LOCK_EX) with
// EINTR retry. An open with O_NOFOLLOW that hits an existing symlink, or a lock file
// that is non-regular / cross-device / wrong owner-mode / hardlinked (nlink != 1) is
// an existing-but-unsafe descendant -> StateMismatch; a genuine open/flock I/O
// failure -> WriteFailed.
[[nodiscard]] std::expected<FdGuard, PayloadStoreError> acquire_lock(int ckpt_fd,
                                                                    std::uint64_t root_dev) {
    const int fd = ::openat(ckpt_fd, "pointer.lock",
                            O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) {
        // The open failed. Distinguish an existing-but-unsafe entry at the lock path
        // (symlink via ELOOP, a directory via EISDIR, or any other existing non-safe-
        // regular node) from a genuine absence / write I/O failure via a dirfd-relative
        // no-follow fstatat. The open() and this fstatat are NOT an atomic snapshot, so
        // this is best-effort classification, not a race-free observation; the safety
        // property is that the fstatat never follows a symlink and EVERY outcome
        // (existing-unsafe, absent, or racing) fails closed (StateMismatch/WriteFailed),
        // never opening or trusting an unsafe node.
        struct stat est{};
        if (::fstatat(ckpt_fd, "pointer.lock", &est, AT_SYMLINK_NOFOLLOW) == 0) {
            const bool safe_regular =
                S_ISREG(est.st_mode) && static_cast<std::uint64_t>(est.st_dev) == root_dev &&
                est.st_uid == ::geteuid() && (est.st_mode & (S_IWGRP | S_IWOTH)) == 0 &&
                est.st_nlink == 1;
            return std::unexpected(safe_regular ? PayloadStoreError::WriteFailed
                                                : PayloadStoreError::StateMismatch);
        }
        return std::unexpected(PayloadStoreError::WriteFailed);
    }
    FdGuard guard(fd);
    struct stat st{};
    if (::fstat(fd, &st) != 0) {
        return std::unexpected(PayloadStoreError::WriteFailed);
    }
    if (!S_ISREG(st.st_mode) || static_cast<std::uint64_t>(st.st_dev) != root_dev ||
        st.st_uid != ::geteuid() || (st.st_mode & (S_IWGRP | S_IWOTH)) != 0 || st.st_nlink != 1) {
        return std::unexpected(PayloadStoreError::StateMismatch);
    }
    while (::flock(fd, LOCK_EX) != 0) {
        if (errno == EINTR) {
            continue;
        }
        return std::unexpected(PayloadStoreError::WriteFailed);
    }
    return guard;
}

// Remove a store-owned generation staging OR final directory and its known-name
// children under the checkpoint fd. Refuses any unknown / symlink / non-regular entry
// (fail-closed CommitInterrupted) rather than deleting it. `dir_name` is a store-
// generated name; children are only record / manifest / slot-<hex>.
[[nodiscard]] std::expected<void, PayloadStoreError>
remove_generation_dir(int ckpt_fd, const std::string &dir_name, std::uint64_t root_dev) {
    const int dfd = ::openat(ckpt_fd, dir_name.c_str(),
                             O_DIRECTORY | O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (dfd < 0) {
        if (errno == ENOENT) {
            return {}; // nothing to clean
        }
        return std::unexpected(PayloadStoreError::CommitInterrupted);
    }
    FdGuard dguard(dfd);
    struct stat dst{};
    if (::fstat(dfd, &dst) != 0 || !S_ISDIR(dst.st_mode) ||
        static_cast<std::uint64_t>(dst.st_dev) != root_dev || dst.st_uid != ::geteuid() ||
        (dst.st_mode & (S_IWGRP | S_IWOTH)) != 0) {
        return std::unexpected(PayloadStoreError::CommitInterrupted);
    }
    // A slot child name must be exactly "slot-" + 16 lowercase-hex digits.
    const auto is_slot_child = [](const std::string &n) noexcept {
        if (n.rfind("slot-", 0) != 0 || n.size() != 5 + 16) {
            return false;
        }
        for (std::size_t i = 5; i < n.size(); ++i) {
            const char c = n[i];
            const bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
            if (!ok) {
                return false;
            }
        }
        return true;
    };
    // Enumerate children via fdopendir on a dup (readdir needs a DIR*).
    const int dup_fd = ::openat(dfd, ".", O_DIRECTORY | O_RDONLY | O_CLOEXEC);
    if (dup_fd < 0) {
        return std::unexpected(PayloadStoreError::CommitInterrupted);
    }
    DIR *dir = ::fdopendir(dup_fd);
    if (dir == nullptr) {
        ::close(dup_fd);
        return std::unexpected(PayloadStoreError::CommitInterrupted);
    }
    std::vector<std::string> names;
    bool bad_entry = false;
    for (struct dirent *ent = ::readdir(dir); ent != nullptr; ent = ::readdir(dir)) {
        const std::string n = ent->d_name;
        if (n == "." || n == "..") {
            continue;
        }
        if (n != "record" && n != "manifest" && !is_slot_child(n)) {
            bad_entry = true;
            break;
        }
        names.push_back(n);
    }
    ::closedir(dir); // also closes dup_fd
    if (bad_entry) {
        return std::unexpected(PayloadStoreError::CommitInterrupted);
    }
    for (const std::string &n : names) {
        struct stat cst{};
        if (::fstatat(dfd, n.c_str(), &cst, AT_SYMLINK_NOFOLLOW) != 0 || !S_ISREG(cst.st_mode) ||
            static_cast<std::uint64_t>(cst.st_dev) != root_dev || cst.st_uid != ::geteuid() ||
            (cst.st_mode & (S_IWGRP | S_IWOTH)) != 0 || cst.st_nlink != 1) {
            return std::unexpected(PayloadStoreError::CommitInterrupted);
        }
        if (::unlinkat(dfd, n.c_str(), 0) != 0 && errno != ENOENT) {
            return std::unexpected(PayloadStoreError::CommitInterrupted);
        }
    }
    dguard.reset(); // close before rmdir
    if (::unlinkat(ckpt_fd, dir_name.c_str(), AT_REMOVEDIR) != 0 && errno != ENOENT) {
        return std::unexpected(PayloadStoreError::CommitInterrupted);
    }
    return {};
}

// Remove a residual store-owned regular file (pointer.tmp) under a dirfd, validating
// exact store-owned metadata first. A symlink / foreign / atypical entry is refused
// (CommitInterrupted) and NOT deleted. Absence is success.
[[nodiscard]] std::expected<void, PayloadStoreError>
remove_regular_if_present(int dir_fd, const char *name, std::uint64_t root_dev) {
    struct stat st{};
    if (::fstatat(dir_fd, name, &st, AT_SYMLINK_NOFOLLOW) != 0) {
        if (errno == ENOENT) {
            return {};
        }
        return std::unexpected(PayloadStoreError::CommitInterrupted);
    }
    if (!S_ISREG(st.st_mode) || static_cast<std::uint64_t>(st.st_dev) != root_dev ||
        st.st_uid != ::geteuid() || (st.st_mode & (S_IWGRP | S_IWOTH)) != 0 || st.st_nlink != 1) {
        return std::unexpected(PayloadStoreError::CommitInterrupted);
    }
    if (::unlinkat(dir_fd, name, 0) != 0 && errno != ENOENT) {
        return std::unexpected(PayloadStoreError::CommitInterrupted);
    }
    return {};
}

// Build the AHFLPS/AHFLCM/AHFLGP artifacts for an Available generation, stage them,
// fsync per the locked phase order, publish the immutable gen dir, and swap the
// pointer. `record` already has auth_header set. Returns the published generation M.
//
// Split into a PURE BUILD phase (encode + cap + cross-check into memory, ZERO
// filesystem mutation) and a durable PUBLISH phase, so any invalid / cap / set error
// returns before a stage directory or orphan can be created.

// One pre-built artifact file: its store-generated name and its encoded bytes.
struct BuiltFile {
    std::string name;
    std::vector<std::uint8_t> bytes;
};

struct BuiltGeneration {
    std::vector<BuiltFile> files;        // record?, slot-*, manifest (in write order)
    std::vector<std::uint8_t> pointer_bytes;
    std::size_t manifest_index{0};       // index into files of the manifest (written last)
    bool has_record{false};              // Available (true) vs Consumed (false)
    std::size_t slot_count{0};           // number of slot-* files (Available)
};

// Build + validate every artifact into memory. Enforces record.entry_id == wf,
// distinct-slot canonical sort + exact-set-equality vs the record's memo references,
// record/slot/manifest byte caps. No filesystem mutation.
[[nodiscard]] std::expected<BuiltGeneration, PayloadStoreError>
build_generation(std::uint64_t manifest_cap, const StoreLimits &limits, CoreWorkflowId wf,
                 ResumeCheckpointId ckpt, std::uint64_t gen,
                 std::span<const std::uint8_t, 16> key_id, std::span<const std::uint8_t> key,
                 const std::optional<CoreWasmResumeRecord> &available_record,
                 std::span<const Slot> slots, const std::optional<ConsumedBody> &consumed) {
    BuiltGeneration built;
    CommitManifest manifest;
    manifest.wf = wf;
    manifest.ckpt = ckpt;
    manifest.generation = gen;
    manifest.auth = AuthHeader{1, {}, gen};
    std::copy(key_id.begin(), key_id.end(), manifest.auth.key_id.begin());

    if (available_record.has_value()) {
        built.has_record = true;
        if (!(available_record->entry_id == wf)) {
            return std::unexpected(PayloadStoreError::StateMismatch);
        }
        // Record artifact.
        auto record_result = core_wasm_resume::encode_and_authenticate(*available_record, key);
        if (!record_result.ok()) {
            return std::unexpected(PayloadStoreError::Malformed);
        }
        std::vector<std::uint8_t> record_bytes = std::move(*record_result.bytes);
        if (record_bytes.size() > limits.max_record_artifact_bytes) {
            return std::unexpected(PayloadStoreError::SizeCapExceeded);
        }
        const auto record_digest = artifact_digest_hex(record_bytes);
        if (!record_digest.has_value()) {
            return std::unexpected(PayloadStoreError::Malformed);
        }
        AvailableBody body;
        body.record_sha256 = *record_digest;
        body.record_len = record_bytes.size();

        // Distinct slot set derived from ALL memo references in the record.
        std::vector<std::uint64_t> expected;
        for (const auto &node : available_record->nodes) {
            for (const auto &memo : node.memo) {
                expected.push_back(memo.result_slot.value);
            }
        }
        std::sort(expected.begin(), expected.end());
        expected.erase(std::unique(expected.begin(), expected.end()), expected.end());

        // Caller slots canonical-sorted; reject duplicates; exact-set-equal expected.
        std::vector<Slot> sorted(slots.begin(), slots.end());
        std::sort(sorted.begin(), sorted.end(),
                  [](const Slot &a, const Slot &b) { return a.slot.value < b.slot.value; });
        for (std::size_t i = 1; i < sorted.size(); ++i) {
            if (!(sorted[i - 1].slot.value < sorted[i].slot.value)) {
                return std::unexpected(PayloadStoreError::SlotSetMismatch); // duplicate id
            }
        }
        if (sorted.size() != expected.size()) {
            return std::unexpected(PayloadStoreError::SlotSetMismatch); // missing / extra
        }
        for (std::size_t i = 0; i < sorted.size(); ++i) {
            if (sorted[i].slot.value != expected[i]) {
                return std::unexpected(PayloadStoreError::SlotSetMismatch);
            }
        }
        built.files.reserve(sorted.size() + 2);
        built.files.push_back(BuiltFile{"record", std::move(record_bytes)});
        for (const Slot &s : sorted) {
            SlotArtifact art;
            art.wf = wf;
            art.ckpt = ckpt;
            art.generation = gen;
            art.slot = s.slot;
            art.payload.assign(s.payload.begin(), s.payload.end());
            art.auth = AuthHeader{1, {}, gen};
            std::copy(key_id.begin(), key_id.end(), art.auth.key_id.begin());
            auto slot_bytes = encode_slot(art, key);
            if (!slot_bytes.has_value()) {
                return std::unexpected(slot_bytes.error());
            }
            if (slot_bytes->size() > limits.max_slot_artifact_bytes) {
                return std::unexpected(PayloadStoreError::SizeCapExceeded);
            }
            const auto slot_digest = artifact_digest_hex(*slot_bytes);
            if (!slot_digest.has_value()) {
                return std::unexpected(PayloadStoreError::Malformed);
            }
            body.slots.push_back(SlotMeta{s.slot, *slot_digest, s.payload.size()});
            built.files.push_back(BuiltFile{slot_file_name(s.slot), std::move(*slot_bytes)});
        }
        built.slot_count = sorted.size();
        manifest.state = std::move(body);
    } else {
        manifest.state = *consumed;
        built.files.reserve(1);
    }

    // Manifest artifact (written LAST).
    auto manifest_bytes = encode_manifest(manifest, key);
    if (!manifest_bytes.has_value()) {
        return std::unexpected(manifest_bytes.error());
    }
    if (manifest_bytes->size() > manifest_cap) {
        return std::unexpected(PayloadStoreError::SizeCapExceeded);
    }
    const auto manifest_digest = artifact_digest_hex(*manifest_bytes);
    if (!manifest_digest.has_value()) {
        return std::unexpected(PayloadStoreError::Malformed);
    }
    built.manifest_index = built.files.size();
    built.files.push_back(BuiltFile{"manifest", std::move(*manifest_bytes)});

    // Pointer artifact (written after the generation is published).
    GenerationPointer pointer;
    pointer.wf = wf;
    pointer.ckpt = ckpt;
    pointer.current_generation = gen;
    pointer.manifest_sha256 = *manifest_digest;
    pointer.auth = AuthHeader{1, {}, gen};
    std::copy(key_id.begin(), key_id.end(), pointer.auth.key_id.begin());
    auto pointer_bytes = encode_pointer(pointer, key);
    if (!pointer_bytes.has_value()) {
        return std::unexpected(pointer_bytes.error());
    }
    built.pointer_bytes = std::move(*pointer_bytes);
    return built;
}

[[nodiscard]] std::expected<std::uint64_t, PayloadStoreError>
publish_generation(int ckpt_fd, std::uint64_t root_dev, std::uint64_t manifest_cap,
                   const StoreLimits &limits, CoreWorkflowId wf, ResumeCheckpointId ckpt,
                   std::uint64_t gen, std::span<const std::uint8_t, 16> key_id,
                   std::span<const std::uint8_t> key,
                   const std::optional<CoreWasmResumeRecord> &available_record,
                   std::span<const Slot> slots, const std::optional<ConsumedBody> &consumed,
                   TransactionObserver observer, void *ctx) {
    // PURE BUILD (no fs mutation): any invalid / cap / set error returns here, before
    // any stage directory or orphan exists.
    auto built = build_generation(manifest_cap, limits, wf, ckpt, gen, key_id, key,
                                  available_record, slots, consumed);
    if (!built.has_value()) {
        return std::unexpected(built.error());
    }

    const std::string stage_name = generation_stage_name(gen);
    const std::string gen_name = generation_dir_name(gen);

    // Orphan cleanup under the lock: a residual stage or an unreferenced final gen-M
    // (from a crashed prior attempt at this same M) are removed known-name-only; an
    // unknown/symlink entry fails closed. Never adopt-by-content, never forward.
    if (auto r = remove_generation_dir(ckpt_fd, stage_name, root_dev); !r.has_value()) {
        return std::unexpected(r.error());
    }
    if (auto r = remove_generation_dir(ckpt_fd, gen_name, root_dev); !r.has_value()) {
        return std::unexpected(r.error());
    }
    if (auto r = remove_regular_if_present(ckpt_fd, "pointer.tmp", root_dev); !r.has_value()) {
        return std::unexpected(r.error());
    }
    if (!fsync_checked(ckpt_fd)) {
        return std::unexpected(PayloadStoreError::WriteFailed);
    }

    // Create the staging directory and write every pre-built file.
    if (::mkdirat(ckpt_fd, stage_name.c_str(), 0700) != 0) {
        return std::unexpected(PayloadStoreError::WriteFailed);
    }
    auto stage_dir = open_dir_checked(ckpt_fd, stage_name, root_dev);
    if (!stage_dir.has_value()) {
        return std::unexpected(PayloadStoreError::WriteFailed);
    }
    notify(observer, ctx, StorePhase::StageDirCreated);
    const int stage_fd = stage_dir->get();

    for (std::size_t i = 0; i < built->files.size(); ++i) {
        const BuiltFile &f = built->files[i];
        if (!write_artifact(stage_fd, f.name, f.bytes)) {
            return std::unexpected(PayloadStoreError::WriteFailed);
        }
        if (built->has_record) {
            if (i == 0) {
                notify(observer, ctx, StorePhase::RecordFileFsynced);
            } else if (i + 1 == built->manifest_index && built->slot_count > 0) {
                notify(observer, ctx, StorePhase::SlotFilesFsynced);
            }
        }
        if (i == built->manifest_index) {
            notify(observer, ctx, StorePhase::ManifestFileFsynced);
        }
    }

    if (!fsync_checked(stage_fd)) {
        return std::unexpected(PayloadStoreError::WriteFailed);
    }
    notify(observer, ctx, StorePhase::StageDirFsynced);

    // Publish immutable generation: renameat2(RENAME_NOREPLACE). ENOSYS/EOPNOTSUPP
    // -> UnsupportedFilesystem (never fall back to a plain rename that could clobber).
    if (::renameat2(ckpt_fd, stage_name.c_str(), ckpt_fd, gen_name.c_str(),
                    RENAME_NOREPLACE) != 0) {
        if (errno == ENOSYS || errno == EOPNOTSUPP || errno == EINVAL) {
            return std::unexpected(PayloadStoreError::UnsupportedFilesystem);
        }
        return std::unexpected(PayloadStoreError::WriteFailed);
    }
    notify(observer, ctx, StorePhase::GenerationRenamed);
    if (!fsync_checked(ckpt_fd)) {
        return std::unexpected(PayloadStoreError::WriteFailed);
    }
    notify(observer, ctx, StorePhase::CheckpointDirFsyncedAfterGeneration);

    // Write + atomically swap the pointer.
    if (!write_artifact(ckpt_fd, "pointer.tmp", built->pointer_bytes)) {
        return std::unexpected(PayloadStoreError::WriteFailed);
    }
    notify(observer, ctx, StorePhase::PointerFileFsynced);
    if (::renameat(ckpt_fd, "pointer.tmp", ckpt_fd, "pointer") != 0) {
        return std::unexpected(PayloadStoreError::WriteFailed);
    }
    notify(observer, ctx, StorePhase::PointerRenamed);
    if (!fsync_checked(ckpt_fd)) {
        return std::unexpected(PayloadStoreError::WriteFailed);
    }
    notify(observer, ctx, StorePhase::CheckpointDirFsyncedAfterPointer);
    return gen;
}

// Read + authenticate the live pointer and its current manifest under a checkpoint
// dirfd, cross-checking the namespace and the manifest digest/generation. Returns the
// decoded pointer + manifest, or NotFound (no pointer) / an error.
struct LivePointer {
    GenerationPointer pointer;
    CommitManifest manifest;
    std::uint64_t manifest_artifact_size{0}; // whole current-manifest artifact byte size
};

[[nodiscard]] std::expected<LivePointer, PayloadStoreError>
read_live_pointer(int ckpt_fd, std::uint64_t root_dev, std::uint64_t manifest_cap,
                  CoreWorkflowId wf,
                  ResumeCheckpointId ckpt, std::span<const std::uint8_t, 16> key_id,
                  std::span<const std::uint8_t> key) {
    auto pointer_bytes =
        read_artifact(ckpt_fd, "pointer", root_dev, kMaxGenerationPointerBytes,
                      ArtifactReadKind::Pointer);
    if (!pointer_bytes.has_value()) {
        return std::unexpected(pointer_bytes.error()); // NotFound if pointer ENOENT
    }
    auto pointer = decode_pointer(*pointer_bytes, key_id, key);
    if (!pointer.has_value()) {
        return std::unexpected(pointer.error());
    }
    if (!(pointer->wf == wf) || !(pointer->ckpt == ckpt)) {
        return std::unexpected(PayloadStoreError::CrossCheckpointRejected);
    }
    const std::uint64_t gen = pointer->current_generation;

    auto gen_dir = open_dir_checked(ckpt_fd, generation_dir_name(gen), root_dev);
    if (!gen_dir.has_value()) {
        return std::unexpected(PayloadStoreError::StateMismatch);
    }
    auto manifest_bytes =
        read_artifact(gen_dir->get(), "manifest", root_dev, manifest_cap,
                      ArtifactReadKind::Immutable);
    if (!manifest_bytes.has_value()) {
        return std::unexpected(manifest_bytes.error());
    }
    // The pointer's manifest_sha256 must match the actual manifest artifact bytes.
    const auto manifest_digest = artifact_digest_hex(*manifest_bytes);
    if (!manifest_digest.has_value() || !(*manifest_digest == pointer->manifest_sha256)) {
        return std::unexpected(PayloadStoreError::StateMismatch);
    }
    auto manifest = decode_manifest(*manifest_bytes, key_id, key, manifest_cap);
    if (!manifest.has_value()) {
        return std::unexpected(manifest.error());
    }
    if (!(manifest->wf == wf) || !(manifest->ckpt == ckpt)) {
        return std::unexpected(PayloadStoreError::CrossCheckpointRejected);
    }
    if (manifest->generation != gen) {
        return std::unexpected(PayloadStoreError::StateMismatch);
    }
    return LivePointer{std::move(*pointer), std::move(*manifest), manifest_bytes->size()};
}

// Fully admit a live Available generation: authenticate the record, derive the
// distinct slot set, exact-set-equal the manifest, and read+cross-check every slot.
// Returns the ResolvedAvailable or an error. `live.manifest.state` must be Available.
[[nodiscard]] std::expected<ResolvedAvailable, PayloadStoreError>
admit_available(int ckpt_fd, std::uint64_t root_dev, const StoreLimits &limits, CoreWorkflowId wf,
                ResumeCheckpointId ckpt, std::span<const std::uint8_t, 16> key_id,
                std::span<const std::uint8_t> key, const LivePointer &live) {
    const auto &avail = std::get<AvailableBody>(live.manifest.state);
    const std::uint64_t gen = live.manifest.generation;
    auto gen_dir = open_dir_checked(ckpt_fd, generation_dir_name(gen), root_dev);
    if (!gen_dir.has_value()) {
        return std::unexpected(PayloadStoreError::StateMismatch);
    }
    // Record artifact.
    auto record_bytes = read_artifact(gen_dir->get(), "record", root_dev,
                                      limits.max_record_artifact_bytes,
                                      ArtifactReadKind::Immutable);
    if (!record_bytes.has_value()) {
        return std::unexpected(record_bytes.error());
    }
    if (record_bytes->size() != avail.record_len) {
        return std::unexpected(PayloadStoreError::StateMismatch);
    }
    const auto record_digest = artifact_digest_hex(*record_bytes);
    if (!record_digest.has_value() || !(*record_digest == avail.record_sha256)) {
        return std::unexpected(PayloadStoreError::StateMismatch);
    }
    auto decoded = core_wasm_resume::decode_and_authenticate(*record_bytes, key_id, key);
    if (!decoded.ok()) {
        return std::unexpected(PayloadStoreError::IntegrityFailed);
    }
    CoreWasmResumeRecord record = std::move(*decoded.record);
    if (!(record.entry_id == wf) || record.auth_header.generation != gen ||
        record.guarantees != 0) {
        return std::unexpected(PayloadStoreError::StateMismatch);
    }
    // Distinct slot set derived from all memo references.
    std::vector<std::uint64_t> expected;
    for (const auto &node : record.nodes) {
        for (const auto &memo : node.memo) {
            expected.push_back(memo.result_slot.value);
        }
    }
    std::sort(expected.begin(), expected.end());
    expected.erase(std::unique(expected.begin(), expected.end()), expected.end());
    // The manifest slot set (already sorted+distinct by B0) must exact-set-equal it.
    if (avail.slots.size() != expected.size()) {
        return std::unexpected(PayloadStoreError::SlotSetMismatch);
    }
    for (std::size_t i = 0; i < expected.size(); ++i) {
        if (avail.slots[i].slot.value != expected[i]) {
            return std::unexpected(PayloadStoreError::SlotSetMismatch);
        }
    }
    // P1: now that the actual distinct slot count is known, verify the manifest
    // artifact size against a tightened checked bound (fixed overhead + count *
    // per-SlotMeta worst case) rather than only the permanent conservative cap.
    {
        auto per_slots = checked_mul(static_cast<std::uint64_t>(avail.slots.size()),
                                     kManifestPerSlotMeta);
        auto tight = per_slots.has_value() ? checked_add(kManifestFixedOverhead, *per_slots)
                                           : std::optional<std::uint64_t>{};
        if (!tight.has_value() ||
            static_cast<std::uint64_t>(live.manifest_artifact_size) > *tight) {
            return std::unexpected(PayloadStoreError::SizeCapExceeded);
        }
    }
    // Read + cross-check every slot artifact.
    std::vector<ResolvedSlot> resolved;
    resolved.reserve(avail.slots.size());
    for (const auto &meta : avail.slots) {
        auto slot_bytes = read_artifact(gen_dir->get(), slot_file_name(meta.slot), root_dev,
                                        limits.max_slot_artifact_bytes,
                                        ArtifactReadKind::Immutable);
        if (!slot_bytes.has_value()) {
            return std::unexpected(slot_bytes.error());
        }
        const auto slot_digest = artifact_digest_hex(*slot_bytes);
        if (!slot_digest.has_value() || !(*slot_digest == meta.slot_artifact_sha256)) {
            return std::unexpected(PayloadStoreError::StateMismatch);
        }
        auto slot = decode_slot(*slot_bytes, key_id, key, limits.max_slot_artifact_bytes);
        if (!slot.has_value()) {
            return std::unexpected(slot.error());
        }
        if (!(slot->wf == wf) || !(slot->ckpt == ckpt) || slot->generation != gen ||
            !(slot->slot == meta.slot) || slot->payload.size() != meta.payload_len) {
            return std::unexpected(PayloadStoreError::StateMismatch);
        }
        resolved.push_back(ResolvedSlot{meta.slot, std::move(slot->payload)});
    }
    return ResolvedAvailable{gen, std::move(record), std::move(resolved)};
}

} // namespace

std::expected<std::uint64_t, PayloadStoreError> IntegrityPayloadStore::publish_available(
    ir::core::CoreWorkflowId wf, ResumeCheckpointId ckpt,
    std::uint64_t expected_current_generation, CoreWasmResumeRecord record,
    std::span<const Slot> slots, std::span<const std::uint8_t, 16> key_id,
    std::span<const std::uint8_t> key) {
    if (wf.value == CoreWorkflowId::kInvalid || ckpt.value == ResumeCheckpointId::kInvalid) {
        return std::unexpected(PayloadStoreError::Malformed);
    }
    if (record.guarantees != 0) {
        return std::unexpected(PayloadStoreError::StateMismatch);
    }
    if (expected_current_generation == std::numeric_limits<std::uint64_t>::max()) {
        return std::unexpected(PayloadStoreError::GenerationMismatch); // N+1 would overflow
    }
    for (const Slot &s : slots) {
        if (s.slot.value == PayloadSlotId::kInvalid) {
            return std::unexpected(PayloadStoreError::Malformed);
        }
    }
    const std::uint64_t target = expected_current_generation + 1;

    // expected==0 is the first-publish create path; expected>0 must open an EXISTING
    // checkpoint (a doomed CAS must not create a directory or emit an observer phase).
    FdGuard ckpt_dir_holder;
    if (expected_current_generation == 0) {
        auto ckpt_dir =
            open_checkpoint_dir(root_fd_, root_dev_, wf, ckpt, observer_, observer_context_);
        if (!ckpt_dir.has_value()) {
            return std::unexpected(ckpt_dir.error());
        }
        ckpt_dir_holder = std::move(*ckpt_dir);
    } else {
        auto ckpt_dir = open_existing_checkpoint_dir(root_fd_, root_dev_, wf, ckpt,
                                                     PayloadStoreError::GenerationMismatch);
        if (!ckpt_dir.has_value()) {
            return std::unexpected(ckpt_dir.error());
        }
        ckpt_dir_holder = std::move(*ckpt_dir);
    }
    const int ckpt_fd = ckpt_dir_holder.get();
    auto lock = acquire_lock(ckpt_fd, root_dev_);
    if (!lock.has_value()) {
        return std::unexpected(lock.error());
    }
    // CAS FIRST (error priority): the live pointer must match expected_current_generation.
    auto live = read_live_pointer(ckpt_fd, root_dev_, manifest_cap_, wf, ckpt, key_id, key);
    if (live.has_value()) {
        if (live->pointer.current_generation != expected_current_generation) {
            return std::unexpected(PayloadStoreError::GenerationMismatch);
        }
        if (std::holds_alternative<ConsumedBody>(live->manifest.state)) {
            return std::unexpected(PayloadStoreError::Consumed); // no resurrect
        }
    } else if (live.error() == PayloadStoreError::NotFound) {
        if (expected_current_generation != 0) {
            return std::unexpected(PayloadStoreError::GenerationMismatch);
        }
    } else {
        return std::unexpected(live.error());
    }
    // Store owns generation assignment: set the record's auth header to {1,key_id,M}.
    record.auth_header.alg_version = 1;
    std::copy(key_id.begin(), key_id.end(), record.auth_header.key_id.begin());
    record.auth_header.generation = target;
    return publish_generation(ckpt_fd, root_dev_, manifest_cap_, limits_, wf, ckpt, target,
                              key_id, key, std::optional<CoreWasmResumeRecord>{std::move(record)},
                              slots, std::nullopt, observer_, observer_context_);
}

std::expected<std::uint64_t, PayloadStoreError> IntegrityPayloadStore::mark_consumed(
    ir::core::CoreWorkflowId wf, ResumeCheckpointId ckpt,
    std::uint64_t expected_current_generation, std::span<const std::uint8_t, 16> key_id,
    std::span<const std::uint8_t> key) {
    if (wf.value == CoreWorkflowId::kInvalid || ckpt.value == ResumeCheckpointId::kInvalid) {
        return std::unexpected(PayloadStoreError::Malformed);
    }
    if (expected_current_generation == std::numeric_limits<std::uint64_t>::max()) {
        return std::unexpected(PayloadStoreError::GenerationMismatch);
    }
    const std::uint64_t target = expected_current_generation + 1;

    // Open-existing only: mark_consumed never creates missing directories. A genuinely
    // absent checkpoint -> NotFound (cannot consume nothing); existing-but-unsafe ->
    // StateMismatch.
    auto ckpt_dir = open_existing_checkpoint_dir(root_fd_, root_dev_, wf, ckpt,
                                                 PayloadStoreError::NotFound);
    if (!ckpt_dir.has_value()) {
        return std::unexpected(ckpt_dir.error());
    }
    const int ckpt_fd = ckpt_dir->get();
    auto lock = acquire_lock(ckpt_fd, root_dev_);
    if (!lock.has_value()) {
        return std::unexpected(lock.error());
    }
    auto live = read_live_pointer(ckpt_fd, root_dev_, manifest_cap_, wf, ckpt, key_id, key);
    if (!live.has_value()) {
        return std::unexpected(live.error()); // NotFound / error: cannot consume nothing
    }
    // CAS FIRST, then live-Consumed.
    if (live->pointer.current_generation != expected_current_generation) {
        return std::unexpected(PayloadStoreError::GenerationMismatch);
    }
    if (std::holds_alternative<ConsumedBody>(live->manifest.state)) {
        return std::unexpected(PayloadStoreError::Consumed); // already consumed
    }
    // Full admission of the live Available generation before tombstoning it.
    auto admitted = admit_available(ckpt_fd, root_dev_, limits_, wf, ckpt, key_id, key, *live);
    if (!admitted.has_value()) {
        return std::unexpected(admitted.error());
    }
    // Bind the tombstone to the consumed generation + the live manifest digest that
    // read_live_pointer already verified against the pointer (no re-read needed).
    ConsumedBody consumed;
    consumed.consumed_generation = live->pointer.current_generation;
    consumed.consumed_manifest_sha256 = live->pointer.manifest_sha256;
    return publish_generation(ckpt_fd, root_dev_, manifest_cap_, limits_, wf, ckpt, target,
                              key_id, key, std::nullopt, {}, std::optional<ConsumedBody>{consumed},
                              observer_, observer_context_);
}

std::expected<ResolvedGeneration, PayloadStoreError>
IntegrityPayloadStore::load(ir::core::CoreWorkflowId wf, ResumeCheckpointId ckpt,
                            std::span<const std::uint8_t, 16> key_id,
                            std::span<const std::uint8_t> key) {
    if (wf.value == CoreWorkflowId::kInvalid || ckpt.value == ResumeCheckpointId::kInvalid) {
        return std::unexpected(PayloadStoreError::Malformed);
    }
    // Lock-free open-existing: a genuinely absent checkpoint -> NotFound; existing but
    // unsafe -> StateMismatch. Never creates directories.
    auto ckpt_dir = open_existing_checkpoint_dir(root_fd_, root_dev_, wf, ckpt,
                                                 PayloadStoreError::NotFound);
    if (!ckpt_dir.has_value()) {
        return std::unexpected(ckpt_dir.error());
    }
    const int inner_fd = ckpt_dir->get();
    auto live = read_live_pointer(inner_fd, root_dev_, manifest_cap_, wf, ckpt, key_id, key);
    if (!live.has_value()) {
        return std::unexpected(live.error()); // NotFound if no pointer
    }
    if (std::holds_alternative<ConsumedBody>(live->manifest.state)) {
        const auto &cb = std::get<ConsumedBody>(live->manifest.state);
        // The tombstone must consume exactly the immediately-preceding generation.
        if (cb.consumed_generation == std::numeric_limits<std::uint64_t>::max() ||
            cb.consumed_generation + 1 != live->manifest.generation) {
            return std::unexpected(PayloadStoreError::StateMismatch);
        }
        // Authenticate the bound previous Available manifest.
        auto prev_dir = open_dir_checked(inner_fd, generation_dir_name(cb.consumed_generation),
                                         root_dev_);
        if (!prev_dir.has_value()) {
            return std::unexpected(PayloadStoreError::StateMismatch);
        }
        auto prev_bytes = read_artifact(prev_dir->get(), "manifest", root_dev_, manifest_cap_,
                                        ArtifactReadKind::Immutable);
        if (!prev_bytes.has_value()) {
            return std::unexpected(prev_bytes.error());
        }
        const auto prev_digest = artifact_digest_hex(*prev_bytes);
        if (!prev_digest.has_value() || !(*prev_digest == cb.consumed_manifest_sha256)) {
            return std::unexpected(PayloadStoreError::StateMismatch);
        }
        auto prev = decode_manifest(*prev_bytes, key_id, key, manifest_cap_);
        if (!prev.has_value()) {
            return std::unexpected(prev.error());
        }
        if (!(prev->wf == wf) || !(prev->ckpt == ckpt) ||
            prev->generation != cb.consumed_generation ||
            !std::holds_alternative<AvailableBody>(prev->state)) {
            return std::unexpected(PayloadStoreError::StateMismatch);
        }
        return ResolvedGeneration{
            ResolvedConsumed{live->manifest.generation, cb.consumed_generation}};
    }
    auto admitted = admit_available(inner_fd, root_dev_, limits_, wf, ckpt, key_id, key, *live);
    if (!admitted.has_value()) {
        return std::unexpected(admitted.error());
    }
    return ResolvedGeneration{std::move(*admitted)};
}

#endif // __linux__
IntegrityPayloadStore::IntegrityPayloadStore(IntegrityPayloadStore &&other) noexcept
    : root_fd_(other.root_fd_), root_dev_(other.root_dev_), limits_(other.limits_),
      manifest_cap_(other.manifest_cap_), observer_(other.observer_),
      observer_context_(other.observer_context_) {
    other.root_fd_ = -1;
}

IntegrityPayloadStore &IntegrityPayloadStore::operator=(IntegrityPayloadStore &&other) noexcept {
    if (this != &other) {
#if defined(__linux__)
        if (root_fd_ >= 0) {
            ::close(root_fd_);
        }
#endif
        root_fd_ = other.root_fd_;
        root_dev_ = other.root_dev_;
        limits_ = other.limits_;
        manifest_cap_ = other.manifest_cap_;
        observer_ = other.observer_;
        observer_context_ = other.observer_context_;
        other.root_fd_ = -1;
    }
    return *this;
}

IntegrityPayloadStore::~IntegrityPayloadStore() {
#if defined(__linux__)
    if (root_fd_ >= 0) {
        ::close(root_fd_);
        root_fd_ = -1;
    }
#endif
}

} // namespace ahfl::runtime::payload_store
