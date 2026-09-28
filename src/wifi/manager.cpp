// src/wifi/manager.cpp
//
// Implementación del gestor de reconexión Wi-Fi.
//
// Backends por plataforma:
//  - Linux / Android(Termux): NetworkManager via `nmcli` (o libnm si LUX_HAS_NETWORKMANAGER)
//  - Windows: `netsh wlan`
//  - macOS: `networksetup`
//
// La API pública es síncrona: connect_if_needed() comprueba la conectividad
// real (resolución DNS) y reconecta si hace falta.

#include "wifi/manager.hpp"

#include <iostream>
#include <cstdlib>
#include <array>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <chrono>
#include <thread>
#include <memory>
#include <algorithm>

#if !defined(_WIN32)
#include <sys/socket.h>
#include <netdb.h>
#else
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#if defined(LUX_HAS_NETWORKMANAGER)
#include <glib.h>
#include <NetworkManager.h>
#endif

#if defined(_WIN32)
#include <windows.h>
#define LUX_POPEN  _popen
#define LUX_PCLOSE _pclose
#else
#define LUX_POPEN  popen
#define LUX_PCLOSE pclose
#endif

namespace wifi {

namespace {

// Ejecuta un comando y devuelve su salida combinada (stdout+stderr)
std::string run_command(const std::string& cmd) {
    std::string result;
    FILE* pipe = LUX_POPEN(cmd.c_str(), "r");
    if (!pipe) return "";
    std::array<char, 256> buf;
    while (fgets(buf.data(), buf.size(), pipe) != nullptr) {
        result += buf.data();
    }
    LUX_PCLOSE(pipe);
    return result;
}

// Conectividad real: resolver un host público. No depende del tipo de interfaz.
bool has_internet() {
    // getaddrinfo de un host estable; si resuelve, hay ruta activa
    struct addrinfo hints {};
    struct addrinfo* res = nullptr;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo("connectivity-check.ubuntu.com", "80", &hints, &res) == 0 && res) {
        freeaddrinfo(res);
        return true;
    }
    // Segundo intento con otro host por si el primero está bloqueado
    res = nullptr;
    if (getaddrinfo("dns.google", "443", &hints, &res) == 0 && res) {
        freeaddrinfo(res);
        return true;
    }
    return false;
}

} // namespace

Manager::Manager(const std::string& ssid, const std::string& password)
    : ssid_(ssid), password_(password) {}

bool Manager::is_connected() {
    return has_internet();
}

bool Manager::connect_if_needed() {
    if (is_connected()) {
        state_ = State::CONNECTED;
        return true;
    }
    return connect();
}

bool Manager::connect() {
    state_ = State::CONNECTING;
    std::cout << "[*] Wi-Fi: intentando conectar a '" << ssid_ << "'...\n";

    bool ok = false;
#if defined(_WIN32)
    ok = connect_netsh();
#elif defined(__APPLE__)
    ok = connect_networksetup();
#else
    ok = connect_nmcli();
#endif

    if (!ok) {
        state_ = State::FAILED;
        std::cerr << "[!] Wi-Fi: no se pudo iniciar la conexión a '" << ssid_ << "'\n";
        return false;
    }

    // Esperar a que haya conectividad real
    if (wait_for_connectivity(30)) {
        state_ = State::CONNECTED;
        std::cout << "[*] Wi-Fi: conectado a '" << ssid_ << "'\n";
        return true;
    }

    state_ = State::FAILED;
    std::cerr << "[!] Wi-Fi: conectado pero sin acceso a Internet\n";
    return false;
}

