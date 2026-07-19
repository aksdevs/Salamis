// SPDX-License-Identifier: GPL-3.0-only

#include "salamis/SalmisCluster.hpp"
#include "salamis/SalmisWebConsole.hpp"
#include "salamis/SalmisManagement.hpp"
#include "salamis/SalmisRuntime.hpp"
#include "salamis/SalmisCore.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>

namespace {

std::filesystem::path test_path(const std::string& name) {
    auto path = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove(path);
    return path;
}

void supports_upsert_get_and_search() {
    const auto path = test_path("salamis_vector_database_tests.svdb");
    salamis::VectorDatabase db(path);

    db.upsert("alpha", {1.0F, 0.0F, 0.0F});
    db.upsert("beta", {0.0F, 1.0F, 0.0F});
    db.upsert("gamma", {0.8F, 0.2F, 0.0F});

    const auto alpha = db.get("alpha");
    assert(alpha.has_value());
    assert(alpha->values.size() == 3);

    const auto results = db.search({1.0F, 0.0F, 0.0F}, 2);
    assert(results.size() == 2);
    assert(results[0].id == "alpha");
    assert(results[1].id == "gamma");
}

void persists_to_disk() {
    const auto path = test_path("salamis_persistence_tests.svdb");
    {
        salamis::VectorDatabase db(path);
        db.upsert("persisted", {4.0F, 5.0F});
    }

    salamis::VectorDatabase reopened(path);
    assert(reopened.size() == 1);
    assert(reopened.dimensions() == 2);
    assert(reopened.get("persisted").has_value());
}

void rejects_dimension_mismatch() {
    const auto path = test_path("salamis_dimension_tests.svdb");
    salamis::VectorDatabase db(path);
    db.upsert("one", {1.0F, 2.0F});

    bool rejected = false;
    try {
        db.upsert("bad", {1.0F, 2.0F, 3.0F});
    } catch (const salamis::VectorDatabaseError&) {
        rejected = true;
    }
    assert(rejected);
}

void deletes_records() {
    const auto path = test_path("salamis_delete_tests.svdb");
    salamis::VectorDatabase db(path);
    db.upsert("one", {1.0F, 2.0F});
    assert(db.erase("one"));
    assert(!db.get("one").has_value());
}

void supports_l2_and_dot_metrics() {
    const auto l2_path = test_path("salamis_l2_tests.svdb");
    salamis::VectorDatabase l2(l2_path, salamis::Metric::L2);
    l2.upsert("near", {1.0F, 1.0F});
    l2.upsert("far", {9.0F, 9.0F});
    assert(l2.search({1.0F, 2.0F}, 1).front().id == "near");

    const auto dot_path = test_path("salamis_dot_tests.svdb");
    salamis::VectorDatabase dot(dot_path, salamis::Metric::Dot);
    dot.upsert("small", {1.0F, 0.0F});
    dot.upsert("large", {3.0F, 0.0F});
    assert(dot.search({1.0F, 0.0F}, 1).front().id == "large");
}

void upsert_replaces_existing_record_on_disk() {
    const auto path = test_path("salamis_replace_tests.svdb");
    {
        salamis::VectorDatabase db(path);
        db.upsert("same", {1.0F, 0.0F});
        db.upsert("same", {0.0F, 1.0F});
    }

    salamis::VectorDatabase reopened(path);
    assert(reopened.size() == 1);
    assert(reopened.search({0.0F, 1.0F}, 1).front().id == "same");
}

void rejects_empty_and_non_finite_vectors() {
    const auto path = test_path("salamis_validation_tests.svdb");
    salamis::VectorDatabase db(path);

    bool empty_rejected = false;
    try {
        db.upsert("empty", {});
    } catch (const salamis::VectorDatabaseError&) {
        empty_rejected = true;
    }
    assert(empty_rejected);

    bool nan_rejected = false;
    try {
        db.upsert("nan", {std::numeric_limits<float>::quiet_NaN()});
    } catch (const salamis::VectorDatabaseError&) {
        nan_rejected = true;
    }
    assert(nan_rejected);
}

void loads_cluster_config_and_selects_owner() {
    const auto path = test_path("salamis_cluster_tests.conf");
    {
        std::ofstream stream(path);
        stream << "node-a 127.0.0.1 7101 node-a.svdb\n";
        stream << "node-b 127.0.0.1 7102 node-b.svdb\n";
    }

    const auto config = salamis::ClusterConfig::load(path);
    assert(config.nodes().size() == 2);
    assert(config.node_by_id("node-a").port == 7101);
    const auto& owner = config.owner_for_id("doc-1");
    assert(owner.id == "node-a" || owner.id == "node-b");
    std::filesystem::remove(path);
}

void supports_variable_cluster_sizes() {
    for (const auto node_count : {1, 2, 3, 5}) {
        const auto path = test_path("salamis_variable_cluster_" + std::to_string(node_count) + ".conf");
        {
            std::ofstream stream(path);
            for (int i = 0; i < node_count; ++i) {
                stream << "node-" << i << " 127.0.0.1 " << (7200 + i) << " node-" << i << ".svdb\n";
            }
        }

        const auto config = salamis::ClusterConfig::load(path);
        assert(config.nodes().size() == static_cast<std::size_t>(node_count));
        for (const auto& id : {"doc-1", "doc-2", "doc-3", "doc-4"}) {
            const auto shard = config.shard_for_id(id);
            assert(shard < static_cast<std::uint64_t>(node_count));
            const auto& owner = config.owner_for_id(id);
            assert(owner.id.find("node-") == 0);
        }
        std::filesystem::remove(path);
    }
}

void preserves_single_node_ownership() {
    const auto path = test_path("salamis_single_node_cluster.conf");
    {
        std::ofstream stream(path);
        stream << "node-a 127.0.0.1 7101 node-a.svdb\n";
    }

    const auto config = salamis::ClusterConfig::load(path);
    assert(config.owner_for_id("doc-1").id == "node-a");
    assert(config.owner_for_id("another-document").id == "node-a");
    assert(config.shard_for_id("any-id") == 0);
    std::filesystem::remove(path);
}

void rejects_invalid_cluster_config() {
    const auto empty_path = test_path("salamis_empty_cluster_tests.conf");
    {
        std::ofstream stream(empty_path);
        stream << "# no nodes\n";
    }

    bool empty_rejected = false;
    try {
        salamis::ClusterConfig::load(empty_path);
    } catch (const salamis::VectorDatabaseError&) {
        empty_rejected = true;
    }
    assert(empty_rejected);

    const auto duplicate_path = test_path("salamis_duplicate_cluster_tests.conf");
    {
        std::ofstream stream(duplicate_path);
        stream << "node-a 127.0.0.1 7101 a.svdb\n";
        stream << "node-a 127.0.0.1 7102 b.svdb\n";
    }

    bool duplicate_rejected = false;
    try {
        salamis::ClusterConfig::load(duplicate_path);
    } catch (const salamis::VectorDatabaseError&) {
        duplicate_rejected = true;
    }
    assert(duplicate_rejected);

    std::filesystem::remove(empty_path);
    std::filesystem::remove(duplicate_path);
}

void handles_tcp_protocol_commands() {
    const auto path = test_path("salamis_protocol_tests.svdb");
    salamis::VectorDatabase db(path);
    salamis::TcpVectorServer server("127.0.0.1", 0, db);

    assert(server.handle_request("PUT doc-1 1 0 0") == "OK");
    assert(server.handle_request("PING") == "PONG");
    assert(server.handle_request("GET doc-1").rfind("VECTOR doc-1", 0) == 0);
    assert(server.handle_request("SEARCH 1 1 0 0").find("doc-1") != std::string::npos);
    assert(server.handle_request("DELETE doc-1") == "OK");
    assert(server.handle_request("DELETE doc-1") == "NOT_FOUND");
    assert(server.handle_request("UNKNOWN").rfind("ERR", 0) == 0);
}

void formats_management_reports() {
    const auto path = test_path("salamis_management_tests.conf");
    {
        std::ofstream stream(path);
        stream << "node-a 127.0.0.1 7101 node-a.svdb\n";
        stream << "node-b 127.0.0.1 7102 node-b.svdb\n";
    }

    salamis::ManagementConsole manager(salamis::ClusterConfig::load(path));
    const auto nodes = manager.nodes_report();
    assert(nodes.find("NODE\tHOST\tPORT\tSTORE") != std::string::npos);
    assert(nodes.find("node-a") != std::string::npos);

    const auto owner = manager.owner_report("doc-1");
    assert(owner.find("doc-1 -> node-") != std::string::npos);
    std::filesystem::remove(path);
}

void serves_web_console_surfaces() {
    const auto path = test_path("salamis_web_cluster_tests.conf");
    {
        std::ofstream stream(path);
        stream << "node-a 127.0.0.1 7101 node-a.svdb\n";
    }
    salamis::WebConsole console(salamis::ClusterConfig::load(path));

    const auto user = console.handle({"GET", "/", ""});
    assert(user.status == 200);
    assert(user.content_type.find("text/html") != std::string::npos);
    assert(user.body.find("Salamis User Console") != std::string::npos);

    const auto admin = console.handle({"GET", "/admin", ""});
    assert(admin.status == 200);
    assert(admin.body.find("Salamis Admin Console") != std::string::npos);

    const auto status = console.handle({"GET", "/api/admin/cluster", ""});
    assert(status.status == 200);
    assert(status.body.find("\"node-a\"") != std::string::npos);

    const auto missing = console.handle({"GET", "/missing", ""});
    assert(missing.status == 404);
    std::filesystem::remove(path);
}

void validates_web_api_payloads() {
    const auto path = test_path("salamis_web_validation_tests.conf");
    {
        std::ofstream stream(path);
        stream << "node-a 127.0.0.1 7101 node-a.svdb\n";
    }
    salamis::WebConsole console(salamis::ClusterConfig::load(path));
    const auto response = console.handle({"POST", "/api/search", "{\"limit\":1}"});
    assert(response.status == 400);
    assert(response.body.find("values") != std::string::npos);
    std::filesystem::remove(path);
}

} // namespace

int main() {
    supports_upsert_get_and_search();
    persists_to_disk();
    rejects_dimension_mismatch();
    deletes_records();
    supports_l2_and_dot_metrics();
    upsert_replaces_existing_record_on_disk();
    rejects_empty_and_non_finite_vectors();
    loads_cluster_config_and_selects_owner();
    supports_variable_cluster_sizes();
    preserves_single_node_ownership();
    rejects_invalid_cluster_config();
    handles_tcp_protocol_commands();
    formats_management_reports();
    serves_web_console_surfaces();
    validates_web_api_payloads();
    std::cout << "all tests passed\n";
}
