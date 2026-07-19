// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "salamis/SalmisCluster.hpp"

#include <string>

namespace salamis {

class ManagementConsole {
public:
    explicit ManagementConsole(ClusterConfig config);

    [[nodiscard]] std::string nodes_report() const;
    [[nodiscard]] std::string owner_report(const std::string& vector_id) const;
    [[nodiscard]] std::string health_report() const;

private:
    ClusterConfig config_;
};

} // namespace salamis