bool Manager::wait_for_connectivity(int timeout_seconds) {
    for (int i = 0; i < timeout_seconds * 2; ++i) {
        if (has_internet()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    return false;
}

// ---------------------------------------------------------------------------
// Linux / Termux: NetworkManager via nmcli
// ---------------------------------------------------------------------------
bool Manager::connect_nmcli() {
#if defined(LUX_HAS_NETWORKMANAGER)
    // Implementación preferente con libnm podría añadirse aquí;
    // nmcli es igual de válido y no requiere dependencias en runtime.
#endif
    if (run_command("which nmcli").empty()) {
        std::cerr << "[!] nmcli no disponible en este sistema\n";
        return false;
    }

    // ¿Existe ya una conexión guardada con ese SSID?
    std::string existing =
        run_command("nmcli -t -f NAME connection show | grep -F '" + ssid_ + "' | head -1");

    std::string cmd;
    if (!existing.empty()) {
        // Conectar a la conexión guardada
        std::string name = existing;
        while (!name.empty() && (name.back() == '\n' || name.back() == '\r'))
            name.pop_back();
        cmd = "nmcli connection up id '" + name + "'";
    } else {
        // Crear conexión nueva con la contraseña indicada
        cmd = "nmcli device wifi connect '" + ssid_ + "'";
        if (!password_.empty()) {
            cmd += " password '" + password_ + "'";
        }
    }

    std::string out = run_command(cmd);
    if (out.find("successfully") != std::string::npos ||
        out.find("activated") != std::string::npos) {
        return true;
    }

    // nmcli a veces activa sin mensaje claro; lo dirime wait_for_connectivity
    return true;
}

// ---------------------------------------------------------------------------
// Windows: netsh wlan
// ---------------------------------------------------------------------------
bool Manager::connect_netsh() {
    // Crear perfil XML temporal con el SSID y la clave
    std::string profile =
        "<?xml version=\"1.0\"?>"
        "<WLANProfile xmlns=\"http://www.microsoft.com/networking/WLAN/profile/v1\">"
        "<name>" + ssid_ + "</name>"
        "<SSIDConfig><SSID><name>" + ssid_ + "</name></SSID></SSIDConfig>"
        "<connectionType>ESS</connectionType>"
        "<connectionMode>auto</connectionMode>"
        "<MSM><security>"
        "<authEncryption><authentication>WPA2PSK</authentication>"
        "<encryption>AES</encryption><useOneX>false</useOneX></authEncryption>"
        "<sharedKey><keyType>passPhrase</keyType><protected>false</protected>"
        "<keyMaterial>" + password_ + "</keyMaterial></sharedKey>"
        "</security></MSM></WLANProfile>";

    std::string xml_path = "lux_wifi_profile.xml";
    {
        std::ofstream f(xml_path, std::ios::trunc);
        if (!f.is_open()) return false;
        f << profile;
    }

    run_command("netsh wlan add profile filename=\"" + xml_path + "\" user=all");
    std::string out = run_command("netsh wlan connect name=\"" + ssid_ + "\"");
    std::remove(xml_path.c_str());

    return out.find("successfully") != std::string::npos ||
           out.find("correctamente") != std::string::npos ||
           out.find("completed") != std::string::npos;
}

// ---------------------------------------------------------------------------
// macOS: networksetup
// ---------------------------------------------------------------------------
bool Manager::connect_networksetup() {
    // Detectar el servicio de red activo (normalmente "Wi-Fi")
    std::string services = run_command("networksetup -listallnetworkservices");
    std::string service;
    std::istringstream iss(services);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.find("Wi-Fi") != std::string::npos ||
            line.find("AirPort") != std::string::npos) {
            service = line;
            // Quitar el asterisco inicial si la red está deshabilitada
            if (!service.empty() && service[0] == '*') service.erase(0, 1);
            break;
        }
    }
    if (service.empty()) {
        std::cerr << "[!] No se encontró el servicio Wi-Fi en macOS\n";
        return false;
    }

    // Encender la interfaz y asociar
    run_command("networksetup -setairportpower \"" + service + "\" on");

    if (!password_.empty()) {
        run_command("networksetup -setairportnetwork \"" + service +
                    "\" \"" + ssid_ + "\" \"" + password_ + "\"");
    } else {
        run_command("networksetup -setairportnetwork \"" + service +
                    "\" \"" + ssid_ + "\"");
    }
    return true; // wait_for_connectivity lo confirmará
}

} // namespace wifi
