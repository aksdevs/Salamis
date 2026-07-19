// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "salamis/SalmisCore.hpp"

#include <cstdint>
#include <string>

namespace salamis {

class TcpVectorClient {
public:
    TcpVectorClient(std::string host, std::uint16_t port);

    [[nodiscard]] std::string request(const std::string& line) const;

private:
    std::string host_;
    std::uint16_t port_;
};

class TcpVectorServer {
public:
    TcpVectorServer(std::string host, std::uint16_t port, VectorDatabase& database);

    void run();
    [[nodiscard]] std::string handle_request(const std::string& line);

private:
    std::string host_;
    std::uint16_t port_;
    VectorDatabase& database_;
};

} // namespace salamis
