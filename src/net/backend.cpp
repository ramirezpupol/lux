// src/net/backend.cpp
// Fábrica del backend de red por defecto.

#include "net/backend.hpp"

#if defined(LUX_HAVE_CURL)
#include "net/curl_backend.hpp"
#endif

namespace engine {

NetworkBackendPtr create_default_backend() {
#if defined(LUX_HAVE_CURL)
    auto backend = std::make_shared<CurlBackend>();
    if (backend->init()) {
        return backend;
    }
    return nullptr;
#else
    return nullptr;
#endif
}

} // namespace engine
