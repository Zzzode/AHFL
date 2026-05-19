#include "ahfl/support/thread_pool.hpp"

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

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

void test_basic_submit() {
    ahfl::support::ThreadPool pool(2);
    auto f = pool.submit([]() { return 42; });
    check(f.get() == 42, "basic_submit.returns_value");
}

void test_parallel_execution() {
    ahfl::support::ThreadPool pool(4);
    std::atomic<int> counter{0};
    std::vector<std::future<void>> futures;
    for (int i = 0; i < 100; ++i) {
        futures.push_back(pool.submit([&counter]() { counter.fetch_add(1); }));
    }
    for (auto &f : futures) f.get();
    check(counter.load() == 100, "parallel.all_executed");
}

void test_thread_count() {
    ahfl::support::ThreadPool pool(3);
    check(pool.thread_count() == 3, "thread_count.matches");
}

} // anonymous namespace

int main() {
    test_basic_submit();
    test_parallel_execution();
    test_thread_count();
    std::cout << pass_count << "/" << test_count << " tests passed\n";
    return (pass_count == test_count) ? EXIT_SUCCESS : EXIT_FAILURE;
}
