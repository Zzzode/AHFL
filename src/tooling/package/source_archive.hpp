#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ahfl::package {

struct SourceArchiveFile {
    std::string path;
    std::uint64_t size_bytes{0};
    std::string sha256;
};

struct SourceArchive {
    std::string format_version{"ahfl.source_archive.v1"};
    std::string package_name;
    std::string manifest_sha256;
    std::string archive_sha256;
    std::vector<SourceArchiveFile> files;
    std::string payload;
};

struct SourceArchiveResult {
    std::optional<SourceArchive> archive;
    std::vector<std::string> diagnostics;

    [[nodiscard]] bool has_errors() const {
        return !diagnostics.empty();
    }
};

struct SourceArchiveMaterializationResult {
    std::optional<std::filesystem::path> package_root;
    std::vector<std::string> files;
    std::vector<std::string> diagnostics;

    [[nodiscard]] bool has_errors() const {
        return !diagnostics.empty();
    }
};

[[nodiscard]] SourceArchiveResult build_source_archive(const std::filesystem::path &package_root,
                                                       std::string_view package_name);
[[nodiscard]] std::string serialize_source_archive_manifest(const SourceArchive &archive);
[[nodiscard]] SourceArchiveResult parse_source_archive_manifest(std::string_view input);
[[nodiscard]] SourceArchiveMaterializationResult
materialize_source_archive(const SourceArchive &archive,
                           std::string_view payload,
                           const std::filesystem::path &output_root);

} // namespace ahfl::package
