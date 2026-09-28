// src/net/remote_folder_parser.cpp
//
// Parseador de listados de carpetas remotas (HTML).
// Extrae enlaces <a href> filtrando anclas, scripts, assets comunes
// (iconos, css, fuentes) y schemes externos; resuelve URLs relativas
// y clasifica subcarpetas.

#include "net/remote_folder_parser.hpp"

#include <regex>
#include <cstdlib>
#include <cctype>
#include <algorithm>

namespace engine {

namespace {

// Enlaces que no son recursos descargables
bool is_skippable(const std::string& href) {
    if (href.empty()) return true;
    if (href[0] == '#') return true;                       // ancla (#main)
    if (href.rfind("javascript:", 0) == 0) return true;    // javascript:void(0)
    if (href.rfind("mailto:", 0) == 0) return true;
    if (href.rfind("tel:", 0) == 0) return true;
    if (href == "/" || href == "./" || href == "../") return true;
    if (!href.empty() && href[0] == '?') return true;      // orden de Apache
    return false;
}

// Assets típicos de una página web que no interesan en un listado
bool is_web_asset(const std::string& href) {
    static const std::regex asset_re(
        R"(\.(css|js|mjs|png|jpe?g|gif|svg|ico|woff2?|ttf|eot|otf|map)(\?[^#]*)?$)",
        std::regex::icase);
    // Solo si son rutas de recursos (después de la última '/')
    size_t slash = href.find_last_of('/');
    std::string name = (slash == std::string::npos) ? href : href.substr(slash + 1);
    return std::regex_search(name, asset_re);
}

bool is_directory_link(const std::string& href, const std::string& text) {
    std::string lower_text = text;
    std::transform(lower_text.begin(), lower_text.end(), lower_text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (!href.empty() && href.back() == '/') return true;
    if (lower_text.find("parent directory") != std::string::npos) return true;
    return false;
}

std::string html_unescape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        if (s[i] == '&') {
            if (s.compare(i, 5, "&amp;") == 0)  { out += '&';  i += 5; continue; }
            if (s.compare(i, 4, "&lt;") == 0)   { out += '<';  i += 4; continue; }
            if (s.compare(i, 4, "&gt;") == 0)   { out += '>';  i += 4; continue; }
            if (s.compare(i, 6, "&quot;") == 0) { out += '"';  i += 6; continue; }
            if (s.compare(i, 5, "&#39;") == 0)  { out += '\''; i += 5; continue; }
        }
        out += s[i++];
    }
    return out;
}

} // namespace

std::vector<Resource> parse_resources_from_html(const std::string& html,
                                                const std::string& base_url) {
    std::vector<Resource> resources;
    if (html.empty()) return resources;

    static const std::regex link_re(R"(<a\s+[^>]*href=["']([^"']+)["'][^>]*>(.*?)</a>)",
                                    std::regex::icase);

    auto begin = std::sregex_iterator(html.begin(), html.end(), link_re);
    auto end = std::sregex_iterator();

    for (auto it = begin; it != end; ++it) {
        std::smatch m = *it;
        std::string href = html_unescape(m[1].str());
        std::string text = m[2].str();

        if (is_skippable(href)) continue;

        // Ignorar links que apuntan fuera del árbol base (otro host)
        if (href.rfind("http://", 0) == 0 || href.rfind("https://", 0) == 0) {
            size_t base_auth = base_url.find("://");
            size_t href_auth = href.find("://");
            if (base_auth != std::string::npos && href_auth != std::string::npos) {
                std::string base_host = base_url.substr(base_auth + 3);
                std::string href_host = href.substr(href_auth + 3);
                base_host = base_host.substr(0, base_host.find('/'));
                href_host = href_host.substr(0, href_host.find('/'));
                if (base_host != href_host) continue;
            }
        }

        // Ignorar assets típicos de web (css/js/fuentes/iconos)
        if (is_web_asset(href)) continue;

        Resource res;
        res.url = resolve_url(base_url, href);

        // No re-descargar el propio listado ni la raíz del sitio
        if (res.url == base_url) continue;
        {
            size_t base_auth = base_url.find("://");
            if (base_auth != std::string::npos) {
                size_t base_slash = base_url.find('/', base_auth + 3);
                std::string site_root = (base_slash == std::string::npos)
                    ? base_url : base_url.substr(0, base_slash + 1);
                if (res.url == site_root) continue;
            }
        }

        res.is_directory = is_directory_link(href, text);
        res.local_path = extract_file_name(res.url);
        if (res.local_path.empty() || res.local_path == "index.html") {
            if (res.is_directory) continue;
            res.local_path = "resource";
        }

        // Evitar duplicados
        bool dup = false;
        for (const auto& r : resources) {
            if (r.url == res.url) { dup = true; break; }
        }
        if (dup) continue;

        resources.push_back(std::move(res));
    }

    return resources;
}

