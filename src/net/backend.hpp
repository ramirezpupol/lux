// src/net/backend.hpp
//
// Abstracción del backend de red para Lux.
// Permite usar libcurl, Boost.Beast u otro backend sin cambiar la lógica.

#pragma once

#include <string>
#include <memory>
#include <cstdint>
#include <map>

namespace engine {

// Interfaz abstracta del backend de red
class NetworkBackend {
public:
    virtual ~NetworkBackend() = default;

    // Inicialización (opcional)
    virtual bool init() = 0;

    // Realizar petición GET
    virtual bool get(const std::string& url,
                     std::string& out_body,
                     std::map<std::string, std::string>& out_headers) = 0;

    // Realizar petición GET con rango (reanudación).
    // Si end == 0 significa "hasta el final del recurso".
    virtual bool get_range(const std::string& url,
                           uint64_t start,
                           uint64_t end,
                           std::string& out_body,
                           std::map<std::string, std::string>& out_headers) = 0;

    // Realizar petición HEAD
    virtual bool head(const std::string& url,
                      std::map<std::string, std::string>& out_headers) = 0;

    // Código de estado HTTP de la última operación (0 si no hay)
    virtual int last_status() const { return 0; }

    // Establecer agente de usuario
    virtual void set_user_agent(const std::string& ua) = 0;

    // Establecer timeout (ms)
    virtual void set_timeout_ms(int ms) = 0;

    // Establecer número de hilos de I/O
    virtual void set_io_threads(int n) = 0;
};

using NetworkBackendPtr = std::shared_ptr<NetworkBackend>;

// Fábrica del backend por defecto (implementada en backend.cpp; usa
// libcurl cuando está disponible).
NetworkBackendPtr create_default_backend();

} // namespace engine
