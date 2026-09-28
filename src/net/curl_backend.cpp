// src/net/curl_backend.cpp
//
// Implementación del backend de red con libcurl.
//
// Seguridad de hilos: cada petición crea su propio CURL easy handle, por lo
// que varias descargas en paralelo pueden usar el mismo backend sin condición
// de carrera. curl_global_init se ejecuta una única vez.

#include "net/curl_backend.hpp"

#include <stdexcept>
#include <iostream>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <cctype>
#include <mutex>
#include <atomic>

#include <curl/curl.h>

namespace engine {

namespace {

// Inicialización global de libcurl (una sola vez por proceso)
std::once_flag g_curl_global_once;

void ensure_curl_global_init() {
    std::call_once(g_curl_global_once, []() {
        if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
            std::cerr << "[!] curl_global_init falló\n";
        }
    });
}

struct WriteContext {
    std::string* body;
};

struct HeaderContext {
    std::map<std::string, std::string>* headers;
};

size_t write_callback(void* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* ctx = static_cast<WriteContext*>(userdata);
    size_t total = size * nmemb;
    if (ctx && ctx->body) {
        ctx->body->append(static_cast<char*>(ptr), total);
    }
    return total;
}

size_t header_callback(void* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* ctx = static_cast<HeaderContext*>(userdata);
    std::string line(static_cast<char*>(ptr), size * nmemb);

    while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
        line.pop_back();
    if (line.empty()) return size * nmemb;

    size_t sep = line.find(':');
    if (sep != std::string::npos) {
        std::string key = line.substr(0, sep);
        std::string val = line.substr(sep + 1);
        auto trim = [](std::string& s) {
            size_t b = s.find_first_not_of(" \t");
            size_t e = s.find_last_not_of(" \t");
            if (b == std::string::npos) { s.clear(); return; }
            s = s.substr(b, e - b + 1);
        };
        trim(key);
        trim(val);
        std::transform(key.begin(), key.end(), key.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (ctx && ctx->headers) {
            (*ctx->headers)[key] = val;
        }
    }
    return size * nmemb;
}

// RAII para el handle easy
struct EasyHandle {
    CURL* h;
    EasyHandle() : h(curl_easy_init()) {}
    ~EasyHandle() { if (h) curl_easy_cleanup(h); }
    EasyHandle(const EasyHandle&) = delete;
    EasyHandle& operator=(const EasyHandle&) = delete;
};

} // namespace

CurlBackend::~CurlBackend() = default;

bool CurlBackend::init() {
    ensure_curl_global_init();

    // Verificar que podemos crear un handle
    EasyHandle test;
    if (!test.h) {
        std::cerr << "[!] Error al inicializar libcurl\n";
        return false;
    }
    return true;
}

int CurlBackend::last_status() const {
    return last_status_.load();
}

void CurlBackend::set_user_agent(const std::string& ua) {
    user_agent_ = ua;
}

void CurlBackend::set_timeout_ms(int ms) {
    timeout_ms_ = ms;
}

void CurlBackend::set_io_threads(int n) {
    io_threads_ = n;
}

bool CurlBackend::perform(const std::string& url,
                          bool is_head,
                          const std::string& range,
                          std::string* out_body,
                          std::map<std::string, std::string>* out_headers) {
    ensure_curl_global_init();

    EasyHandle eh;
    if (!eh.h) return false;

    WriteContext wctx{ out_body };
    HeaderContext hctx{ out_headers };

    curl_easy_setopt(eh.h, CURLOPT_URL, url.c_str());
    curl_easy_setopt(eh.h, CURLOPT_USERAGENT, user_agent_.c_str());
    curl_easy_setopt(eh.h, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(eh.h, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(eh.h, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(eh.h, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(eh.h, CURLOPT_SSL_VERIFYPEER, 1L);

    // Timeout de conexión corto; sin timeout total (archivos grandes lentos)
    curl_easy_setopt(eh.h, CURLOPT_CONNECTTIMEOUT_MS, 15000L);
    // Si el servidor deja de enviar datos durante 30s -> abortar
    curl_easy_setopt(eh.h, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(eh.h, CURLOPT_LOW_SPEED_TIME, 30L);

    if (is_head) {
        curl_easy_setopt(eh.h, CURLOPT_NOBODY, 1L);
    }
    if (!range.empty()) {
        curl_easy_setopt(eh.h, CURLOPT_RANGE, range.c_str());
    }

    if (out_body) {
        curl_easy_setopt(eh.h, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(eh.h, CURLOPT_WRITEDATA, &wctx);
    }
    if (out_headers) {
        curl_easy_setopt(eh.h, CURLOPT_HEADERFUNCTION, header_callback);
        curl_easy_setopt(eh.h, CURLOPT_HEADERDATA, &hctx);
    }

    last_status_.store(0);
    CURLcode res = curl_easy_perform(eh.h);
    if (res != CURLE_OK) {
        std::cerr << "[!] curl: " << curl_easy_strerror(res) << "\n";
        return false;
    }

    long code = 0;
    curl_easy_getinfo(eh.h, CURLINFO_RESPONSE_CODE, &code);
    last_status_.store(static_cast<int>(code));
    return true;
}

bool CurlBackend::get(const std::string& url,
                      std::string& out_body,
                      std::map<std::string, std::string>& out_headers) {
    out_body.clear();
    out_headers.clear();
    return perform(url, false, "", &out_body, &out_headers);
}

bool CurlBackend::get_range(const std::string& url,
                            uint64_t start,
                            uint64_t end,
                            std::string& out_body,
                            std::map<std::string, std::string>& out_headers) {
    out_body.clear();
    out_headers.clear();

    char range_str[64] = {0};
    if (end > start) {
        std::snprintf(range_str, sizeof(range_str), "%llu-%llu",
                      static_cast<unsigned long long>(start),
                      static_cast<unsigned long long>(end - 1));
    } else {
        std::snprintf(range_str, sizeof(range_str), "%llu-",
                      static_cast<unsigned long long>(start));
    }

    return perform(url, false, range_str, &out_body, &out_headers);
}

bool CurlBackend::head(const std::string& url,
                       std::map<std::string, std::string>& out_headers) {
    out_headers.clear();
    return perform(url, true, "", nullptr, &out_headers);
}

} // namespace engine
