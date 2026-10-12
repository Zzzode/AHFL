#include "tooling/cli/llm_run_lease.hpp"

#include "base/json/json_value.hpp"
#include "base/support/atomic_file.hpp"

#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <system_error>

#if defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace ahfl::cli {
namespace {

[[nodiscard]] bool valid_binding(std::string_view binding) {
    if (binding.size() != 64)
        return false;
    for (const char value : binding) {
        if (!(value >= '0' && value <= '9') && !(value >= 'a' && value <= 'f'))
            return false;
    }
    return true;
}

#if defined(__unix__) || defined(__APPLE__)
[[nodiscard]] bool sync_directory(const std::filesystem::path &path);

[[nodiscard]] std::expected<void, std::string> private_directory(const std::filesystem::path &path,
                                                                 bool require_private_mode = true) {
    struct stat info {};
    bool created = false;
    if (::lstat(path.c_str(), &info) != 0) {
        if (errno != ENOENT || ::mkdir(path.c_str(), 0700) != 0 ||
            ::lstat(path.c_str(), &info) != 0) {
            return std::unexpected("Cannot create private LLM run directory: " + path.string());
        }
        created = true;
    }
    if (!S_ISDIR(info.st_mode) || info.st_uid != ::getuid() ||
        (require_private_mode ? (info.st_mode & 0777) != 0700 : (info.st_mode & 0022) != 0)) {
        return std::unexpected(
            std::string(
                require_private_mode
                    ? "LLM run directory must be owned by this user, mode 0700, and not a symlink: "
                    : "AHFL directory must be owned by this user, not writable by other users, and "
                      "not a symlink: ") +
            path.string());
    }
    if (created &&
        !sync_directory(path.has_parent_path() ? path.parent_path() : std::filesystem::path("."))) {
        return std::unexpected("Cannot durably create private LLM run directory: " + path.string());
    }
    return {};
}

[[nodiscard]] std::expected<void, std::string> private_file(const std::filesystem::path &path) {
    struct stat info {};
    if (::lstat(path.c_str(), &info) != 0) {
        if (errno == ENOENT)
            return {};
        return std::unexpected("Cannot inspect LLM run file: " + path.string());
    }
    if (!S_ISREG(info.st_mode) || info.st_uid != ::getuid() || (info.st_mode & 0777) != 0600 ||
        info.st_nlink != 1) {
        return std::unexpected(
            "LLM run file must be owned by this user, mode 0600, and not a link: " + path.string());
    }
    return {};
}

[[nodiscard]] bool sync_directory(const std::filesystem::path &path) {
    const int fd = ::open(path.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (fd < 0)
        return false;
    int result;
    do {
        result = ::fsync(fd);
    } while (result != 0 && errno == EINTR);
    ::close(fd);
    return result == 0;
}

[[nodiscard]] std::expected<void, std::string> write_manifest(const std::filesystem::path &path,
                                                              std::string_view binding,
                                                              std::uint64_t namespace_id,
                                                              bool completed) {
    if (auto inspected = private_file(path); !inspected)
        return inspected;
    const auto temporary = ahfl::support::atomic_temporary_path(path);
    if (auto inspected = private_file(temporary); !inspected)
        return inspected;
    const int fd = ::open(temporary.c_str(), O_CREAT | O_WRONLY | O_NOFOLLOW, 0600);
    if (fd < 0)
        return std::unexpected("Cannot prepare private LLM run manifest");
    struct stat info {};
    const bool secure = ::fstat(fd, &info) == 0 && S_ISREG(info.st_mode) &&
                        info.st_uid == ::getuid() && (info.st_mode & 0777) == 0600 &&
                        info.st_nlink == 1;
    ::close(fd);
    if (!secure)
        return std::unexpected("Unsafe LLM run manifest temporary file");
    auto root = ahfl::json::JsonValue::make_object();
    root->set("version", ahfl::json::JsonValue::make_int(1));
    root->set("binding", ahfl::json::JsonValue::make_string(std::string(binding)));
    root->set("namespace",
              ahfl::json::JsonValue::make_int(static_cast<std::int64_t>(namespace_id)));
    root->set("completed", ahfl::json::JsonValue::make_bool(completed));
    if (!ahfl::support::atomic_replace_text(
            path, ahfl::json::serialize_json(*root), {.durable = true})) {
        return std::unexpected("Cannot durably publish LLM active run manifest");
    }
    return {};
}

struct Manifest {
    std::uint64_t namespace_id{};
    bool completed{};
};

[[nodiscard]] std::expected<std::optional<Manifest>, std::string>
read_manifest(const std::filesystem::path &path, std::string_view binding) {
    if (auto inspected = private_file(path); !inspected)
        return std::unexpected(inspected.error());
    std::error_code error;
    if (!std::filesystem::exists(path, error)) {
        if (error)
            return std::unexpected("Cannot inspect active LLM run manifest");
        return std::nullopt;
    }
    const auto size = std::filesystem::file_size(path, error);
    if (error || size > 4096)
        return std::unexpected("LLM active run manifest exceeds its size bound");
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        return std::unexpected("Cannot read active LLM run manifest");
    const std::string text{std::istreambuf_iterator<char>(stream),
                           std::istreambuf_iterator<char>()};
    const auto parsed = ahfl::json::parse_json(text);
    if (!parsed || !*parsed || !(*parsed)->is_object() || (*parsed)->object_fields.size() != 4) {
        return std::unexpected("Invalid LLM active run manifest");
    }
    const auto *version = (*parsed)->get("version");
    const auto *saved_binding = (*parsed)->get("binding");
    const auto *namespace_value = (*parsed)->get("namespace");
    const auto *completed = (*parsed)->get("completed");
    const auto namespace_id = namespace_value ? namespace_value->as_uint() : std::nullopt;
    const auto state = completed ? completed->as_bool() : std::nullopt;
    if (!version || version->as_int() != 1 || !saved_binding ||
        saved_binding->as_string() != binding || !namespace_id || *namespace_id == 0 ||
        *namespace_id > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) ||
        !state) {
        return std::unexpected(
            "LLM active run manifest version, binding, namespace or state is invalid");
    }
    return Manifest{*namespace_id, *state};
}

[[nodiscard]] std::expected<std::uint64_t, std::string> random_namespace() {
    const int fd = ::open("/dev/urandom", O_RDONLY);
    if (fd < 0)
        return std::unexpected("Secure randomness is unavailable for LLM run identity");
    std::uint64_t value{};
    std::size_t offset = 0;
    auto *bytes = reinterpret_cast<unsigned char *>(&value);
    while (offset < sizeof(value)) {
        const auto count = ::read(fd, bytes + offset, sizeof(value) - offset);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0) {
            ::close(fd);
            return std::unexpected("Cannot read secure LLM run identity");
        }
        offset += static_cast<std::size_t>(count);
    }
    ::close(fd);
    value &= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    return value == 0 ? std::uint64_t{1} : value;
}
#endif

} // namespace

