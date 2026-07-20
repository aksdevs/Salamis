// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <shared_mutex>
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

struct DatabaseStats {
    std::size_t vectors = 0;
    std::size_t dimensions = 0;
    std::size_t pending_writes = 0;
};

enum class DurabilityMode {
    FlushOnWrite,
    ManualFlush
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
    explicit VectorDatabase(std::filesystem::path storage_path,
                            Metric metric = Metric::Cosine,
                            DurabilityMode durability = DurabilityMode::FlushOnWrite);

    void upsert(std::string id, std::vector<float> values);
    void upsert_many(std::vector<VectorRecord> records);
    [[nodiscard]] bool erase(const std::string& id);
    [[nodiscard]] std::optional<VectorRecord> get(const std::string& id) const;
    [[nodiscard]] std::vector<SearchResult> search(const std::vector<float>& query, std::size_t limit) const;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] std::size_t dimensions() const noexcept;
    [[nodiscard]] DatabaseStats stats() const noexcept;
    void flush() const;

private:
    struct StoredVector {
        std::vector<float> values;
        float norm = 0.0F;
    };

    void load_from_disk();
    void validate_vector(const std::vector<float>& values) const;
    [[nodiscard]] float score(const std::vector<float>& lhs, float lhs_norm, const StoredVector& rhs) const;
    [[nodiscard]] std::vector<VectorRecord> records_in_stable_order() const;
    void mark_dirty();

    DiskVectorStore store_;
    Metric metric_;
    DurabilityMode durability_;
    std::size_t dimensions_ = 0;
    mutable std::shared_mutex mutex_;
    mutable std::size_t pending_writes_ = 0;
    std::unordered_map<std::string, StoredVector> vectors_;
};

} // namespace salamis
