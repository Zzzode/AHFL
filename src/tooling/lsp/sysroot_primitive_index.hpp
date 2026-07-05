#pragma once

#include <filesystem>
#include <optional>
#include <string_view>
#include <unordered_map>

#include "tooling/lsp/protocol_types.hpp"
#include "tooling/lsp/workspace_index.hpp"

namespace ahfl::lsp {

inline constexpr std::string_view kSysrootPrimitiveHomeSchemaVersion = "sysroot-primitive-home-v1";

struct SysrootPrimitiveIndexInput {
    std::filesystem::path std_manifest;
};

class SysrootPrimitiveIndex {
  public:
    [[nodiscard]] std::optional<Location> home_location_for_type(const TypeKey &type) const;
    [[nodiscard]] std::optional<Location> home_location_for_primitive(PrimitiveKind kind) const;
    [[nodiscard]] std::optional<std::filesystem::path>
    missing_home_path_for_primitive(PrimitiveKind kind) const;

    void add_home(PrimitiveKind kind, Location location);
    void add_missing_home(PrimitiveKind kind, std::filesystem::path expected_path);

  private:
    std::unordered_map<PrimitiveKind, Location> homes_;
    std::unordered_map<PrimitiveKind, std::filesystem::path> missing_homes_;
};

[[nodiscard]] SysrootPrimitiveIndex
build_sysroot_primitive_index(const SysrootPrimitiveIndexInput &input);
[[nodiscard]] std::string_view primitive_kind_name(PrimitiveKind kind) noexcept;

} // namespace ahfl::lsp
