// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "salamis/SalmisCore.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace salamis {

struct NodeConfig {
    std::string id;
    std::string host;
    std::uint16_t port = 0;
    std::filesystem::path store_path;
};

struct NodeHealth {
    std::string id;
    bool reachable = false;
};

struct LeaderElectionResult {
    std::optional<NodeConfig> leader;
    std::size_t reachable_nodes = 0;
    std::size_t total_nodes = 0;

    [[nodiscard]] bool has_quorum() const noexcept;
};

class ClusterConfig {
public:
    static ClusterConfig load(const std::filesystem::path& path);

    [[nodiscard]] const std::vector<NodeConfig>& nodes() const noexcept;
    [[nodiscard]] const NodeConfig& node_by_id(const std::string& id) const;
    [[nodiscard]] const NodeConfig& owner_for_id(const std::string& vector_id) const;
    [[nodiscard]] std::uint64_t shard_for_id(const std::string& vector_id) const;
    [[nodiscard]] LeaderElectionResult elect_leader(const std::vector<NodeHealth>& health) const;

private:
    [[nodiscard]] static std::uint64_t stable_hash(const std::string& value) noexcept;

    std::vector<NodeConfig> nodes_;
};

class DistributedVectorDatabase {
public:
    explicit DistributedVectorDatabase(ClusterConfig config);

    void upsert(std::string id, const std::vector<float>& values);
    [[nodiscard]] bool erase(const std::string& id);
    [[nodiscard]] std::vector<SearchResult> search(const std::vector<float>& query, std::size_t limit) const;

private:
    ClusterConfig config_;
};

} // namespace salamis
