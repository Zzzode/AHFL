#include "tooling/package/source_archive.hpp"

#include "base/json/json_value.hpp"
#include "base/support/sha256.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace ahfl::package {
namespace {

[[nodiscard]] std::filesystem::path normalize_path(const std::filesystem::path &path) {
    std::error_code error;
    const auto absolute = std::filesystem::absolute(path, error);
    const auto candidate = error ? path.lexically_normal() : absolute.lexically_normal();
    const auto canonical = std::filesystem::weakly_canonical(candidate, error);
    return (error ? candidate : canonical).lexically_normal();
}

[[nodiscard]] bool is_hidden_or_build_dir(std::string_view name) {
    return name == ".git" || name == ".hg" || name == ".svn" || name == ".ahfl" ||
           name == "build" || name == "dist" || name == "target" || name == ".cache" ||
           name.starts_with("cmake-build-");
}

[[nodiscard]] bool has_key(std::initializer_list<std::string_view> keys,
                           std::string_view candidate) {
    return std::find(keys.begin(), keys.end(), candidate) != keys.end();
}

void reject_unknown_fields(const ahfl::json::JsonValue &object,
                           std::initializer_list<std::string_view> allowed,
                           std::string_view context,
                           std::vector<std::string> &diagnostics) {
    for (const auto &[key, _] : object.object_fields) {
        if (!has_key(allowed, key)) {
            diagnostics.push_back("unsupported source archive manifest field '" +
                                  std::string{context} + key + "'");
        }
    }
}

[[nodiscard]] bool is_lower_hex(char value) {
    return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f');
}

[[nodiscard]] bool is_sha256_digest(std::string_view value) {
    constexpr std::string_view prefix = "sha256:";
    if (!value.starts_with(prefix) || value.size() != prefix.size() + 64) {
        return false;
    }
    return std::all_of(
        value.begin() + static_cast<std::ptrdiff_t>(prefix.size()), value.end(), is_lower_hex);
}

[[nodiscard]] bool is_valid_package_name(std::string_view value) {
    if (value.empty() || value.front() < 'a' || value.front() > 'z') {
        return false;
    }
    bool previous_dash = false;
    for (const char item : value) {
        const bool lower = item >= 'a' && item <= 'z';
        const bool digit = item >= '0' && item <= '9';
        if (lower || digit) {
            previous_dash = false;
            continue;
        }
        if (item == '-' && !previous_dash) {
            previous_dash = true;
            continue;
        }
        return false;
    }
    return value.back() != '-';
}

[[nodiscard]] bool is_source_archive_file(const std::filesystem::path &path) {
    const auto filename = path.filename().generic_string();
    if (filename == "ahfl.lock" || filename == ".DS_Store") {
        return false;
    }
    const auto extension = path.extension().generic_string();
    return extension == ".ahfl" || extension == ".toml";
}

[[nodiscard]] bool relative_path_stays_inside(const std::filesystem::path &path) {
    if (path.empty() || path.is_absolute()) {
        return false;
    }
    for (const auto &part : path.lexically_normal()) {
        if (part == "..") {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::optional<std::string> read_file(const std::filesystem::path &path,
                                                   std::vector<std::string> &diagnostics) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        diagnostics.push_back("failed to read source archive file '" + path.generic_string() + "'");
        return std::nullopt;
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    if (!input.good() && !input.eof()) {
        diagnostics.push_back("failed to finish reading source archive file '" +
                              path.generic_string() + "'");
        return std::nullopt;
    }
    return buffer.str();
}

[[nodiscard]] bool directory_is_empty(const std::filesystem::path &path,
                                      std::vector<std::string> &diagnostics) {
    std::error_code error;
    if (!std::filesystem::exists(path, error)) {
        return true;
    }
    if (error) {
        diagnostics.push_back("failed to inspect source archive materialization root: " +
                              path.generic_string());
        return false;
    }
    if (!std::filesystem::is_directory(path, error) || error) {
        diagnostics.push_back("source archive materialization root must be a directory: " +
                              path.generic_string());
        return false;
    }
    const auto iterator = std::filesystem::directory_iterator(path, error);
    if (error) {
        diagnostics.push_back("failed to inspect source archive materialization root: " +
                              path.generic_string());
        return false;
    }
    return iterator == std::filesystem::directory_iterator{};
}

[[nodiscard]] std::string normalize_newlines(std::string_view input) {
    std::string output;
    output.reserve(input.size());
    for (std::size_t index = 0; index < input.size(); ++index) {
        if (input[index] == '\r') {
            output.push_back('\n');
            if (index + 1 < input.size() && input[index + 1] == '\n') {
                ++index;
            }
            continue;
        }
        output.push_back(input[index]);
    }
    return output;
}

[[nodiscard]] std::optional<std::string_view> read_payload_token(std::string_view payload,
                                                                 std::size_t &cursor) {
    const auto end = payload.find('\0', cursor);
    if (end == std::string_view::npos) {
        return std::nullopt;
    }
    const auto token = payload.substr(cursor, end - cursor);
    cursor = end + 1;
    return token;
}

[[nodiscard]] std::optional<std::uint64_t> parse_payload_size(std::string_view token) {
    if (token.empty()) {
        return std::nullopt;
    }
    std::uint64_t value = 0;
    for (const char item : token) {
        if (item < '0' || item > '9') {
            return std::nullopt;
        }
        const auto digit = static_cast<std::uint64_t>(item - '0');
        if (value > (std::numeric_limits<std::uint64_t>::max() - digit) / 10U) {
            return std::nullopt;
        }
        value = (value * 10U) + digit;
    }
    return value;
}

struct PayloadFile {
    std::string path;
    std::string content;
};

[[nodiscard]] std::vector<PayloadFile>
parse_source_archive_payload(std::string_view payload,
                             std::string_view expected_package,
                             std::vector<std::string> &diagnostics) {
    std::vector<PayloadFile> files;
    std::size_t cursor = 0;
    const auto format = read_payload_token(payload, cursor);
    if (!format.has_value() || *format != "ahfl.source_archive.v1") {
        diagnostics.push_back(
            "source archive payload format_version must be 'ahfl.source_archive.v1'");
        return files;
    }
    const auto package = read_payload_token(payload, cursor);
    if (!package.has_value() || *package != expected_package) {
        diagnostics.push_back("source archive payload package does not match manifest package");
        return files;
    }

    while (cursor < payload.size()) {
        const auto path = read_payload_token(payload, cursor);
        if (!path.has_value() || path->empty()) {
            diagnostics.push_back("source archive payload contains an invalid file path token");
            return files;
        }
        const auto size_token = read_payload_token(payload, cursor);
        if (!size_token.has_value()) {
            diagnostics.push_back("source archive payload is missing file size for " +
                                  std::string{*path});
            return files;
        }
        const auto size = parse_payload_size(*size_token);
        if (!size.has_value()) {
            diagnostics.push_back("source archive payload file size is invalid for " +
                                  std::string{*path});
            return files;
        }
        if (*size > static_cast<std::uint64_t>(payload.size() - cursor)) {
            diagnostics.push_back("source archive payload file content is truncated for " +
                                  std::string{*path});
            return files;
        }
        const auto content = payload.substr(cursor, static_cast<std::size_t>(*size));
        cursor += static_cast<std::size_t>(*size);
        if (cursor >= payload.size() || payload[cursor] != '\0') {
            diagnostics.push_back("source archive payload file content is not NUL-terminated for " +
                                  std::string{*path});
            return files;
        }
        ++cursor;
        files.push_back(PayloadFile{.path = std::string{*path}, .content = std::string{content}});
    }
    return files;
}

[[nodiscard]] bool collect_source_files(const std::filesystem::path &root,
                                        std::vector<std::filesystem::path> &files,
                                        std::vector<std::string> &diagnostics) {
    std::error_code error;
    if (!std::filesystem::exists(root, error) || error) {
        diagnostics.push_back("source archive package root does not exist: " +
                              root.generic_string());
        return false;
    }
    if (!std::filesystem::is_directory(root, error) || error) {
        diagnostics.push_back("source archive package root must be a directory: " +
                              root.generic_string());
        return false;
    }

    try {
        std::filesystem::recursive_directory_iterator iterator(
            root, std::filesystem::directory_options::skip_permission_denied);
        const std::filesystem::recursive_directory_iterator end;
        for (; iterator != end; ++iterator) {
            const auto filename = iterator->path().filename().generic_string();
            error.clear();
            if (iterator->is_symlink(error) && !error) {
                diagnostics.push_back("source archive does not allow symlinks: " +
                                      iterator->path().generic_string());
                iterator.disable_recursion_pending();
                continue;
            }
            error.clear();
            if (iterator->is_directory(error) && !error) {
                if (is_hidden_or_build_dir(filename)) {
                    iterator.disable_recursion_pending();
                }
                continue;
            }
            error.clear();
            if (!iterator->is_regular_file(error) || error) {
                error.clear();
                continue;
            }
            if (is_source_archive_file(iterator->path())) {
                files.push_back(normalize_path(iterator->path()));
            }
        }
    } catch (const std::filesystem::filesystem_error &ex) {
        diagnostics.push_back("failed to scan source archive package root '" +
                              root.generic_string() + "': " + ex.what());
        return false;
    }

    std::sort(files.begin(), files.end(), [](const auto &lhs, const auto &rhs) {
        return lhs.generic_string() < rhs.generic_string();
    });
    return true;
}

void validate_digest_field(std::string_view value,
                           std::string_view field,
                           std::vector<std::string> &diagnostics) {
    if (!is_sha256_digest(value)) {
        diagnostics.push_back("source archive manifest field '" + std::string{field} +
                              "' must be sha256:<64 lowercase hex digits>");
    }
}

[[nodiscard]] const ahfl::json::JsonValue *required_field(const ahfl::json::JsonValue &object,
                                                          std::string_view key,
                                                          std::vector<std::string> &diagnostics) {
    const auto *field = object.get(key);
    if (field == nullptr) {
        diagnostics.push_back("source archive manifest is missing required field '" +
                              std::string{key} + "'");
    }
    return field;
}

[[nodiscard]] std::optional<std::string> read_string(const ahfl::json::JsonValue &object,
                                                     std::string_view key,
                                                     std::vector<std::string> &diagnostics) {
    const auto *field = required_field(object, key, diagnostics);
    if (field == nullptr) {
        return std::nullopt;
    }
    auto value = field->as_string();
    if (!value.has_value() || value->empty()) {
        diagnostics.push_back("source archive manifest field '" + std::string{key} +
                              "' must be a non-empty string");
        return std::nullopt;
    }
    return std::string{*value};
}

[[nodiscard]] std::optional<std::uint64_t> read_u64(const ahfl::json::JsonValue &object,
                                                    std::string_view key,
                                                    std::vector<std::string> &diagnostics) {
    const auto *field = required_field(object, key, diagnostics);
    if (field == nullptr) {
        return std::nullopt;
    }
    auto value = field->as_int();
    if (!value.has_value() || *value < 0) {
        diagnostics.push_back("source archive manifest field '" + std::string{key} +
                              "' must be a non-negative integer");
        return std::nullopt;
    }
    return static_cast<std::uint64_t>(*value);
}

} // namespace

SourceArchiveResult build_source_archive(const std::filesystem::path &package_root,
                                         std::string_view package_name) {
    SourceArchiveResult result;
    if (!is_valid_package_name(package_name)) {
        result.diagnostics.push_back("source archive package name must be kebab-case");
        return result;
    }
    const auto root = normalize_path(package_root);
    std::vector<std::filesystem::path> files;
    if (!collect_source_files(root, files, result.diagnostics) || result.has_errors()) {
        return result;
    }

    SourceArchive archive;
    archive.package_name = std::string{package_name};
    std::string payload = "ahfl.source_archive.v1";
    payload.push_back('\0');
    payload.append(archive.package_name);
    payload.push_back('\0');

    for (const auto &file : files) {
        std::error_code error;
        auto relative = std::filesystem::relative(file, root, error);
        if (error || !relative_path_stays_inside(relative)) {
            relative = file.lexically_relative(root);
        }
        if (!relative_path_stays_inside(relative)) {
            result.diagnostics.push_back("source archive file escapes package root: " +
                                         file.generic_string());
            continue;
        }
        auto content = read_file(file, result.diagnostics);
        if (!content.has_value()) {
            continue;
        }
        auto normalized = normalize_newlines(*content);
        const auto relative_path = relative.lexically_normal().generic_string();
        const auto digest = "sha256:" + ahfl::support::sha256_hex(normalized);
        if (relative_path == "ahfl.toml") {
            archive.manifest_sha256 = digest;
        }
        archive.files.push_back(SourceArchiveFile{
            .path = relative_path,
            .size_bytes = static_cast<std::uint64_t>(normalized.size()),
            .sha256 = digest,
        });
        payload.append(relative_path);
        payload.push_back('\0');
        payload.append(std::to_string(normalized.size()));
        payload.push_back('\0');
        payload.append(normalized);
        payload.push_back('\0');
    }

    if (archive.manifest_sha256.empty()) {
        result.diagnostics.push_back("source archive requires ahfl.toml at package root");
    }
    if (archive.files.empty()) {
        result.diagnostics.push_back("source archive requires at least one source file");
    }
    if (result.has_errors()) {
        return result;
    }

    archive.payload = std::move(payload);
    archive.archive_sha256 = "sha256:" + ahfl::support::sha256_hex(archive.payload);
    result.archive = std::move(archive);
    return result;
}

std::string serialize_source_archive_manifest(const SourceArchive &archive) {
    auto root = ahfl::json::JsonValue::make_object();
    root->set("format_version", ahfl::json::JsonValue::make_string(archive.format_version));
    root->set("package", ahfl::json::JsonValue::make_string(archive.package_name));
    root->set("manifest_sha256", ahfl::json::JsonValue::make_string(archive.manifest_sha256));
    root->set("archive_sha256", ahfl::json::JsonValue::make_string(archive.archive_sha256));
    auto files = ahfl::json::JsonValue::make_array();
    for (const auto &file : archive.files) {
        auto item = ahfl::json::JsonValue::make_object();
        item->set("path", ahfl::json::JsonValue::make_string(file.path));
        item->set("size_bytes",
                  ahfl::json::JsonValue::make_int(static_cast<std::int64_t>(file.size_bytes)));
        item->set("sha256", ahfl::json::JsonValue::make_string(file.sha256));
        files->push(std::move(item));
    }
    root->set("files", std::move(files));
    return ahfl::json::serialize_json(*root);
}

SourceArchiveMaterializationResult
materialize_source_archive(const SourceArchive &archive,
                           std::string_view payload,
                           const std::filesystem::path &output_root) {
    SourceArchiveMaterializationResult result;

    auto parsed_manifest =
        parse_source_archive_manifest(serialize_source_archive_manifest(archive));
    if (parsed_manifest.has_errors()) {
        result.diagnostics.insert(result.diagnostics.end(),
                                  parsed_manifest.diagnostics.begin(),
                                  parsed_manifest.diagnostics.end());
        return result;
    }

    const auto payload_digest = "sha256:" + ahfl::support::sha256_hex(payload);
    if (payload_digest != archive.archive_sha256) {
        result.diagnostics.push_back("source archive payload digest does not match archive_sha256");
        return result;
    }

    const auto normalized_root = normalize_path(output_root);
    if (!directory_is_empty(normalized_root, result.diagnostics)) {
        if (!result.has_errors()) {
            result.diagnostics.push_back("source archive materialization root must be empty: " +
                                         normalized_root.generic_string());
        }
        return result;
    }

    std::map<std::string, SourceArchiveFile> manifest_files;
    for (const auto &file : archive.files) {
        if (!relative_path_stays_inside(std::filesystem::path{file.path})) {
            result.diagnostics.push_back(
                "source archive manifest file path must stay inside package root: " + file.path);
            continue;
        }
        if (!manifest_files.emplace(file.path, file).second) {
            result.diagnostics.push_back("source archive manifest contains duplicate file path: " +
                                         file.path);
        }
    }
    if (manifest_files.find("ahfl.toml") == manifest_files.end()) {
        result.diagnostics.push_back("source archive manifest must list root ahfl.toml");
    }
    if (result.has_errors()) {
        return result;
    }

    auto payload_files =
        parse_source_archive_payload(payload, archive.package_name, result.diagnostics);
    if (result.has_errors()) {
        return result;
    }

    std::map<std::string, PayloadFile> payload_by_path;
    for (auto &file : payload_files) {
        if (!relative_path_stays_inside(std::filesystem::path{file.path})) {
            result.diagnostics.push_back(
                "source archive payload file path must stay inside package root: " + file.path);
            continue;
        }
        if (!payload_by_path.emplace(file.path, std::move(file)).second) {
            result.diagnostics.push_back("source archive payload contains duplicate file path: " +
                                         file.path);
        }
    }
    if (result.has_errors()) {
        return result;
    }

    for (const auto &[path, manifest_file] : manifest_files) {
        const auto payload_it = payload_by_path.find(path);
        if (payload_it == payload_by_path.end()) {
            result.diagnostics.push_back("source archive payload is missing file: " + path);
            continue;
        }
        const auto &content = payload_it->second.content;
        if (manifest_file.size_bytes != content.size()) {
            result.diagnostics.push_back("source archive payload size mismatch for " + path);
        }
        const auto digest = "sha256:" + ahfl::support::sha256_hex(content);
        if (manifest_file.sha256 != digest) {
            result.diagnostics.push_back("source archive payload digest mismatch for " + path);
        }
        if (path == "ahfl.toml" && archive.manifest_sha256 != digest) {
            result.diagnostics.push_back(
                "source archive payload ahfl.toml digest does not match manifest_sha256");
        }
    }
    for (const auto &[path, _] : payload_by_path) {
        if (manifest_files.find(path) == manifest_files.end()) {
            result.diagnostics.push_back(
                "source archive payload contains file not listed in manifest: " + path);
        }
    }
    if (result.has_errors()) {
        return result;
    }

    std::error_code error;
    std::filesystem::create_directories(normalized_root, error);
    if (error) {
        result.diagnostics.push_back("failed to create source archive materialization root: " +
                                     normalized_root.generic_string());
        return result;
    }

    for (const auto &[path, payload_file] : payload_by_path) {
        const auto output_path = (normalized_root / std::filesystem::path{path}).lexically_normal();
        std::filesystem::create_directories(output_path.parent_path(), error);
        if (error) {
            result.diagnostics.push_back("failed to create directory for materialized file: " +
                                         output_path.parent_path().generic_string());
            return result;
        }
        std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
        if (!output) {
            result.diagnostics.push_back("failed to write materialized source archive file: " +
                                         output_path.generic_string());
            return result;
        }
        output << payload_file.content;
        if (!output) {
            result.diagnostics.push_back(
                "failed to finish writing materialized source archive file: " +
                output_path.generic_string());
            return result;
        }
        result.files.push_back(path);
    }

    std::sort(result.files.begin(), result.files.end());
    result.package_root = normalized_root;
    return result;
}

SourceArchiveResult parse_source_archive_manifest(std::string_view input) {
    SourceArchiveResult result;
    auto parsed = ahfl::json::parse_json(input);
    if (!parsed.has_value() || !*parsed || !(*parsed)->is_object()) {
        result.diagnostics.push_back("source archive manifest must be a JSON object");
        return result;
    }
    const auto &root = **parsed;
    reject_unknown_fields(
        root,
        {"format_version", "package", "manifest_sha256", "archive_sha256", "files"},
        "",
        result.diagnostics);
    SourceArchive archive;
    if (auto value = read_string(root, "format_version", result.diagnostics); value.has_value()) {
        archive.format_version = std::move(*value);
        if (archive.format_version != "ahfl.source_archive.v1") {
            result.diagnostics.push_back(
                "source archive manifest format_version must be 'ahfl.source_archive.v1'");
        }
    }
    if (auto value = read_string(root, "package", result.diagnostics); value.has_value()) {
        archive.package_name = std::move(*value);
        if (!is_valid_package_name(archive.package_name)) {
            result.diagnostics.push_back(
                "source archive manifest field 'package' must be kebab-case");
        }
    }
    if (auto value = read_string(root, "manifest_sha256", result.diagnostics); value.has_value()) {
        archive.manifest_sha256 = std::move(*value);
        validate_digest_field(archive.manifest_sha256, "manifest_sha256", result.diagnostics);
    }
    if (auto value = read_string(root, "archive_sha256", result.diagnostics); value.has_value()) {
        archive.archive_sha256 = std::move(*value);
        validate_digest_field(archive.archive_sha256, "archive_sha256", result.diagnostics);
    }
    const auto *files = required_field(root, "files", result.diagnostics);
    if (files != nullptr) {
        if (!files->is_array()) {
            result.diagnostics.push_back("source archive manifest field 'files' must be an array");
        } else {
            for (const auto &item : files->array_items) {
                if (!item->is_object()) {
                    result.diagnostics.push_back(
                        "source archive manifest files items must be objects");
                    continue;
                }
                const auto context = "files[" + std::to_string(archive.files.size()) + "].";
                reject_unknown_fields(
                    *item, {"path", "size_bytes", "sha256"}, context, result.diagnostics);
                SourceArchiveFile file;
                if (auto value = read_string(*item, "path", result.diagnostics);
                    value.has_value()) {
                    file.path = std::move(*value);
                    if (!relative_path_stays_inside(std::filesystem::path{file.path})) {
                        result.diagnostics.push_back(
                            "source archive manifest file path must be relative and stay inside "
                            "the package root: " +
                            file.path);
                    }
                    if (std::find_if(archive.files.begin(),
                                     archive.files.end(),
                                     [&](const SourceArchiveFile &existing) {
                                         return existing.path == file.path;
                                     }) != archive.files.end()) {
                        result.diagnostics.push_back(
                            "source archive manifest contains duplicate file path: " + file.path);
                    }
                }
                if (auto value = read_u64(*item, "size_bytes", result.diagnostics);
                    value.has_value()) {
                    file.size_bytes = *value;
                }
                if (auto value = read_string(*item, "sha256", result.diagnostics);
                    value.has_value()) {
                    file.sha256 = std::move(*value);
                    validate_digest_field(file.sha256, "files[].sha256", result.diagnostics);
                }
                archive.files.push_back(std::move(file));
            }
        }
    }

    if (archive.files.empty()) {
        result.diagnostics.push_back("source archive manifest must list at least one file");
    }
    const auto manifest_file =
        std::find_if(archive.files.begin(),
                     archive.files.end(),
                     [&](const SourceArchiveFile &file) { return file.path == "ahfl.toml"; });
    if (manifest_file == archive.files.end()) {
        result.diagnostics.push_back("source archive manifest must list root ahfl.toml");
    } else if (!archive.manifest_sha256.empty() &&
               archive.manifest_sha256 != manifest_file->sha256) {
        result.diagnostics.push_back(
            "source archive manifest_sha256 must match the ahfl.toml file digest");
    }

    if (!result.has_errors()) {
        result.archive = std::move(archive);
    }
    return result;
}

} // namespace ahfl::package
