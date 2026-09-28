// src/net/http_client.hpp
//
// Cliente HTTP para descargas de archivos (HTTP/1.1, HTTPS, Range, HEAD).

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <cstdint>
#include <map>

#include "net/backend.hpp"

namespace engine {

class HttpClient {
public:
    struct Response {
        int           status_code = 0;
        std::string   body;
        std::map<std::string, std::string> headers;
        bool          ok = false;
        uint64_t      content_length = 0;
        std::string   content_type;
        std::string   url;

        bool is_success() const { return status_code >= 200 && status_code < 300; }
    };

    // Si no se pasa backend, se crea/usa uno compartido por defecto.
    explicit HttpClient(NetworkBackend* backend = nullptr);
    ~HttpClient();

    Response get(const std::string& url);
    Response get_range(const std::string& url, uint64_t start, uint64_t end);
    Response head(const std::string& url);

    void set_threads(int n);
    Response retry_with_backoff(const std::string& url, int max_retries);

    std::string last_error() const { return last_error_; }

private:
    NetworkBackend* backend_ = nullptr;   // no es propietario si es externo
    bool owns_backend_ = false;
    std::string last_error_;
    int io_threads_ = 4;

    void handle_error(const std::string& msg);
    static NetworkBackend* shared_backend();
};

} // namespace engine
