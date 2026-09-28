// src/net/workers.hpp
//
// Pool de hilos de I/O para maximizar el rendimiento de descarga.

#pragma once

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>
#include <future>
#include <stdexcept>

namespace engine {

class WorkerPool {
public:
    explicit WorkerPool(size_t num_threads);
    ~WorkerPool();

    WorkerPool(const WorkerPool&) = delete;
    WorkerPool& operator=(const WorkerPool&) = delete;

    void set_thread_count(size_t n);
    size_t thread_count() const { return threads_.size(); }

    // Las plantillas deben definirse en el header
    template <typename F, typename... Args>
    auto submit(F&& f, Args&&... args)
        -> std::future<decltype(f(args...))> {
        using return_type = decltype(f(args...));
        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...));
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            if (stop_) {
                return std::future<return_type>();
            }
            tasks_.push([task]() { (*task)(); });
        }
        condition_.notify_one();
        return task->get_future();
    }

private:
    std::vector<std::thread> threads_;
    std::queue<std::function<void()>> tasks_;
    std::mutex queue_mutex_;
    std::condition_variable condition_;
    std::atomic<bool> stop_;
};

} // namespace engine
