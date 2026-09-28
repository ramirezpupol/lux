// src/io/filesystem_utils.hpp
// Filesystem utilities for Lux.

#pragma once

#include <string>
#include <optional>
#include <filesystem>
#include <system_error>

namespace io {

namespace fs = std::filesystem;

// Crear directorios padre de un archivo si no existen
bool ensure_parent_dirs(const std::string& path);

// Comprobar si un archivo existe y su tamaño
std::optional<uint64_t> file_size(const std::string& path);

// Mover un archivo (reemplazar si existe)
bool move_file(const std::string& from, const std::string& to);

} // namespace io
