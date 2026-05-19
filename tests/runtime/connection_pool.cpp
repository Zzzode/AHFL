#include "ahfl/runtime/connection_pool.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

using namespace ahfl::runtime;

int test_count = 0;
int pass_count = 0;

void check(bool condition, const std::string &test_name) {
    ++test_count;
    if (condition) {
        ++pass_count;
    } else {
        std::cerr << "FAIL: " << test_name << "\n";
    }
}

void test_under_limit_allowed() {
    ConnectionPool pool(ConnectionPool::Config{.max_connections = 4, .max_per_host = 2});
    check(pool.can_connect("host_a"), "under_limit.can_connect");
    check(pool.active_count() == 0, "under_limit.initial_zero");
}

void test_over_global_limit_rejected() {
    ConnectionPool pool(ConnectionPool::Config{.max_connections = 2, .max_per_host = 2});
    pool.record_start("host_a");
    pool.record_start("host_b");
    check(pool.active_count() == 2, "global_limit.count_2");
    check(!pool.can_connect("host_c"), "global_limit.rejected");
}

void test_over_host_limit_rejected() {
    ConnectionPool pool(ConnectionPool::Config{.max_connections = 8, .max_per_host = 2});
    pool.record_start("host_a");
    pool.record_start("host_a");
    check(pool.active_for_host("host_a") == 2, "host_limit.count_2");
    check(!pool.can_connect("host_a"), "host_limit.rejected");
    // Different host should still be allowed
    check(pool.can_connect("host_b"), "host_limit.other_allowed");
}

void test_record_end_frees_slot() {
    ConnectionPool pool(ConnectionPool::Config{.max_connections = 1, .max_per_host = 1});
    pool.record_start("host_a");
    check(!pool.can_connect("host_a"), "free_slot.full");
    pool.record_end("host_a");
    check(pool.can_connect("host_a"), "free_slot.freed");
    check(pool.active_count() == 0, "free_slot.zero_active");
}

void test_evict_idle_cleans() {
    ConnectionPool pool(ConnectionPool::Config{
        .max_connections = 8,
        .max_per_host = 4,
        .idle_timeout = std::chrono::seconds{0}, // immediate expiry
    });
    pool.record_start("host_a");
    pool.record_end("host_a");
    // After record_end, the entry has 0 active and should be eligible for eviction
    pool.evict_idle();
    check(pool.active_for_host("host_a") == 0, "evict_idle.cleaned");
}

} // anonymous namespace

int main() {
    test_under_limit_allowed();
    test_over_global_limit_rejected();
    test_over_host_limit_rejected();
    test_record_end_frees_slot();
    test_evict_idle_cleans();

    std::cout << pass_count << "/" << test_count << " tests passed\n";
    return (pass_count == test_count) ? EXIT_SUCCESS : EXIT_FAILURE;
}
