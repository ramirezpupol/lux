// src/engine/progress_tracker.hpp
//
// Rastreador de progreso de descarga.

#pragma once

#include <iostream>
#include <mutex>
#include <string>
#include <chrono>
#include <atomic>

#include "engine/types.hpp"

namespace engine {

// Rastreador de progreso central
class ProgressTracker {
public:
    ProgressTracker() = default;

    void update(const Progress& p);

    void set_file_name(const std::string& name) {
        std::lock_guard<std::mutex> lock(mtx_);
        current_file_ = name;
    }

    const std::string& current_file() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return current_file_;
    }

    uint64_t total_downloaded() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return total_downloaded_;
    }

    uint64_t completed_bytes() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return completed_bytes_;
    }

    void reset();

private:
    mutable std::mutex mtx_;
    std::string current_file_;
    uint64_t total_downloaded_ = 0;
    uint64_t completed_bytes_ = 0;
    std::chrono::steady_clock::time_point last_update_;
};

// Delegación al renderer de la CLI
extern ProgressTracker& global_tracker();

} // namespace engine
