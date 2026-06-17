#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/type_context.hpp"
#include "ahfl/compiler/semantics/type_resolver.hpp"
#include "ahfl/compiler/semantics/types.hpp"

#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

struct CapturedDiagnostic {
    std::string code;
    std::string message;
    ahfl::SourceRange range;
};

struct TypeResolverFixture {
    ahfl::Frontend frontend;
    ahfl::Owned<ahfl::ast::Program> program;
    ahfl::ResolveResult resolve;
    ahfl::TypeContext types;
    ahfl::TypeAliasResolutionState alias_state;
    std::vector<CapturedDiagnostic> diagnostics;
    ahfl::TypeResolver *resolver{nullptr};

    explicit TypeResolverFixture(std::string_view source) {
        auto parse = frontend.parse_text("type_resolver_test.ahfl", std::string(source));
        REQUIRE_FALSE(parse.has_errors());
        REQUIRE(parse.program != nullptr);
        program = std::move(parse.program);

        const ahfl::Resolver resolver_pass;
        resolve = resolver_pass.resolve(*program);
    }

    [[nodiscard]] const ahfl::ast::TypeAliasDecl &alias_decl(std::string_view name) const {
        for (const auto &declaration : program->declarations) {
            if (declaration->kind != ahfl::ast::NodeKind::TypeAliasDecl) {
                continue;
            }
            const auto &alias = static_cast<const ahfl::ast::TypeAliasDecl &>(*declaration);
            if (alias.name == name) {
                return alias;
            }
        }
        FAIL("missing alias declaration");
        throw std::logic_error("missing alias declaration");
    }

    [[nodiscard]] ahfl::SymbolId type_symbol(std::string_view name,
                                             std::string_view module = "resolver") const {
        const auto symbol =
            resolve.symbol_table.find_local(ahfl::SymbolNamespace::Types, name, module);
        REQUIRE(symbol.has_value());
        return symbol->get().id;
    }

    [[nodiscard]] ahfl::MaybeCRef<ahfl::ast::TypeAliasDecl>
    alias_decl_for(ahfl::SymbolId id) const {
        const auto symbol = resolve.symbol_table.get(id);
        if (!symbol.has_value()) {
            return std::nullopt;
        }

        for (const auto &declaration : program->declarations) {
            if (declaration->kind != ahfl::ast::NodeKind::TypeAliasDecl) {
                continue;
            }
            const auto &alias = static_cast<const ahfl::ast::TypeAliasDecl &>(*declaration);
            if (alias.name == symbol->get().local_name) {
                return std::cref(alias);
            }
        }

        return std::nullopt;
    }

    [[nodiscard]] ahfl::TypeResolver make_resolver(std::size_t *alias_body_resolution_count) {
        return ahfl::TypeResolver{resolve,
                                  types,
                                  alias_state,
                                  []() { return std::optional<ahfl::SourceId>{}; },
                                  [this](ahfl::ErrorCode<ahfl::DiagnosticCategory::TypeCheck> code,
                                         std::string message,
                                         ahfl::SourceRange range) {
                                      diagnostics.push_back(CapturedDiagnostic{
                                          .code = code.full_code(),
                                          .message = std::move(message),
                                          .range = range,
                                      });
                                  },
                                  [this](ahfl::SymbolId id) { return alias_decl_for(id); },
                                  [this, alias_body_resolution_count](
                                      ahfl::SymbolId, const ahfl::ast::TypeSyntax &aliased_type) {
                                      if (alias_body_resolution_count != nullptr) {
                                          ++(*alias_body_resolution_count);
                                      }
                                      REQUIRE(resolver != nullptr);
                                      return resolver->resolve_type(aliased_type);
                                  }};
    }
};

} // namespace

TEST_CASE("TypeResolver caches resolved type aliases") {
    TypeResolverFixture fixture{R"AHFL(
module resolver;

struct Request {
    value: String;
}

type RequestAlias = Request;
)AHFL"};
    REQUIRE_FALSE(fixture.resolve.has_errors());

    std::size_t alias_body_resolution_count = 0;
    auto resolver = fixture.make_resolver(&alias_body_resolution_count);
    fixture.resolver = &resolver;

    const auto alias_symbol = fixture.type_symbol("RequestAlias");
    const auto request_symbol = fixture.type_symbol("Request");
    const auto first =
        resolver.resolve_type_alias(alias_symbol, fixture.alias_decl("RequestAlias").range);
    const auto second =
        resolver.resolve_type_alias(alias_symbol, fixture.alias_decl("RequestAlias").range);

    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    CHECK(alias_body_resolution_count == 1);
    CHECK(fixture.alias_state.resolved_alias_types.contains(alias_symbol.value));
    CHECK(first == second);

    const auto *struct_type = first->get_if<ahfl::types::StructT>();
    REQUIRE(struct_type != nullptr);
    REQUIRE(struct_type->symbol.has_value());
    CHECK(*struct_type->symbol == request_symbol);
    CHECK(fixture.diagnostics.empty());
}

TEST_CASE("TypeResolver reports alias cycles at the resolver boundary") {
    TypeResolverFixture fixture{R"AHFL(
module resolver;

type A = B;
type B = A;
)AHFL"};

    std::size_t alias_body_resolution_count = 0;
    auto resolver = fixture.make_resolver(&alias_body_resolution_count);
    fixture.resolver = &resolver;

    const auto alias_symbol = fixture.type_symbol("A");
    const auto resolved = resolver.resolve_type_alias(alias_symbol, fixture.alias_decl("A").range);

    REQUIRE(resolved != nullptr);
    CHECK(resolved->holds<ahfl::types::ErrorT>());
    CHECK(alias_body_resolution_count == 2);
    REQUIRE_FALSE(fixture.diagnostics.empty());
    CHECK(fixture.diagnostics.front().code == "typecheck.INVALID_TYPE_REFERENCE");
    CHECK(fixture.diagnostics.front().message == "type alias cycle reached during type resolution");
}
