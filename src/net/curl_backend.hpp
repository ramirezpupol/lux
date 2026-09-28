// src/net/curl_backend.hpp
//
// Backend de red concreto usando libcurl (Linux, Android, macOS, Windows).
// Thread-safe: cada petición usa su propio handle easy.

#pragma once

#include "net/backend.hpp"
#include <atomic>

namespace engine {

class CurlBackend : public NetworkBackend {
public:
    ~CurlBackend() override;

    bool init() override;
    bool get(const std::string& url,
             std::string& out_body,
             std::map<std::string, std::string>& out_headers) override;
    bool get_range(const std::string& url,
                   uint64_t start,
                   uint64_t end,
                   std::string& out_body,
                   std::map<std::string, std::string>& out_headers) override;
    bool head(const std::string& url,
              std::map<std::string, std::string>& out_headers) override;
    int  last_status() const override;
    void set_user_agent(const std::string& ua) override;
    void set_timeout_ms(int ms) override;
    void set_io_threads(int n) override;

private:
    // Realiza una petición con un handle propio (thread-safe)
    bool perform(const std::string& url,
                 bool is_head,
                 const std::string& range,   // "" si no hay rango
                 std::string* out_body,
                 std::map<std::string, std::string>* out_headers);

    std::string user_agent_ = "Lux/1.0";
    int         timeout_ms_ = 30000;
    int         io_threads_ = 4;
    std::atomic<int> last_status_{0};
};

} // namespace engine
