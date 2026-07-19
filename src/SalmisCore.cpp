// SPDX-License-Identifier: GPL-3.0-only

#include "salamis/SalmisCore.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>

namespace salamis {
namespace {

constexpr std::uint32_t kMagic = 0x53414C56; // SALV
constexpr std::uint32_t kVersion = 1;

void replace_file(const std::filesystem::path& source, const std::filesystem::path& destination) {
    std::error_code error;
    std::filesystem::remove(destination, error);
    if (error) {
        std::filesystem::remove(source);
        throw VectorDatabaseError("failed to replace existing vector store: " + destination.string());
    }

    std::filesystem::rename(source, destination, error);
    if (error) {
        std::filesystem::remove(source);
        throw VectorDatabaseError("failed to move vector store into place: " + destination.string());
    }
}

template <typename T>
void write_pod(std::ofstream& stream, T value) {
    stream.write(reinterpret_cast<const char*>(&value), sizeof(T));
    if (!stream) {
        throw VectorDatabaseError("failed to write vector store");
    }
}

template <typename T>
T read_pod(std::ifstream& stream) {
    T value{};
    stream.read(reinterpret_cast<char*>(&value), sizeof(T));
    if (!stream) {
        throw VectorDatabaseError("corrupt or truncated vector store");
    }
    return value;
}

float dot_product(const std::vector<float>& lhs, const std::vector<float>& rhs) {
    float total = 0.0F;
    for (std::size_t i = 0; i < lhs.size(); ++i) {
        total += lhs[i] * rhs[i];
    }
    return total;
}

float magnitude(const std::vector<float>& values) {
    return std::sqrt(dot_product(values, values));
}

} // namespace

VectorDatabaseError::VectorDatabaseError(const std::string& message)
    : std::runtime_error(message) {}

DiskVectorStore::DiskVectorStore(std::filesystem::path path)
    : path_(std::move(path)) {}

std::vector<VectorRecord> DiskVectorStore::load() const {
    if (!std::filesystem::exists(path_)) {
        return {};
    }

    std::ifstream stream(path_, std::ios::binary);
    if (!stream) {
        throw VectorDatabaseError("failed to open vector store for reading: " + path_.string());
    }

    const auto magic = read_pod<std::uint32_t>(stream);
    const auto version = read_pod<std::uint32_t>(stream);
    if (magic != kMagic || version != kVersion) {
        throw VectorDatabaseError("unsupported vector store format: " + path_.string());
    }

    const auto count = read_pod<std::uint64_t>(stream);
    const auto dimensions = read_pod<std::uint64_t>(stream);
    std::vector<VectorRecord> records;
    records.reserve(static_cast<std::size_t>(count));

    for (std::uint64_t record_index = 0; record_index < count; ++record_index) {
        const auto id_size = read_pod<std::uint64_t>(stream);
        std::string id(static_cast<std::size_t>(id_size), '\0');
        stream.read(id.data(), static_cast<std::streamsize>(id_size));
        if (!stream) {
            throw VectorDatabaseError("corrupt vector id in store");
        }

        std::vector<float> values(static_cast<std::size_t>(dimensions));
        stream.read(reinterpret_cast<char*>(values.data()),
                    static_cast<std::streamsize>(values.size() * sizeof(float)));
        if (!stream) {
            throw VectorDatabaseError("corrupt vector values in store");
        }

        records.push_back(VectorRecord{std::move(id), std::move(values)});
    }

    return records;
}

void DiskVectorStore::save(const std::vector<VectorRecord>& records) const {
    const auto parent = path_.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }

    auto temp_path = path_;
    temp_path += ".tmp";
    std::ofstream stream(temp_path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        throw VectorDatabaseError("failed to open vector store for writing: " + temp_path.string());
    }

    const auto dimensions = records.empty() ? 0 : records.front().values.size();
    write_pod<std::uint32_t>(stream, kMagic);
    write_pod<std::uint32_t>(stream, kVersion);
    write_pod<std::uint64_t>(stream, static_cast<std::uint64_t>(records.size()));
    write_pod<std::uint64_t>(stream, static_cast<std::uint64_t>(dimensions));

    for (const auto& record : records) {
        write_pod<std::uint64_t>(stream, static_cast<std::uint64_t>(record.id.size()));
        stream.write(record.id.data(), static_cast<std::streamsize>(record.id.size()));
        stream.write(reinterpret_cast<const char*>(record.values.data()),
                     static_cast<std::streamsize>(record.values.size() * sizeof(float)));
        if (!stream) {
            throw VectorDatabaseError("failed to write vector record");
        }
    }

