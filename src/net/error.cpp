// src/net/error.cpp
// Clasificación de errores de red para Lux.

#include "net/error.hpp"

namespace engine {

std::string net_error_to_string(NetError e) {
    switch (e) {
        case NetError::OK:             return "Sin error";
        case NetError::TIMEOUT:        return "Timeout de conexión";
        case NetError::DNS_RESOLUTION_FAILED:
            return "Error de resolución de DNS";
        case NetError::CONNECTION_REFUSED:
            return "Conexión rechazada";
        case NetError::CONNECTION_RESET:
            return "Conexión reiniciada por el servidor";
        case NetError::NETWORK_DOWN:
            return "Red caída (WiFi desconectada)";
        case NetError::SSL_ERROR:      return "Error SSL/TLS";
        case NetError::HTTP_ERROR:     return "Error HTTP (código no exitoso)";
        case NetError::FILE_ERROR:     return "Error de archivo";
        case NetError::UNKNOWN:        return "Error desconocido";
    }
    return "Desconocido";
}

NetError classify_error(int err_code) {
    switch (err_code) {
        case ETIMEDOUT:
        case EAGAIN:            // EWOULDBLOCK is the same value as EAGAIN on Linux
            return NetError::TIMEOUT;
        case ECONNREFUSED:        return NetError::CONNECTION_REFUSED;
        case ECONNRESET:          return NetError::CONNECTION_RESET;
        case ENETDOWN:
        case ENETUNREACH:
        case EHOSTUNREACH:
            return NetError::NETWORK_DOWN;
        case EINVAL:              return NetError::DNS_RESOLUTION_FAILED;
        default:                  return NetError::UNKNOWN;
    }
}

} // namespace engine
