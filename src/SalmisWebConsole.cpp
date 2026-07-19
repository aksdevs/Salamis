// SPDX-License-Identifier: GPL-3.0-only

#include "salamis/SalmisWebConsole.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using http_socket_type = SOCKET;
constexpr http_socket_type kInvalidHttpSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
using http_socket_type = int;
constexpr http_socket_type kInvalidHttpSocket = -1;
#endif

namespace salamis {
namespace {

constexpr const char* kUserHtml = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Salamis Console</title>
<style>
:root{color-scheme:light;--ink:#172026;--muted:#5d6872;--line:#d7dde3;--panel:#f7f9fb;--accent:#0f766e;--danger:#b42318}
*{box-sizing:border-box}body{margin:0;font-family:Inter,Segoe UI,Arial,sans-serif;color:var(--ink);background:#fff}
header{display:flex;align-items:center;justify-content:space-between;padding:18px 28px;border-bottom:1px solid var(--line)}
h1{font-size:22px;margin:0}main{display:grid;grid-template-columns:1fr 1fr;gap:22px;padding:24px;max-width:1180px;margin:0 auto}
section{border:1px solid var(--line);border-radius:8px;padding:18px;background:var(--panel)}h2{font-size:16px;margin:0 0 14px}
label{display:block;font-size:13px;color:var(--muted);margin-top:12px}input,textarea{width:100%;border:1px solid var(--line);border-radius:6px;padding:10px;font:inherit;background:#fff}
textarea{min-height:92px;resize:vertical}button{border:0;border-radius:6px;background:var(--accent);color:#fff;padding:10px 14px;font-weight:650;margin-top:14px;cursor:pointer}
pre{white-space:pre-wrap;background:#101820;color:#e6edf3;border-radius:6px;padding:14px;min-height:110px;overflow:auto}
a{color:var(--accent);text-decoration:none;font-weight:650}@media(max-width:760px){main{grid-template-columns:1fr;padding:14px}header{padding:14px}}
</style>
</head>
<body>
<header><h1>Salamis User Console</h1><a href="/admin">Admin</a></header>
<main>
<section><h2>Upsert Vector</h2><label>Vector ID</label><input id="put-id" value="doc-1"><label>Values</label><textarea id="put-values">0.1, 0.2, 0.3</textarea><button onclick="putVector()">Save Vector</button></section>
<section><h2>Search</h2><label>Top K</label><input id="search-limit" value="5"><label>Query Values</label><textarea id="search-values">1.0, 0.0, 0.0</textarea><button onclick="search()">Search</button><pre id="output"></pre></section>
</main>
<script>
const values = text => text.split(/[,\s]+/).filter(Boolean).map(Number);
const show = data => output.textContent = typeof data === 'string' ? data : JSON.stringify(data,null,2);
async function putVector(){const r=await fetch('/api/vectors',{method:'POST',body:JSON.stringify({id:document.getElementById('put-id').value,values:values(document.getElementById('put-values').value)})});show(await r.json());}
async function search(){const r=await fetch('/api/search',{method:'POST',body:JSON.stringify({limit:Number(document.getElementById('search-limit').value),values:values(document.getElementById('search-values').value)})});show(await r.json());}
</script>
</body>
</html>)HTML";

constexpr const char* kAdminHtml = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Salamis Admin</title>
<style>
:root{--ink:#172026;--muted:#5d6872;--line:#d7dde3;--panel:#f7f9fb;--accent:#175cd3}
*{box-sizing:border-box}body{margin:0;font-family:Inter,Segoe UI,Arial,sans-serif;color:var(--ink)}
header{display:flex;align-items:center;justify-content:space-between;padding:18px 28px;border-bottom:1px solid var(--line)}
h1{font-size:22px;margin:0}main{padding:24px;max-width:1180px;margin:0 auto}table{border-collapse:collapse;width:100%;background:#fff}
th,td{text-align:left;border-bottom:1px solid var(--line);padding:12px}th{font-size:12px;color:var(--muted);text-transform:uppercase;letter-spacing:.04em}
section{border:1px solid var(--line);border-radius:8px;padding:18px;background:var(--panel)}button{border:0;border-radius:6px;background:var(--accent);color:#fff;padding:10px 14px;font-weight:650;cursor:pointer}
a{color:var(--accent);text-decoration:none;font-weight:650}
</style>
</head>
<body>
<header><h1>Salamis Admin Console</h1><a href="/">User Console</a></header>
<main><section><button onclick="load()">Refresh Cluster</button><table><thead><tr><th>Node</th><th>Host</th><th>Port</th><th>Store</th></tr></thead><tbody id="nodes"></tbody></table></section></main>
<script>
async function load(){const data=await (await fetch('/api/admin/cluster')).json();nodes.innerHTML=data.nodes.map(n=>`<tr><td>${n.id}</td><td>${n.host}</td><td>${n.port}</td><td>${n.store}</td></tr>`).join('');}
load();
</script>
</body>
</html>)HTML";

class SocketRuntime {
public:
    SocketRuntime() {
#ifdef _WIN32
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            throw VectorDatabaseError("failed to initialize Winsock");
        }
#endif
    }
    ~SocketRuntime() {
#ifdef _WIN32
        WSACleanup();
#endif
    }
};

void close_socket(http_socket_type socket) {
#ifdef _WIN32
    closesocket(socket);
#else
    close(socket);
#endif
}

http_socket_type listen_socket(const std::string& host, std::uint16_t port) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    addrinfo* result = nullptr;
    const auto port_text = std::to_string(port);
    const char* bind_host = host == "0.0.0.0" ? nullptr : host.c_str();
    if (getaddrinfo(bind_host, port_text.c_str(), &hints, &result) != 0) {
        throw VectorDatabaseError("failed to resolve web bind address: " + host);
    }

    http_socket_type server = kInvalidHttpSocket;
    for (auto* item = result; item != nullptr; item = item->ai_next) {
        const auto candidate = socket(item->ai_family, item->ai_socktype, item->ai_protocol);
        if (candidate == kInvalidHttpSocket) {
            continue;
        }
        int reuse = 1;
        setsockopt(candidate, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
        if (bind(candidate, item->ai_addr, static_cast<int>(item->ai_addrlen)) == 0 && listen(candidate, 64) == 0) {
            server = candidate;
            break;
        }
        close_socket(candidate);
    }
    freeaddrinfo(result);
    if (server == kInvalidHttpSocket) {
        throw VectorDatabaseError("failed to listen on " + host + ":" + port_text);
    }
    return server;
}

void send_all(http_socket_type socket, const std::string& payload) {
    std::size_t sent_total = 0;
    while (sent_total < payload.size()) {
        const auto sent = send(socket, payload.data() + sent_total, static_cast<int>(payload.size() - sent_total), 0);
        if (sent <= 0) {
            throw VectorDatabaseError("socket error while sending HTTP response");
        }
        sent_total += static_cast<std::size_t>(sent);
    }
}

std::string receive_request(http_socket_type socket) {
    std::string data;
    char buffer[4096]{};
    while (data.find("\r\n\r\n") == std::string::npos) {
        const auto received = recv(socket, buffer, sizeof(buffer), 0);
        if (received <= 0) {
            break;
        }
        data.append(buffer, static_cast<std::size_t>(received));
        if (data.size() > 65536) {
            throw VectorDatabaseError("HTTP request headers too large");
        }
    }
    const auto header_end = data.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        return data;
    }

    std::size_t content_length = 0;
    std::istringstream headers(data.substr(0, header_end));
    std::string line;
    while (std::getline(headers, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const auto separator = line.find(':');
        if (separator == std::string::npos) {
            continue;
        }
        auto key = line.substr(0, separator);
        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        if (key == "content-length") {
            content_length = static_cast<std::size_t>(std::stoull(line.substr(separator + 1)));
        }
    }

    const auto expected = header_end + 4 + content_length;
    while (data.size() < expected) {
        const auto received = recv(socket, buffer, sizeof(buffer), 0);
        if (received <= 0) {
            break;
        }
        data.append(buffer, static_cast<std::size_t>(received));
    }
    return data;
}

HttpRequest parse_http_request(const std::string& raw) {
    std::istringstream stream(raw);
    HttpRequest request;
    stream >> request.method >> request.path;
    const auto body_start = raw.find("\r\n\r\n");
    if (body_start != std::string::npos) {
        request.body = raw.substr(body_start + 4);
    }
    return request;
}

std::string status_text(int status) {
    switch (status) {
    case 200:
        return "OK";
    case 201:
        return "Created";
    case 400:
        return "Bad Request";
    case 404:
        return "Not Found";
    case 500:
        return "Internal Server Error";
    default:
        return "OK";
    }
}

std::string to_http_wire(const HttpResponse& response) {
    std::ostringstream wire;
    wire << "HTTP/1.1 " << response.status << ' ' << status_text(response.status) << "\r\n"
         << "Content-Type: " << response.content_type << "\r\n"
         << "Content-Length: " << response.body.size() << "\r\n"
         << "Connection: close\r\n\r\n"
         << response.body;
    return wire.str();
}

std::string json_escape(const std::string& value) {
    std::string escaped;
    for (const auto ch : value) {
        if (ch == '\\' || ch == '"') {
            escaped.push_back('\\');
        }
        escaped.push_back(ch);
    }
    return escaped;
}

std::string extract_string_field(const std::string& body, const std::string& field) {
    const auto key = "\"" + field + "\"";
    const auto key_pos = body.find(key);
    if (key_pos == std::string::npos) {
        throw VectorDatabaseError("missing JSON string field: " + field);
    }
    const auto colon = body.find(':', key_pos + key.size());
    const auto first_quote = body.find('"', colon + 1);
    const auto second_quote = body.find('"', first_quote + 1);
    if (colon == std::string::npos || first_quote == std::string::npos || second_quote == std::string::npos) {
        throw VectorDatabaseError("invalid JSON string field: " + field);
    }
    return body.substr(first_quote + 1, second_quote - first_quote - 1);
}

std::size_t extract_size_field(const std::string& body, const std::string& field, std::size_t fallback) {
    const auto key = "\"" + field + "\"";
    const auto key_pos = body.find(key);
    if (key_pos == std::string::npos) {
        return fallback;
    }
    const auto colon = body.find(':', key_pos + key.size());
    if (colon == std::string::npos) {
        throw VectorDatabaseError("invalid JSON number field: " + field);
    }
    return static_cast<std::size_t>(std::stoull(body.substr(colon + 1)));
}

std::vector<float> extract_values_field(const std::string& body) {
    const auto key_pos = body.find("\"values\"");
    if (key_pos == std::string::npos) {
        throw VectorDatabaseError("missing JSON array field: values");
    }
    const auto open = body.find('[', key_pos);
    const auto close = body.find(']', open + 1);
    if (open == std::string::npos || close == std::string::npos) {
        throw VectorDatabaseError("invalid JSON array field: values");
    }

    auto values_text = body.substr(open + 1, close - open - 1);
    std::replace(values_text.begin(), values_text.end(), ',', ' ');
    std::istringstream stream(values_text);
    std::vector<float> values;
    float value = 0.0F;
    while (stream >> value) {
        values.push_back(value);
    }
    if (values.empty()) {
        throw VectorDatabaseError("values array cannot be empty");
    }
    return values;
}

HttpResponse json_response(std::string body, int status = 200) {
    return HttpResponse{status, "application/json; charset=utf-8", std::move(body)};
}

} // namespace

WebConsole::WebConsole(ClusterConfig config)
    : config_(std::move(config)) {}

HttpResponse WebConsole::handle(const HttpRequest& request) const {
    try {
        if (request.method == "GET" && request.path == "/") {
            return user_console();
        }
        if (request.method == "GET" && request.path == "/admin") {
            return admin_console();
        }
        if (request.method == "GET" && request.path == "/api/admin/cluster") {
            return cluster_status();
        }
        if (request.method == "POST" && request.path == "/api/vectors") {
            return upsert_vector(request.body);
        }
        if (request.method == "POST" && request.path == "/api/search") {
            return search_vectors(request.body);
        }
        if (request.method == "DELETE" && request.path.rfind("/api/vectors/", 0) == 0) {
            return delete_vector(request.path);
        }
        return json_response("{\"error\":\"not found\"}", 404);
    } catch (const std::exception& error) {
        return json_response("{\"error\":\"" + json_escape(error.what()) + "\"}", 400);
    }
}

HttpResponse WebConsole::user_console() const {
    return HttpResponse{200, "text/html; charset=utf-8", kUserHtml};
}

HttpResponse WebConsole::admin_console() const {
    return HttpResponse{200, "text/html; charset=utf-8", kAdminHtml};
}

HttpResponse WebConsole::cluster_status() const {
    std::ostringstream body;
    body << "{\"nodes\":[";
    bool first = true;
    for (const auto& node : config_.nodes()) {
        if (!first) {
            body << ',';
        }
        first = false;
        body << "{\"id\":\"" << json_escape(node.id)
             << "\",\"host\":\"" << json_escape(node.host)
             << "\",\"port\":" << node.port
             << ",\"store\":\"" << json_escape(node.store_path.string()) << "\"}";
    }
    body << "]}";
    return json_response(body.str());
}

HttpResponse WebConsole::upsert_vector(const std::string& body) const {
    const auto id = extract_string_field(body, "id");
    const auto values = extract_values_field(body);
    DistributedVectorDatabase(config_).upsert(id, values);
    const auto& owner = config_.owner_for_id(id);
    return json_response("{\"status\":\"ok\",\"owner\":\"" + json_escape(owner.id) + "\"}", 201);
}

HttpResponse WebConsole::search_vectors(const std::string& body) const {
    const auto limit = extract_size_field(body, "limit", 10);
    const auto values = extract_values_field(body);
    const auto results = DistributedVectorDatabase(config_).search(values, limit);

    std::ostringstream response;
    response << "{\"results\":[";
    for (std::size_t i = 0; i < results.size(); ++i) {
        if (i > 0) {
            response << ',';
        }
        response << "{\"id\":\"" << json_escape(results[i].id) << "\",\"score\":" << results[i].score << "}";
    }
    response << "]}";
    return json_response(response.str());
}

HttpResponse WebConsole::delete_vector(const std::string& path) const {
    const auto id = path.substr(std::string("/api/vectors/").size());
    if (id.empty()) {
        return json_response("{\"error\":\"vector id is required\"}", 400);
    }
    const auto deleted = DistributedVectorDatabase(config_).erase(id);
    return json_response(std::string("{\"deleted\":") + (deleted ? "true" : "false") + "}");
}

HttpConsoleServer::HttpConsoleServer(std::string host, std::uint16_t port, WebConsole console)
    : host_(std::move(host)), port_(port), console_(std::move(console)) {}

void HttpConsoleServer::run() {
    SocketRuntime runtime;
    const auto server = listen_socket(host_, port_);
    while (true) {
        const auto client = accept(server, nullptr, nullptr);
        if (client == kInvalidHttpSocket) {
            continue;
        }
        try {
            send_all(client, to_http_wire(console_.handle(parse_http_request(receive_request(client)))));
        } catch (const std::exception& error) {
            send_all(client, to_http_wire(json_response("{\"error\":\"" + json_escape(error.what()) + "\"}", 500)));
        }
        close_socket(client);
    }
}

} // namespace salamis
