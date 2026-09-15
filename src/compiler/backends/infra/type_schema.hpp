#pragma once

#include <map>
#include <string>

#include "ahfl/compiler/ir/ir.hpp"
#include "compiler/backends/infra/openapi_spec.hpp"

namespace ahfl::backends {

// Maps AHFL-IR type references (TypeRef + the program's StructDecl table) onto
// JSON Schema (OpenAPI 3.0 subset) trees. Named user structs are emitted once
// into components/schemas keyed by their sanitized canonical name and referenced
// inline via {"$ref": ...}; scalar primitives, Decimal/Duration refinements and
// bounded List/Set/Map collections are emitted inline. Component emission is
// recursive-struct safe.
class OpenApiTypeMapper {
  public:
    explicit OpenApiTypeMapper(const ir::AhflIr &program);
    ~OpenApiTypeMapper();

    // Map one type reference. Unresolved / unsupported kinds map to the
    // unconstrained schema {} rather than failing.
    [[nodiscard]] OpenApiSchema map_type(const ir::TypeRef &type);

    // Move out the accumulated components/schemas table (sorted by key).
    [[nodiscard]] std::map<std::string, OpenApiSchema> release_components();

  private:
    struct StructIndex;

    [[nodiscard]] OpenApiSchema map_struct(const ir::TypeRef &type);
    [[nodiscard]] std::string component_key(std::string_view canonical_name);

    const ir::AhflIr &program_;
    std::map<std::string, OpenApiSchema> components_;
    std::map<std::string, std::string, std::less<>> component_keys_; // canonical -> key
    Owned<StructIndex> index_;
};

} // namespace ahfl::backends
