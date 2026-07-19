// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace salamis {

enum class Metric {
    Cosine,
    L2,
    Dot
};

struct VectorRecord {
    std::string id;
    std::vector<float> values;
};

struct SearchResult {
    std::string id;
    float score = 0.0F;
};

class VectorDatabaseError : public std::runtime_error {
public:
    explicit VectorDatabaseError(const std::string& message);
};

class DiskVectorStore {
public:
    explicit DiskVectorStore(std::filesystem::path path);

    [[nodiscard]] std::vector<VectorRecord> load() const;
    void save(const std::vector<VectorRecord>& records) const;
    [[nodiscard]] const std::filesystem::path& path() const noexcept;

private:
    std::filesystem::path path_;
};

class VectorDatabase {
public:
    explicit VectorDatabase(std::filesystem::path storage_path, Metric metric = Metric::Cosine);

    void upsert(std::string id, std::vector<float> values);
    [[nodiscard]] bool erase(const std::string& id);
    [[nodiscard]] std::optional<VectorRecord> get(const std::string& id) const;
    [[nodiscard]] std::vector<SearchResult> search(const std::vector<float>& query, std::size_t limit) const;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] std::size_t dimensions() const noexcept;
    void flush() const;

private:
    void load_from_disk();
    void validate_vector(const std::vector<float>& values) const;
    [[nodiscard]] float score(const std::vector<float>& lhs, const std::vector<float>& rhs) const;
    [[nodiscard]] std::vector<VectorRecord> records_in_stable_order() const;

    DiskVectorStore store_;
    Metric metric_;
    std::size_t dimensions_ = 0;
    std::unordered_map<std::string, std::vector<float>> vectors_;
};

} // namespace salamis
