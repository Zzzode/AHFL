#pragma once

#include <map>
#include <string>
#include <vector>

#include "ahfl/compiler/ir/ir.hpp"
#include "compiler/backends/infra/openapi_spec.hpp"

namespace ahfl::backends {

// Maps AHFL-IR type references (TypeRef + the program's StructDecl / EnumDecl
// tables) onto JSON Schema (OpenAPI 3.0 subset) trees.
//
//   * Scalar primitives, Decimal/Duration refinements and bounded
//     List/Set/Map collections are emitted inline.
//   * User structs are emitted once into components/schemas, keyed by a
//     sanitized canonical name, and referenced inline via {"$ref": ...}.
//     Recursive structs terminate via a reservation-before-descent scheme.
//   * C-style nominal enums (unit-only variants) become a string schema with
//     "enum" populated from the variant list. Payload-carrying enums become
//     an externally tagged "oneOf" component; generic enums are instantiated
//     from the declaration's member-type template arena at the use-site type
//     arguments.
//
// Kinds that cannot cross a JSON boundary (Unit, Any, Fn, Never, Unresolved)
// map to the empty schema, which OpenApiSchema::has_content() lets the
// renderer translate into "no request/response body" rather than a required
// empty JSON object.
//
// A nominal type whose declaration the program does not carry (e.g. an
// unresolved cross-module symbol) is a lowering error collected in errors():
// the mapper never emits a $ref without a matching component.
class OpenApiTypeMapper {
  public:
    explicit OpenApiTypeMapper(const ir::AhflIr &program);
    ~OpenApiTypeMapper();

    // Map one type reference. Unresolved / unsupported kinds map to the
    // unconstrained schema {} rather than failing.
    [[nodiscard]] OpenApiSchema map_type(const ir::TypeRef &type);

    // Move out the accumulated components/schemas table (sorted by key).
    [[nodiscard]] std::map<std::string, OpenApiSchema> release_components();

    // Lowering errors accumulated while mapping (unresolved nominal types).
    // Non-empty means the generated document must not be emitted.
    [[nodiscard]] const std::vector<std::string> &errors() const noexcept {
        return errors_;
    }

  private:
    struct NominalIndex;

    [[nodiscard]] OpenApiSchema map_struct(const ir::TypeRef &type);
    [[nodiscard]] OpenApiSchema map_enum(const ir::TypeRef &type);

    // Instantiate a declaration-owned member-type template node into a
    // concrete TypeRef, substituting parameter-position nodes with the
    // use-site type arguments.
    [[nodiscard]] ir::TypeRef
    resolve_template(const std::vector<ir::MemberTypeTemplateNode> &arena,
                     std::uint32_t root,
                     const std::vector<const ir::TypeRef *> &type_args) const;

    // components/schemas key for a canonical name (plus a type-argument
    // discriminator for instantiated generics); unique across distinct
    // canonicals that sanitize to the same token.
    [[nodiscard]] std::string component_key(const ir::TypeRef &type);

    const ir::AhflIr &program_;
    std::map<std::string, OpenApiSchema> components_;
    std::map<std::string, std::string, std::less<>> component_keys_; // canonical -> key
    std::map<std::string, std::string, std::less<>> key_owners_;     // key -> canonical
    std::vector<std::string> errors_;
    Owned<NominalIndex> index_;
};

} // namespace ahfl::backends
