// SPDX-License-Identifier: GPL-3.0-only

#include "salamis/SalmisRuntime.hpp"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_type = SOCKET;
constexpr socket_type kInvalidSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
using socket_type = int;
constexpr socket_type kInvalidSocket = -1;
#endif

namespace salamis {
namespace {

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

void close_socket(socket_type socket) {
#ifdef _WIN32
    closesocket(socket);
#else
    close(socket);
#endif
}

std::string socket_error(const std::string& action) {
    return "socket error while " + action;
}

socket_type connect_socket(const std::string& host, std::uint16_t port) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* result = nullptr;
    const auto port_text = std::to_string(port);
    if (getaddrinfo(host.c_str(), port_text.c_str(), &hints, &result) != 0) {
        throw VectorDatabaseError("failed to resolve host: " + host);
    }

    socket_type connected = kInvalidSocket;
    for (auto* item = result; item != nullptr; item = item->ai_next) {
        const auto candidate = socket(item->ai_family, item->ai_socktype, item->ai_protocol);
        if (candidate == kInvalidSocket) {
            continue;
        }
        if (connect(candidate, item->ai_addr, static_cast<int>(item->ai_addrlen)) == 0) {
            connected = candidate;
            break;
        }
        close_socket(candidate);
    }

    freeaddrinfo(result);
    if (connected == kInvalidSocket) {
        throw VectorDatabaseError("failed to connect to " + host + ":" + port_text);
    }
    return connected;
}

socket_type listen_socket(const std::string& host, std::uint16_t port) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    addrinfo* result = nullptr;
    const auto port_text = std::to_string(port);
    const char* bind_host = host == "0.0.0.0" ? nullptr : host.c_str();
    if (getaddrinfo(bind_host, port_text.c_str(), &hints, &result) != 0) {
        throw VectorDatabaseError("failed to resolve bind address: " + host);
    }

    socket_type server = kInvalidSocket;
    for (auto* item = result; item != nullptr; item = item->ai_next) {
        const auto candidate = socket(item->ai_family, item->ai_socktype, item->ai_protocol);
        if (candidate == kInvalidSocket) {
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
    if (server == kInvalidSocket) {
        throw VectorDatabaseError("failed to listen on " + host + ":" + port_text);
    }
    return server;
}

std::string read_line(socket_type socket) {
    std::string line;
    char ch = '\0';
    while (true) {
        const auto received = recv(socket, &ch, 1, 0);
        if (received <= 0) {
            break;
        }
        if (ch == '\n') {
            break;
        }
        if (ch != '\r') {
            line.push_back(ch);
        }
    }
    return line;
}

void send_all(socket_type socket, const std::string& payload) {
    std::size_t sent_total = 0;
    while (sent_total < payload.size()) {
        const auto sent = send(socket, payload.data() + sent_total, static_cast<int>(payload.size() - sent_total), 0);
        if (sent <= 0) {
            throw VectorDatabaseError(socket_error("sending data"));
        }
        sent_total += static_cast<std::size_t>(sent);
    }
}

std::vector<float> parse_values(std::istringstream& stream) {
    std::vector<float> values;
    float value = 0.0F;
    while (stream >> value) {
        values.push_back(value);
    }
    return values;
}

std::string format_results(const std::vector<SearchResult>& results) {
    std::ostringstream response;
    response << "RESULTS " << results.size();
    for (const auto& result : results) {
        response << ' ' << result.id << ' ' << result.score;
    }
    return response.str();
}

} // namespace

TcpVectorClient::TcpVectorClient(std::string host, std::uint16_t port)
    : host_(std::move(host)), port_(port) {}

std::string TcpVectorClient::request(const std::string& line) const {
    SocketRuntime runtime;
    const auto socket = connect_socket(host_, port_);
    send_all(socket, line + "\n");
    const auto response = read_line(socket);
    close_socket(socket);
    return response;
}

TcpVectorServer::TcpVectorServer(std::string host, std::uint16_t port, VectorDatabase& database)
    : host_(std::move(host)), port_(port), database_(database) {}

void TcpVectorServer::run() {
    SocketRuntime runtime;
    const auto server = listen_socket(host_, port_);
    std::cout << "listening on " << host_ << ':' << port_ << '\n';

    while (true) {
        const auto client = accept(server, nullptr, nullptr);
        if (client == kInvalidSocket) {
            continue;
        }
        try {
            send_all(client, handle_request(read_line(client)) + "\n");
        } catch (const std::exception& error) {
            send_all(client, std::string("ERR ") + error.what() + "\n");
        }
        close_socket(client);
    }
}

std::string TcpVectorServer::handle_request(const std::string& line) {
    std::istringstream stream(line);
    std::string command;
    stream >> command;

    if (command == "PUT") {
        std::string id;
        stream >> id;
        database_.upsert(id, parse_values(stream));
        return "OK";
    }

    if (command == "PING") {
        return "PONG";
    }

    if (command == "STATS") {
        const auto stats = database_.stats();
        std::ostringstream response;
        response << "STATS " << stats.vectors << ' ' << stats.dimensions << ' ' << stats.pending_writes;
        return response.str();
    }

    if (command == "DELETE") {
        std::string id;
        stream >> id;
        return database_.erase(id) ? "OK" : "NOT_FOUND";
    }

    if (command == "GET") {
        std::string id;
        stream >> id;
        const auto record = database_.get(id);
        if (!record) {
            return "NOT_FOUND";
        }
        std::ostringstream response;
        response << "VECTOR " << record->id;
        for (const auto value : record->values) {
            response << ' ' << value;
        }
        return response.str();
    }

    if (command == "SEARCH") {
        std::size_t limit = 0;
        stream >> limit;
        return format_results(database_.search(parse_values(stream), limit));
    }

    return "ERR unknown command";
}

} // namespace salamis
