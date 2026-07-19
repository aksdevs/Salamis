// SPDX-License-Identifier: GPL-3.0-only

#include "salamis/SalmisManagement.hpp"

#include "salamis/SalmisRuntime.hpp"

#include <sstream>
#include <utility>

namespace salamis {

ManagementConsole::ManagementConsole(ClusterConfig config)
    : config_(std::move(config)) {}

std::string ManagementConsole::nodes_report() const {
    std::ostringstream report;
    report << "NODE\tHOST\tPORT\tSTORE\n";
    for (const auto& node : config_.nodes()) {
        report << node.id << '\t' << node.host << '\t' << node.port << '\t' << node.store_path.string() << '\n';
    }
    return report.str();
}

std::string ManagementConsole::owner_report(const std::string& vector_id) const {
    const auto& owner = config_.owner_for_id(vector_id);
    std::ostringstream report;
    report << vector_id << " -> " << owner.id << " (" << owner.host << ':' << owner.port << ")\n";
    return report.str();
}

std::string ManagementConsole::health_report() const {
    std::ostringstream report;
    report << "NODE\tSTATUS\tDETAIL\n";
    for (const auto& node : config_.nodes()) {
        try {
            const auto response = TcpVectorClient(node.host, node.port).request("PING");
            report << node.id << '\t' << (response == "PONG" ? "up" : "degraded") << '\t' << response << '\n';
        } catch (const std::exception& error) {
            report << node.id << "\tdown\t" << error.what() << '\n';
        }
    }
    return report.str();
}

} // namespace salamis
