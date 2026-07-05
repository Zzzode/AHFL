#include "tooling/lsp/sysroot_primitive_index.hpp"

#include <array>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
#include <system_error>

namespace ahfl::lsp {

namespace {

[[nodiscard]] bool is_unreserved_uri_char(unsigned char ch) noexcept {
    return std::isalnum(ch) != 0 || ch == '-' || ch == '_' || ch == '.' || ch == '~' || ch == '/';
}

[[nodiscard]] std::string percent_encode_path(std::string_view path) {
    constexpr char kHex[] = "0123456789ABCDEF";
    std::string encoded;
    encoded.reserve(path.size());
    for (const unsigned char ch : path) {
        if (is_unreserved_uri_char(ch)) {
            encoded.push_back(static_cast<char>(ch));
            continue;
        }
        encoded.push_back('%');
        encoded.push_back(kHex[(ch >> 4) & 0xF]);
        encoded.push_back(kHex[ch & 0xF]);
    }
    return encoded;
}

[[nodiscard]] std::string uri_from_path(const std::filesystem::path &path) {
    return "file://" + percent_encode_path(path.generic_string());
}

[[nodiscard]] std::filesystem::path normalize_path(const std::filesystem::path &path) {
    std::error_code error;
    const auto absolute = std::filesystem::absolute(path, error);
    auto candidate = error ? path.lexically_normal() : absolute.lexically_normal();
    const auto canonical = std::filesystem::weakly_canonical(candidate, error);
    if (!error) {
        candidate = canonical.lexically_normal();
    }
    return candidate;
}

struct PrimitiveHomeSpec {
    PrimitiveKind kind;
    std::string_view file_name;
};

constexpr auto kPrimitiveHomes = std::array{
    PrimitiveHomeSpec{.kind = PrimitiveKind::Unit, .file_name = "unit.ahfl"},
    PrimitiveHomeSpec{.kind = PrimitiveKind::Bool, .file_name = "bool.ahfl"},
    PrimitiveHomeSpec{.kind = PrimitiveKind::Int, .file_name = "int.ahfl"},
    PrimitiveHomeSpec{.kind = PrimitiveKind::Float, .file_name = "float.ahfl"},
    PrimitiveHomeSpec{.kind = PrimitiveKind::String, .file_name = "string.ahfl"},
    PrimitiveHomeSpec{.kind = PrimitiveKind::UUID, .file_name = "uuid.ahfl"},
    PrimitiveHomeSpec{.kind = PrimitiveKind::Timestamp, .file_name = "time.ahfl"},
    PrimitiveHomeSpec{.kind = PrimitiveKind::Duration, .file_name = "time.ahfl"},
    PrimitiveHomeSpec{.kind = PrimitiveKind::Decimal, .file_name = "decimal.ahfl"},
};

[[nodiscard]] Location home_location_for_file(const std::filesystem::path &path) {
    return Location{
        .uri = uri_from_path(path),
        .range =
            Range{
                .start = Position{.line = 0, .character = 0},
                .end = Position{.line = 0, .character = 0},
            },
    };
}

} // namespace

std::optional<Location> SysrootPrimitiveIndex::home_location_for_type(const TypeKey &type) const {
    if (type.kind != TypeKey::Kind::Primitive || !type.primitive.has_value()) {
        return std::nullopt;
    }
    return home_location_for_primitive(*type.primitive);
}

std::optional<Location>
SysrootPrimitiveIndex::home_location_for_primitive(PrimitiveKind kind) const {
    const auto found = homes_.find(kind);
    if (found == homes_.end()) {
        return std::nullopt;
    }
    return found->second;
}

void SysrootPrimitiveIndex::add_home(PrimitiveKind kind, Location location) {
    homes_.insert_or_assign(kind, std::move(location));
}

std::optional<std::filesystem::path>
SysrootPrimitiveIndex::missing_home_path_for_primitive(PrimitiveKind kind) const {
    const auto found = missing_homes_.find(kind);
    if (found == missing_homes_.end()) {
        return std::nullopt;
    }
    return found->second;
}

void SysrootPrimitiveIndex::add_missing_home(PrimitiveKind kind,
                                            std::filesystem::path expected_path) {
    missing_homes_.insert_or_assign(kind, std::move(expected_path));
}

SysrootPrimitiveIndex build_sysroot_primitive_index(const SysrootPrimitiveIndexInput &input) {
    SysrootPrimitiveIndex index;
    const auto std_root = normalize_path(input.std_manifest).parent_path();

    for (const auto &home : kPrimitiveHomes) {
        const auto source_path = normalize_path(std_root / home.file_name);
        std::error_code error;
        if (!std::filesystem::is_regular_file(source_path, error) || error) {
            index.add_missing_home(home.kind, source_path);
            continue;
        }
        index.add_home(home.kind, home_location_for_file(source_path));
    }

    return index;
}

std::string_view primitive_kind_name(PrimitiveKind kind) noexcept {
    switch (kind) {
    case PrimitiveKind::Unit:
        return "Unit";
    case PrimitiveKind::Bool:
        return "Bool";
    case PrimitiveKind::Int:
        return "Int";
    case PrimitiveKind::Float:
        return "Float";
    case PrimitiveKind::String:
        return "String";
    case PrimitiveKind::UUID:
        return "UUID";
    case PrimitiveKind::Timestamp:
        return "Timestamp";
    case PrimitiveKind::Duration:
        return "Duration";
    case PrimitiveKind::Decimal:
        return "Decimal";
    }
    return "unknown";
}

} // namespace ahfl::lsp
