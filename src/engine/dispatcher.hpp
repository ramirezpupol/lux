// src/engine/dispatcher.hpp
//
// Motor de descarga de Lux.
// Gestiona la descarga (multihilo por rangos), la reanudación,
// la descarga recursiva y el reporte de progreso.

#pragma once

#include <string>
#include <filesystem>
#include <memory>
#include <vector>
#include <functional>
#include <atomic>
#include <mutex>
#include <fstream>

#include "engine/types.hpp"
#include "net/remote_folder_parser.hpp"
#include "net/backend.hpp"
#include "io/state_file.hpp"

namespace fs = std::filesystem;

namespace engine {

class Dispatcher {
public:
    Dispatcher(const std::string& url,
               const std::string& output,
               bool recursive = false);
    ~Dispatcher();

    // Configuración
    void set_threads(int n) { threads_ = (n > 0) ? n : DEFAULT_THREADS; }
    void set_processes(int n) { processes_ = (n > 0) ? n : DEFAULT_PROCESSES; }
    void set_max_retries(int n) { max_retries_ = (n > 0) ? n : 1; }
    void set_quiet(bool q) { quiet_ = q; }
    void set_verbose(bool v) { verbose_ = v; }
    void set_state_file(const std::shared_ptr<io::StateFile>& sf) { state_file_ = sf; }

    // Suscribirse a notificaciones de progreso
    void set_progress_observer(std::function<void(const Progress&)> obs) {
        on_progress_ = std::move(obs);
    }

    // Flujo principal: descarga hasta completar (con reintentos internos)
    // Devuelve true si la descarga terminó correctamente.
    bool download();

    // Reanuda desde el último estado guardado (si existe)
    bool resume_from_state();

    // Bytes descargados en total (para el resumen final)
    uint64_t bytes_downloaded() const { return bytes_downloaded_.load(); }

private:
    // Descarga un archivo individual (con reintentos y reanudación por Range)
    bool download_single(const std::string& url, const fs::path& output_path);

    // Descarga recursiva de carpetas remotas
    bool download_recursive(const std::string& folder_url,
                            const fs::path& base_output,
                            int depth);

    // Sonda el recurso: tamaño total, soporte de rangos, tipo de contenido
    struct ResourceInfo {
        uint64_t total_size = 0;
        bool     accepts_ranges = false;
        bool     is_html = false;
        bool     ok = false;
    };
    ResourceInfo probe(const std::string& url);

    // Descarga en paralelo por rangos usando varios hilos
    bool download_parallel(const std::string& url,
                           const fs::path& output_path,
                           uint64_t total_size,
                           int worker_count);

    // Descarga secuencial (un solo hulo, con reanudación por Range)
    bool download_sequential(const std::string& url,
                             const fs::path& output_path,
                             uint64_t total_size);

    // Manejo de errores de red: decide si reintentar o abortar
    int handle_network_error(const std::string& error_msg);

    // Notificar progreso al observador
    void notify_progress(uint64_t downloaded_now, uint64_t total);

    // Miembros
    std::string url_;
    fs::path    output_;
    bool        recursive_ = false;

    int  threads_ = DEFAULT_THREADS;
    int  processes_ = DEFAULT_PROCESSES;
    int  max_retries_ = 5;
    bool quiet_ = false;
    bool verbose_ = false;

    std::shared_ptr<io::StateFile> state_file_;
    std::function<void(const Progress&)> on_progress_;

    // Contadores y estado compartido entre hilos
    std::atomic<uint64_t> bytes_downloaded_{0};
    std::atomic<int>      active_errors_{0};
    std::mutex            io_mutex_;          // protege escritura del archivo y estado
    std::mutex            progress_mutex_;    // agrupa actualizaciones de progreso
    std::chrono::steady_clock::time_point start_time_;
    std::string           current_file_;
    std::string           recursive_root_;          // prefijo del subárbol recursivo
    fs::path              recursive_output_root_;   // raíz de salida de la recursión
};

} // namespace engine