std::expected<std::unique_ptr<LLMRunLease>, std::string>
LLMRunLease::open(std::string_view binding_hex,
                  std::optional<std::filesystem::path> explicit_recovery_path) {
    if (!valid_binding(binding_hex))
        return std::unexpected("LLM run binding must be a lowercase SHA-256 hex digest");
#if defined(__unix__) || defined(__APPLE__)
    auto lease = std::unique_ptr<LLMRunLease>(new LLMRunLease());
    lease->binding_hex_ = binding_hex;
    lease->managed_ = !explicit_recovery_path.has_value();
    if (explicit_recovery_path) {
        if (explicit_recovery_path->empty())
            return std::unexpected("Explicit LLM recovery path must not be empty");
        lease->recovery_path_ = *explicit_recovery_path;
        lease->session_root_ = std::filesystem::path(explicit_recovery_path->string() + ".llm");
        if (auto secure = private_directory(lease->session_root_); !secure)
            return std::unexpected(secure.error());
        std::error_code path_error;
        const auto status = std::filesystem::symlink_status(lease->recovery_path_, path_error);
        if (path_error && path_error != std::errc::no_such_file_or_directory)
            return std::unexpected("could not read recovery snapshot at recovery path '" +
                                   lease->recovery_path_.string() + "': cannot inspect path");
        if (std::filesystem::is_symlink(status) ||
            (std::filesystem::exists(status) && !std::filesystem::is_regular_file(status))) {
            return std::unexpected(
                "could not read recovery snapshot at recovery path '" +
                lease->recovery_path_.string() +
                "': explicit LLM recovery path must be a regular file without a symlink");
        }
        lease->binding_root_ = lease->session_root_;
    } else {
        const char *home = std::getenv("HOME");
        if (!home || std::string_view(home).empty())
            return std::unexpected("HOME is unavailable for managed LLM recovery");
        const auto ahfl_root = std::filesystem::path(home) / ".ahfl";
        const auto runs_root = ahfl_root / "runs";
        lease->binding_root_ = runs_root / std::string(binding_hex);
        for (const auto &path : {ahfl_root, runs_root, lease->binding_root_}) {
            if (auto secure = private_directory(path, path != ahfl_root); !secure)
                return std::unexpected(secure.error());
        }
    }
    const auto lock_path = lease->binding_root_ / ".run.lock";
    if (auto secure = private_file(lock_path); !secure)
        return std::unexpected(secure.error());
    lease->lock_fd_ = ::open(lock_path.c_str(), O_CREAT | O_RDWR | O_NOFOLLOW, 0600);
    if (lease->lock_fd_ < 0)
        return std::unexpected("Cannot open private LLM run lock");
    struct stat lock_info {};
    if (::fstat(lease->lock_fd_, &lock_info) != 0 || !S_ISREG(lock_info.st_mode) ||
        lock_info.st_uid != ::getuid() || (lock_info.st_mode & 0777) != 0600 ||
        lock_info.st_nlink != 1) {
        return std::unexpected("Unsafe private LLM run lock");
    }
    if (::flock(lease->lock_fd_, LOCK_EX | LOCK_NB) != 0)
        return std::unexpected("LLM run is busy in another command");
    if (!lease->managed_) {
        auto marker = read_manifest(lease->session_root_ / ".completed.json", binding_hex);
        if (!marker)
            return std::unexpected(marker.error());
        if (*marker) {
            if (!(**marker).completed)
                return std::unexpected("Invalid explicit completion marker");
            if (auto cleaned = lease->complete(true); !cleaned)
                return std::unexpected(cleaned.error());
            lease->completed_ = false;
        }
        return lease;
    }
    const auto active_path = lease->binding_root_ / "active.json";
    const auto manifest = read_manifest(active_path, binding_hex);
    if (!manifest)
        return std::unexpected(manifest.error());
    if (*manifest && !(**manifest).completed) {
        lease->namespace_id_ = (**manifest).namespace_id;
        lease->run_root_ = lease->binding_root_ / std::to_string(lease->namespace_id_);
        // An active manifest never silently recreates a missing run.
        std::error_code error;
        if (!std::filesystem::exists(lease->run_root_, error) || error) {
            return std::unexpected(
                "Active LLM run directory is missing; recovery cannot be reused");
        }
    } else {
        if (*manifest) {
            const auto previous = lease->binding_root_ / std::to_string((**manifest).namespace_id);
            std::error_code error;
            std::filesystem::remove_all(previous, error);
            if (error)
                return std::unexpected("Cannot clean completed LLM run namespace");
        }
        for (int attempt = 0; attempt < 8; ++attempt) {
            const auto identity = random_namespace();
            if (!identity)
                return std::unexpected(identity.error());
            lease->namespace_id_ = *identity;
            lease->run_root_ = lease->binding_root_ / std::to_string(*identity);
            if (::mkdir(lease->run_root_.c_str(), 0700) == 0)
                break;
            if (errno != EEXIST || attempt == 7)
                return std::unexpected("Cannot create unique private LLM run namespace");
        }
        if (!sync_directory(lease->binding_root_))
            return std::unexpected("Cannot durably create LLM run namespace");
        if (auto saved = write_manifest(active_path, binding_hex, lease->namespace_id_, false);
            !saved) {
            return std::unexpected(saved.error());
        }
    }
    if (auto secure = private_directory(lease->run_root_); !secure)
        return std::unexpected(secure.error());
    lease->recovery_path_ = lease->run_root_ / "workflow-recovery.json";
    lease->session_root_ = lease->run_root_ / "llm";
    if (auto secure = private_directory(lease->session_root_); !secure)
        return std::unexpected(secure.error());
    return lease;
#else
    (void)explicit_recovery_path;
    return std::unexpected("Durable private LLM run leases are unavailable on this platform; POSIX "
                           "file locking is required");
#endif
}

