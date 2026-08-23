#include "tooling/incremental/cache_core.hpp"

#include "base/json/json_value.hpp"
#include "base/support/atomic_file.hpp"
#include "base/support/sha256.hpp"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <utility>

namespace ahfl::incremental {

namespace {

namespace fs = std::filesystem;

[[nodiscard]] fs::path canonical_project_path(const fs::path &path) {
    std::error_code error;
    const auto absolute = fs::absolute(path, error);
    const auto candidate = error ? path.lexically_normal() : absolute.lexically_normal();
    const auto canonical = fs::weakly_canonical(candidate, error);
    return (error ? candidate : canonical).lexically_normal();
}

[[nodiscard]] std::string read_file(const fs::path &path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return {};
    }
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

[[nodiscard]] std::string fnv1a64_hex(std::string_view bytes) {
    constexpr char kHex[] = "0123456789abcdef";
    const auto hash = fnv1a64(bytes);
    std::string result(16, '0');
    for (std::size_t index = 0; index < 8; ++index) {
        const auto byte = static_cast<unsigned char>((hash >> (56 - index * 8)) & 0xFFU);
        result[index * 2] = kHex[byte >> 4];
        result[index * 2 + 1] = kHex[byte & 0x0FU];
    }
    return result;
}

[[nodiscard]] std::int64_t to_epoch_seconds(std::chrono::system_clock::time_point time) {
    return std::chrono::duration_cast<std::chrono::seconds>(time.time_since_epoch()).count();
}

[[nodiscard]] std::chrono::system_clock::time_point from_epoch_seconds(std::int64_t seconds) {
    return std::chrono::system_clock::time_point{std::chrono::seconds{seconds}};
}

// Serializes a CacheKey into a JSON object.
[[nodiscard]] std::unique_ptr<json::JsonValue> to_json(const CacheKey &key) {
    auto object = json::JsonValue::make_object();
    object->set("project_root_hash", json::JsonValue::make_string(key.project_root_hash));
    object->set("source_path", json::JsonValue::make_string(key.source_path));
    object->set("content_hash",
                json::JsonValue::make_int(static_cast<std::int64_t>(key.content_hash)));
    object->set("toolchain_fingerprint",
                json::JsonValue::make_string(key.toolchain_fingerprint));
    return object;
}

// Parses a CacheKey from a JSON object. Returns nullopt on any mismatch.
[[nodiscard]] std::optional<CacheKey> cache_key_from_json(const json::JsonValue &value) {
    if (!value.is_object()) {
        return std::nullopt;
    }
    CacheKey key;
    const auto *project_hash = value.get("project_root_hash");
    const auto *source_path = value.get("source_path");
    const auto *content_hash = value.get("content_hash");
    const auto *toolchain = value.get("toolchain_fingerprint");
    if (project_hash == nullptr || source_path == nullptr || content_hash == nullptr ||
        toolchain == nullptr) {
        return std::nullopt;
    }
    const auto project_hash_text = project_hash->as_string();
    const auto source_path_text = source_path->as_string();
    const auto content_hash_value = content_hash->as_int();
    const auto toolchain_text = toolchain->as_string();
    if (!project_hash_text.has_value() || !source_path_text.has_value() ||
        !content_hash_value.has_value() || !toolchain_text.has_value()) {
        return std::nullopt;
    }
    key.project_root_hash = std::string{*project_hash_text};
    key.source_path = std::string{*source_path_text};
    key.content_hash = static_cast<std::uint64_t>(*content_hash_value);
    key.toolchain_fingerprint = std::string{*toolchain_text};
    return key;
}

[[nodiscard]] std::string string_field(const json::JsonValue &object,
                                       std::string_view name,
                                       std::string_view fallback = {}) {
    const auto *field = object.get(name);
    if (field == nullptr) {
        return std::string{fallback};
    }
    const auto text = field->as_string();
    return text ? std::string{*text} : std::string{fallback};
}

[[nodiscard]] std::uint64_t int_field(const json::JsonValue &object,
                                      std::string_view name,
                                      std::uint64_t fallback = 0) {
    const auto *field = object.get(name);
    if (field == nullptr) {
        return fallback;
    }
    const auto value = field->as_int();
    return value ? static_cast<std::uint64_t>(*value) : fallback;
}

} // namespace

std::uint64_t fnv1a64(std::string_view bytes) noexcept {
    constexpr std::uint64_t kOffsetBasis = 14695981039346656037ULL;
    constexpr std::uint64_t kPrime = 1099511628211ULL;
    std::uint64_t hash = kOffsetBasis;
    for (const char byte : bytes) {
        hash ^= static_cast<unsigned char>(byte);
        hash *= kPrime;
    }
    return hash;
}

std::string project_root_hash(const fs::path &project_root) {
    return support::sha256_hex(canonical_project_path(project_root).generic_string());
}

std::string default_toolchain_fingerprint() {
    // The cache schema version is the dominant identity component: any format
    // change bumps it. The C++ standard and a coarse build identity follow so
    // that ABI-affecting compiler changes also invalidate caches.
    std::string fingerprint;
    fingerprint += "ahfl-cache/";
    fingerprint += kCacheSchemaVersion;
    fingerprint += "/cxx";
    fingerprint += std::to_string(__cplusplus);
#if defined(_MSC_VER)
    fingerprint += "/msvc";
    fingerprint += std::to_string(_MSC_VER);
#elif defined(__clang__)
    fingerprint += "/clang";
    fingerprint += std::to_string(__clang_major__);
#elif defined(__GNUC__)
    fingerprint += "/gcc";
    fingerprint += std::to_string(__GNUC__);
#endif
    return fingerprint;
}

PersistentCache::PersistentCache(fs::path cache_dir) : cache_dir_(std::move(cache_dir)) {
    std::error_code error;
    fs::create_directories(cache_dir_, error);
    load_index();
}

std::string PersistentCache::entry_file_name(const std::string &source_path) const {
    return fnv1a64_hex(source_path) + ".json";
}

fs::path PersistentCache::entry_file_path(const std::string &source_path) const {
    return cache_dir_ / entry_file_name(source_path);
}

void PersistentCache::load_index() {
    index_.clear();
    const auto index_path = cache_dir_ / "index.json";
    const auto text = read_file(index_path);
    if (text.empty()) {
        return;
    }
    const auto parsed = json::parse_json(text);
    if (!parsed.has_value() || !parsed->get() || !(*parsed)->is_object()) {
        return;
    }
    const auto &root = *(*parsed);
    if (string_field(root, "schema_version") != kCacheIndexSchemaVersion) {
        return;
    }
    const auto *entries = root.get("entries");
    if (entries == nullptr || !entries->is_object()) {
        return;
    }
    for (const auto &[name, value] : entries->object_fields) {
        if (value == nullptr || !value->is_object()) {
            continue;
        }
        IndexEntry entry;
        entry.file_name = string_field(*value, "file");
        if (entry.file_name.empty()) {
            continue;
        }
        entry.cached_at = from_epoch_seconds(
            static_cast<std::int64_t>(int_field(*value, "cached_at", 0)));
        index_.emplace(name, std::move(entry));
    }
}

void PersistentCache::save_index() const {
    auto entries = json::JsonValue::make_object();
    for (const auto &[source_path, entry] : index_) {
        auto record = json::JsonValue::make_object();
        record->set("file", json::JsonValue::make_string(entry.file_name));
        record->set("cached_at",
                    json::JsonValue::make_int(to_epoch_seconds(entry.cached_at)));
        entries->set(source_path, std::move(record));
    }
    auto root = json::JsonValue::make_object();
    root->set("schema_version", json::JsonValue::make_string(
                                    std::string{kCacheIndexSchemaVersion}));
    root->set("entries", std::move(entries));

    const auto index_path = cache_dir_ / "index.json";
    (void)support::atomic_replace_text(index_path, json::serialize_json(*root));
}

PersistentCacheLookupResult PersistentCache::lookup(const CacheKey &key) const {
    const auto index_it = index_.find(key.source_path);
    if (index_it == index_.end()) {
        return {PersistentCacheHitKind::Miss, std::nullopt};
    }
    const auto text = read_file(cache_dir_ / index_it->second.file_name);
    if (text.empty()) {
        return {PersistentCacheHitKind::Miss, std::nullopt};
    }
    const auto parsed = json::parse_json(text);
    if (!parsed.has_value() || !parsed->get() || !(*parsed)->is_object()) {
        return {PersistentCacheHitKind::Miss, std::nullopt};
    }
    const auto &root = *(*parsed);
    if (string_field(root, "schema_version") != kCacheSchemaVersion) {
        return {PersistentCacheHitKind::Miss, std::nullopt};
    }
    const auto *key_json = root.get("cache_key");
    if (key_json == nullptr) {
        return {PersistentCacheHitKind::Miss, std::nullopt};
    }
    const auto stored_key = cache_key_from_json(*key_json);
    if (!stored_key.has_value() || *stored_key != key) {
        return {PersistentCacheHitKind::Miss, std::nullopt};
    }

    PersistentCacheEntry entry;
    entry.key = *stored_key;
    entry.source_graph_revision = string_field(root, "source_graph_revision");
    entry.resolver_snapshot_version = string_field(root, "resolver_snapshot_version");
    entry.signature_fingerprint = int_field(root, "signature_fingerprint", 0);
    entry.serialized_typed_hir = string_field(root, "serialized_typed_hir");
    entry.cached_at =
        from_epoch_seconds(static_cast<std::int64_t>(int_field(root, "cached_at", 0)));
    return {PersistentCacheHitKind::Hit, std::move(entry)};
}

void PersistentCache::store(const PersistentCacheEntry &entry) {
    auto root = json::JsonValue::make_object();
    root->set("schema_version",
              json::JsonValue::make_string(std::string{kCacheSchemaVersion}));
    root->set("cache_key", to_json(entry.key));
    root->set("source_graph_revision",
              json::JsonValue::make_string(entry.source_graph_revision));
    root->set("resolver_snapshot_version",
              json::JsonValue::make_string(entry.resolver_snapshot_version));
    root->set("signature_fingerprint",
              json::JsonValue::make_int(static_cast<std::int64_t>(
                  entry.signature_fingerprint)));
    root->set("serialized_typed_hir",
              json::JsonValue::make_string(entry.serialized_typed_hir));
    root->set("cached_at",
              json::JsonValue::make_int(to_epoch_seconds(entry.cached_at)));

    const auto file_name = entry_file_name(entry.key.source_path);
    const auto file_path = cache_dir_ / file_name;
    const auto write_result =
        support::atomic_replace_text(file_path, json::serialize_json(*root));
    if (!write_result.has_value()) {
        return;
    }
    index_[entry.key.source_path] =
        IndexEntry{file_name, entry.cached_at};
    save_index();
}

void PersistentCache::invalidate(const std::string &source_path) {
    const auto index_it = index_.find(source_path);
    if (index_it == index_.end()) {
        return;
    }
    const auto file_path = cache_dir_ / index_it->second.file_name;
    std::error_code error;
    fs::remove(file_path, error);
    index_.erase(index_it);
    save_index();
}

void PersistentCache::clear() {
    for (const auto &[source_path, entry] : index_) {
        (void)source_path;
        std::error_code error;
        fs::remove(cache_dir_ / entry.file_name, error);
    }
    index_.clear();
    const auto index_path = cache_dir_ / "index.json";
    std::error_code error;
    fs::remove(index_path, error);
}

std::size_t PersistentCache::entry_count() const {
    return index_.size();
}

const fs::path &PersistentCache::cache_dir() const noexcept {
    return cache_dir_;
}

} // namespace ahfl::incremental
