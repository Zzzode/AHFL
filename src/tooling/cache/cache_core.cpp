#include "tooling/cache/cache_core.hpp"

#include "base/json/json_value.hpp"
#include "base/support/atomic_file.hpp"
#include "base/support/sha256.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <utility>
#include <vector>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(__linux__)
#include <unistd.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

namespace ahfl::cache {

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
    object->set("content_digest", json::JsonValue::make_string(key.content_digest));
    object->set("producer_digest", json::JsonValue::make_string(key.producer_digest));
    object->set("configuration_digest", json::JsonValue::make_string(key.configuration_digest));
    object->set("dependency_digest", json::JsonValue::make_string(key.dependency_digest));
    return object;
}

// Parses a CacheKey from a JSON object. Returns nullopt on any mismatch.
[[nodiscard]] std::optional<CacheKey> cache_key_from_json(const json::JsonValue &value) {
    if (!value.is_object() || value.object_fields.size() != 6) {
        return std::nullopt;
    }
    CacheKey key;
    const auto *project_hash = value.get("project_root_hash");
    const auto *source_path = value.get("source_path");
    const auto *content_digest = value.get("content_digest");
    const auto *toolchain = value.get("producer_digest");
    const auto *configuration = value.get("configuration_digest");
    const auto *dependency = value.get("dependency_digest");
    if (project_hash == nullptr || source_path == nullptr || content_digest == nullptr ||
        toolchain == nullptr || configuration == nullptr || dependency == nullptr) {
        return std::nullopt;
    }
    const auto project_hash_text = project_hash->as_string();
    const auto source_path_text = source_path->as_string();
    const auto content_digest_value = content_digest->as_string();
    const auto toolchain_text = toolchain->as_string();
    const auto configuration_text = configuration->as_string();
    const auto dependency_text = dependency->as_string();
    if (!project_hash_text.has_value() || !source_path_text.has_value() ||
        !content_digest_value.has_value() || !toolchain_text.has_value() ||
        !configuration_text.has_value() || !dependency_text.has_value()) {
        return std::nullopt;
    }
    key.project_root_hash = std::string{*project_hash_text};
    key.source_path = std::string{*source_path_text};
    key.content_digest = std::string{*content_digest_value};
    key.producer_digest = std::string{*toolchain_text};
    key.configuration_digest = std::string{*configuration_text};
    key.dependency_digest = std::string{*dependency_text};
    return key;
}

[[nodiscard]] std::string string_field(const json::JsonValue &object, std::string_view name) {
    const auto *field = object.get(name);
    const auto text = field == nullptr ? std::nullopt : field->as_string();
    return text.has_value() ? std::string{*text} : std::string{};
}

[[nodiscard]] std::uint64_t int_field(const json::JsonValue &object, std::string_view name) {
    return static_cast<std::uint64_t>(*object.get(name)->as_int());
}

} // namespace

std::string project_root_hash(const fs::path &project_root) {
    return support::sha256_hex(canonical_project_path(project_root).generic_string());
}

std::string producer_digest(const fs::path &executable) {
    const auto bytes = read_file(executable);
    return bytes.empty() ? std::string{} : support::sha256_hex(bytes);
}

std::string default_producer_digest() {
    static const std::string digest = [] {
        fs::path executable;
#if defined(__APPLE__)
        std::uint32_t size = 0;
        (void)_NSGetExecutablePath(nullptr, &size);
        std::vector<char> buffer(size);
        if (_NSGetExecutablePath(buffer.data(), &size) == 0) {
            executable = buffer.data();
        }
#elif defined(__linux__)
        std::error_code error;
        executable = fs::read_symlink("/proc/self/exe", error);
#elif defined(_WIN32)
        std::vector<wchar_t> buffer(32768);
        const auto length =
            GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length > 0 && length < buffer.size()) {
            executable = std::wstring(buffer.data(), length);
        }
#endif
        return producer_digest(executable);
    }();
    return digest;
}

PersistentCache::PersistentCache(fs::path cache_dir) : cache_dir_(std::move(cache_dir)) {
    std::error_code error;
    fs::create_directories(cache_dir_, error);
    load_index();
}

