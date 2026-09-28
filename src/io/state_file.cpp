// src/io/state_file.cpp
//
// Implementación del archivo de estado para reanudación.

#include "state_file.hpp"

#include <iostream>
#include <system_error>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <filesystem>

namespace io {

namespace fs = std::filesystem;

namespace {

nlohmann::json state_to_json(const engine::DownloadState& s) {
    nlohmann::json j;
    j["url"]              = s.url;
    j["output_path"]      = s.output_path;
    j["bytes_downloaded"] = s.bytes_downloaded;
    j["is_recursive"]     = s.is_recursive;
    j["last_error"]       = s.last_error;
    j["retry_count"]      = s.retry_count;
    j["threads"]          = s.threads;
    return j;
}

engine::DownloadState state_from_json(const nlohmann::json& j) {
    engine::DownloadState s;
    s.url              = j.value("url", std::string());
    s.output_path      = j.value("output_path", std::string());
    s.bytes_downloaded = j.value("bytes_downloaded", 0ULL);
    s.is_recursive     = j.value("is_recursive", false);
    s.last_error       = j.value("last_error", std::string());
    s.retry_count      = j.value("retry_count", 0);
    s.threads          = j.value("threads", 4);
    return s;
}

bool write_states(const std::string& path,
                  const std::vector<engine::DownloadState>& states) {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& s : states) arr.push_back(state_to_json(s));

    // Escritura atómica: primero a .tmp y luego renombrar
    std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out.is_open()) {
            std::cerr << "[!] No se pudo abrir " << tmp << " para escritura\n";
            return false;
        }
        out << arr.dump(4) << std::endl;
    }
    std::error_code ec;
    fs::rename(tmp, path, ec);
    if (ec) {
        // Fallback: copia directa
        fs::copy_file(tmp, path, fs::copy_options::overwrite_existing, ec);
        if (ec) return false;
        fs::remove(tmp, ec);
    }
    return true;
}

} // namespace

bool StateFile::save(const engine::DownloadState& state) {
    // Un solo estado (el más reciente)
    std::vector<engine::DownloadState> states{state};
    return write_states(path_, states);
}

bool StateFile::push(const engine::DownloadState& state) {
    auto states = load();
    states.push_back(state);
    return write_states(path_, states);
}

std::vector<engine::DownloadState> StateFile::load() {
    std::vector<engine::DownloadState> states;

    std::ifstream in(path_);
    if (!in.is_open()) return states; // No existe estado

    try {
        nlohmann::json j;
        in >> j;

        if (j.is_array()) {
            for (const auto& elem : j) states.push_back(state_from_json(elem));
        } else if (j.is_object()) {
            states.push_back(state_from_json(j));
        }
    } catch (const std::exception& e) {
        std::cerr << "[!] Estado corrupto (" << path_ << "): " << e.what() << "\n";
    }

    return states;
}

std::shared_ptr<StateFile> StateFile::open(const std::string& output_path) {
    // El archivo de estado vive junto al archivo de salida
    fs::path p(output_path);
    std::string state_path = p.parent_path().empty()
        ? "lux_state.json"
        : (p.parent_path() / (p.filename().string() + ".luxstate.json")).string();
    return std::make_shared<StateFile>(state_path);
}

} // namespace io
