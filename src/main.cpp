// SPDX-License-Identifier: GPL-3.0-only

#include "salamis/SalmisCluster.hpp"
#include "salamis/SalmisWebConsole.hpp"
#include "salamis/SalmisManagement.hpp"
#include "salamis/SalmisRuntime.hpp"
#include "salamis/SalmisCore.hpp"

#include <iostream>
#include <sstream>

namespace {

std::vector<float> parse_vector(int start, int argc, char* argv[]) {
    std::vector<float> values;
    for (int i = start; i < argc; ++i) {
        values.push_back(std::stof(argv[i]));
    }
    return values;
}

void print_usage() {
    std::cerr << "Usage:\n"
              << "  salamis_cli <store> put <id> <v1> <v2> ...\n"
              << "  salamis_cli <store> get <id>\n"
              << "  salamis_cli <store> search <limit> <v1> <v2> ...\n"
              << "  salamis_cli <store> delete <id>\n"
              << "  salamis_cli serve <cluster.conf> <node-id>\n"
              << "  salamis_cli cluster <cluster.conf> put <id> <v1> <v2> ...\n"
              << "  salamis_cli cluster <cluster.conf> search <limit> <v1> <v2> ...\n"
              << "  salamis_cli cluster <cluster.conf> delete <id>\n"
              << "  salamis_cli manage <cluster.conf> nodes\n"
              << "  salamis_cli manage <cluster.conf> owner <id>\n"
              << "  salamis_cli manage <cluster.conf> health\n"
              << "  salamis_cli web <cluster.conf> <host> <port>\n"
              << "  salamis_cli gui <cluster.conf> <host> <port>\n";
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc < 3) {
        print_usage();
        return 1;
    }

    try {
        const std::string mode = argv[1];
        if (mode == "serve") {
            if (argc != 4) {
                print_usage();
                return 1;
            }
            const auto config = salamis::ClusterConfig::load(argv[2]);
            const auto& node = config.node_by_id(argv[3]);
            salamis::VectorDatabase db(node.store_path);
            salamis::TcpVectorServer(node.host, node.port, db).run();
            return 0;
        }

        if (mode == "cluster") {
            if (argc < 5) {
                print_usage();
                return 1;
            }
            salamis::DistributedVectorDatabase db(salamis::ClusterConfig::load(argv[2]));
            const std::string command = argv[3];

            if (command == "put") {
                if (argc < 6) {
                    print_usage();
                    return 1;
                }
                db.upsert(argv[4], parse_vector(5, argc, argv));
                std::cout << "ok\n";
                return 0;
            }

            if (command == "search") {
                if (argc < 6) {
                    print_usage();
                    return 1;
                }
                const auto limit = static_cast<std::size_t>(std::stoull(argv[4]));
                for (const auto& result : db.search(parse_vector(5, argc, argv), limit)) {
                    std::cout << result.id << ' ' << result.score << '\n';
                }
                return 0;
            }

            if (command == "delete") {
                if (argc != 5) {
                    print_usage();
                    return 1;
                }
                return db.erase(argv[4]) ? 0 : 2;
            }

            print_usage();
            return 1;
        }

        if (mode == "manage") {
            if (argc < 4) {
                print_usage();
                return 1;
            }
            const salamis::ManagementConsole manager(salamis::ClusterConfig::load(argv[2]));
            const std::string command = argv[3];
            if (command == "nodes") {
                std::cout << manager.nodes_report();
                return 0;
            }
            if (command == "owner") {
                if (argc != 5) {
                    print_usage();
                    return 1;
                }
                std::cout << manager.owner_report(argv[4]);
                return 0;
            }
            if (command == "health") {
                std::cout << manager.health_report();
                return 0;
            }
            print_usage();
            return 1;
        }

        if (mode == "web" || mode == "gui") {
            if (argc != 5) {
                print_usage();
                return 1;
            }
            std::cout << "Salamis GUI available at http://" << argv[3] << ':' << argv[4] << '\n';
            salamis::HttpConsoleServer(
                argv[3],
                static_cast<std::uint16_t>(std::stoul(argv[4])),
                salamis::WebConsole(salamis::ClusterConfig::load(argv[2])))
                .run();
            return 0;
        }

        salamis::VectorDatabase db(argv[1]);
        const std::string command = argv[2];

        if (command == "put") {
            if (argc < 5) {
                print_usage();
                return 1;
            }
            db.upsert(argv[3], parse_vector(4, argc, argv));
            std::cout << "ok\n";
            return 0;
        }

        if (command == "get") {
            if (argc != 4) {
                print_usage();
                return 1;
            }
            const auto record = db.get(argv[3]);
            if (!record) {
                return 2;
            }
            std::cout << record->id;
            for (const auto value : record->values) {
                std::cout << ' ' << value;
            }
            std::cout << '\n';
            return 0;
        }

        if (command == "search") {
            if (argc < 5) {
                print_usage();
                return 1;
            }
            const auto limit = static_cast<std::size_t>(std::stoull(argv[3]));
            for (const auto& result : db.search(parse_vector(4, argc, argv), limit)) {
                std::cout << result.id << ' ' << result.score << '\n';
            }
            return 0;
        }

        if (command == "delete") {
            if (argc != 4) {
                print_usage();
                return 1;
            }
            return db.erase(argv[3]) ? 0 : 2;
        }

        print_usage();
        return 1;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
