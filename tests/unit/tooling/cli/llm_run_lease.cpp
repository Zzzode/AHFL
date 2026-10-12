#include "tooling/cli/llm_run_lease.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {
int checks{};
int failures{};
void check(bool condition, const char *name) {
    ++checks;
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << name << '\n';
    }
}
} // namespace

int main() {
#if defined(__unix__) || defined(__APPLE__)
    auto fixture_name = (std::filesystem::temp_directory_path() / "ahfl-llm-lease-XXXXXX").string();
    const char *created = ::mkdtemp(fixture_name.data());
    if (!created) {
        std::cerr << "Cannot create private lease fixture\n";
        return 1;
    }
    const auto temporary = std::filesystem::path(created);
    // An isolated native fixture uses a private home; the launching user's environment is untouched.
    const char *previous_home = std::getenv("HOME");
    const bool had_home = previous_home != nullptr;
    const std::string saved_home = previous_home ? previous_home : "";
    ::setenv("HOME", temporary.c_str(), 1);
    const std::string binding(64, 'a');
    const std::string other_binding(64, 'b');
    using ahfl::cli::LLMRunLease;
    check(!LLMRunLease::open("invalid"), "invalid binding rejected");
    auto first = LLMRunLease::open(binding);
    check(first.has_value(), "managed first run opens");
    std::filesystem::path recovery;
    if (first) {
        recovery = (*first)->recovery_path();
        check(std::filesystem::exists((*first)->session_root()),
              "private session directory exists");
        struct stat info {};
        check(::stat((*first)->session_root().c_str(), &info) == 0 && (info.st_mode & 0777) == 0700,
              "session directory mode private");
        const auto concurrent = LLMRunLease::open(binding);
        check(!concurrent && concurrent.error().find("busy") != std::string::npos,
              "same binding concurrent command busy");
        auto distinct = LLMRunLease::open(other_binding);
        check(distinct.has_value(), "independent binding allowed");
        if (distinct)
            check((*distinct)->recovery_path() != recovery, "binding namespaces isolated");
        first->reset();
        auto resumed = LLMRunLease::open(binding);
        check(resumed && (*resumed)->recovery_path() == recovery,
              "released lease resumes exact active namespace");
        if (resumed) {
            std::ofstream(recovery) << "checkpoint bytes";
            check((*resumed)->complete().has_value(), "managed completion durable");
            check(!std::filesystem::exists(recovery.parent_path()), "completed namespace cleaned");
            check((*resumed)->complete().has_value(), "completion idempotent");
            resumed->reset();
            auto fresh = LLMRunLease::open(binding);
            check(fresh && (*fresh)->recovery_path() != recovery,
                  "after completed new namespace allocated");
        }
    }
    ::chmod((temporary / ".ahfl").c_str(), 0755);
    auto prior_tutorial_directory = LLMRunLease::open(std::string(64, 'd'));
    check(prior_tutorial_directory.has_value(),
          "owned nonwritable preexisting ahfl directory accepted");
    if (prior_tutorial_directory)
        prior_tutorial_directory->reset();
    ::chmod((temporary / ".ahfl").c_str(), 0775);
    check(!LLMRunLease::open(std::string(64, 'd')), "shared writable ahfl directory rejected");
    ::chmod((temporary / ".ahfl").c_str(), 0755);
    const auto explicit_path = temporary / "manual-recovery.json";
    std::ofstream(explicit_path) << "manual checkpoint";
    auto explicit_lease = LLMRunLease::open(binding, explicit_path);
    check(explicit_lease && (*explicit_lease)->recovery_path() == explicit_path,
          "explicit recovery path preserved");
    if (explicit_lease) {
        check((*explicit_lease)->session_root() ==
                  std::filesystem::path(explicit_path.string() + ".llm"),
              "explicit session path derived");
        check((*explicit_lease)->complete().has_value() && std::filesystem::exists(explicit_path),
              "explicit completion preserves manual checkpoint");
        check(!LLMRunLease::open(binding, explicit_path), "explicit concurrent command busy");
        explicit_lease->reset();
    }
    const auto fresh_explicit_path = temporary / "fresh-explicit.json";
    auto fresh_explicit = LLMRunLease::open(other_binding, fresh_explicit_path);
    check(fresh_explicit.has_value(), "fresh explicit lease opens");
    if (fresh_explicit) {
        const auto root = (*fresh_explicit)->session_root();
        std::ofstream(fresh_explicit_path) << "fresh checkpoint";
        for (const auto *name : {"session.key", "session.index", "1.json"}) {
            const auto path = root / name;
            std::ofstream(path) << "private session fixture";
            ::chmod(path.c_str(), 0600);
        }
        check((*fresh_explicit)->complete(true).has_value(),
              "fresh explicit completion cleans state");
        check(!std::filesystem::exists(fresh_explicit_path), "fresh explicit checkpoint removed");
        check(std::filesystem::exists(root / ".run.lock") &&
                  std::distance(std::filesystem::directory_iterator(root),
                                std::filesystem::directory_iterator{}) == 1,
              "fresh explicit completion retains only lock inode");
        check((*fresh_explicit)->complete(true).has_value(), "fresh explicit cleanup idempotent");
        check(!LLMRunLease::open(other_binding, fresh_explicit_path),
              "explicit cleanup retains live lease exclusion");
        fresh_explicit->reset();
        auto next = LLMRunLease::open(other_binding, fresh_explicit_path);
        check(next.has_value(), "cleaned fresh explicit path opens again");
    }
    // The durable completion marker commits success before multi-file cleanup. Opening rolls
    // cleanup forward after process death, including when the checkpoint was already removed.
    for (const bool checkpoint_removed : {false, true}) {
        const auto interrupted_path =
            temporary / (checkpoint_removed ? "partial-cleanup.json" : "marker-only.json");
        auto interrupted = LLMRunLease::open(binding, interrupted_path);
        check(interrupted.has_value(), "interrupted explicit fixture opens");
        if (!interrupted)
            continue;
        const auto root = (*interrupted)->session_root();
        if (!checkpoint_removed)
            std::ofstream(interrupted_path) << "committed checkpoint";
        const auto record = root / "1.json";
        std::ofstream(record) << "remaining completed session";
        ::chmod(record.c_str(), 0600);
        const auto marker = root / ".completed.json";
        std::ofstream(marker) << "{\"version\":1,\"binding\":\"" << binding
                              << "\",\"namespace\":1,\"completed\":true}";
        ::chmod(marker.c_str(), 0600);
        interrupted->reset();
        auto rolled_forward = LLMRunLease::open(binding, interrupted_path);
        check(rolled_forward.has_value(), "durable explicit completion marker rolls forward");
        check(!std::filesystem::exists(interrupted_path) && !std::filesystem::exists(record) &&
                  !std::filesystem::exists(marker),
              "rollforward removes checkpoint remaining session and marker");
        check(std::filesystem::exists(root / ".run.lock"), "rollforward preserves lock inode");
        if (rolled_forward) {
            check(!LLMRunLease::open(binding, interrupted_path),
                  "rollforward acquires next run lease");
            std::ofstream(interrupted_path) << "new run checkpoint";
            check((*rolled_forward)->complete().has_value() &&
                      std::filesystem::exists(interrupted_path),
                  "rollforward does not mark the next explicit run completed");
        }
    }
    const auto previous_directory = std::filesystem::current_path();
    std::filesystem::current_path(temporary);
    auto relative = LLMRunLease::open(other_binding, "relative-recovery.json");
    check(relative &&
              (*relative)->recovery_path() == std::filesystem::path("relative-recovery.json"),
          "explicit relative filename supported");
    if (relative)
        relative->reset();
    std::filesystem::current_path(previous_directory);
    const auto binding_root = temporary / ".ahfl" / "runs" / binding;
    const auto manifest = binding_root / "active.json";
    {
        std::ofstream output(manifest);
        output << "{invalid}";
    }
    check(!LLMRunLease::open(binding), "malformed active manifest rejected");
    std::filesystem::remove(manifest);
    std::filesystem::create_symlink(explicit_path, manifest);
    check(!LLMRunLease::open(binding), "symlink active manifest rejected");
    std::filesystem::remove(manifest);
    ::chmod(binding_root.c_str(), 0755);
    check(!LLMRunLease::open(binding), "nonprivate run directory rejected");
    ::chmod(binding_root.c_str(), 0700);
    const auto symlink_path = temporary / "linked-recovery.json";
    std::filesystem::create_symlink(explicit_path, symlink_path);
    check(!LLMRunLease::open(binding, symlink_path), "explicit symlink recovery rejected");
    // A process that terminates without destructors still releases the OS lease; its active run is reused.
    const std::string crash_binding(64, 'c');
    const auto pid = ::fork();
    if (pid == 0) {
        const auto crash = LLMRunLease::open(crash_binding);
        ::_exit(crash ? 0 : 2);
    }
    int status{};
    check(pid > 0 && ::waitpid(pid, &status, 0) == pid && WIFEXITED(status) &&
              WEXITSTATUS(status) == 0,
          "child acquires lease then terminates");
    auto after_crash = LLMRunLease::open(crash_binding);
    check(after_crash.has_value(), "lease released automatically after process death");
    if (after_crash)
        after_crash->reset();
    if (had_home)
        ::setenv("HOME", saved_home.c_str(), 1);
    else
        ::unsetenv("HOME");
    std::filesystem::remove_all(temporary);
#else
    const auto unsupported = ahfl::cli::LLMRunLease::open(std::string(64, 'a'));
    check(!unsupported && unsupported.error().find("unavailable") != std::string::npos,
          "unsupported platform truthful limitation");
#endif
    std::cout << checks - failures << '/' << checks << " LLM run lease checks passed\n";
    return failures == 0 ? 0 : 1;
}
