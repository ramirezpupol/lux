// src/io/filesystem_utils.cpp
// Filesystem utilities for Lux.

#include "filesystem_utils.hpp"

#include <iostream>

namespace io {

bool ensure_parent_dirs(const std::string& path) {
    try {
        fs::path p(path);
        if (p.has_parent_path()) {
            fs::create_directories(p.parent_path());
        }
        return true;
    } catch (const fs::filesystem_error& e) {
        std::cerr << "[!] Error de sistema de archivos: " << e.what() << "\n";
        return false;
    }
}

std::optional<uint64_t> file_size(const std::string& path) {
    try {
        fs::path p(path);
        if (!fs::exists(p)) return std::nullopt;
        if (!fs::is_regular_file(p)) return std::nullopt;
        return fs::file_size(p);
    } catch (const fs::filesystem_error& e) {
        std::cerr << "[!] Error de sistema de archivos: " << e.what() << "\n";
        return std::nullopt;
    }
}

bool move_file(const std::string& from, const std::string& to) {
    try {
        fs::path f(from);
        fs::path t(to);
        if (fs::exists(t)) {
            fs::remove(t);
        }
        fs::rename(f, t);
        return true;
    } catch (const fs::filesystem_error& e) {
        std::cerr << "[!] Error de sistema de archivos: " << e.what() << "\n";
        return false;
    }
}

} // namespace io
