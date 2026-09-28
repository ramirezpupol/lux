// src/net/error.hpp
// Error classification for Lux.

#pragma once

#include <string>
#include <system_error>
#include <cerrno>

namespace engine {

// Network error codes
enum class NetError {
    OK,
    TIMEOUT,
    DNS_RESOLUTION_FAILED,
    CONNECTION_REFUSED,
    CONNECTION_RESET,
    NETWORK_DOWN,      // WiFi disconnected
    SSL_ERROR,
    HTTP_ERROR,        // Unsuccessful HTTP status code
    FILE_ERROR,
    UNKNOWN
};

// Convert a network error code to a human readable string
std::string net_error_to_string(NetError e);

// Classify a system errno / error code
NetError classify_error(int err_code);

} // namespace engine