LLMRunLease::~LLMRunLease() {
#if defined(__unix__) || defined(__APPLE__)
    if (lock_fd_ >= 0)
        ::close(lock_fd_);
#endif
}

std::expected<void, std::string> LLMRunLease::complete(bool discard_fresh_explicit_run) {
    if (completed_ || (!managed_ && !discard_fresh_explicit_run))
        return {};
#if defined(__unix__) || defined(__APPLE__)
    if (!managed_) {
        const auto marker = session_root_ / ".completed.json";
        if (auto saved = write_manifest(marker, binding_hex_, 1, true); !saved)
            return saved;
        std::error_code error;
        std::filesystem::remove(recovery_path_, error);
        const auto recovery_parent = recovery_path_.has_parent_path() ? recovery_path_.parent_path()
                                                                      : std::filesystem::path(".");
        if (error || !sync_directory(recovery_parent))
            return std::unexpected(
                "Completed explicit run recorded, but checkpoint cleanup failed");
        for (std::filesystem::directory_iterator it(session_root_, error), end; !error && it != end;
             it.increment(error)) {
            if (it->path().filename() == ".run.lock" || it->path() == marker)
                continue;
            if (auto admitted = private_file(it->path()); !admitted)
                return admitted;
            std::filesystem::remove(it->path(), error);
            if (error)
                break;
        }
        if (error || !sync_directory(session_root_))
            return std::unexpected("Completed explicit run recorded, but session cleanup failed");
        std::filesystem::remove(marker, error);
        if (error || !sync_directory(session_root_))
            return std::unexpected("Explicit completion cleanup could not be persisted");
        completed_ = true;
        return {};
    }
    if (auto saved =
            write_manifest(binding_root_ / "active.json", binding_hex_, namespace_id_, true);
        !saved)
        return saved;
    completed_ = true;
    std::error_code error;
    std::filesystem::remove_all(run_root_, error);
    if (error || !sync_directory(binding_root_))
        return std::unexpected("Completed LLM run recorded, but namespace cleanup failed");
    return {};
#else
    return std::unexpected("Durable LLM run completion is unavailable on this platform");
#endif
}

} // namespace ahfl::cli
