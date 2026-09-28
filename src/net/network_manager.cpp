// src/net/network_manager.cpp
// Network manager implementation for Lux.

#include "net/network_manager.hpp"

#include <iostream>
#include <algorithm>
#include <regex>

namespace engine {

NetworkManager::NetworkManager()
    : client_(), pool_(), backend_(nullptr) {}

NetworkManager::~NetworkManager() = default;

bool NetworkManager::init(int io_threads) {
    backend_ = create_default_backend();
    if (!backend_) {
        std::cerr << "[!] No se pudo crear un backend de red\n";
        return false;
    }

    if (!backend_->init()) {
        std::cerr << "[!] Error inicializando el backend de red\n";
        return false;
    }

    pool_.set_thread_count(io_threads);
    client_.set_threads(io_threads);

    return true;
}

bool NetworkManager::fetch_page(const std::string& url, std::string& html_out) {
    if (!backend_) {
        html_out.clear();
        return false;
    }

    std::string body;
    std::map<std::string, std::string> headers;
    bool ok = backend_->get(url, body, headers);
    if (ok) {
        html_out = std::move(body);
        return true;
    }

    return false;
}

bool NetworkManager::download_range(const std::string& url,
                                    uint64_t start,
                                    uint64_t end,
                                    std::string& out_body) {
    if (!backend_) {
        out_body.clear();
        return false;
    }

    std::string body;
    std::map<std::string, std::string> headers;
    bool ok = backend_->get_range(url, start, end, body, headers);
    if (ok) {
        out_body = std::move(body);
        return true;
    }
    return false;
}

bool NetworkManager::get_metadata(const std::string& url, std::map<std::string, std::string>& headers) {
    if (!backend_) return false;
    return backend_->head(url, headers);
}

bool NetworkManager::is_remote_folder(const std::string& html) {
    // Detect folders: HTML containing multiple links (e.g. Apache/nginx)
    if (html.empty()) return false;
    static const std::regex folder_pattern(R"(<a[^>]*href=["']([^"']+)["'][^>]*>)");
    return std::regex_search(html, folder_pattern);
}

} // namespace engine
