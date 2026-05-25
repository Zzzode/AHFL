#include "support/process.hpp"

#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <signal.h>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <utility>

#include <spawn.h>

extern char **environ;

namespace ahfl::support {

namespace {

class FdGuard {
  public:
    FdGuard() = default;
    explicit FdGuard(int fd) : fd_(fd) {}
    ~FdGuard() { reset(); }

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

    [[nodiscard]] int get() const { return fd_; }
    [[nodiscard]] bool valid() const { return fd_ >= 0; }

    int release() {
        const int value = fd_;
        fd_ = -1;
        return value;
    }

    void reset(int fd = -1) {
        if (fd_ >= 0) {
            close(fd_);
        }
        fd_ = fd;
    }

  private:
    int fd_{-1};
};

struct TempFile {
    std::filesystem::path path;
    FdGuard fd;

    TempFile(std::filesystem::path file_path, FdGuard file_fd)
        : path(std::move(file_path)), fd(std::move(file_fd)) {}

    TempFile(TempFile &&) noexcept = default;
    TempFile &operator=(TempFile &&) noexcept = default;
    TempFile(const TempFile &) = delete;
    TempFile &operator=(const TempFile &) = delete;

    ~TempFile() {
        fd.reset();
        if (!path.empty()) {
            std::error_code ec;
            std::filesystem::remove(path, ec);
        }
    }
};

[[nodiscard]] std::filesystem::path make_temp_path(std::string_view suffix) {
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    std::ostringstream name;
    name << "ahfl-process-" << getpid() << "-" << now << "-" << suffix;
    return std::filesystem::temp_directory_path() / name.str();
}

[[nodiscard]] std::optional<TempFile> open_temp_file(std::string_view suffix) {
    for (int attempt = 0; attempt < 8; ++attempt) {
        auto path = make_temp_path(std::string(suffix) + "-" + std::to_string(attempt));
        int fd = open(path.c_str(), O_CREAT | O_EXCL | O_RDWR | O_CLOEXEC, S_IRUSR | S_IWUSR);
        if (fd >= 0) {
            return TempFile{std::move(path), FdGuard{fd}};
        }
    }
    return std::nullopt;
}

[[nodiscard]] bool write_all(int fd, std::string_view data) {
    while (!data.empty()) {
        const auto written = write(fd, data.data(), data.size());
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        data.remove_prefix(static_cast<std::size_t>(written));
    }
    return true;
}

[[nodiscard]] std::string read_file(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return {};
    }
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

[[nodiscard]] int decode_wait_status(int status) {
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }
    return -1;
}

} // namespace

ProcessResult launch_process(const ProcessConfig &config) {
    ProcessResult result;

    auto stdout_file = open_temp_file("stdout");
    auto stderr_file = open_temp_file("stderr");
    if (!stdout_file.has_value() || !stderr_file.has_value()) {
        result.stderr_output = "failed to create process output files";
        return result;
    }

    std::array<int, 2> stdin_pipe{-1, -1};
    const bool has_stdin = config.stdin_input.has_value();
    if (has_stdin && pipe(stdin_pipe.data()) != 0) {
        result.stderr_output = "failed to create stdin pipe: " + std::string(std::strerror(errno));
        return result;
    }
    FdGuard stdin_read{stdin_pipe[0]};
    FdGuard stdin_write{stdin_pipe[1]};

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, stdout_file->fd.get(), STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, stderr_file->fd.get(), STDERR_FILENO);
    if (has_stdin) {
        posix_spawn_file_actions_adddup2(&actions, stdin_read.get(), STDIN_FILENO);
        posix_spawn_file_actions_addclose(&actions, stdin_write.get());
    }

    std::vector<char *> argv;
    argv.push_back(const_cast<char *>(config.executable.c_str()));
    for (const auto &arg : config.arguments) {
        argv.push_back(const_cast<char *>(arg.c_str()));
    }
    argv.push_back(nullptr);

    pid_t pid = 0;
    const int spawn_error = posix_spawnp(&pid,
                                         config.executable.c_str(),
                                         &actions,
                                         nullptr,
                                         argv.data(),
                                         environ);
    posix_spawn_file_actions_destroy(&actions);

    if (spawn_error != 0) {
        result.stderr_output = "posix_spawnp failed: " + std::string(std::strerror(spawn_error));
        return result;
    }

    stdin_read.reset();
    if (has_stdin) {
        if (!write_all(stdin_write.get(), *config.stdin_input)) {
            result.stderr_output = "failed to write process stdin: " +
                                   std::string(std::strerror(errno));
        }
        stdin_write.reset();
    }

    const auto deadline = std::chrono::steady_clock::now() + config.timeout;
    int status = 0;
    while (true) {
        const int wait_result = waitpid(pid, &status, WNOHANG);
        if (wait_result == pid) {
            result.exit_code = decode_wait_status(status);
            break;
        }
        if (wait_result < 0) {
            result.exit_code = -1;
            result.stderr_output += "waitpid failed: " + std::string(std::strerror(errno));
            break;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            kill(pid, SIGKILL);
            static_cast<void>(waitpid(pid, &status, 0));
            result.timed_out = true;
            result.exit_code = -1;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    stdout_file->fd.reset();
    stderr_file->fd.reset();
    result.stdout_output = read_file(stdout_file->path);
    const auto stderr_output = read_file(stderr_file->path);
    if (!stderr_output.empty()) {
        if (!result.stderr_output.empty()) {
            result.stderr_output += '\n';
        }
        result.stderr_output += stderr_output;
    }

    return result;
}

std::optional<std::string> find_executable(std::string_view name) {
    if (name.empty()) {
        return std::nullopt;
    }
    if (name.front() == '/') {
        const std::filesystem::path candidate{name};
        if (std::filesystem::exists(candidate) && access(candidate.c_str(), X_OK) == 0) {
            return candidate.string();
        }
        return std::nullopt;
    }

    const char *path_env = std::getenv("PATH");
    if (path_env == nullptr) {
        return std::nullopt;
    }

    std::istringstream paths{std::string{path_env}};
    std::string dir;
    while (std::getline(paths, dir, ':')) {
        if (dir.empty()) {
            continue;
        }
        auto candidate = std::filesystem::path{dir} / std::string{name};
        if (std::filesystem::exists(candidate) && access(candidate.c_str(), X_OK) == 0) {
            return candidate.string();
        }
    }
    return std::nullopt;
}

} // namespace ahfl::support
