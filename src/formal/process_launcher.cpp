#include "ahfl/formal/process_launcher.hpp"

#include <array>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <sys/wait.h>
#include <unistd.h>

#include <spawn.h>

extern char **environ;

namespace ahfl::formal {

namespace {

struct PipeGuard {
    int fds[2]{-1, -1};

    [[nodiscard]] bool create() {
        return pipe(fds) == 0;
    }

    void close_read() {
        if (fds[0] >= 0) {
            close(fds[0]);
            fds[0] = -1;
        }
    }

    void close_write() {
        if (fds[1] >= 0) {
            close(fds[1]);
            fds[1] = -1;
        }
    }

    ~PipeGuard() {
        close_read();
        close_write();
    }

    PipeGuard() = default;
    PipeGuard(const PipeGuard &) = delete;
    PipeGuard &operator=(const PipeGuard &) = delete;
};

std::string read_fd(int fd) {
    std::string result;
    std::array<char, 4096> buf{};
    while (true) {
        auto n = read(fd, buf.data(), buf.size());
        if (n <= 0) break;
        result.append(buf.data(), static_cast<std::size_t>(n));
    }
    return result;
}

} // namespace

[[nodiscard]] ProcessResult
launch_process(const ProcessConfig &config) {
    ProcessResult result;

    PipeGuard stdout_pipe;
    PipeGuard stderr_pipe;

    if (!stdout_pipe.create() || !stderr_pipe.create()) {
        result.exit_code = -1;
        result.stderr_output = "Failed to create pipes: " + std::string(std::strerror(errno));
        return result;
    }

    posix_spawn_file_actions_t file_actions;
    posix_spawn_file_actions_init(&file_actions);
    posix_spawn_file_actions_adddup2(&file_actions, stdout_pipe.fds[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&file_actions, stderr_pipe.fds[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&file_actions, stdout_pipe.fds[0]);
    posix_spawn_file_actions_addclose(&file_actions, stderr_pipe.fds[0]);

    // Build argv
    std::vector<const char *> argv;
    argv.push_back(config.executable.c_str());
    for (const auto &arg : config.arguments) {
        argv.push_back(arg.c_str());
    }
    argv.push_back(nullptr);

    pid_t pid = 0;
    int spawn_err = posix_spawnp(
        &pid,
        config.executable.c_str(),
        &file_actions,
        nullptr,
        const_cast<char *const *>(reinterpret_cast<const char *const *>(argv.data())),
        environ
    );

    posix_spawn_file_actions_destroy(&file_actions);

    if (spawn_err != 0) {
        result.exit_code = -1;
        result.stderr_output = "posix_spawnp failed: " + std::string(std::strerror(spawn_err));
        return result;
    }

    // Close write ends in parent
    stdout_pipe.close_write();
    stderr_pipe.close_write();

    // Read output
    result.stdout_output = read_fd(stdout_pipe.fds[0]);
    result.stderr_output = read_fd(stderr_pipe.fds[0]);

    // Wait with timeout
    auto deadline = std::chrono::steady_clock::now() + config.timeout;
    int status = 0;
    while (true) {
        int w = waitpid(pid, &status, WNOHANG);
        if (w == pid) {
            if (WIFEXITED(status)) {
                result.exit_code = WEXITSTATUS(status);
            } else {
                result.exit_code = -1;
            }
            break;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            result.timed_out = true;
            result.exit_code = -1;
            break;
        }
        usleep(10000); // 10ms poll
    }

    return result;
}

[[nodiscard]] std::optional<std::string>
find_executable(std::string_view name) {
    if (name.empty()) return std::nullopt;

    // If absolute path, check directly
    if (name.front() == '/') {
        std::filesystem::path p{name};
        if (std::filesystem::exists(p) && access(p.c_str(), X_OK) == 0) {
            return std::string{name};
        }
        return std::nullopt;
    }

    const char *path_env = std::getenv("PATH");
    if (path_env == nullptr) return std::nullopt;

    std::istringstream path_stream{std::string{path_env}};
    std::string dir;
    while (std::getline(path_stream, dir, ':')) {
        if (dir.empty()) continue;
        auto full = std::filesystem::path{dir} / std::string{name};
        if (std::filesystem::exists(full) && access(full.c_str(), X_OK) == 0) {
            return full.string();
        }
    }

    return std::nullopt;
}

} // namespace ahfl::formal
