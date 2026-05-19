#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <future>
#include <mutex>
#include <thread>
#include <vector>

namespace ahfl::support {

class ThreadPool {
public:
    explicit ThreadPool(std::size_t num_threads = 0);
    ~ThreadPool();

    ThreadPool(const ThreadPool &) = delete;
    ThreadPool &operator=(const ThreadPool &) = delete;

    template <typename F>
    [[nodiscard]] auto submit(F &&task) -> std::future<std::invoke_result_t<F>> {
        using R = std::invoke_result_t<F>;
        auto packaged = std::make_shared<std::packaged_task<R()>>(std::forward<F>(task));
        auto future = packaged->get_future();
        {
            std::lock_guard lock(mutex_);
            tasks_.emplace_back([packaged]() { (*packaged)(); });
        }
        cv_.notify_one();
        return future;
    }

    [[nodiscard]] std::size_t thread_count() const;
    void shutdown();

private:
    void worker_loop();

    std::vector<std::thread> workers_;
    std::deque<std::function<void()>> tasks_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    bool stopped_{false};
};

} // namespace ahfl::support
