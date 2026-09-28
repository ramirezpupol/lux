// src/net/network_manager.hpp
// Main network manager for Lux.
// Combines the HTTP client, the network backend and the thread pool.

#pragma once

#include <string>
#include <memory>

#include "net/http_client.hpp"
#include "net/backend.hpp"
#include "net/workers.hpp"

namespace engine {

class NetworkManager {
public:
    NetworkManager();
    ~NetworkManager();

    // Initialize the backend and the thread pool
    bool init(int io_threads = 4);

    // Fetch a page (HTML) from a URL
    bool fetch_page(const std::string& url, std::string& html_out);

    // Download a resource with range support
    bool download_range(const std::string& url,
                        uint64_t start,
                        uint64_t end,
                        std::string& out_body);

    // Get resource metadata (HEAD)
    bool get_metadata(const std::string& url, std::map<std::string, std::string>& headers);

    // Detect whether a URL points to a folder (HTML with multiple resources)
    bool is_remote_folder(const std::string& html);

    // Get the worker pool for external use
    const WorkerPool& worker_pool() const { return pool_; }

private:
    HttpClient client_;
    WorkerPool pool_;
    std::shared_ptr<NetworkBackend> backend_;
};

} // namespace engine
