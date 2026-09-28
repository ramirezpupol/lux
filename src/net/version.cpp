// src/net/version.cpp
// Version utilities for Lux.

#include "net/version.hpp"

const std::string& lux_version() {
    static const std::string v = Lux_VERSION;
    return v;
}
