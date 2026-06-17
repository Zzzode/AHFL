#pragma once

#include "ahfl/compiler/semantics/typed_hir.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace ahfl {

inline constexpr std::string_view kTypedProgramCacheSchemaVersion = "AHFL_TYPED_HIR_CACHE_V1";
inline constexpr std::string_view kConstValueArtifactSchemaVersion =
    "AHFL_CONST_VALUE_ARTIFACT_V1";

struct TypedProgramCacheMetadata {
    std::string schema_version{std::string(kTypedProgramCacheSchemaVersion)};
    std::string source_graph_revision;
    std::string source_content_hash;
    std::string resolver_snapshot_version;
};

enum class TypedProgramCacheLoadStatus {
    Hit,
    SchemaMismatch,
    SourceRevisionMismatch,
    SourceContentHashMismatch,
    ResolverSnapshotMismatch,
    InvalidPayload,
};

struct TypedProgramCacheLoadResult {
    TypedProgramCacheLoadStatus status{TypedProgramCacheLoadStatus::InvalidPayload};
    std::optional<TypedProgram> program;
};

[[nodiscard]] std::string serialize_typed_program_json(const TypedProgram &program);
[[nodiscard]] std::optional<TypedProgram>
deserialize_typed_program_json(std::string_view json_text);
[[nodiscard]] std::string
serialize_typed_program_cache_json(const TypedProgram &program,
                                   const TypedProgramCacheMetadata &metadata);
[[nodiscard]] TypedProgramCacheLoadResult
load_typed_program_cache_json(std::string_view json_text,
                              const TypedProgramCacheMetadata &expected_metadata);

} // namespace ahfl
