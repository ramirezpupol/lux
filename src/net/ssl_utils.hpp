// src/net/ssl_utils.hpp
//
// Utilidades SSL para HTTPS seguro.

#pragma once

#include <string>
#include <stdexcept>

#include <openssl/ssl.h>
#include <openssl/err.h>

namespace ssl {

// Inicializar la librería SSL
inline void init() {
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();
}

// Limpiar recursos SSL
inline void cleanup() {
    EVP_cleanup();
    ERR_free_strings();
    // Nota: en OpenSSL 1.1+ no es necesario liberar la librería global,
    // pero se deja aquí por compatibilidad.
}

// Entrust root certificado
inline std::string get_default_cert_file() {
    // Buscar archivo ca-bundle.pem
    const char* env = std::getenv("SSL_CERT_FILE");
    if (env && env[0]) return std::string(env);
    return "/etc/ssl/certs/ca-certificates.crt"; // Debian/Ubuntu
}

} // namespace ssl
