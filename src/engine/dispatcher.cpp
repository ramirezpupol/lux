// src/engine/dispatcher.cpp
//
// Implementación del motor de descarga de Lux.
//
// Estrategia:
//  1. Sonda la URL (HEAD): tamaño total, aceptación de rangos, tipo de contenido.
//  2. Si acepta rangos y el archivo es grande -> descarga en paralelo por rangos
//     (N hilos escriben a su desplazamiento correspondiente).
//  3. Si no -> descarga secuencial con reanudación por Range desde el tamaño
//     local del archivo parcial.
//  4. Ante fallo de red: reintento con backoff exponencial; clasificación del
//     error (timeout, DNS, red caída...) para decidir la estrategia.
//  5. Progreso: se agrega entre hilos y se notifica a un observador (la CLI
//     renderiza una barra en una sola línea con \r).

#include "engine/dispatcher.hpp"

#include <iostream>
#include <algorithm>
#include <thread>
#include <chrono>
#include <cmath>
#include <cstdio>

#include "net/error.hpp"
#include "net/http_client.hpp"
#include "io/filesystem_utils.hpp"
#include "net/workers.hpp"

namespace fs = std::filesystem;

namespace engine {

namespace {
constexpr size_t  kMinParallelSize = 8u * 1024u * 1024u; // 8 MiB para paralelizar
constexpr uint64_t kMinChunk       = 1u * 1024u * 1024u; // chunks >= 1 MiB

std::string human_size(uint64_t bytes) {
    const char* units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    double v = static_cast<double>(bytes);
    int u = 0;
    while (v >= 1024.0 && u < 4) { v /= 1024.0; ++u; }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.1f %s", v, units[u]);
    return buf;
}
} // namespace

Dispatcher::Dispatcher(const std::string& url,
                       const std::string& output,
                       bool recursive)
    : url_(url), output_(output), recursive_(recursive) {}

Dispatcher::~Dispatcher() = default;

// ---------------------------------------------------------------------------
// Progreso
// ---------------------------------------------------------------------------
void Dispatcher::notify_progress(uint64_t downloaded_now, uint64_t total) {
    if (!on_progress_) return;
    Progress p;
    p.downloaded = downloaded_now;
    p.total = total;
    p.percent = (total > 0)
        ? (static_cast<double>(downloaded_now) / static_cast<double>(total)) * 100.0
        : 0.0;
    p.file_name = current_file_;
    p.start_time = start_time_;
    p.last_update = std::chrono::steady_clock::now();
    on_progress_(p);
}

// ---------------------------------------------------------------------------
// Clasificación de errores (texto curl -> decisión)
// ---------------------------------------------------------------------------
int Dispatcher::handle_network_error(const std::string& error_msg) {
    if (error_msg.find("timeout") != std::string::npos ||
        error_msg.find("Timeout") != std::string::npos) {
        return 1; // timeout: reintentar con backoff
    }
    if (error_msg.find("Couldn't resolve host") != std::string::npos ||
        error_msg.find("Couldn't resolve proxy") != std::string::npos) {
        return 0; // DNS inválido: la URL no existe, no reintentar en bucle
    }
    if (error_msg.find("network down") != std::string::npos ||
        error_msg.find("NetworkUnreachable") != std::string::npos ||
        error_msg.find("NetUnreachable") != std::string::npos ||
        error_msg.find("Connection reset") != std::string::npos ||
        error_msg.find("Connection refused") != std::string::npos ||
        error_msg.find("Resolving timed out") != std::string::npos ||
        error_msg.find("DNS") != std::string::npos) {
        return 2; // red caída: reintentar más fuerte (posible WiFi)
    }
    return 0; // error no recuperable
}

// ---------------------------------------------------------------------------
// Sonda del recurso (HEAD)
// ---------------------------------------------------------------------------
Dispatcher::ResourceInfo Dispatcher::probe(const std::string& url) {
    ResourceInfo info;

    HttpClient client;
    auto resp = client.head(url);
    if (!resp.ok) {
        info.ok = false;
        return info;
    }

    info.ok = true;
    info.total_size = resp.content_length;
    auto it = resp.headers.find("accept-ranges");
    info.accepts_ranges = (it != resp.headers.end() &&
                           it->second.find("bytes") != std::string::npos);
    auto ct = resp.headers.find("content-type");
    if (ct != resp.headers.end()) {
        info.is_html = ct->second.find("text/html") != std::string::npos;
    }
    return info;
}

// ---------------------------------------------------------------------------
// Descarga en paralelo por rangos
// ---------------------------------------------------------------------------
bool Dispatcher::download_parallel(const std::string& url,
                                   const fs::path& output_path,
                                   uint64_t total_size,
                                   int worker_count) {
    if (!io::ensure_parent_dirs(output_path.string())) return false;

    // Preasignar archivo
    {
        std::ofstream pre(output_path, std::ios::binary | std::ios::out);
        if (!pre.is_open()) {
            std::cerr << "[!] No se pudo crear " << output_path << "\n";
            return false;
        }
        pre.seekp(static_cast<std::streamoff>(total_size - 1));
        pre.put('\0');
    }

    // Dividir en rangos
    uint64_t per_worker = std::max<uint64_t>(total_size / worker_count, kMinChunk);
    struct Range { uint64_t start; uint64_t end; };
    std::vector<Range> ranges;
    for (uint64_t off = 0; off < total_size; off += per_worker) {
        uint64_t end = std::min(off + per_worker, total_size);
        ranges.push_back({off, end});
        if (end >= total_size) break;
    }

    std::atomic<uint64_t> completed{0};
    std::atomic<bool>     failed{false};
    std::mutex            file_mutex;

    WorkerPool pool(static_cast<size_t>(worker_count));
    std::vector<std::future<void>> futures;

    for (const auto& r : ranges) {
        futures.push_back(pool.submit([&, r]() {
            HttpClient client;
            int attempt = 0;
            while (attempt < max_retries_ && !failed.load()) {
                auto resp = client.get_range(url, r.start, r.end);
                if (resp.ok && resp.is_success()) {
                    std::lock_guard<std::mutex> lock(file_mutex);
                    std::ofstream out(output_path, std::ios::binary | std::ios::in | std::ios::out);
                    out.seekp(static_cast<std::streamoff>(r.start));
                    out.write(resp.body.data(), static_cast<std::streamsize>(resp.body.size()));
                    if (out.good()) {
                        completed.fetch_add(resp.body.size());
                        return;
                    }
                }
                ++attempt;
                int cls = handle_network_error(client.last_error());
                if (cls == 0) break;  // error no recuperable: abortar este rango
                int backoff = std::min(1 << std::min(attempt, 5), 30);
                if (cls == 2) backoff = std::min(backoff * 2, 60);
                std::this_thread::sleep_for(std::chrono::seconds(backoff));
            }
            failed.store(true);
        }));
    }

    for (auto& f : futures) f.wait();
    (void)completed;
    return !failed.load();
}

// ---------------------------------------------------------------------------
// Descarga secuencial con reanudación
// ---------------------------------------------------------------------------
bool Dispatcher::download_sequential(const std::string& url,
                                     const fs::path& output_path,
                                     uint64_t total_size) {
    if (!io::ensure_parent_dirs(output_path.string())) return false;

    uint64_t existing = io::file_size(output_path.string()).value_or(0);
    if (total_size > 0 && existing >= total_size) {
        // Ya está completo
        return true;
    }
    HttpClient client;
    int attempt = 0;
    while (attempt < max_retries_) {
        auto resp = (existing > 0)
            ? client.get_range(url, existing, 0)   // desde 'existing' hasta el final
            : client.get(url);

        if (resp.ok && (resp.is_success() ||
                        resp.status_code == 206 /* Partial Content */)) {
            std::ofstream out(output_path,
                              std::ios::binary | std::ios::app);
            if (!out.is_open()) {
                std::cerr << "[!] No se pudo abrir " << output_path << " para escritura\n";
                return false;
            }
            out.write(resp.body.data(), static_cast<std::streamsize>(resp.body.size()));
            out.close();

            uint64_t file_now = io::file_size(output_path.string()).value_or(0);
            notify_progress(file_now, total_size);

            // Guardar estado para reanudación
            if (state_file_) {
                DownloadState st;
                st.url = url;
                st.output_path = output_path.string();
                st.bytes_downloaded = io::file_size(output_path.string()).value_or(0);
                st.is_recursive = recursive_;
                st.threads = threads_;
                state_file_->save(st);
            }
            return true;
        }

        ++attempt;
        int cls = handle_network_error(client.last_error());
        if (cls == 0) return false;  // error no recuperable (DNS, etc.): abortar
        int backoff = std::min(1 << std::min(attempt, 5), 30);
        if (cls == 2) backoff = std::min(backoff * 2, 60);
        if (verbose_)
            std::cerr << "[i] Reintento " << attempt << "/" << max_retries_
                      << " en " << backoff << "s\n";
        std::this_thread::sleep_for(std::chrono::seconds(backoff));
        existing = io::file_size(output_path.string()).value_or(0);
    }
    return false;
}

// ---------------------------------------------------------------------------
// Descarga de un archivo individual
// ---------------------------------------------------------------------------
bool Dispatcher::download_single(const std::string& url, const fs::path& output_path) {
    current_file_ = output_path.filename().string();
    if (!quiet_)
        std::cout << "[*] Descargando: " << current_file_ << "\n";

    ResourceInfo info = probe(url);
    if (!info.ok) {
        // Sin HEAD: intentar descarga directa secuencial
        return download_sequential(url, output_path, 0);
    }

    uint64_t total = info.total_size;
    bool use_parallel = info.accepts_ranges &&
                        total >= kMinParallelSize &&
                        threads_ > 1;

    bool ok = false;
    if (use_parallel) {
        if (!quiet_)
            std::cout << "[*] Modo paralelo: " << threads_
                      << " hilos, " << human_size(total) << "\n";
        ok = download_parallel(url, output_path, total, threads_);
        if (ok) bytes_downloaded_.fetch_add(total);
    } else {
        ok = download_sequential(url, output_path, total);
    }

    notify_progress(ok ? total : 0, total);
    if (ok && !quiet_) {
        std::cout << "[✓] Completado: " << output_path.string()
                  << " (" << human_size(total) << ")\n";
    }
    return ok;
}

// ---------------------------------------------------------------------------
// Descarga recursiva (limitada al subárbol de la URL raíz)
// ---------------------------------------------------------------------------
bool Dispatcher::download_recursive(const std::string& folder_url,
                                    const fs::path& base_output,
                                    int depth) {
    if (depth > 5) return true; // límite de profundidad

    // Fijar una sola vez la raíz de salida: todos los niveles escriben
    // relativo a ella para no duplicar componentes de ruta
    if (depth == 0) {
        recursive_output_root_ = base_output;
        recursive_root_.clear();
    }

    HttpClient client;
    auto resp = client.get(folder_url);
    if (!resp.ok || !resp.is_success()) {
        std::cerr << "[!] No se pudo listar " << folder_url << "\n";
        return false;
    }

    auto resources = parse_resources_from_html(resp.body, folder_url);
    if (resources.empty()) {
        std::cout << "[i] Sin recursos en " << folder_url << "\n";
        return true;
    }

    // La URL raíz define el subárbol permitido (profundidad 0)
    if (recursive_root_.empty()) recursive_root_ = url_;
    std::string root_prefix = recursive_root_;
    while (!root_prefix.empty() && root_prefix.back() != '/')
        root_prefix.pop_back();

    fs::create_directories(recursive_output_root_);
    bool all_ok = true;
    for (const auto& res : resources) {
        // Solo descender dentro del subárbol de la URL raíz
        if (res.url.rfind(root_prefix, 0) != 0) continue;

        // Ruta relativa al directorio raíz (preserva jerarquía)
        std::string rel = res.url.substr(root_prefix.size());
        while (!rel.empty() && rel.front() == '/') rel.erase(0, 1);
        if (rel.empty()) continue;
        fs::path target = recursive_output_root_ / fs::path(rel);

        if (res.is_directory) {
            if (!download_recursive(res.url, target, depth + 1)) all_ok = false;
        } else {
            if (!download_single(res.url, target)) all_ok = false;
        }
    }
    return all_ok;
}

// ---------------------------------------------------------------------------
// Flujo principal
// ---------------------------------------------------------------------------
bool Dispatcher::download() {
    start_time_ = std::chrono::steady_clock::now();

    bool ok = false;
    ResourceInfo info = probe(url_);

    bool treat_as_folder = recursive_ && info.ok && info.is_html;

    if (treat_as_folder) {
        ok = download_recursive(url_, output_, 0);
    } else {
        ok = download_single(url_, output_);
    }

    // Guardar estado final
    if (state_file_) {
        DownloadState st;
        st.url = url_;
        st.output_path = output_.string();
        st.bytes_downloaded = bytes_downloaded_.load();
        st.is_recursive = recursive_;
        st.threads = threads_;
        st.last_error = ok ? "" : "descarga incompleta";
        state_file_->save(st);
    }

    if (!quiet_) {
        auto secs = std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::steady_clock::now() - start_time_).count();
        std::cout << (ok ? "[✓] Descarga finalizada en " : "[!] Descarga fallida tras ")
                  << secs << "s\n";
    }
    return ok;
}

// ---------------------------------------------------------------------------
// Reanudación desde estado guardado
// ---------------------------------------------------------------------------
bool Dispatcher::resume_from_state() {
    if (!state_file_) return false;
    auto states = state_file_->load();
    if (states.empty()) return false;

    const auto& s = states.front();
    if (s.url.empty()) return false;

    url_ = s.url;
    output_ = s.output_path;

    if (!quiet_)
        std::cout << "[*] Reanudando: " << bytes_downloaded_
                  << " bytes ya descargados\n";
    return true;
}

} // namespace engine
