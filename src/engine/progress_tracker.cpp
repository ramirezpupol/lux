// src/engine/progress_tracker.cpp
//
// Implementación del rastreador de progreso.

#include "engine/progress_tracker.hpp"

namespace engine {

// Instancia global del rastreador
namespace {
engine::ProgressTracker instance;
}

ProgressTracker& global_tracker() {
    return instance;
}

void ProgressTracker::update(const Progress& p) {
    std::lock_guard<std::mutex> lock(mtx_);

    total_downloaded_ += p.downloaded;
    completed_bytes_ += p.downloaded; // simplificado: bytes descargados en este paso

    current_file_ = p.file_name;
    last_update_ = p.last_update;
}

void ProgressTracker::reset() {
    std::lock_guard<std::mutex> lock(mtx_);
    total_downloaded_ = 0;
    completed_bytes_ = 0;
    current_file_ = "";
    last_update_ = std::chrono::steady_clock::now();
}

} // namespace engine
