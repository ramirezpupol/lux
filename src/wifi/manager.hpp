// src/wifi/manager.hpp
//
// Gestor de reconexión Wi-Fi para Lux.
//
// Backends por plataforma:
//  - Linux / Android(Termux): NetworkManager vía `nmcli` (o libnm si LUX_HAS_NETWORKMANAGER)
//  - Windows: `netsh wlan`
//  - macOS:   `networksetup`
//
// La API es síncrona: connect_if_needed() comprueba conectividad real
// (resolución DNS + conexión TCP) y reconecta si hace falta.

#pragma once

#include <string>

namespace wifi {

class Manager {
public:
    Manager(const std::string& ssid, const std::string& password);

    // Si hay conectividad no hace nada; si no, intenta reconectar.
    bool connect_if_needed();

    // Fuerza la conexión a la red indicada
    bool connect();

    // Comprueba si hay conectividad a Internet (independiente del Wi-Fi)
    bool is_connected();

    enum class State {
        IDLE,
        CONNECTING,
        CONNECTED,
        FAILED,
        NOT_SUPPORTED
    };
    State state() const { return state_; }

private:
    std::string ssid_;
    std::string password_;
    State       state_ = State::IDLE;

    // Espera activa hasta que haya conectividad (con timeout)
    bool wait_for_connectivity(int timeout_seconds);

    // Backends
    bool connect_nmcli();        // Linux/Termux con NetworkManager
    bool connect_netsh();        // Windows
    bool connect_networksetup(); // macOS
};

} // namespace wifi
