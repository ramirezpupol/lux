// src/cli/renderer.hpp
//
// Renderizador de la barra de progreso en la terminal.
// Actualiza la línea actual sin imprimir nuevas líneas (usando \r y ANSI).

#pragma once

#include <string>
#include <chrono>
#include <atomic>
#include <mutex>
#include <algorithm>

namespace cli {

// Configuración del renderizador
struct RendererConfig {
    bool enabled = true;
    int  width   = 30;          // caracteres de la barra
    std::string prefix = "[*]";
    bool use_ansi = true;
};

// Renderizador de progreso
class Renderer {
public:
    Renderer(const RendererConfig& cfg = RendererConfig());
    ~Renderer();

    // Inicializar el renderizado (oculta el cursor si hay TTY)
    void init();

    // Actualizar con un nuevo progreso (0..100, bytes, total, nombre)
    void update(double percent,
                unsigned long long downloaded,
                unsigned long long total,
                const std::string& file_name);

    // Finalizar (restaurar cursor y saltar línea)
    void finish();

    void disable() { enabled_ = false; }
    void enable()  { enabled_ = true; }

    void set_width(int w) { config_.width = std::max(10, w); }

private:
    RendererConfig config_;
    std::mutex     mtx_;
    std::atomic<bool> enabled_{true};
    bool           initialized_ = false;
    bool           is_tty_ = false;
    std::chrono::steady_clock::time_point start_time_;
    std::chrono::steady_clock::time_point last_render_;
};

// Barra visual: [██████░░░░░░]
std::string make_progress_bar(double percent, int width);

} // namespace cli
