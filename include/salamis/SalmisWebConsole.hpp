// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "salamis/SalmisCluster.hpp"

#include <cstdint>
#include <map>
#include <string>

namespace salamis {

struct HttpRequest {
    std::string method;
    std::string path;
    std::string body;
};

struct HttpResponse {
    int status = 200;
    std::string content_type = "text/plain; charset=utf-8";
    std::string body;
};

class WebConsole {
public:
    explicit WebConsole(ClusterConfig config);

    [[nodiscard]] HttpResponse handle(const HttpRequest& request) const;

private:
    [[nodiscard]] HttpResponse user_console() const;
    [[nodiscard]] HttpResponse admin_console() const;
    [[nodiscard]] HttpResponse cluster_status() const;
    [[nodiscard]] HttpResponse upsert_vector(const std::string& body) const;
    [[nodiscard]] HttpResponse search_vectors(const std::string& body) const;
    [[nodiscard]] HttpResponse delete_vector(const std::string& path) const;

    ClusterConfig config_;
};

class HttpConsoleServer {
public:
    HttpConsoleServer(std::string host, std::uint16_t port, WebConsole console);

    void run();

private:
    std::string host_;
    std::uint16_t port_;
    WebConsole console_;
};

} // namespace salamis
