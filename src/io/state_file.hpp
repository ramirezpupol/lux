// src/io/state_file.hpp
//
// Manejo de archivos de estado para la reanudación de descargas.

#pragma once

#include <string>
#include <fstream>
#include <memory>
#include <vector>

#include <nlohmann/json.hpp>

#include "engine/types.hpp"

namespace io {

// Clase para guardar y restaurar el estado de descarga
class StateFile {
public:
    explicit StateFile(const std::string& path = "lux_state.json")
        : path_(path) {}

    // Guardar estado actual (reescribe el archivo con una lista de estados)
    bool save(const engine::DownloadState& state);

    // Cargar estados guardados
    std::vector<engine::DownloadState> load();

    // Añadir estado a la lista persistida
    bool push(const engine::DownloadState& state);

    // Crea un StateFile con la ruta estándar junto al archivo de salida
    static std::shared_ptr<StateFile> open(const std::string& output_path);

    const std::string& path() const { return path_; }

private:
    std::string path_;
};

} // namespace io
