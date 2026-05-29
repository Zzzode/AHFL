#include "runtime/engine/sandbox.hpp"
#include <cstdio>

static int test_count = 0;
static int pass_count = 0;

static void check(bool condition, const char* name) {
    test_count++;
    if (condition) {
        pass_count++;
    } else {
        std::printf("FAIL: %s\n", name);
    }
}

int main() {
    // Test 1: Network policy - denied when disabled
    {
        ahfl::runtime::SandboxConfig config;
        config.network.allow_network = false;
        ahfl::runtime::Sandbox sandbox(config);

        check(!sandbox.check_network_access("example.com", 443), "network denied when disabled");
    }

    // Test 2: Network policy - host allow-list
    {
        ahfl::runtime::SandboxConfig config;
        config.network.allow_network = true;
        config.network.allowed_hosts = {"api.example.com", "cdn.example.com"};
        ahfl::runtime::Sandbox sandbox(config);

        check(sandbox.check_network_access("api.example.com", 443), "allowed host passes");
        check(!sandbox.check_network_access("evil.com", 443), "blocked host denied");
    }

    // Test 3: Filesystem isolation
    {
        ahfl::runtime::SandboxConfig config;
        config.filesystem.allow_filesystem = true;
        config.filesystem.read_paths = {"/data/input/"};
        config.filesystem.write_paths = {"/tmp/output/"};
        ahfl::runtime::Sandbox sandbox(config);

        check(sandbox.check_fs_read("/data/input/file.txt"), "read allowed path");
        check(!sandbox.check_fs_read("/etc/passwd"), "read denied path");
        check(sandbox.check_fs_write("/tmp/output/result.json"), "write allowed path");
        check(!sandbox.check_fs_write("/etc/shadow"), "write denied path");
    }

    // Test 4: Resource quotas
    {
        ahfl::runtime::SandboxConfig config;
        config.resources.max_memory_bytes = 1024 * 1024;  // 1 MB
        config.resources.max_threads = 4;
        ahfl::runtime::Sandbox sandbox(config);

        check(sandbox.check_memory(512 * 1024), "memory within quota");
        check(!sandbox.check_memory(2 * 1024 * 1024), "memory exceeds quota");
        check(sandbox.check_thread_count(4), "threads within quota");
        check(!sandbox.check_thread_count(8), "threads exceed quota");
    }

    // Test 5: Default deny filesystem
    {
        ahfl::runtime::SandboxConfig config;
        config.filesystem.allow_filesystem = false;
        ahfl::runtime::Sandbox sandbox(config);

        check(!sandbox.check_fs_read("/any/path"), "fs read denied when disabled");
        check(!sandbox.check_fs_write("/any/path"), "fs write denied when disabled");
    }

    std::printf("%d/%d tests passed\n", pass_count, test_count);
    return (pass_count == test_count) ? 0 : 1;
}