std::string extract_file_name(const std::string& url) {
    // Quitar query/fragmento
    std::string clean = url;
    size_t cut = clean.find_first_of("?#");
    if (cut != std::string::npos) clean = clean.substr(0, cut);

    // Quitar barra final
    while (!clean.empty() && clean.back() == '/') clean.pop_back();

    size_t q = clean.find_last_of('/');
    if (q != std::string::npos) {
        std::string name = clean.substr(q + 1);
        if (!name.empty()) return name;
        return "index.html";
    }
    return clean.empty() ? "index.html" : clean;
}

std::string resolve_url(const std::string& base_url, const std::string& rel) {
    if (rel.empty()) return base_url;

    // URL absoluta
    if (rel.rfind("http://", 0) == 0 || rel.rfind("https://", 0) == 0)
        return rel;

    // Protocol-relative: //host/path
    if (rel.rfind("//", 0) == 0) {
        size_t q = base_url.find("://");
        if (q != std::string::npos)
            return base_url.substr(0, q) + ":" + rel;
        return "http:" + rel;
    }

    // Raíz del host
    if (rel[0] == '/') {
        size_t q = base_url.find("://");
        if (q != std::string::npos) {
            size_t a = base_url.find('/', q + 3);
            if (a != std::string::npos) {
                return base_url.substr(0, a) + rel;
            }
            return base_url + rel;
        }
        return rel;
    }

    // Resolver ./ y ../ respecto al directorio base
    std::string base_dir = base_url;
    size_t scheme_pos = base_dir.find("://");
    size_t last_slash = base_dir.find_last_of('/');
    if (scheme_pos != std::string::npos &&
        last_slash != std::string::npos &&
        last_slash > scheme_pos + 3) {
        base_dir = base_dir.substr(0, last_slash + 1);
    } else {
        base_dir += "/";
    }

    std::string combined = base_dir + rel;

    // Tokenizar y resolver . y ..
    std::vector<std::string> stack;
    size_t start = 0;
    if (combined.rfind("http://", 0) == 0)       { stack.push_back("http:");  start = 6; }  // se corrige abajo
    else if (combined.rfind("https://", 0) == 0) { stack.push_back("https:"); start = 7; }

    if (start > 0) {
        // Añadir "//" vacío tras el esquema y el host como primer token
        size_t host_start = combined.find('/', start);
        std::string host = combined.substr(0, start) + "//";
        (void)host;
        // Reinicio: procesar tokens después del esquema
        std::string rest = combined.substr(start);
        stack.push_back("//");
        stack.back() = ""; // marcador de doble barra
        stack.pop_back();

        size_t i = 0;
        while (i <= rest.size()) {
            size_t slash = rest.find('/', i);
            std::string token = (slash == std::string::npos)
                ? rest.substr(i) : rest.substr(i, slash - i);
            if (slash == std::string::npos) {
                if (!token.empty() && token != ".") stack.push_back(token);
                break;
            }
            if (token == "..") {
                if (stack.size() > 1) stack.pop_back(); // nunca quitar el esquema
            } else if (!token.empty() && token != ".") {
                stack.push_back(token);
            }
            i = slash + 1;
        }
    } else {
        size_t i = 0;
        while (i <= combined.size()) {
            size_t slash = combined.find('/', i);
            std::string token = (slash == std::string::npos)
                ? combined.substr(i) : combined.substr(i, slash - i);
            if (slash == std::string::npos) {
                if (!token.empty() && token != ".") stack.push_back(token);
                break;
            }
            if (token == "..") {
                if (!stack.empty()) stack.pop_back();
            } else if (!token.empty() && token != ".") {
                stack.push_back(token);
            }
            i = slash + 1;
        }
    }

    // Reconstruir: el primer token "http:" o "https:" va seguido de "//"
    std::string out;
    if (!stack.empty() && (stack[0] == "http:" || stack[0] == "https:")) {
        out = stack[0] + "//";
        for (size_t k = 1; k < stack.size(); ++k) {
            out += stack[k];
            if (k + 1 < stack.size()) out += "/";
        }
    } else {
        for (size_t k = 0; k < stack.size(); ++k) {
            out += stack[k];
            if (k + 1 < stack.size()) out += "/";
        }
    }

    // Preservar barra final si el rel termina en /
    if (!rel.empty() && rel.back() == '/' && !out.empty() && out.back() != '/')
        out += "/";
    return out;
}

} // namespace engine
