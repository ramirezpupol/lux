// lux/src/main.cpp
//
// Punto de entrada de Lux, el descargador robusto por CLI.
//
// Compilación:
//   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
//   cmake --build build --config Release
//
// Uso:
//   lux --url <URL> --output <ruta> [opciones...]

#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <memory>
#include <thread>
#include <chrono>

#include "engine/dispatcher.hpp"
#include "engine/types.hpp"
#include "cli/renderer.hpp"
#include "wifi/manager.hpp"
#include "net/error.hpp"
#include "io/state_file.hpp"
#include "net/version.hpp"

namespace fs = std::filesystem;

using engine::lux_error;

// ---------------------------------------------------------------------------
// Ayuda
// ---------------------------------------------------------------------------
static void print_help(const char* prog) {
    std::cout <<
        "Lux — descargador robusto de archivos por CLI\n"
        "Versión: " << lux_version() << "\n\n"
        "Uso: " << prog << " --url <URL> --output <ruta> [opciones]\n\n"
        "Opciones:\n"
        "  -u, --url <url>          URL del archivo a descargar\n"
        "  -o, --output <ruta>      Ruta del archivo destino\n"
        "  -r, --recursive          Descarga recursiva de carpetas remotas\n"
        "  -w, --wifi <ssid> <pw>   Reconexión Wi-Fi automática (opcional)\n"
        "  -t, --threads <n>        Hilos de descarga (por defecto: 4)\n"
        "  -p, --processes <n>      Procesos para paralelizar (opcional)\n"
        "  -c, --continue           Continuar una descarga interrumpida\n"
        "  -v, --verbose            Registros detallados\n"
        "  -q, --quiet              Solo progreso, sin mensajes extra\n"
        "  -h, --help               Esta ayuda\n\n"
        "Ejemplos:\n"
        "  " << prog << " -u https://e.com/a.zip -o a.zip\n"
        "  " << prog << " -u https://e.com/folder/ -r -o ./folder\n"
        "  " << prog << " -u https://e.com/a.zip -o a.zip -w MiRed pw\n"
        "  " << prog << " -u https://e.com/a.zip -o a.zip -c\n";
}

// ---------------------------------------------------------------------------
// Parseo de línea de comandos
// ---------------------------------------------------------------------------
struct Config {
    std::string url;
    std::string output;
    bool recursive   = false;
    std::string wifi_ssid;
    std::string wifi_pw;
    int  threads     = engine::DEFAULT_THREADS;
    int  processes   = engine::DEFAULT_PROCESSES;
    bool continue_dl = false;
    bool verbose     = false;
    bool quiet       = false;
};

static void fail(const std::string& msg) {
    std::cerr << "[!] " << msg << "\n";
    std::exit(1);
}

static Config parse_args(int argc, char** argv) {
    Config cfg;
    std::vector<std::string> args(argv + 1, argv + argc);

    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        auto next = [&](const std::string& opt) {
            if (i + 1 >= args.size()) fail("Falta argumento para " + opt);
            return args[++i];
        };

        if (a == "-h" || a == "--help") { print_help(argv[0]); std::exit(0); }
        else if (a == "-u" || a == "--url")       cfg.url       = next("--url");
        else if (a == "-o" || a == "--output")    cfg.output    = next("--output");
        else if (a == "-r" || a == "--recursive") cfg.recursive = true;
        else if (a == "-w" || a == "--wifi") {
            cfg.wifi_ssid = next("--wifi");
            // La contraseña puede venir después; si no, queda vacía
            if (i + 1 < args.size() && args[i + 1][0] != '-') {
                cfg.wifi_pw = args[++i];
            }
        }
        else if (a == "-t" || a == "--threads")   cfg.threads   = std::stoi(next("--threads"));
        else if (a == "-p" || a == "--processes") cfg.processes = std::stoi(next("--processes"));
        else if (a == "-c" || a == "--continue")  cfg.continue_dl = true;
        else if (a == "-v" || a == "--verbose")   cfg.verbose   = true;
        else if (a == "-q" || a == "--quiet")     cfg.quiet     = true;
        else fail("Opción desconocida: " + a);
    }

    if (cfg.url.empty() || cfg.output.empty())
        fail("Faltan --url o --output. Usa -h para ayuda.");

    if (cfg.threads < 1) fail("El número de hilos debe ser >= 1");

    return cfg;
}

// ---------------------------------------------------------------------------
// Punto de entrada
// ---------------------------------------------------------------------------
int main(int argc, char** argv) {
    try {
        Config cfg = parse_args(argc, argv);

        if (!cfg.quiet) {
            std::cout << "[*] Lux " << lux_version() << "\n";
            std::cout << "[*] URL: " << cfg.url << "\n";
            std::cout << "[*] Destino: " << cfg.output << "\n";
        }

        // 1. Reconexión Wi-Fi si se indica
        if (!cfg.wifi_ssid.empty()) {
            wifi::Manager wifi_man(cfg.wifi_ssid, cfg.wifi_pw);
            if (!wifi_man.connect_if_needed()) {
                fail("No se pudo establecer la conexión Wi-Fi '" + cfg.wifi_ssid + "'");
            }
            if (!cfg.quiet)
                std::cout << "[*] Wi-Fi conectado: " << cfg.wifi_ssid << "\n";
        }

        // 2. Estado de reanudación
        auto state_file = io::StateFile::open(cfg.output);

        // 3. Motor de descarga
        engine::Dispatcher dispatcher(cfg.url, cfg.output, cfg.recursive);
        dispatcher.set_threads(cfg.threads);
        dispatcher.set_processes(cfg.processes);
        dispatcher.set_quiet(cfg.quiet);
        dispatcher.set_verbose(cfg.verbose);
        dispatcher.set_state_file(state_file);

        if (cfg.continue_dl) {
            if (!dispatcher.resume_from_state()) {
                if (!cfg.quiet)
                    std::cout << "[*] No se encontró un estado guardado; empezando de nuevo.\n";
            } else {
                if (!cfg.quiet)
                    std::cout << "[*] Continuando descarga desde el punto guardado.\n";
            }
        }

        // 4. Renderizador de progreso (barra en una sola línea)
        cli::Renderer renderer;
        renderer.init();
        dispatcher.set_progress_observer(
            [&renderer](const engine::Progress& p) {
                renderer.update(p.percent, p.downloaded, p.total, p.file_name);
            });

        // 5. Ejecutar (con bucle de reintentos global ante fallo)
        bool ok = false;
        const int kMaxRounds = 10;
        for (int round = 0; round < kMaxRounds && !ok; ++round) {
            ok = dispatcher.download();
            if (!ok && !cfg.quiet) {
                std::cerr << "[i] Ronda " << (round + 1) << " fallida; "
                          << "reintentando descarga completa...\n";
                // Habilitar reanudación automática entre rondas
                dispatcher.resume_from_state();
                std::this_thread::sleep_for(std::chrono::seconds(2));
            }
        }
        renderer.finish();

        if (!ok) {
            std::cerr << "[!] No se pudo completar la descarga.\n";
            return 2;
        }
    } catch (const lux_error& e) {
        std::cerr << "[!] Lux: " << e.what() << "\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "[!] Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
