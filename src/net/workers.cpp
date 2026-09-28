// src/net/workers.cpp
// Thread pool implementation for Lux.

#include "net/workers.hpp"

namespace engine {

WorkerPool::WorkerPool(size_t num_threads) : stop_(false) {
    set_thread_count(num_threads);
}

WorkerPool::~WorkerPool() {
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        stop_ = true;
    }
    condition_.notify_all();
    for (auto& t : threads_) {
        if (t.joinable()) t.join();
    }
}

void WorkerPool::set_thread_count(size_t n) {
    if (n == threads_.size()) return;
    size_t old_count = threads_.size();
    threads_.resize(n);
    for (size_t i = old_count; i < n; ++i) {
        threads_[i] = std::thread([this]() {
            while (true) {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(queue_mutex_);
                    condition_.wait(lock, [this]() { return stop_ || !tasks_.empty(); });
                    if (stop_ && tasks_.empty()) return;
                    task = std::move(tasks_.front());
                    tasks_.pop();
                }
                task();
            }
        });
    }
}

// submit es una plantilla: está definida en workers.hpp

} // namespace engine
