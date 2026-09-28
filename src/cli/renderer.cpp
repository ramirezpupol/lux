// src/cli/renderer.cpp
//
// Implementación del renderizador de progreso.
// Dibuja una única línea con \r: [barra] %  descargado/total  velocidad  ETA

#include "renderer.hpp"

#include <iostream>
#include <iomanip>
#include <sstream>
#include <cmath>

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace cli {

namespace {

bool detect_tty() {
#if defined(_WIN32)
    return _isatty(_fileno(stdout)) != 0;
#else
    return isatty(STDOUT_FILENO) != 0;
#endif
}

std::string fmt_bytes(unsigned long long bytes) {
    const char* units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    double v = static_cast<double>(bytes);
    int u = 0;
    while (v >= 1024.0 && u < 4) { v /= 1024.0; ++u; }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.1f %s", v, units[u]);
    return buf;
}

std::string fmt_eta(double seconds) {
    if (seconds <= 0.0 || std::isinf(seconds) || std::isnan(seconds))
        return "--:--";
    int total = static_cast<int>(seconds);
    int h = total / 3600;
    int m = (total % 3600) / 60;
    int s = total % 60;
    char buf[32];
    if (h > 0) std::snprintf(buf, sizeof(buf), "%d:%02d:%02d", h, m, s);
    else       std::snprintf(buf, sizeof(buf), "%02d:%02d", m, s);
    return buf;
}

} // namespace

Renderer::Renderer(const RendererConfig& cfg)
    : config_(cfg), start_time_(std::chrono::steady_clock::now()),
      last_render_(std::chrono::steady_clock::now()) {}

Renderer::~Renderer() {
    finish();
}

void Renderer::init() {
    if (initialized_) return;
    is_tty_ = detect_tty();
    if (is_tty_ && config_.use_ansi) {
        // Ocultar cursor durante la descarga
        std::cout << "\033[?25l";
        std::cout.flush();
    }
    start_time_ = std::chrono::steady_clock::now();
    initialized_ = true;
}

void Renderer::update(double percent,
                      unsigned long long downloaded,
                      unsigned long long total,
                      const std::string& file_name) {
    if (!enabled_.load()) return;

    std::lock_guard<std::mutex> lock(mtx_);

    // Limitar refresco a ~10 Hz para no saturar la terminal
    auto now = std::chrono::steady_clock::now();
    auto since = std::chrono::duration_cast<std::chrono::milliseconds>(
                     now - last_render_).count();
    if (since < 100 && percent < 100.0) return;
    last_render_ = now;

    std::ostringstream oss;

    if (is_tty_) {
        oss << "\r\033[K";  // retorno de carro + limpiar línea
    } else {
        // Sin TTY: actualizar de vez en cuando con nueva línea
        if (since < 1000) return;
        oss << "";
    }

    oss << config_.prefix << " " << file_name << " ";
    if (config_.width > 0) {
        oss << make_progress_bar(percent, config_.width) << " ";
    }
    oss << std::fixed << std::setprecision(1) << percent << "%  "
        << fmt_bytes(downloaded);
    if (total > 0) oss << "/" << fmt_bytes(total);

    // Velocidad y ETA
    double elapsed = std::chrono::duration<double>(now - start_time_).count();
    if (elapsed > 0.5 && downloaded > 0) {
        double speed = static_cast<double>(downloaded) / elapsed;
        oss << "  " << fmt_bytes(static_cast<unsigned long long>(speed)) << "/s";
        if (total > downloaded) {
            double eta = (static_cast<double>(total - downloaded)) / speed;
            oss << "  ETA " << fmt_eta(eta);
        }
    }

    std::cout << oss.str();
    if (!is_tty_) std::cout << "\n";
    std::cout.flush();
}

void Renderer::finish() {
    if (!initialized_) return;
    if (is_tty_ && config_.use_ansi) {
        std::cout << "\r\033[K\033[?25h";  // limpiar línea y restaurar cursor
    }
    std::cout.flush();
    initialized_ = false;
}

std::string make_progress_bar(double percent, int width) {
    if (width <= 0) return {};
    int pos = static_cast<int>(percent / 100.0 * width);
    pos = std::max(0, std::min(width, pos));
    std::string bar;
    bar.reserve(static_cast<size_t>(width) + 2);
    bar += "[";
    for (int i = 0; i < width; ++i) bar += (i < pos) ? "█" : "░";
    bar += "]";
    return bar;
}

} // namespace cli
