#include "verification/formal/bundled_solver.hpp"

#include <filesystem>

#if defined(__linux__)
#include <unistd.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <cstdint>
#endif

namespace ahfl::formal {

std::filesystem::path
bundled_solver_path(std::initializer_list<std::string_view> candidates) {
    std::filesystem::path exe_dir;

#if defined(__linux__)
    // /proc/self/exe is a symlink to the running executable.
    char buf[4096];
    if (const ssize_t len = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        len > 0) {
        buf[len] = '\0';
        exe_dir = std::filesystem::path(buf).parent_path();
    }
#elif defined(__APPLE__)
    char buf[4096];
    std::uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) == 0) {
        exe_dir = std::filesystem::path(buf).parent_path();
    }
#else
    // No executable-directory discovery on this platform; fall through to
    // environment-variable and PATH lookup in the caller.
    return {};
#endif

    if (exe_dir.empty())
        return {};

    for (const std::string_view name : candidates) {
        if (auto candidate = exe_dir / name; std::filesystem::exists(candidate))
            return candidate;
    }
    return {};
}

} // namespace ahfl::formal
