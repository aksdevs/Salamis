// SPDX-License-Identifier: GPL-3.0-only

#include "salamis/SalmisCluster.hpp"

#include "salamis/SalmisRuntime.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <unordered_set>
#include <utility>

namespace salamis {
namespace {

std::string vector_to_wire(const std::vector<float>& values) {
    std::ostringstream stream;
    for (const auto value : values) {
        stream << ' ' << value;
    }
    return stream.str();
}

std::vector<SearchResult> parse_search_response(const std::string& response) {
    std::istringstream stream(response);
    std::string status;
    std::size_t count = 0;
    stream >> status >> count;
    if (status != "RESULTS") {
        throw VectorDatabaseError("unexpected search response: " + response);
    }

    std::vector<SearchResult> results;
    results.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        SearchResult result;
        stream >> result.id >> result.score;
        if (!stream) {
            throw VectorDatabaseError("malformed search response: " + response);
        }
        results.push_back(std::move(result));
    }
    return results;
}

void require_ok(const std::string& response) {
    if (response.rfind("OK", 0) != 0) {
        throw VectorDatabaseError("remote node rejected request: " + response);
    }
}

} // namespace

bool LeaderElectionResult::has_quorum() const noexcept {
    return reachable_nodes > total_nodes / 2;
}

ClusterConfig ClusterConfig::load(const std::filesystem::path& path) {
    std::ifstream stream(path);
    if (!stream) {
        throw VectorDatabaseError("failed to open cluster config: " + path.string());
    }

    ClusterConfig config;
    std::unordered_set<std::string> ids;
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }

        std::istringstream parts(line);
        NodeConfig node;
        parts >> node.id >> node.host >> node.port >> node.store_path;
        if (!parts || node.id.empty() || node.host.empty() || node.port == 0) {
            throw VectorDatabaseError("invalid cluster config line: " + line);
        }
        if (!ids.insert(node.id).second) {
            throw VectorDatabaseError("duplicate node id in cluster config: " + node.id);
        }
        config.nodes_.push_back(std::move(node));
    }

    if (config.nodes_.empty()) {
        throw VectorDatabaseError("cluster config must contain at least one node");
    }

    std::sort(config.nodes_.begin(), config.nodes_.end(), [](const NodeConfig& lhs, const NodeConfig& rhs) {
        return lhs.id < rhs.id;
    });
    return config;
}

const std::vector<NodeConfig>& ClusterConfig::nodes() const noexcept {
    return nodes_;
}

const NodeConfig& ClusterConfig::node_by_id(const std::string& id) const {
    const auto found = std::find_if(nodes_.begin(), nodes_.end(), [&](const NodeConfig& node) {
        return node.id == id;
    });
    if (found == nodes_.end()) {
        throw VectorDatabaseError("node not found in cluster config: " + id);
    }
    return *found;
}

const NodeConfig& ClusterConfig::owner_for_id(const std::string& vector_id) const {
    return nodes_[static_cast<std::size_t>(shard_for_id(vector_id))];
}

std::uint64_t ClusterConfig::shard_for_id(const std::string& vector_id) const {
    if (nodes_.empty()) {
        throw VectorDatabaseError("cluster config must contain at least one node");
    }
    return stable_hash(vector_id) % nodes_.size();
}

std::uint64_t ClusterConfig::stable_hash(const std::string& value) noexcept {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const auto ch : value) {
        hash ^= static_cast<unsigned char>(ch);
        hash *= 1099511628211ULL;
    }
    return hash;
}

LeaderElectionResult ClusterConfig::elect_leader(const std::vector<NodeHealth>& health) const {
    LeaderElectionResult result;
    result.total_nodes = nodes_.size();

    std::unordered_set<std::string> reachable;
    for (const auto& node : health) {
        if (node.reachable) {
            reachable.insert(node.id);
        }
    }
    result.reachable_nodes = reachable.size();

    if (!result.has_quorum()) {
        return result;
    }

    for (const auto& node : nodes_) {
        if (reachable.find(node.id) != reachable.end()) {
            result.leader = node;
            return result;
        }
    }
    return result;
}

DistributedVectorDatabase::DistributedVectorDatabase(ClusterConfig config)
    : config_(std::move(config)) {}

void DistributedVectorDatabase::upsert(std::string id, const std::vector<float>& values) {
    const auto& owner = config_.owner_for_id(id);
    require_ok(TcpVectorClient(owner.host, owner.port).request("PUT " + id + vector_to_wire(values)));
}

bool DistributedVectorDatabase::erase(const std::string& id) {
    const auto& owner = config_.owner_for_id(id);
    const auto response = TcpVectorClient(owner.host, owner.port).request("DELETE " + id);
    if (response == "NOT_FOUND") {
        return false;
    }
    require_ok(response);
    return true;
}

std::vector<SearchResult> DistributedVectorDatabase::search(const std::vector<float>& query, std::size_t limit) const {
    std::vector<SearchResult> merged;
    for (const auto& node : config_.nodes()) {
        auto results = parse_search_response(
            TcpVectorClient(node.host, node.port).request("SEARCH " + std::to_string(limit) + vector_to_wire(query)));
        merged.insert(merged.end(), results.begin(), results.end());
    }

    const auto result_count = std::min(limit, merged.size());
    std::partial_sort(
        merged.begin(),
        merged.begin() + static_cast<std::ptrdiff_t>(result_count),
        merged.end(),
        [](const SearchResult& lhs, const SearchResult& rhs) {
            if (lhs.score == rhs.score) {
                return lhs.id < rhs.id;
            }
            return lhs.score > rhs.score;
        });
    merged.resize(result_count);
    return merged;
}

} // namespace salamis
