// src/engine/types.hpp
//
// Tipos de datos compartidos entre los componentes del motor de descarga.

#pragma once

#include <cstdint>
#include <string>
#include <chrono>
#include <stdexcept>

namespace engine {

// Constantes de configuración por defecto
inline constexpr int DEFAULT_THREADS   = 4;
inline constexpr int DEFAULT_PROCESSES = 0;  // 0 => sin multiproceso

// Excepción para errores fatales de la aplicación
class lux_error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct Progress {
    double   percent = 0.0;      // 0.0 - 100.0
    uint64_t downloaded = 0;     // bytes descargados
    uint64_t total = 0;          // bytes totales (0 si desconocido)
    std::string file_name;       // nombre del archivo actual
    std::chrono::steady_clock::time_point start_time;
    std::chrono::steady_clock::time_point last_update;

    Progress()
        : start_time(std::chrono::steady_clock::now()),
          last_update(std::chrono::steady_clock::now()) {}
};

// Estado del archivo (para reanudación)
struct DownloadState {
    std::string url;
    std::string output_path;
    uint64_t bytes_downloaded = 0;   // bytes completados
    bool     is_recursive = false;
    std::string last_error;
    std::chrono::steady_clock::time_point last_update;
    int      retry_count = 0;
    int      threads = 4;

    DownloadState()
        : last_update(std::chrono::steady_clock::now()) {}
};

} // namespace engine
