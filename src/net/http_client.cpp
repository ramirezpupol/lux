// src/net/http_client.cpp
//
// Implementación del cliente HTTP de Lux sobre un NetworkBackend.

#include "net/http_client.hpp"

#include <iostream>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <chrono>
#include <thread>
#include <mutex>

namespace engine {

namespace {
// Backend compartido entre todas las instancias de HttpClient (lazy).
std::mutex g_backend_mutex;
NetworkBackendPtr g_backend;
}

NetworkBackend* HttpClient::shared_backend() {
    std::lock_guard<std::mutex> lock(g_backend_mutex);
    if (!g_backend) {
        g_backend = create_default_backend();
    }
    return g_backend.get();
}

HttpClient::HttpClient(NetworkBackend* backend)
    : backend_(backend), owns_backend_(false) {
    if (!backend_) {
        backend_ = shared_backend();
    }
}

HttpClient::~HttpClient() = default;

void HttpClient::handle_error(const std::string& msg) {
    last_error_ = msg;
}

void HttpClient::set_threads(int n) {
    io_threads_ = (n > 0) ? n : 4;
    if (backend_) backend_->set_io_threads(io_threads_);
}

HttpClient::Response HttpClient::get(const std::string& url) {
    Response resp;
    resp.url = url;
    if (!backend_) {
        handle_error("backend no disponible");
        return resp;
    }

    resp.ok = backend_->get(url, resp.body, resp.headers);
    resp.status_code = backend_->last_status();
    if (resp.ok) {
        auto cl = resp.headers.find("content-length");
        if (cl != resp.headers.end()) {
            try { resp.content_length = std::stoull(cl->second); } catch (...) {}
        }
        auto ct = resp.headers.find("content-type");
        if (ct != resp.headers.end()) resp.content_type = ct->second;
    } else {
        handle_error("fallo en GET");
    }
    return resp;
}

HttpClient::Response HttpClient::get_range(const std::string& url,
                                           uint64_t start,
                                           uint64_t end) {
    Response resp;
    resp.url = url;
    if (!backend_) {
        handle_error("backend no disponible");
        return resp;
    }

    resp.ok = backend_->get_range(url, start, end, resp.body, resp.headers);
    resp.status_code = backend_->last_status();
    if (resp.ok) {
        auto cl = resp.headers.find("content-length");
        if (cl != resp.headers.end()) {
            try { resp.content_length = std::stoull(cl->second); } catch (...) {}
        }
        auto ct = resp.headers.find("content-type");
        if (ct != resp.headers.end()) resp.content_type = ct->second;
    } else {
        handle_error("fallo en GET (range)");
    }
    return resp;
}

HttpClient::Response HttpClient::head(const std::string& url) {
    Response resp;
    resp.url = url;
    if (!backend_) {
        handle_error("backend no disponible");
        return resp;
    }

    std::map<std::string, std::string> headers;
    resp.ok = backend_->head(url, headers);
    resp.status_code = backend_->last_status();
    resp.headers = headers;
    if (resp.ok) {
        auto cl = headers.find("content-length");
        if (cl != headers.end()) {
            try { resp.content_length = std::stoull(cl->second); } catch (...) {}
        }
        auto ct = headers.find("content-type");
        if (ct != headers.end()) resp.content_type = ct->second;
    } else {
        handle_error("fallo en HEAD");
    }
    return resp;
}

HttpClient::Response HttpClient::retry_with_backoff(const std::string& url,
                                                    int max_retries) {
    Response resp;
    for (int attempt = 0; attempt <= max_retries; ++attempt) {
        resp = get(url);
        if (resp.is_success()) return resp;
        if (attempt < max_retries) {
            int backoff = 1 << attempt;
            std::this_thread::sleep_for(std::chrono::seconds(backoff));
        }
    }
    return resp;
}

} // namespace engine