std::string PersistentCache::entry_file_name(const std::string &source_path) const {
    return support::sha256_hex(source_path) + ".json";
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
    if (string_field(root, "format") != kCacheIndexFormat) {
        return;
    }
    const auto *entries = root.get("entries");
    if (entries == nullptr || !entries->is_object()) {
        return;
    }
    if (root.object_fields.size() != 2) {
        return;
    }
    for (const auto &[name, value] : entries->object_fields) {
        const auto *file = value == nullptr ? nullptr : value->get("file");
        const auto *cached = value == nullptr ? nullptr : value->get("cached_at");
        const auto *accessed = value == nullptr ? nullptr : value->get("last_accessed");
        if (value == nullptr || !value->is_object() || value->object_fields.size() != 3 ||
            file == nullptr || !file->as_string().has_value() ||
            *file->as_string() != entry_file_name(name) || cached == nullptr ||
            !cached->as_int().has_value() || accessed == nullptr ||
            !accessed->as_int().has_value()) {
            index_.clear();
            return;
        }
        index_.emplace(name,
                       IndexEntry{std::string{*file->as_string()},
                                  from_epoch_seconds(*cached->as_int()),
                                  from_epoch_seconds(*accessed->as_int())});
    }
}

void PersistentCache::save_index() const {
    auto entries = json::JsonValue::make_object();
    for (const auto &[source_path, entry] : index_) {
        auto record = json::JsonValue::make_object();
        record->set("file", json::JsonValue::make_string(entry.file_name));
        record->set("cached_at", json::JsonValue::make_int(to_epoch_seconds(entry.cached_at)));
        record->set("last_accessed",
                    json::JsonValue::make_int(to_epoch_seconds(entry.last_accessed)));
        entries->set(source_path, std::move(record));
    }
    auto root = json::JsonValue::make_object();
    root->set("format", json::JsonValue::make_string(std::string{kCacheIndexFormat}));
    root->set("entries", std::move(entries));

    const auto index_path = cache_dir_ / "index.json";
    (void)support::atomic_replace_text(index_path, json::serialize_json(*root));
}

