// src/net/remote_folder_parser.hpp
//
// Parseador de listados de carpetas remotas (HTTP).

#pragma once

#include <string>
#include <vector>
#include <optional>
#include <cstdint>
#include <map>

namespace engine {

struct Resource {
    std::string url;
    std::string local_path;
    uint64_t    size = 0;
    bool        is_directory = false;
};

std::vector<Resource> parse_resources_from_html(const std::string& html,
                                                const std::string& base_url);

std::string extract_file_name(const std::string& url);
std::string resolve_url(const std::string& base_url, const std::string& rel);

} // namespace engine
