#pragma once

#include "ahfl/compiler/semantics/typed_hir.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace ahfl {

inline constexpr std::string_view kTypedProgramCacheFormat = "ahfl.typed-hir-cache";
inline constexpr std::string_view kConstValueArtifactFormat = "ahfl.const-value-artifact";

struct TypedProgramCacheMetadata {
    std::string format{std::string(kTypedProgramCacheFormat)};
    std::string source_graph_digest;
    std::string source_content_digest;
    std::string resolver_snapshot_digest;
};

enum class TypedProgramCacheLoadStatus {
    Hit,
    FormatMismatch,
    SourceGraphMismatch,
    SourceContentDigestMismatch,
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