PersistentCacheLookupResult PersistentCache::lookup(const CacheKey &key) {
    // Lazy TTL eviction: drop stale entries before serving any request so a
    // long-lived cache never grows beyond its useful set.
    evict_expired(kDefaultTtl);

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
    if (string_field(root, "format") != kCacheFormat) {
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

    if (root.object_fields.size() != 7) {
        return {PersistentCacheHitKind::Miss, std::nullopt};
    }
    for (const auto name :
         {"source_graph_digest", "resolver_snapshot_digest", "serialized_typed_hir"}) {
        const auto *field = root.get(name);
        if (field == nullptr || !field->as_string().has_value()) {
            return {PersistentCacheHitKind::Miss, std::nullopt};
        }
    }
    for (const auto name : {"signature_fingerprint", "cached_at"}) {
        const auto *field = root.get(name);
        if (field == nullptr || !field->as_int().has_value()) {
            return {PersistentCacheHitKind::Miss, std::nullopt};
        }
    }
    PersistentCacheEntry entry;
    entry.key = *stored_key;
    entry.source_graph_digest = string_field(root, "source_graph_digest");
    entry.resolver_snapshot_digest = string_field(root, "resolver_snapshot_digest");
    entry.signature_fingerprint = int_field(root, "signature_fingerprint");
    entry.serialized_typed_hir = string_field(root, "serialized_typed_hir");
    entry.cached_at = from_epoch_seconds(static_cast<std::int64_t>(int_field(root, "cached_at")));

    // Refresh the access time and persist it. This is a write on every read,
    // but the index is small and writes are atomic (RFC 0016 "Cache lifecycle").
    index_it->second.last_accessed = std::chrono::system_clock::now();
    save_index();
    return {PersistentCacheHitKind::Hit, std::move(entry)};
}

void PersistentCache::store(const PersistentCacheEntry &entry) {
    evict_expired(kDefaultTtl);

    auto root = json::JsonValue::make_object();
    root->set("format", json::JsonValue::make_string(std::string{kCacheFormat}));
    root->set("cache_key", to_json(entry.key));
    root->set("source_graph_digest", json::JsonValue::make_string(entry.source_graph_digest));
    root->set("resolver_snapshot_digest",
              json::JsonValue::make_string(entry.resolver_snapshot_digest));
    root->set("signature_fingerprint",
              json::JsonValue::make_int(static_cast<std::int64_t>(entry.signature_fingerprint)));
    root->set("serialized_typed_hir", json::JsonValue::make_string(entry.serialized_typed_hir));
    root->set("cached_at", json::JsonValue::make_int(to_epoch_seconds(entry.cached_at)));

    const auto file_name = entry_file_name(entry.key.source_path);
    const auto file_path = cache_dir_ / file_name;
    const auto write_result = support::atomic_replace_text(file_path, json::serialize_json(*root));
    if (!write_result.has_value()) {
        return;
    }
    index_[entry.key.source_path] =
        IndexEntry{file_name, entry.cached_at, std::chrono::system_clock::now()};
    save_index();
    // Eager LRU eviction: keep the cache directory within its size budget.
    evict_lru(kDefaultMaxCacheBytes);
}

bool PersistentCache::remove_entry(const std::string &source_path) {
    const auto index_it = index_.find(source_path);
    if (index_it == index_.end()) {
        return false;
    }
    std::error_code error;
    fs::remove(cache_dir_ / index_it->second.file_name, error);
    index_.erase(index_it);
    return true;
}

void PersistentCache::invalidate(const std::string &source_path) {
    if (remove_entry(source_path)) {
        save_index();
    }
}

void PersistentCache::evict_expired(std::chrono::seconds ttl) {
    const auto now = std::chrono::system_clock::now();
    std::vector<std::string> victims;
    victims.reserve(index_.size());
    for (const auto &[source_path, entry] : index_) {
        if (now - entry.last_accessed > ttl) {
            victims.push_back(source_path);
        }
    }
    if (victims.empty()) {
        return;
    }
    for (const auto &source_path : victims) {
        (void)remove_entry(source_path);
    }
    save_index();
}

void PersistentCache::evict_lru(std::size_t max_bytes) {
    struct SizedEntry {
        std::string source_path;
        std::size_t size;
        std::chrono::system_clock::time_point last_accessed;
    };
    std::vector<SizedEntry> entries;
    entries.reserve(index_.size());
    std::size_t total = 0;
    for (const auto &[source_path, entry] : index_) {
        std::error_code error;
        const auto file_size = fs::file_size(cache_dir_ / entry.file_name, error);
        const auto size = error ? std::size_t{0} : static_cast<std::size_t>(file_size);
        total += size;
        entries.push_back({source_path, size, entry.last_accessed});
    }
    if (total <= max_bytes) {
        return;
    }
    // Oldest access time first; source path breaks ties so eviction is
    // deterministic regardless of hash-map iteration order.
    std::sort(entries.begin(), entries.end(), [](const SizedEntry &lhs, const SizedEntry &rhs) {
        if (lhs.last_accessed != rhs.last_accessed) {
            return lhs.last_accessed < rhs.last_accessed;
        }
        return lhs.source_path < rhs.source_path;
    });
    bool changed = false;
    for (const auto &entry : entries) {
        if (total <= max_bytes) {
            break;
        }
        if (remove_entry(entry.source_path)) {
            total -= entry.size;
            changed = true;
        }
    }
    if (changed) {
        save_index();
    }
}

void PersistentCache::set_last_accessed_for_test(const std::string &source_path,
                                                 std::chrono::system_clock::time_point when) {
    const auto index_it = index_.find(source_path);
    if (index_it == index_.end()) {
        return;
    }
    index_it->second.last_accessed = when;
    save_index();
}

std::optional<std::chrono::system_clock::time_point>
PersistentCache::last_accessed_for_test(const std::string &source_path) const {
    const auto index_it = index_.find(source_path);
    if (index_it == index_.end()) {
        return std::nullopt;
    }
    return index_it->second.last_accessed;
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

} // namespace ahfl::cache
