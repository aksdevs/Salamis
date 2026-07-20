// SPDX-License-Identifier: GPL-3.0-only

#include "salamis/SalmisManagement.hpp"

#include "salamis/SalmisRuntime.hpp"

#include <algorithm>
#include <sstream>
#include <utility>
#include <vector>

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
    const auto health = probe_health();
    for (const auto& node : config_.nodes()) {
        const auto found = std::find_if(health.begin(), health.end(), [&](const NodeHealth& item) {
            return item.id == node.id;
        });
        report << node.id << '\t' << (found != health.end() && found->reachable ? "up" : "down") << '\t'
               << (found != health.end() && found->reachable ? "PONG" : "unreachable") << '\n';
    }
    return report.str();
}

std::string ManagementConsole::leader_report() const {
    const auto election = config_.elect_leader(probe_health());
    std::ostringstream report;
    report << "REACHABLE\tTOTAL\tQUORUM\tLEADER\n";
    report << election.reachable_nodes << '\t' << election.total_nodes << '\t'
           << (election.has_quorum() ? "yes" : "no") << '\t';
    if (election.leader) {
        report << election.leader->id << " (" << election.leader->host << ':' << election.leader->port << ')';
    } else {
        report << "none";
    }
    report << '\n';
    return report.str();
}

std::vector<NodeHealth> ManagementConsole::probe_health() const {
    std::vector<NodeHealth> health;
    health.reserve(config_.nodes().size());
    for (const auto& node : config_.nodes()) {
        try {
            const auto response = TcpVectorClient(node.host, node.port).request("PING");
            health.push_back(NodeHealth{node.id, response == "PONG"});
        } catch (const std::exception&) {
            health.push_back(NodeHealth{node.id, false});
        }
    }
    return health;
}

} // namespace salamis