    stream.close();
    if (!stream) {
        throw VectorDatabaseError("failed to finalize vector store");
    }

    replace_file(temp_path, path_);
}

const std::filesystem::path& DiskVectorStore::path() const noexcept {
    return path_;
}

VectorDatabase::VectorDatabase(std::filesystem::path storage_path, Metric metric)
    : store_(std::move(storage_path)), metric_(metric) {
    load_from_disk();
}

void VectorDatabase::upsert(std::string id, std::vector<float> values) {
    if (id.empty()) {
        throw VectorDatabaseError("vector id cannot be empty");
    }
    validate_vector(values);
    if (dimensions_ == 0) {
        dimensions_ = values.size();
    }
    vectors_[std::move(id)] = std::move(values);
    flush();
}

bool VectorDatabase::erase(const std::string& id) {
    const auto erased = vectors_.erase(id) > 0;
    if (erased) {
        if (vectors_.empty()) {
            dimensions_ = 0;
        }
        flush();
    }
    return erased;
}

std::optional<VectorRecord> VectorDatabase::get(const std::string& id) const {
    const auto found = vectors_.find(id);
    if (found == vectors_.end()) {
        return std::nullopt;
    }
    return VectorRecord{found->first, found->second};
}

std::vector<SearchResult> VectorDatabase::search(const std::vector<float>& query, std::size_t limit) const {
    if (limit == 0 || vectors_.empty()) {
        return {};
    }
    validate_vector(query);

    std::vector<SearchResult> results;
    results.reserve(vectors_.size());
    for (const auto& [id, values] : vectors_) {
        results.push_back(SearchResult{id, score(query, values)});
    }

    const auto result_count = std::min(limit, results.size());
    std::partial_sort(
        results.begin(),
        results.begin() + static_cast<std::ptrdiff_t>(result_count),
        results.end(),
        [](const SearchResult& lhs, const SearchResult& rhs) {
            if (lhs.score == rhs.score) {
                return lhs.id < rhs.id;
            }
            return lhs.score > rhs.score;
        });
    results.resize(result_count);
    return results;
}

std::size_t VectorDatabase::size() const noexcept {
    return vectors_.size();
}

std::size_t VectorDatabase::dimensions() const noexcept {
    return dimensions_;
}

void VectorDatabase::flush() const {
    store_.save(records_in_stable_order());
}

void VectorDatabase::load_from_disk() {
    for (auto record : store_.load()) {
        if (record.id.empty()) {
            throw VectorDatabaseError("stored vector id cannot be empty");
        }
        validate_vector(record.values);
        if (dimensions_ == 0) {
            dimensions_ = record.values.size();
        }
        vectors_[std::move(record.id)] = std::move(record.values);
    }
}

void VectorDatabase::validate_vector(const std::vector<float>& values) const {
    if (values.empty()) {
        throw VectorDatabaseError("vector cannot be empty");
    }
    if (dimensions_ != 0 && values.size() != dimensions_) {
        throw VectorDatabaseError("vector dimensions do not match database dimensions");
    }
    for (const auto value : values) {
        if (!std::isfinite(value)) {
            throw VectorDatabaseError("vector values must be finite");
        }
    }
}

float VectorDatabase::score(const std::vector<float>& lhs, const std::vector<float>& rhs) const {
    switch (metric_) {
    case Metric::Cosine: {
        const auto denominator = magnitude(lhs) * magnitude(rhs);
        if (denominator == 0.0F) {
            return -std::numeric_limits<float>::infinity();
        }
        return dot_product(lhs, rhs) / denominator;
    }
    case Metric::L2: {
        float distance = 0.0F;
        for (std::size_t i = 0; i < lhs.size(); ++i) {
            const auto delta = lhs[i] - rhs[i];
            distance += delta * delta;
        }
        return -std::sqrt(distance);
    }
    case Metric::Dot:
        return dot_product(lhs, rhs);
    }
    return -std::numeric_limits<float>::infinity();
}

std::vector<VectorRecord> VectorDatabase::records_in_stable_order() const {
    std::vector<VectorRecord> records;
    records.reserve(vectors_.size());
    for (const auto& [id, values] : vectors_) {
        records.push_back(VectorRecord{id, values});
    }
    std::sort(records.begin(), records.end(), [](const VectorRecord& lhs, const VectorRecord& rhs) {
        return lhs.id < rhs.id;
    });
    return records;
}

} // namespace salamis
