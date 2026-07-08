#include "tooling/lsp/analysis_service.hpp"

#include "ahfl/compiler/frontend/ast.hpp"
#include "base/support/sha256.hpp"
#include "compiler/package_graph/package_graph.hpp"
#include "compiler/project_discovery/discovery.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <filesystem>
#include <limits>
#include <system_error>
#include <type_traits>
#include <unordered_set>
#include <variant>
#include <vector>

namespace ahfl::lsp {

namespace {

constexpr std::string_view kWorkspaceIndexSchemaVersion = "lsp-workspace-index-v1";
constexpr std::string_view kWorkspaceIndexIdentitySchemaVersion = "lsp-workspace-index-identity-v1";
constexpr std::size_t kExtraSourceUnitIdBase = std::size_t{1} << 48U;
constexpr std::string_view kDiagnosticDetachedSourceUnit = "N::detached_source_unit";
constexpr std::string_view kDiagnosticDetachedImport = "E::detached_import";
constexpr std::string_view kDiagnosticDetachedUnknownNominalType =
    "E::detached_unknown_nominal_type";
constexpr std::string_view kDiagnosticPrimitiveHomeUnavailable = "W::primitive_home_unavailable";

[[nodiscard]] bool is_hex(char ch) noexcept {
    return std::isxdigit(static_cast<unsigned char>(ch)) != 0;
}

[[nodiscard]] int hex_value(char ch) noexcept {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return 10 + (ch - 'a');
    }
    if (ch >= 'A' && ch <= 'F') {
        return 10 + (ch - 'A');
    }
    return 0;
}

[[nodiscard]] std::string percent_decode(std::string_view text) {
    std::string decoded;
    decoded.reserve(text.size());
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (text[index] == '%' && index + 2 < text.size() && is_hex(text[index + 1]) &&
            is_hex(text[index + 2])) {
            const auto value = (hex_value(text[index + 1]) << 4) | hex_value(text[index + 2]);
            decoded.push_back(static_cast<char>(value));
            index += 2;
            continue;
        }
        decoded.push_back(text[index]);
    }
    return decoded;
}

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

[[nodiscard]] bool path_is_equal_or_descendant(const std::filesystem::path &path,
                                               const std::filesystem::path &ancestor) {
    auto path_part = path.begin();
    for (auto ancestor_part = ancestor.begin(); ancestor_part != ancestor.end(); ++ancestor_part) {
        if (path_part == path.end() || *path_part != *ancestor_part) {
            return false;
        }
        ++path_part;
    }
    return true;
}

[[nodiscard]] std::string_view
toolchain_scope_name(project_discovery::ToolchainProfileScope scope) noexcept {
    switch (scope) {
    case project_discovery::ToolchainProfileScope::GlobalDefault:
        return "global-default";
    case project_discovery::ToolchainProfileScope::WorkspaceFolder:
        return "workspace-folder-uri";
    }
    return "global-default";
}

[[nodiscard]] LspAnalysisMode
analysis_mode_from_discovery(project_discovery::AnalysisContextKind kind) noexcept {
    switch (kind) {
    case project_discovery::AnalysisContextKind::PackageGraph:
        return LspAnalysisMode::PackageGraph;
    case project_discovery::AnalysisContextKind::SourceSysroot:
        return LspAnalysisMode::SourceSysroot;
    case project_discovery::AnalysisContextKind::DetachedSourceUnit:
        return LspAnalysisMode::DetachedSourceUnit;
    }
    return LspAnalysisMode::DetachedSourceUnit;
}

[[nodiscard]] std::string_view analysis_mode_name(LspAnalysisMode mode) noexcept {
    switch (mode) {
    case LspAnalysisMode::PackageGraph:
        return "package-graph";
    case LspAnalysisMode::SourceSysroot:
        return "source-sysroot";
    case LspAnalysisMode::DetachedSourceUnit:
        return "detached-source-unit";
    }
    return "detached-source-unit";
}

[[nodiscard]] Position to_lsp_position(const SourceFile &source, std::size_t offset) {
    const auto pos = source.locate(offset);
    return Position{
        .line = static_cast<std::uint32_t>(pos.line > 0 ? pos.line - 1 : 0),
        .character = static_cast<std::uint32_t>(pos.column > 0 ? pos.column - 1 : 0),
    };
}

[[nodiscard]] Range to_lsp_range(const SourceFile &source, SourceRange range) {
    const auto bounded_begin = std::min(range.begin_offset, source.content.size());
    const auto bounded_end =
        std::max(bounded_begin, std::min(range.end_offset, source.content.size()));
    return Range{
        .start = to_lsp_position(source, bounded_begin),
        .end = to_lsp_position(source, bounded_end),
    };
}

[[nodiscard]] SourceRange fallback_range(const SourceFile &source) {
    return SourceRange{
        .begin_offset = 0,
        .end_offset = std::min<std::size_t>(source.content.size(), 1),
    };
}

struct PrimitiveTypeUse {
    PrimitiveKind kind;
    SourceRange range;
};

void collect_primitive_type_uses(const ast::TypeSyntax *type, std::vector<PrimitiveTypeUse> &uses);
void collect_primitive_type_uses(const ast::ExprSyntax *expr, std::vector<PrimitiveTypeUse> &uses);
void collect_primitive_type_uses(const ast::BlockSyntax *block,
                                 std::vector<PrimitiveTypeUse> &uses);

void add_primitive_type_use(std::vector<PrimitiveTypeUse> &uses,
                            PrimitiveKind kind,
                            SourceRange range) {
    const auto duplicate =
        std::find_if(uses.begin(), uses.end(), [&](const PrimitiveTypeUse &existing) {
            return existing.kind == kind;
        });
    if (duplicate == uses.end()) {
        uses.push_back(PrimitiveTypeUse{.kind = kind, .range = range});
    }
}

void collect_primitive_type_uses(const ast::TypeParamSyntax *param,
                                 std::vector<PrimitiveTypeUse> &uses) {
    if (param == nullptr) {
        return;
    }
    for (const auto &bound : param->bounds) {
        collect_primitive_type_uses(bound.get(), uses);
    }
}

void collect_primitive_type_uses(const ast::WhereClauseSyntax *where_clause,
                                 std::vector<PrimitiveTypeUse> &uses) {
    if (where_clause == nullptr) {
        return;
    }
    for (const auto &constraint : where_clause->constraints) {
        if (constraint == nullptr) {
            continue;
        }
        collect_primitive_type_uses(constraint->subject.get(), uses);
        for (const auto &argument : constraint->arguments) {
            collect_primitive_type_uses(argument.get(), uses);
        }
        for (const auto &bound : constraint->bounds) {
            collect_primitive_type_uses(bound.get(), uses);
        }
    }
}

void collect_primitive_type_uses(const std::vector<Owned<ast::TypeParamSyntax>> &params,
                                 std::vector<PrimitiveTypeUse> &uses) {
    for (const auto &param : params) {
        collect_primitive_type_uses(param.get(), uses);
    }
}

void collect_primitive_type_uses(const std::vector<Owned<ast::ParamDeclSyntax>> &params,
                                 std::vector<PrimitiveTypeUse> &uses) {
    for (const auto &param : params) {
        if (param != nullptr) {
            collect_primitive_type_uses(param->type.get(), uses);
        }
    }
}

void collect_primitive_type_uses(const ast::EffectClauseSyntax *effect,
                                 std::vector<PrimitiveTypeUse> &uses) {
    if (effect == nullptr) {
        return;
    }
    collect_primitive_type_uses(effect->decreases_expr.get(), uses);
}

void collect_primitive_type_uses(const ast::TypeSyntax *type, std::vector<PrimitiveTypeUse> &uses) {
    if (type == nullptr) {
        return;
    }

    std::visit(
        [&](const auto &node) {
            using Node = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<Node, ast::UnitType>) {
                add_primitive_type_use(uses, PrimitiveKind::Unit, type->range);
            } else if constexpr (std::is_same_v<Node, ast::BoolType>) {
                add_primitive_type_use(uses, PrimitiveKind::Bool, type->range);
            } else if constexpr (std::is_same_v<Node, ast::IntType>) {
                add_primitive_type_use(uses, PrimitiveKind::Int, type->range);
            } else if constexpr (std::is_same_v<Node, ast::FloatType>) {
                add_primitive_type_use(uses, PrimitiveKind::Float, type->range);
            } else if constexpr (std::is_same_v<Node, ast::StringType> ||
                                 std::is_same_v<Node, ast::BoundedStringType>) {
                add_primitive_type_use(uses, PrimitiveKind::String, type->range);
            } else if constexpr (std::is_same_v<Node, ast::UuidType>) {
                add_primitive_type_use(uses, PrimitiveKind::UUID, type->range);
            } else if constexpr (std::is_same_v<Node, ast::TimestampType>) {
                add_primitive_type_use(uses, PrimitiveKind::Timestamp, type->range);
            } else if constexpr (std::is_same_v<Node, ast::DurationType>) {
                add_primitive_type_use(uses, PrimitiveKind::Duration, type->range);
            } else if constexpr (std::is_same_v<Node, ast::DecimalType>) {
                add_primitive_type_use(uses, PrimitiveKind::Decimal, type->range);
            } else if constexpr (std::is_same_v<Node, ast::NamedType>) {
                for (const auto &arg : node.type_args) {
                    collect_primitive_type_uses(arg.get(), uses);
                }
            } else if constexpr (std::is_same_v<Node, ast::FnType>) {
                for (const auto &param : node.params) {
                    collect_primitive_type_uses(param.get(), uses);
                }
                collect_primitive_type_uses(node.return_type.get(), uses);
            } else if constexpr (std::is_same_v<Node, ast::AppType>) {
                for (const auto &arg : node.arguments) {
                    collect_primitive_type_uses(arg.get(), uses);
                }
            }
        },
        type->node);
}

void collect_primitive_type_uses(const ast::StatementSyntax *statement,
                                 std::vector<PrimitiveTypeUse> &uses) {
    if (statement == nullptr) {
        return;
    }
    if (statement->let_stmt != nullptr) {
        collect_primitive_type_uses(statement->let_stmt->type.get(), uses);
        collect_primitive_type_uses(statement->let_stmt->initializer.get(), uses);
    }
    if (statement->assign_stmt != nullptr) {
        collect_primitive_type_uses(statement->assign_stmt->value.get(), uses);
    }
    if (statement->if_stmt != nullptr) {
        collect_primitive_type_uses(statement->if_stmt->condition.get(), uses);
        collect_primitive_type_uses(statement->if_stmt->then_block.get(), uses);
        collect_primitive_type_uses(statement->if_stmt->else_block.get(), uses);
    }
    if (statement->if_let_stmt != nullptr) {
        collect_primitive_type_uses(statement->if_let_stmt->scrutinee.get(), uses);
        collect_primitive_type_uses(statement->if_let_stmt->then_block.get(), uses);
        collect_primitive_type_uses(statement->if_let_stmt->else_block.get(), uses);
    }
    if (statement->return_stmt != nullptr) {
        collect_primitive_type_uses(statement->return_stmt->value.get(), uses);
    }
    if (statement->assert_stmt != nullptr) {
        collect_primitive_type_uses(statement->assert_stmt->condition.get(), uses);
        collect_primitive_type_uses(statement->assert_stmt->message.get(), uses);
    }
    if (statement->unwrap_stmt != nullptr) {
        collect_primitive_type_uses(statement->unwrap_stmt->operand.get(), uses);
    }
    if (statement->requires_stmt != nullptr) {
        collect_primitive_type_uses(statement->requires_stmt->condition.get(), uses);
        collect_primitive_type_uses(statement->requires_stmt->message.get(), uses);
    }
    if (statement->unreachable_stmt != nullptr) {
        collect_primitive_type_uses(statement->unreachable_stmt->message.get(), uses);
    }
    if (statement->expr_stmt != nullptr) {
        collect_primitive_type_uses(statement->expr_stmt->expr.get(), uses);
    }
}

void collect_primitive_type_uses(const ast::BlockSyntax *block,
                                 std::vector<PrimitiveTypeUse> &uses) {
    if (block == nullptr) {
        return;
    }
    for (const auto &statement : block->statements) {
        collect_primitive_type_uses(statement.get(), uses);
    }
}

void collect_primitive_type_uses(const ast::ExprSyntax *expr, std::vector<PrimitiveTypeUse> &uses) {
    if (expr == nullptr) {
        return;
    }

    std::visit(
        [&](const auto &node) {
            using Node = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<Node, ast::CallExpr>) {
                for (const auto &arg : node.type_args) {
                    collect_primitive_type_uses(arg.get(), uses);
                }
                for (const auto &arg : node.arguments) {
                    collect_primitive_type_uses(arg.get(), uses);
                }
            } else if constexpr (std::is_same_v<Node, ast::MethodCallExpr>) {
                collect_primitive_type_uses(node.receiver.get(), uses);
                for (const auto &arg : node.type_args) {
                    collect_primitive_type_uses(arg.get(), uses);
                }
                for (const auto &arg : node.arguments) {
                    collect_primitive_type_uses(arg.get(), uses);
                }
            } else if constexpr (std::is_same_v<Node, ast::StructLiteralExpr>) {
                for (const auto &field : node.fields) {
                    if (field != nullptr) {
                        collect_primitive_type_uses(field->value.get(), uses);
                    }
                }
            } else if constexpr (std::is_same_v<Node, ast::UnaryExpr>) {
                collect_primitive_type_uses(node.operand.get(), uses);
            } else if constexpr (std::is_same_v<Node, ast::BinaryExpr>) {
                collect_primitive_type_uses(node.lhs.get(), uses);
                collect_primitive_type_uses(node.rhs.get(), uses);
            } else if constexpr (std::is_same_v<Node, ast::MemberAccessExpr>) {
                collect_primitive_type_uses(node.base.get(), uses);
            } else if constexpr (std::is_same_v<Node, ast::IndexAccessExpr>) {
                collect_primitive_type_uses(node.base.get(), uses);
                collect_primitive_type_uses(node.index.get(), uses);
            } else if constexpr (std::is_same_v<Node, ast::GroupExpr>) {
                collect_primitive_type_uses(node.inner.get(), uses);
            } else if constexpr (std::is_same_v<Node, ast::MatchExpr>) {
                collect_primitive_type_uses(node.scrutinee.get(), uses);
                for (const auto &arm : node.arms) {
                    if (arm != nullptr) {
                        collect_primitive_type_uses(arm->guard.get(), uses);
                        collect_primitive_type_uses(arm->body.get(), uses);
                    }
                }
            } else if constexpr (std::is_same_v<Node, ast::LambdaExpr>) {
                for (const auto &param : node.params) {
                    if (param != nullptr) {
                        collect_primitive_type_uses(param->type.get(), uses);
                    }
                }
                collect_primitive_type_uses(node.body.get(), uses);
            } else if constexpr (std::is_same_v<Node, ast::UnwrapExprSyntax>) {
                collect_primitive_type_uses(node.operand.get(), uses);
            }
        },
        expr->node);
}

void collect_primitive_type_uses(const ast::TemporalExprSyntax *expr,
                                 std::vector<PrimitiveTypeUse> &uses) {
    if (expr == nullptr) {
        return;
    }
    std::visit(
        [&](const auto &node) {
            using Node = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<Node, ast::EmbeddedTemporalExpr>) {
                collect_primitive_type_uses(node.expr.get(), uses);
            } else if constexpr (std::is_same_v<Node, ast::UnaryTemporalExpr>) {
                collect_primitive_type_uses(node.operand.get(), uses);
            } else if constexpr (std::is_same_v<Node, ast::BinaryTemporalExpr>) {
                collect_primitive_type_uses(node.lhs.get(), uses);
                collect_primitive_type_uses(node.rhs.get(), uses);
            }
        },
        expr->node);
}

std::vector<PrimitiveTypeUse> primitive_type_uses_in_program(const ast::Program &program) {
    std::vector<PrimitiveTypeUse> uses;
    for (const auto &decl : program.declarations) {
        if (decl == nullptr) {
            continue;
        }

        switch (decl->kind) {
        case ast::NodeKind::Program:
            break;
        case ast::NodeKind::ConstDecl: {
            const auto &typed = static_cast<const ast::ConstDecl &>(*decl);
            collect_primitive_type_uses(typed.type.get(), uses);
            collect_primitive_type_uses(typed.value.get(), uses);
            break;
        }
        case ast::NodeKind::TypeAliasDecl: {
            const auto &typed = static_cast<const ast::TypeAliasDecl &>(*decl);
            collect_primitive_type_uses(typed.type_params, uses);
            collect_primitive_type_uses(typed.aliased_type.get(), uses);
            break;
        }
        case ast::NodeKind::StructDecl: {
            const auto &typed = static_cast<const ast::StructDecl &>(*decl);
            collect_primitive_type_uses(typed.type_params, uses);
            collect_primitive_type_uses(typed.where_clause.get(), uses);
            for (const auto &field : typed.fields) {
                if (field != nullptr) {
                    collect_primitive_type_uses(field->type.get(), uses);
                    collect_primitive_type_uses(field->default_value.get(), uses);
                }
            }
            break;
        }
        case ast::NodeKind::EnumDecl: {
            const auto &typed = static_cast<const ast::EnumDecl &>(*decl);
            collect_primitive_type_uses(typed.type_params, uses);
            collect_primitive_type_uses(typed.where_clause.get(), uses);
            for (const auto &variant : typed.variants) {
                if (variant == nullptr) {
                    continue;
                }
                for (const auto &payload : variant->payload) {
                    collect_primitive_type_uses(payload.get(), uses);
                }
                for (const auto &field : variant->named_fields) {
                    if (field != nullptr) {
                        collect_primitive_type_uses(field->type.get(), uses);
                        collect_primitive_type_uses(field->default_value.get(), uses);
                    }
                }
            }
            break;
        }
        case ast::NodeKind::CapabilityDecl: {
            const auto &typed = static_cast<const ast::CapabilityDecl &>(*decl);
            collect_primitive_type_uses(typed.params, uses);
            collect_primitive_type_uses(typed.return_type.get(), uses);
            collect_primitive_type_uses(typed.where_clause.get(), uses);
            break;
        }
        case ast::NodeKind::PredicateDecl: {
            const auto &typed = static_cast<const ast::PredicateDecl &>(*decl);
            collect_primitive_type_uses(typed.params, uses);
            collect_primitive_type_uses(typed.effect_clause.get(), uses);
            break;
        }
        case ast::NodeKind::AgentDecl: {
            const auto &typed = static_cast<const ast::AgentDecl &>(*decl);
            collect_primitive_type_uses(typed.input_type.get(), uses);
            collect_primitive_type_uses(typed.context_type.get(), uses);
            collect_primitive_type_uses(typed.output_type.get(), uses);
            break;
        }
        case ast::NodeKind::ContractDecl: {
            const auto &typed = static_cast<const ast::ContractDecl &>(*decl);
            for (const auto &clause : typed.clauses) {
                if (clause != nullptr) {
                    collect_primitive_type_uses(clause->expr.get(), uses);
                    collect_primitive_type_uses(clause->temporal_expr.get(), uses);
                    if (clause->decreases != nullptr) {
                        for (const auto &term : clause->decreases->decreases_exprs) {
                            collect_primitive_type_uses(term.get(), uses);
                        }
                    }
                }
            }
            break;
        }
        case ast::NodeKind::FlowDecl: {
            const auto &typed = static_cast<const ast::FlowDecl &>(*decl);
            for (const auto &handler : typed.state_handlers) {
                if (handler != nullptr) {
                    collect_primitive_type_uses(handler->body.get(), uses);
                }
            }
            break;
        }
        case ast::NodeKind::WorkflowDecl: {
            const auto &typed = static_cast<const ast::WorkflowDecl &>(*decl);
            collect_primitive_type_uses(typed.input_type.get(), uses);
            collect_primitive_type_uses(typed.output_type.get(), uses);
            for (const auto &node : typed.nodes) {
                if (node != nullptr) {
                    collect_primitive_type_uses(node->input.get(), uses);
                }
            }
            for (const auto &safety : typed.safety) {
                collect_primitive_type_uses(safety.get(), uses);
            }
            for (const auto &liveness : typed.liveness) {
                collect_primitive_type_uses(liveness.get(), uses);
            }
            collect_primitive_type_uses(typed.return_value.get(), uses);
            break;
        }
        case ast::NodeKind::FnDecl: {
            const auto &typed = static_cast<const ast::FnDecl &>(*decl);
            collect_primitive_type_uses(typed.type_params, uses);
            collect_primitive_type_uses(typed.params, uses);
            collect_primitive_type_uses(typed.return_type.get(), uses);
            collect_primitive_type_uses(typed.effect_clause.get(), uses);
            collect_primitive_type_uses(typed.where_clause.get(), uses);
            collect_primitive_type_uses(typed.body.get(), uses);
            break;
        }
        case ast::NodeKind::TraitDecl: {
            const auto &typed = static_cast<const ast::TraitDecl &>(*decl);
            collect_primitive_type_uses(typed.type_params, uses);
            for (const auto &super_trait : typed.super_traits) {
                collect_primitive_type_uses(super_trait.get(), uses);
            }
            collect_primitive_type_uses(typed.where_clause.get(), uses);
            for (const auto &item : typed.items) {
                if (item == nullptr) {
                    continue;
                }
                collect_primitive_type_uses(item->type_params, uses);
                collect_primitive_type_uses(item->params, uses);
                collect_primitive_type_uses(item->return_type.get(), uses);
                collect_primitive_type_uses(item->effect_clause.get(), uses);
                collect_primitive_type_uses(item->where_clause.get(), uses);
                if (item->assoc_type != nullptr) {
                    collect_primitive_type_uses(item->assoc_type->type_params, uses);
                    for (const auto &bound : item->assoc_type->bounds) {
                        collect_primitive_type_uses(bound.get(), uses);
                    }
                    collect_primitive_type_uses(item->assoc_type->default_type.get(), uses);
                }
                if (item->assoc_const != nullptr) {
                    collect_primitive_type_uses(item->assoc_const->type.get(), uses);
                    collect_primitive_type_uses(item->assoc_const->default_value.get(), uses);
                }
            }
            break;
        }
        case ast::NodeKind::ImplDecl: {
            const auto &typed = static_cast<const ast::ImplDecl &>(*decl);
            collect_primitive_type_uses(typed.type_params, uses);
            collect_primitive_type_uses(typed.trait_ref.get(), uses);
            collect_primitive_type_uses(typed.target_type.get(), uses);
            collect_primitive_type_uses(typed.where_clause.get(), uses);
            for (const auto &method : typed.methods) {
                if (method != nullptr) {
                    collect_primitive_type_uses(method->type_params, uses);
                    collect_primitive_type_uses(method->params, uses);
                    collect_primitive_type_uses(method->return_type.get(), uses);
                    collect_primitive_type_uses(method->effect_clause.get(), uses);
                    collect_primitive_type_uses(method->where_clause.get(), uses);
                    collect_primitive_type_uses(method->body.get(), uses);
                }
            }
            for (const auto &item : typed.assoc_items) {
                if (item != nullptr) {
                    collect_primitive_type_uses(item->type.get(), uses);
                }
            }
            for (const auto &item : typed.const_items) {
                if (item != nullptr) {
                    collect_primitive_type_uses(item->type.get(), uses);
                    collect_primitive_type_uses(item->value.get(), uses);
                }
            }
            break;
        }
        case ast::NodeKind::ModuleDecl:
        case ast::NodeKind::ImportDecl:
        case ast::NodeKind::UseDecl:
            break;
        }
    }
    return uses;
}

[[nodiscard]] const package_graph::PackageNode *
root_package_for_lsp(const package_graph::PackageGraph &graph) {
    const auto found =
        std::find_if(graph.packages.begin(), graph.packages.end(), [](const auto &package) {
            return package.source == package_graph::PackageSourceKind::Root;
        });
    return found == graph.packages.end() ? nullptr : &*found;
}

[[nodiscard]] std::vector<std::string>
dependency_prefixes_for_package(const package_graph::PackageGraph &graph,
                                package_graph::PackageId package_id) {
    std::vector<std::string> prefixes;
    for (const auto &dependency : graph.dependencies) {
        if (dependency.from != package_id) {
            continue;
        }

        const auto *target = graph.find_package(dependency.to);
        if (target != nullptr) {
            prefixes.push_back(target->module_prefix);
        }
    }

    std::sort(prefixes.begin(), prefixes.end());
    prefixes.erase(std::unique(prefixes.begin(), prefixes.end()), prefixes.end());
    return prefixes;
}

[[nodiscard]] std::vector<std::string>
artifact_exports_for_package(const package_graph::PackageGraph &graph,
                             package_graph::PackageId package_id) {
    std::vector<std::string> exports;
    const auto *package = graph.find_package(package_id);
    if (package == nullptr) {
        return exports;
    }
    for (const auto &target : package->targets) {
        for (const auto &export_item : target.exports) {
            exports.push_back(export_item.name);
        }
    }

    std::sort(exports.begin(), exports.end());
    exports.erase(std::unique(exports.begin(), exports.end()), exports.end());
    return exports;
}

[[nodiscard]] bool source_unit_has_role(const package_graph::SourceUnitNode &unit,
                                        package_graph::SourceUnitRole role) {
    return std::find(unit.roles.begin(), unit.roles.end(), role) != unit.roles.end();
}

[[nodiscard]] const package_graph::SourceUnitNode *
first_source_unit_for_role(const package_graph::PackageGraph &graph,
                           package_graph::PackageId package,
                           package_graph::SourceUnitRole role) {
    const auto found =
        std::find_if(graph.source_units.begin(), graph.source_units.end(), [&](const auto &unit) {
            return unit.package == package && source_unit_has_role(unit, role);
        });
    return found == graph.source_units.end() ? nullptr : &*found;
}

[[nodiscard]] std::filesystem::path
semantic_entry_file_from_package_graph(const package_graph::PackageGraph &graph,
                                       const package_graph::PackageNode &package,
                                       const std::filesystem::path &fallback) {
    if (const auto *source_unit = first_source_unit_for_role(
            graph, package.id, package_graph::SourceUnitRole::TargetEntry);
        source_unit != nullptr) {
        return source_unit->path;
    }
    return fallback;
}

using NavigationScopeKindMap =
    std::unordered_map<std::string, std::vector<LspNavigationIndexSourceKind>>;

[[nodiscard]] bool append_unique_entry_file(ProjectInput &input,
                                            const std::filesystem::path &path,
                                            bool require_existing_file = true) {
    const auto normalized = std::filesystem::path(AnalysisService::normalized_path_key(path));
    if (std::find(input.entry_files.begin(), input.entry_files.end(), normalized) !=
        input.entry_files.end()) {
        return true;
    }

    std::error_code error;
    if (require_existing_file && (!std::filesystem::exists(normalized, error) || error)) {
        return false;
    }

    input.entry_files.push_back(normalized);
    return true;
}

void record_navigation_scope_kind(NavigationScopeKindMap *scope_kinds,
                                  const std::filesystem::path &path,
                                  LspNavigationIndexSourceKind kind) {
    if (scope_kinds == nullptr) {
        return;
    }

    auto &kinds = (*scope_kinds)[AnalysisService::normalized_path_key(path)];
    if (std::find(kinds.begin(), kinds.end(), kind) == kinds.end()) {
        kinds.push_back(kind);
    }
}

void append_scoped_entry_file(ProjectInput &input,
                              NavigationScopeKindMap *scope_kinds,
                              const std::filesystem::path &path,
                              LspNavigationIndexSourceKind kind,
                              bool require_existing_file = true) {
    if (append_unique_entry_file(input, path, require_existing_file)) {
        record_navigation_scope_kind(scope_kinds, path, kind);
    }
}

void append_exported_entry_files(
    ProjectInput &input,
    const package_graph::PackageGraph &graph,
    const package_graph::PackageNode &package,
    bool include_prelude,
    NavigationScopeKindMap *scope_kinds = nullptr,
    LspNavigationIndexSourceKind kind = LspNavigationIndexSourceKind::PackageExport) {
    for (const auto &source_unit : graph.source_units) {
        if (source_unit.package != package.id ||
            !source_unit_has_role(source_unit, package_graph::SourceUnitRole::Export)) {
            continue;
        }
        if (!include_prelude && package.module_prefix == "std" &&
            source_unit.module_path == "prelude") {
            continue;
        }
        append_scoped_entry_file(input, scope_kinds, source_unit.path, kind);
    }
}

void seed_source_cache_from_graph(ProjectInput &input, const SourceGraph &graph) {
    for (const auto &source : graph.sources) {
        const auto key = AnalysisService::normalized_path_key(source.path);
        input.source_cache.try_emplace(key, source.source.content);
    }
}

[[nodiscard]] std::vector<LspIndexPackageRoot>
index_package_roots_from_graph(const package_graph::PackageGraph &graph) {
    std::vector<LspIndexPackageRoot> roots;
    roots.reserve(graph.packages.size());
    for (const auto &package : graph.packages) {
        roots.push_back(LspIndexPackageRoot{
            .package_id = package.id,
            .module_root = package.module_root,
        });
    }
    return roots;
}

[[nodiscard]] std::string sysroot_primitive_index_cache_key(const LspToolchainCacheKey &key) {
    return key.std_manifest + "#" + key.std_identity + "#" +
           std::string{kSysrootPrimitiveHomeSchemaVersion};
}

[[nodiscard]] std::string
sysroot_index_cache_key_prefix(const LspToolchainCacheKey &key) {
    return key.analysis_mode + "#" + key.workspace_folder_uri + "#" + key.root_manifest + "#" +
           key.workspace_manifest + "#" + key.package_graph_identity + "#" + key.std_manifest +
           "#" + key.std_identity + "#" + key.scope + "#" + key.index_schema_version + "#" +
           key.index_identity_schema_version + "#";
}

[[nodiscard]] std::string
sysroot_index_cache_key(const LspToolchainCacheKey &key,
                        std::string_view open_document_overlay_revision_set) {
    return sysroot_index_cache_key_prefix(key) + std::string{open_document_overlay_revision_set};
}

[[nodiscard]] std::string package_graph_identity(const package_graph::PackageGraph &graph) {
    return "sha256:" + support::sha256_hex(package_graph::serialize_package_graph_json(graph));
}

[[nodiscard]] std::string
workspace_root_index_cache_key_prefix(const std::filesystem::path &workspace_root,
                                      const package_graph::PackageGraph &graph) {
    return AnalysisService::uri_from_path(workspace_root) + "#" + package_graph_identity(graph) +
           "#" + std::string{kWorkspaceIndexSchemaVersion} + "#" +
           std::string{kWorkspaceIndexIdentitySchemaVersion} + "#";
}

[[nodiscard]] std::string
workspace_root_index_cache_key(const std::filesystem::path &workspace_root,
                               const package_graph::PackageGraph &graph,
                               std::string_view open_document_overlay_revision_set) {
    return workspace_root_index_cache_key_prefix(workspace_root, graph) +
           std::string{open_document_overlay_revision_set};
}

template <typename Cache>
[[nodiscard]] const LspWorkspaceIndex *previous_index_for_cache_prefix(const Cache &cache,
                                                                       std::string_view prefix) {
    for (const auto &[key, index] : cache) {
        if (index != nullptr && key.starts_with(prefix)) {
            return index.get();
        }
    }
    return nullptr;
}

[[nodiscard]] std::vector<std::filesystem::path>
paths_from_scope_kinds(const NavigationScopeKindMap &scope_kinds) {
    std::vector<std::filesystem::path> paths;
    paths.reserve(scope_kinds.size());
    for (const auto &[path_key, _] : scope_kinds) {
        paths.emplace_back(path_key);
    }
    return paths;
}

[[nodiscard]] bool is_project_manifest_path(const std::filesystem::path &path) {
    const auto filename = path.filename().generic_string();
    return filename == "ahfl.toml" || filename == "ahfl.workspace.toml";
}

[[nodiscard]] bool source_units_reference_path(const LspWorkspaceIndex &index,
                                               const std::unordered_set<std::string> &path_keys) {
    for (const auto &source_unit : index.source_units()) {
        if (!source_unit.valid) {
            continue;
        }
        if (path_keys.contains(AnalysisService::normalized_path_key(source_unit.path))) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool snapshot_references_path(const LspAnalysisSnapshot &snapshot,
                                            const std::unordered_set<std::string> &path_keys) {
    for (const auto &source : snapshot.sources) {
        if (path_keys.contains(AnalysisService::normalized_path_key(source.path))) {
            return true;
        }
    }
    return snapshot.workspace_index != nullptr &&
           source_units_reference_path(*snapshot.workspace_index, path_keys);
}

[[nodiscard]] const package_graph::PackageNode *
package_for_requested_file(const package_graph::PackageGraph &graph,
                           const std::filesystem::path &requested_file) {
    const auto normalized_request =
        std::filesystem::path(AnalysisService::normalized_path_key(requested_file));
    const package_graph::PackageNode *best = nullptr;
    for (const auto &package : graph.packages) {
        const auto module_root =
            std::filesystem::path(AnalysisService::normalized_path_key(package.module_root));
        if (!path_is_equal_or_descendant(normalized_request, module_root)) {
            continue;
        }
        if (best == nullptr ||
            module_root.generic_string().size() >
                std::filesystem::path(AnalysisService::normalized_path_key(best->module_root))
                    .generic_string()
                    .size()) {
            best = &package;
        }
    }
    return best;
}

[[nodiscard]] std::string source_unit_registry_key(package_graph::PackageId package,
                                                   const std::filesystem::path &path) {
    return std::to_string(package.value) + '\0' + path.generic_string();
}

[[nodiscard]] std::vector<LspIndexSourceUnitSeed> index_source_units_from_scope_kinds(
    const package_graph::PackageGraph &graph,
    const NavigationScopeKindMap &scope_kinds,
    std::unordered_map<std::string, SourceUnitId> &extra_source_unit_ids_by_path,
    std::size_t &next_extra_source_unit_id) {
    std::unordered_map<std::string, std::vector<const package_graph::SourceUnitNode *>>
        graph_units_by_path;
    graph_units_by_path.reserve(graph.source_units.size());
    for (const auto &unit : graph.source_units) {
        graph_units_by_path[std::filesystem::path(AnalysisService::normalized_path_key(unit.path))
                                .generic_string()]
            .push_back(&unit);
    }

    std::vector<LspIndexSourceUnitSeed> source_units;
    source_units.reserve(scope_kinds.size());
    for (const auto &[path_key, kinds] : scope_kinds) {
        const auto path = std::filesystem::path(path_key);
        const auto normalized_path_key =
            std::filesystem::path(AnalysisService::normalized_path_key(path)).generic_string();
        const auto *package = package_for_requested_file(graph, path);
        if (const auto graph_units = graph_units_by_path.find(normalized_path_key);
            graph_units != graph_units_by_path.end()) {
            const auto unit = std::find_if(
                graph_units->second.begin(), graph_units->second.end(), [&](const auto *candidate) {
                    return package != nullptr && candidate->package == package->id;
                });
            const auto *graph_unit =
                unit == graph_units->second.end() ? graph_units->second.front() : *unit;
            source_units.push_back(LspIndexSourceUnitSeed{
                .source_unit_id = graph_unit->id,
                .package_id = graph_unit->package,
                .path = graph_unit->path,
                .scope_kinds = kinds,
            });
            continue;
        }

        source_units.push_back(LspIndexSourceUnitSeed{
            .source_unit_id = SourceUnitId{std::numeric_limits<std::size_t>::max()},
            .package_id = package == nullptr
                              ? package_graph::PackageId{std::numeric_limits<std::size_t>::max()}
                              : package->id,
            .path = path,
            .scope_kinds = kinds,
        });
    }
    std::sort(source_units.begin(),
              source_units.end(),
              [](const LspIndexSourceUnitSeed &lhs, const LspIndexSourceUnitSeed &rhs) {
                  if (lhs.source_unit_id.value != rhs.source_unit_id.value) {
                      return lhs.source_unit_id.value < rhs.source_unit_id.value;
                  }
                  if (lhs.package_id.value != rhs.package_id.value) {
                      return lhs.package_id.value < rhs.package_id.value;
                  }
                  return lhs.path.generic_string() < rhs.path.generic_string();
              });
    for (auto &source_unit : source_units) {
        if (graph.find_source_unit(source_unit.source_unit_id) != nullptr) {
            continue;
        }

        if (next_extra_source_unit_id < kExtraSourceUnitIdBase) {
            next_extra_source_unit_id = kExtraSourceUnitIdBase;
        }

        const auto path_key = source_unit_registry_key(source_unit.package_id, source_unit.path);
        auto [registry_entry, inserted] =
            extra_source_unit_ids_by_path.try_emplace(path_key, SourceUnitId{});
        if (inserted) {
            registry_entry->second = SourceUnitId{next_extra_source_unit_id++};
        }
        source_unit.source_unit_id = registry_entry->second;
    }
    std::sort(source_units.begin(),
              source_units.end(),
              [](const LspIndexSourceUnitSeed &lhs, const LspIndexSourceUnitSeed &rhs) {
                  return lhs.source_unit_id.value < rhs.source_unit_id.value;
              });
    return source_units;
}

void append_open_overlay_entry_files(ProjectInput &input,
                                     NavigationScopeKindMap *scope_kinds,
                                     const package_graph::PackageGraph &graph,
                                     const std::filesystem::path &requested_file) {
    const auto *requested_package = package_for_requested_file(graph, requested_file);
    for (const auto &[path_key, text] : input.source_overlays) {
        (void)text;
        const auto overlay_path = std::filesystem::path(path_key);
        const auto *overlay_package = package_for_requested_file(graph, overlay_path);
        if (overlay_package == nullptr) {
            continue;
        }
        if (overlay_package->source == package_graph::PackageSourceKind::Sysroot &&
            !(requested_package == overlay_package && overlay_package->module_prefix == "std")) {
            continue;
        }
        append_scoped_entry_file(
            input, scope_kinds, overlay_path, LspNavigationIndexSourceKind::OpenOverlay, false);
    }
}

enum class LspProjectInputMode {
    Semantic,
    WorkspaceIndex,
    SysrootIndex,
};

[[nodiscard]] ProjectInput
project_input_from_package_graph(const package_graph::PackageGraph &graph,
                                 const std::filesystem::path &requested_file,
                                 std::unordered_map<std::string, std::string> overlays,
                                 LspProjectInputMode mode,
                                 NavigationScopeKindMap *scope_kinds = nullptr) {
    ProjectInput input;
    const auto *root_package = root_package_for_lsp(graph);
    const auto fallback_entry =
        std::filesystem::path(AnalysisService::normalized_path_key(requested_file));
    if (mode != LspProjectInputMode::SysrootIndex) {
        const auto semantic_entry =
            root_package == nullptr
                ? fallback_entry
                : semantic_entry_file_from_package_graph(graph, *root_package, fallback_entry);
        input.entry_files.push_back(semantic_entry);
        record_navigation_scope_kind(
            scope_kinds, semantic_entry, LspNavigationIndexSourceKind::SemanticEntry);
    }
    input.inject_prelude = false;
    input.source_overlays = std::move(overlays);
    input.enforce_package_dependencies = true;
    input.module_roots.reserve(graph.packages.size());
    for (const auto &package : graph.packages) {
        input.module_roots.push_back(ProjectInput::ModuleRoot{
            .prefix = package.module_prefix,
            .root = package.module_root,
            .exported_modules = package.exported_modules,
            .artifact_exports = artifact_exports_for_package(graph, package.id),
            .dependency_prefixes = dependency_prefixes_for_package(graph, package.id),
            .compiler_intrinsics_allow = package.compiler_intrinsics_allow,
        });
    }
    if (mode == LspProjectInputMode::SysrootIndex) {
        for (const auto &package : graph.packages) {
            if (package.source == package_graph::PackageSourceKind::Sysroot &&
                package.module_prefix == "std") {
                append_exported_entry_files(input,
                                            graph,
                                            package,
                                            true,
                                            scope_kinds,
                                            LspNavigationIndexSourceKind::SysrootExport);
                break;
            }
        }
    } else if (mode == LspProjectInputMode::WorkspaceIndex) {
        const auto *requested_package = package_for_requested_file(graph, requested_file);
        for (const auto &package : graph.packages) {
            if (package.source == package_graph::PackageSourceKind::Sysroot &&
                !(requested_package == &package && package.module_prefix == "std")) {
                continue;
            }
            const auto kind = package.source == package_graph::PackageSourceKind::Sysroot
                                  ? LspNavigationIndexSourceKind::SysrootExport
                                  : LspNavigationIndexSourceKind::PackageExport;
            append_exported_entry_files(input, graph, package, true, scope_kinds, kind);
        }
        append_open_overlay_entry_files(input, scope_kinds, graph, requested_file);
    }
    return input;
}

[[nodiscard]] DiagnosticSeverity to_lsp_severity(ahfl::DiagnosticSeverity severity) {
    switch (severity) {
    case ahfl::DiagnosticSeverity::Error:
        return DiagnosticSeverity::Error;
    case ahfl::DiagnosticSeverity::Warning:
        return DiagnosticSeverity::Warning;
    case ahfl::DiagnosticSeverity::Note:
        return DiagnosticSeverity::Information;
    }
    return DiagnosticSeverity::Error;
}

[[nodiscard]] std::optional<LspDiagnostic> convert_diagnostic(const Diagnostic &diagnostic,
                                                              const LspAnalysisSnapshot &snapshot,
                                                              const LspSourceSnapshot &source,
                                                              std::string_view fallback_code) {
    LspDiagnostic result;
    result.severity = to_lsp_severity(diagnostic.severity);
    result.message = diagnostic.message;
    result.code = diagnostic.code.value_or(std::string(fallback_code));
    result.data = diagnostic.data;
    if (snapshot.analysis_mode == LspAnalysisMode::DetachedSourceUnit &&
        result.code == error_codes::resolve::UnknownSymbol.full_code() &&
        result.message.starts_with("unknown type")) {
        result.code = std::string{kDiagnosticDetachedUnknownNominalType};
    }
    result.source = "ahfl";
    const auto range = diagnostic.range.value_or(fallback_range(*source.source));
    result.range = to_lsp_range(*source.source, range);

    for (const auto &related : diagnostic.related) {
        if (!related.range.has_value()) {
            continue;
        }
        // A related note may live in a different source unit than the primary
        // diagnostic (e.g. "other declaration in module M" surfaced across
        // module boundaries). Prefer source_id, then fall back to the primary
        // source's URI + range conversion when no id is recorded.
        const LspSourceSnapshot *related_source = &source;
        if (related.source_id.has_value()) {
            const auto *by_id = snapshot.source_for_id(*related.source_id);
            if (by_id != nullptr) {
                related_source = by_id;
            }
        }
        if (related_source->source == nullptr) {
            continue;
        }
        LspDiagnostic::RelatedInformation info;
        info.location.uri = related_source->uri;
        info.location.range = to_lsp_range(*related_source->source, *related.range);
        info.message = related.message;
        result.related_information.push_back(std::move(info));
    }

    (void)snapshot;
    return result;
}

void collect_diagnostics_for_uri(std::vector<LspDiagnostic> &out,
                                 const DiagnosticBag &bag,
                                 const LspAnalysisSnapshot &snapshot,
                                 std::string_view target_uri,
                                 std::string_view fallback_code) {
    for (const auto &diagnostic : bag.entries()) {
        const LspSourceSnapshot *source = nullptr;
        if (diagnostic.source_name.has_value()) {
            source = snapshot.source_for_display_name(*diagnostic.source_name);
            if (source == nullptr || source->uri != target_uri) {
                continue;
            }
        } else {
            source = snapshot.source_for_uri(target_uri);
            if (source == nullptr) {
                continue;
            }
        }

        auto converted = convert_diagnostic(diagnostic, snapshot, *source, fallback_code);
        if (converted.has_value()) {
            out.push_back(std::move(*converted));
        }
    }
}

[[nodiscard]] LspDiagnostic
project_discovery_diagnostic(const package_graph::Diagnostic &diagnostic,
                             const LspSourceSnapshot &source) {
    LspDiagnostic result;
    result.severity = DiagnosticSeverity::Error;
    result.message = diagnostic.message;
    result.code = diagnostic.code.empty() ? "project.discovery" : diagnostic.code;
    result.source = "ahfl";
    result.range = source.source == nullptr
                       ? Range{}
                       : to_lsp_range(*source.source, fallback_range(*source.source));
    for (const auto &related : diagnostic.related) {
        if (related.path.empty()) {
            continue;
        }
        LspDiagnostic::RelatedInformation info;
        info.location.uri = AnalysisService::uri_from_path(related.path);
        info.location.range = Range{};
        info.message = related.message;
        result.related_information.push_back(std::move(info));
    }
    return result;
}

void append_project_diagnostics(LspAnalysisSnapshot &snapshot,
                                const std::string &uri,
                                const std::vector<package_graph::Diagnostic> &diagnostics) {
    const auto *source = snapshot.source_for_uri(uri);
    if (source == nullptr) {
        return;
    }
    snapshot.project_diagnostics.reserve(snapshot.project_diagnostics.size() + diagnostics.size());
    for (const auto &diagnostic : diagnostics) {
        snapshot.project_diagnostics.push_back(project_discovery_diagnostic(diagnostic, *source));
    }
}

void append_detached_source_unit_diagnostics(LspAnalysisSnapshot &snapshot,
                                             const std::string &uri,
                                             const SysrootPrimitiveIndex *primitive_index,
                                             bool has_toolchain_profile) {
    const auto *source = snapshot.source_for_uri(uri);
    if (source == nullptr || source->source == nullptr) {
        return;
    }

    snapshot.project_diagnostics.push_back(LspDiagnostic{
        .range = to_lsp_range(*source->source, fallback_range(*source->source)),
        .severity = DiagnosticSeverity::Information,
        .code = std::string{kDiagnosticDetachedSourceUnit},
        .source = "ahfl",
        .message = "this file is not part of an AHFL package; create ahfl.toml or open a "
                   "workspace containing one to enable std imports and workspace navigation",
    });

    if (source->program == nullptr) {
        return;
    }

    const auto primitive_uses = primitive_type_uses_in_program(*source->program);
    if (!primitive_uses.empty() && !has_toolchain_profile) {
        snapshot.project_diagnostics.push_back(LspDiagnostic{
            .range = to_lsp_range(*source->source, primitive_uses.front().range),
            .severity = DiagnosticSeverity::Warning,
            .code = std::string{kDiagnosticPrimitiveHomeUnavailable},
            .source = "ahfl",
            .message = "active AHFL toolchain profile is unavailable; primitive canonical home "
                       "navigation is disabled",
        });
    } else if (primitive_index != nullptr) {
        for (const auto &use : primitive_uses) {
            if (primitive_index->home_location_for_primitive(use.kind).has_value()) {
                continue;
            }
            std::string message = "active sysroot does not provide canonical home for primitive ";
            message += primitive_kind_name(use.kind);
            if (const auto expected = primitive_index->missing_home_path_for_primitive(use.kind);
                expected.has_value()) {
                message += " at '";
                message += expected->generic_string();
                message += "'";
            }
            message += "; primitive navigation may use a virtual home or be unavailable";
            snapshot.project_diagnostics.push_back(LspDiagnostic{
                .range = to_lsp_range(*source->source, use.range),
                .severity = DiagnosticSeverity::Warning,
                .code = std::string{kDiagnosticPrimitiveHomeUnavailable},
                .source = "ahfl",
                .message = std::move(message),
            });
        }
    }

    for (const auto &decl : source->program->declarations) {
        if (decl == nullptr || decl->kind != ast::NodeKind::ImportDecl) {
            continue;
        }
        const auto &import_decl = static_cast<const ast::ImportDecl &>(*decl);
        std::string message = "import declarations require an AHFL package manifest";
        if (import_decl.path != nullptr) {
            message += ": " + import_decl.path->spelling();
        }
        snapshot.project_diagnostics.push_back(LspDiagnostic{
            .range = to_lsp_range(*source->source, import_decl.range),
            .severity = DiagnosticSeverity::Error,
            .code = std::string{kDiagnosticDetachedImport},
            .source = "ahfl",
            .message = std::move(message),
        });
    }
}

void index_source(LspAnalysisSnapshot &snapshot, LspSourceSnapshot source) {
    const auto index = snapshot.sources.size();
    if (!source.uri.empty()) {
        snapshot.source_by_uri[source.uri] = index;
    }
    if (source.source != nullptr) {
        snapshot.source_by_display_name[source.source->display_name] = index;
    }
    if (!source.path.empty()) {
        snapshot.source_by_display_name[source.path.generic_string()] = index;
        snapshot.source_by_display_name[display_path(source.path)] = index;
    }
    if (source.source_id.has_value()) {
        snapshot.source_by_id[source.source_id->value] = index;
    }
    snapshot.sources.push_back(std::move(source));
}

[[nodiscard]] bool same_range(SourceRange lhs, SourceRange rhs) noexcept {
    return lhs.begin_offset == rhs.begin_offset && lhs.end_offset == rhs.end_offset;
}

void build_workspace_def_remap(LspAnalysisSnapshot &snapshot) {
    snapshot.workspace_def_by_symbol.clear();
    if (snapshot.workspace_index == nullptr) {
        return;
    }

    const auto &index_symbols = snapshot.workspace_index->symbols();
    for (const auto &semantic_symbol : snapshot.resolve_result.symbol_table.symbols()) {
        if (!semantic_symbol.source_id.has_value()) {
            continue;
        }
        const auto *source = snapshot.source_for_id(*semantic_symbol.source_id);
        if (source == nullptr) {
            continue;
        }

        const auto index_symbol =
            std::find_if(index_symbols.begin(), index_symbols.end(), [&](const SymbolFact &fact) {
                return fact.kind == semantic_symbol.kind && fact.location.uri == source->uri &&
                       same_range(fact.declaration_range, semantic_symbol.declaration_range);
            });
        if (index_symbol != index_symbols.end()) {
            snapshot.workspace_def_by_symbol.emplace(semantic_symbol.id.value,
                                                     index_symbol->def_id);
        }
    }
}

} // namespace

const LspSourceSnapshot *LspAnalysisSnapshot::source_for_uri(std::string_view uri) const {
    const auto it = source_by_uri.find(std::string(uri));
    if (it == source_by_uri.end()) {
        return nullptr;
    }
    return &sources[it->second];
}

const LspSourceSnapshot *LspAnalysisSnapshot::source_for_id(SourceId id) const {
    const auto it = source_by_id.find(id.value);
    if (it == source_by_id.end()) {
        return nullptr;
    }
    return &sources[it->second];
}

const LspSourceSnapshot *LspAnalysisSnapshot::source_for_display_name(std::string_view name) const {
    const auto it = source_by_display_name.find(std::string(name));
    if (it == source_by_display_name.end()) {
        return nullptr;
    }
    return &sources[it->second];
}

std::optional<DefId> LspAnalysisSnapshot::workspace_def_for_symbol(SymbolId symbol) const {
    const auto found = workspace_def_by_symbol.find(symbol.value);
    if (found == workspace_def_by_symbol.end()) {
        return std::nullopt;
    }
    return found->second;
}

const TypedProgram *LspAnalysisSnapshot::typed_program() const noexcept {
    return type_check_result ? &type_check_result->typed_program : nullptr;
}

std::vector<LspDiagnostic> LspAnalysisSnapshot::diagnostics_for_uri(std::string_view uri) const {
    std::vector<LspDiagnostic> diagnostics;

    if (uri == requested_uri) {
        diagnostics.insert(
            diagnostics.end(), project_diagnostics.begin(), project_diagnostics.end());
    }
    if (project_result) {
        collect_diagnostics_for_uri(
            diagnostics, project_result->diagnostics, *this, uri, "parse.diagnostic");
    }
    if (parse_result) {
        collect_diagnostics_for_uri(
            diagnostics, parse_result->diagnostics, *this, uri, "parse.diagnostic");
    }
    collect_diagnostics_for_uri(
        diagnostics, resolve_result.diagnostics, *this, uri, "resolve.diagnostic");
    if (type_check_result) {
        collect_diagnostics_for_uri(
            diagnostics, type_check_result->diagnostics, *this, uri, "typecheck.diagnostic");
    }
    if (validation_result) {
        collect_diagnostics_for_uri(
            diagnostics, validation_result->diagnostics, *this, uri, "validation.diagnostic");
    }

    return diagnostics;
}

AnalysisService::AnalysisService(const DocumentStore &store) : store_(store) {}

void AnalysisService::set_workspace_folders(std::vector<std::filesystem::path> roots) {
    workspace_folders_.clear();
    workspace_folders_.reserve(roots.size());
    for (const auto &root : roots) {
        if (!root.empty()) {
            workspace_folders_.push_back(std::filesystem::path(normalized_path_key(root)));
        }
    }
    invalidate_all();
}

void AnalysisService::set_toolchain_profiles(project_discovery::ToolchainProfileSet profiles) {
    toolchain_profiles_ = std::move(profiles);
    invalidate_all();
}

void AnalysisService::invalidate_all() {
    cache_.clear();
    sysroot_primitive_index_cache_.clear();
    sysroot_index_cache_.clear();
    workspace_root_index_cache_.clear();
}

void AnalysisService::invalidate_paths(const std::vector<std::filesystem::path> &paths) {
    std::unordered_set<std::string> path_keys;
    path_keys.reserve(paths.size());
    for (const auto &path : paths) {
        const auto normalized = std::filesystem::path(normalized_path_key(path));
        if (is_project_manifest_path(normalized)) {
            invalidate_all();
            return;
        }
        path_keys.insert(normalized.generic_string());
    }
    if (path_keys.empty()) {
        return;
    }
    sysroot_primitive_index_cache_.clear();

    for (auto iter = cache_.begin(); iter != cache_.end();) {
        if (iter->second != nullptr && snapshot_references_path(*iter->second, path_keys)) {
            iter = cache_.erase(iter);
        } else {
            ++iter;
        }
    }

    for (auto iter = sysroot_index_cache_.begin(); iter != sysroot_index_cache_.end();) {
        if (iter->second != nullptr && source_units_reference_path(*iter->second, path_keys)) {
            iter = sysroot_index_cache_.erase(iter);
        } else {
            ++iter;
        }
    }

    for (auto iter = workspace_root_index_cache_.begin();
         iter != workspace_root_index_cache_.end();) {
        if (iter->second != nullptr && source_units_reference_path(*iter->second, path_keys)) {
            iter = workspace_root_index_cache_.erase(iter);
        } else {
            ++iter;
        }
    }
}

const LspAnalysisSnapshot *AnalysisService::snapshot_for_uri(const std::string &uri) {
    const auto *document = store_.get(uri);
    const auto revision = store_.revision(uri);
    const auto hash = store_.content_hash(uri);
    if (document == nullptr || !revision.has_value() || !hash.has_value()) {
        return nullptr;
    }

    const auto toolchain_cache_key = toolchain_cache_key_for_uri(uri);
    const LspWorkspaceIndex *previous_index = nullptr;
    if (const auto existing = cache_.find(uri); existing != cache_.end()) {
        const auto &snapshot = *existing->second;
        const auto dependency_overlay_revision_set =
            open_document_overlay_revision_set_for_snapshot(snapshot);
        if (snapshot.document_version == document->version &&
            snapshot.document_revision == *revision && snapshot.content_hash == *hash &&
            snapshot.open_document_overlay_revision_set == dependency_overlay_revision_set &&
            snapshot.toolchain_cache_key == toolchain_cache_key) {
            return existing->second.get();
        }
        previous_index = snapshot.workspace_index.get();
    }

    auto snapshot = build_snapshot(uri, toolchain_cache_key, previous_index);
    if (!snapshot) {
        return nullptr;
    }

    ++analysis_runs_;
    auto *snapshot_ptr = snapshot.get();
    cache_[uri] = std::move(snapshot);
    return snapshot_ptr;
}

const SysrootPrimitiveIndex *
AnalysisService::sysroot_primitive_index_for_uri(const std::string &uri) {
    const auto key = toolchain_cache_key_for_uri(uri);
    if (!key.has_value() || key->std_manifest.empty()) {
        return nullptr;
    }

    const auto cache_key = sysroot_primitive_index_cache_key(*key);
    if (const auto existing = sysroot_primitive_index_cache_.find(cache_key);
        existing != sysroot_primitive_index_cache_.end()) {
        return existing->second.get();
    }

    auto index = std::make_unique<SysrootPrimitiveIndex>(build_sysroot_primitive_index(
        SysrootPrimitiveIndexInput{.std_manifest = std::filesystem::path(key->std_manifest)}));
    const auto *result = index.get();
    sysroot_primitive_index_cache_[cache_key] = std::move(index);
    return result;
}

const LspWorkspaceIndex *AnalysisService::sysroot_index_for_uri(const std::string &uri) {
    const auto key = toolchain_cache_key_for_uri(uri);
    if (!key.has_value()) {
        return nullptr;
    }

    const auto document_path = path_from_uri(uri);
    if (!document_path.has_value()) {
        return nullptr;
    }

    auto graph_result =
        package_graph::build_package_graph_from_sysroot(package_graph::SysrootBuildInput{
            .sysroot_manifest_path = std::filesystem::path(key->std_manifest),
        });
    if (graph_result.has_errors() || !graph_result.graph.has_value()) {
        return nullptr;
    }
    auto &graph = *graph_result.graph;

    Frontend frontend;
    NavigationScopeKindMap sysroot_scope_kinds;
    auto project_input = project_input_from_package_graph(graph,
                                                          *document_path,
                                                          open_document_overlays(),
                                                          LspProjectInputMode::SysrootIndex,
                                                          &sysroot_scope_kinds);
    const auto overlay_revision_set =
        open_document_overlay_revision_set_for_paths(paths_from_scope_kinds(sysroot_scope_kinds));
    const auto cache_key = sysroot_index_cache_key(*key, overlay_revision_set);
    if (const auto existing = sysroot_index_cache_.find(cache_key);
        existing != sysroot_index_cache_.end()) {
        return existing->second.get();
    }
    const auto cache_key_prefix = sysroot_index_cache_key_prefix(*key);
    const auto *previous_index =
        previous_index_for_cache_prefix(sysroot_index_cache_, cache_key_prefix);
    auto index_input = LspWorkspaceIndexInput{
        .project = std::move(project_input),
        .scope =
            NavigationIndexScope{
                .package_roots = index_package_roots_from_graph(graph),
                .source_units = index_source_units_from_scope_kinds(graph,
                                                                    sysroot_scope_kinds,
                                                                    extra_source_unit_ids_by_path_,
                                                                    next_extra_source_unit_id_),
            },
        .metadata =
            NavigationIndexMetadata{
                .revision = store_.workspace_revision(),
                .index_schema_version = std::string{kWorkspaceIndexSchemaVersion},
                .index_identity_schema_version = std::string{kWorkspaceIndexIdentitySchemaVersion},
            },
        .previous_index = previous_index,
    };
    auto index = std::make_unique<LspWorkspaceIndex>(
        build_lsp_workspace_index(frontend, std::move(index_input)));
    const auto *result = index.get();
    sysroot_index_cache_[cache_key] = std::move(index);
    return result;
}

std::vector<const LspWorkspaceIndex *> AnalysisService::workspace_root_indices() {
    std::vector<const LspWorkspaceIndex *> indices;

    std::vector<project_discovery::WorkspaceBoundary> workspace_boundaries;
    workspace_boundaries.reserve(workspace_folders_.size());
    for (const auto &root : workspace_folders_) {
        workspace_boundaries.push_back(project_discovery::WorkspaceBoundary{.root = root});
    }

    for (const auto &root : workspace_folders_) {
        auto project_context =
            project_discovery::discover_project_context(project_discovery::ProjectDiscoveryInput{
                .document_path = root,
                .workspace_boundaries = workspace_boundaries,
                .toolchains = toolchain_profiles_,
            });
        if (!project_context.context.has_value()) {
            continue;
        }

        Frontend frontend;
        NavigationScopeKindMap workspace_scope_kinds;
        auto project_input = project_input_from_package_graph(project_context.context->graph,
                                                              root,
                                                              open_document_overlays(),
                                                              LspProjectInputMode::WorkspaceIndex,
                                                              &workspace_scope_kinds);
        const auto overlay_revision_set = open_document_overlay_revision_set_for_paths(
            paths_from_scope_kinds(workspace_scope_kinds));
        const auto cache_key = workspace_root_index_cache_key(
            root, project_context.context->graph, overlay_revision_set);
        if (const auto existing = workspace_root_index_cache_.find(cache_key);
            existing != workspace_root_index_cache_.end()) {
            indices.push_back(existing->second.get());
            continue;
        }
        const auto cache_key_prefix =
            workspace_root_index_cache_key_prefix(root, project_context.context->graph);
        const auto *previous_index =
            previous_index_for_cache_prefix(workspace_root_index_cache_, cache_key_prefix);
        auto index_input = LspWorkspaceIndexInput{
            .project = std::move(project_input),
            .scope =
                NavigationIndexScope{
                    .package_roots = index_package_roots_from_graph(project_context.context->graph),
                    .source_units =
                        index_source_units_from_scope_kinds(project_context.context->graph,
                                                            workspace_scope_kinds,
                                                            extra_source_unit_ids_by_path_,
                                                            next_extra_source_unit_id_),
                },
            .metadata =
                NavigationIndexMetadata{
                    .revision = store_.workspace_revision(),
                    .index_schema_version = std::string{kWorkspaceIndexSchemaVersion},
                    .index_identity_schema_version =
                        std::string{kWorkspaceIndexIdentitySchemaVersion},
                },
            .previous_index = previous_index,
        };

        auto index = std::make_unique<LspWorkspaceIndex>(
            build_lsp_workspace_index(frontend, std::move(index_input)));
        const auto *result = index.get();
        workspace_root_index_cache_[cache_key] = std::move(index);
        indices.push_back(result);
    }

    return indices;
}

std::vector<const LspAnalysisSnapshot *> AnalysisService::workspace_snapshots() {
    std::vector<const LspAnalysisSnapshot *> snapshots;
    for (const auto &uri : store_.all_uris()) {
        if (const auto *snapshot = snapshot_for_uri(uri); snapshot != nullptr) {
            snapshots.push_back(snapshot);
        }
    }
    return snapshots;
}

std::size_t AnalysisService::analysis_runs() const noexcept {
    return analysis_runs_;
}

std::optional<std::filesystem::path> AnalysisService::path_from_uri(std::string_view uri) {
    constexpr std::string_view kFilePrefix = "file://";
    if (!uri.starts_with(kFilePrefix)) {
        return std::nullopt;
    }

    auto path_part = uri.substr(kFilePrefix.size());
    if (!path_part.empty() && path_part.front() != '/') {
        const auto slash = path_part.find('/');
        if (slash == std::string_view::npos) {
            return std::nullopt;
        }
        path_part = path_part.substr(slash);
    }

    return std::filesystem::path(percent_decode(path_part));
}

std::string AnalysisService::uri_from_path(const std::filesystem::path &path) {
    const auto normalized = std::filesystem::path(normalized_path_key(path)).generic_string();
    return "file://" + percent_encode_path(normalized);
}

std::string AnalysisService::normalized_path_key(const std::filesystem::path &path) {
    std::error_code error;
    const auto absolute = std::filesystem::absolute(path, error);
    auto candidate = error ? path.lexically_normal() : absolute.lexically_normal();
    const auto canonical = std::filesystem::weakly_canonical(candidate, error);
    if (!error) {
        candidate = canonical.lexically_normal();
    }
    return candidate.generic_string();
}

std::optional<LspToolchainCacheKey>
AnalysisService::toolchain_cache_key_for_uri(const std::string &uri) const {
    const auto document_path = path_from_uri(uri);
    if (!document_path.has_value()) {
        return std::nullopt;
    }

    const auto normalized_document = std::filesystem::path(normalized_path_key(*document_path));

    std::vector<project_discovery::WorkspaceBoundary> workspace_boundaries;
    workspace_boundaries.reserve(workspace_folders_.size());
    std::optional<std::filesystem::path> workspace_root;
    for (const auto &root : workspace_folders_) {
        const auto normalized_root = std::filesystem::path(normalized_path_key(root));
        workspace_boundaries.push_back(
            project_discovery::WorkspaceBoundary{.root = normalized_root});
        if (path_is_equal_or_descendant(normalized_document, normalized_root) &&
            (!workspace_root.has_value() ||
             normalized_root.generic_string().size() > workspace_root->generic_string().size())) {
            workspace_root = normalized_root;
        }
    }

    const auto selection = project_discovery::select_toolchain_profile_for_document(
        toolchain_profiles_, normalized_document);
    if (!selection.has_value()) {
        return std::nullopt;
    }
    const auto project_context =
        project_discovery::discover_project_context(project_discovery::ProjectDiscoveryInput{
            .document_path = normalized_document,
            .workspace_boundaries = workspace_boundaries,
            .toolchains = toolchain_profiles_,
        });
    auto analysis_mode = LspAnalysisMode::DetachedSourceUnit;
    if (project_context.analysis_context.has_value()) {
        analysis_mode = analysis_mode_from_discovery(project_context.analysis_context->kind);
    }

    return LspToolchainCacheKey{
        .analysis_mode = std::string{analysis_mode_name(analysis_mode)},
        .workspace_folder_uri =
            workspace_root.has_value() ? uri_from_path(*workspace_root) : std::string{},
        .root_manifest = project_context.context.has_value()
                             ? normalized_path_key(project_context.context->package_manifest_path)
                             : std::string{},
        .workspace_manifest =
            project_context.context.has_value() &&
                    project_context.context->workspace_manifest_path.has_value()
                ? normalized_path_key(*project_context.context->workspace_manifest_path)
                : std::string{},
        .package_graph_identity = project_context.context.has_value()
                                      ? package_graph_identity(project_context.context->graph)
                                      : std::string{},
        .std_manifest = normalized_path_key(selection->profile.std_manifest),
        .std_identity = selection->profile.std_identity,
        .scope = std::string{toolchain_scope_name(selection->profile.scope)},
        .index_schema_version = std::string{kWorkspaceIndexSchemaVersion},
        .index_identity_schema_version = std::string{kWorkspaceIndexIdentitySchemaVersion},
    };
}

std::unique_ptr<LspAnalysisSnapshot>
AnalysisService::build_snapshot(const std::string &uri,
                                std::optional<LspToolchainCacheKey> toolchain_cache_key,
                                const LspWorkspaceIndex *previous_index) {
    const auto *document = store_.get(uri);
    const auto revision = store_.revision(uri);
    const auto hash = store_.content_hash(uri);
    if (document == nullptr || !revision.has_value() || !hash.has_value()) {
        return nullptr;
    }

    auto snapshot = std::make_unique<LspAnalysisSnapshot>();
    snapshot->requested_uri = uri;
    snapshot->document_version = document->version;
    snapshot->document_revision = *revision;
    snapshot->content_hash = *hash;
    snapshot->workspace_revision = store_.workspace_revision();
    snapshot->toolchain_cache_key = std::move(toolchain_cache_key);

    const auto document_path = path_from_uri(uri);

    Frontend frontend;
    Resolver resolver;
    TypeChecker type_checker;
    Validator validator;

    if (document_path.has_value()) {
        std::vector<project_discovery::WorkspaceBoundary> workspace_boundaries;
        workspace_boundaries.reserve(workspace_folders_.size());
        for (const auto &root : workspace_folders_) {
            workspace_boundaries.push_back(project_discovery::WorkspaceBoundary{.root = root});
        }
        auto project_context =
            project_discovery::discover_project_context(project_discovery::ProjectDiscoveryInput{
                .document_path = *document_path,
                .workspace_boundaries = std::move(workspace_boundaries),
                .toolchains = toolchain_profiles_,
            });
        if (project_context.analysis_context.has_value()) {
            snapshot->analysis_mode =
                analysis_mode_from_discovery(project_context.analysis_context->kind);
        }
        if (project_context.context.has_value()) {
            snapshot->project_aware = true;
            snapshot->package_graph_manifest = project_context.context->graph_manifest_path;

            auto overlays = open_document_overlays();
            auto project_input = project_input_from_package_graph(project_context.context->graph,
                                                                  *document_path,
                                                                  overlays,
                                                                  LspProjectInputMode::Semantic);
            NavigationScopeKindMap workspace_scope_kinds;
            auto index_input = LspWorkspaceIndexInput{
                .project = project_input_from_package_graph(project_context.context->graph,
                                                            *document_path,
                                                            std::move(overlays),
                                                            LspProjectInputMode::WorkspaceIndex,
                                                            &workspace_scope_kinds),
                .scope =
                    NavigationIndexScope{
                        .package_roots =
                            index_package_roots_from_graph(project_context.context->graph),
                        .source_units =
                            index_source_units_from_scope_kinds(project_context.context->graph,
                                                                workspace_scope_kinds,
                                                                extra_source_unit_ids_by_path_,
                                                                next_extra_source_unit_id_),
                    },
                .metadata =
                    NavigationIndexMetadata{
                        .revision = snapshot->workspace_revision,
                        .index_schema_version = std::string{kWorkspaceIndexSchemaVersion},
                        .index_identity_schema_version =
                            std::string{kWorkspaceIndexIdentitySchemaVersion},
                    },
                .previous_index = previous_index,
            };
            auto project_result = ahfl::parse_project(frontend, project_input);
            snapshot->project_result =
                std::make_unique<ProjectParseResult>(std::move(project_result));
            seed_source_cache_from_graph(index_input.project, snapshot->project_result->graph);
            auto workspace_index = build_lsp_workspace_index(frontend, std::move(index_input));
            snapshot->workspace_index =
                std::make_unique<LspWorkspaceIndex>(std::move(workspace_index));

            for (const auto &source : snapshot->project_result->graph.sources) {
                index_source(*snapshot,
                             LspSourceSnapshot{
                                 .uri = uri_from_path(source.path),
                                 .path = source.path,
                                 .source = &source.source,
                                 .program = source.program.get(),
                                 .source_id = source.id,
                             });
            }

            if (!snapshot->project_result->has_errors()) {
                snapshot->resolve_result = resolver.resolve(snapshot->project_result->graph);
                build_workspace_def_remap(*snapshot);
                if (!snapshot->resolve_result.has_errors()) {
                    auto type_result = type_checker.check(snapshot->project_result->graph,
                                                          snapshot->resolve_result);
                    snapshot->type_check_result =
                        std::make_unique<TypeCheckResult>(std::move(type_result));
                    if (!snapshot->type_check_result->has_errors()) {
                        auto validation_result = validator.validate(snapshot->project_result->graph,
                                                                    snapshot->resolve_result,
                                                                    *snapshot->type_check_result);
                        snapshot->validation_result =
                            std::make_unique<ValidationResult>(std::move(validation_result));
                    }
                }
            }

            build_hover_indices(*snapshot);
            snapshot->open_document_overlay_revision_set =
                open_document_overlay_revision_set_for_snapshot(*snapshot);
            return snapshot;
        }

        if (project_context.project_manifest_found || project_context.has_errors()) {
            snapshot->project_aware = project_context.project_manifest_found;
            auto parse_result = frontend.parse_text(document->uri, document->text);
            snapshot->parse_result = std::make_unique<ParseResult>(std::move(parse_result));
            index_source(*snapshot,
                         LspSourceSnapshot{
                             .uri = uri,
                             .path = *document_path,
                             .source = &snapshot->parse_result->source,
                             .program = snapshot->parse_result->program.get(),
                             .source_id = std::nullopt,
                         });
            append_project_diagnostics(*snapshot, uri, project_context.diagnostics);
            build_hover_indices(*snapshot);
            snapshot->open_document_overlay_revision_set =
                open_document_overlay_revision_set_for_snapshot(*snapshot);
            return snapshot;
        }
    }

    auto parse_result = frontend.parse_text(document->uri, document->text);
    snapshot->parse_result = std::make_unique<ParseResult>(std::move(parse_result));
    index_source(*snapshot,
                 LspSourceSnapshot{
                     .uri = uri,
                     .path = document_path.value_or(std::filesystem::path{}),
                     .source = &snapshot->parse_result->source,
                     .program = snapshot->parse_result->program.get(),
                     .source_id = std::nullopt,
                 });
    if (snapshot->analysis_mode == LspAnalysisMode::DetachedSourceUnit) {
        const auto *primitive_index = snapshot->toolchain_cache_key.has_value()
                                          ? sysroot_primitive_index_for_uri(uri)
                                          : nullptr;
        append_detached_source_unit_diagnostics(
            *snapshot, uri, primitive_index, snapshot->toolchain_cache_key.has_value());
    }

    if (!snapshot->parse_result->has_errors() && snapshot->parse_result->program) {
        snapshot->resolve_result = resolver.resolve(*snapshot->parse_result->program);
        if (!snapshot->resolve_result.has_errors()) {
            auto type_result =
                type_checker.check(*snapshot->parse_result->program, snapshot->resolve_result);
            snapshot->type_check_result = std::make_unique<TypeCheckResult>(std::move(type_result));
            if (!snapshot->type_check_result->has_errors()) {
                auto validation_result = validator.validate(*snapshot->parse_result->program,
                                                            snapshot->resolve_result,
                                                            *snapshot->type_check_result);
                snapshot->validation_result =
                    std::make_unique<ValidationResult>(std::move(validation_result));
            }
        }
    }

    build_hover_indices(*snapshot);
    snapshot->open_document_overlay_revision_set =
        open_document_overlay_revision_set_for_snapshot(*snapshot);
    return snapshot;
}

std::unordered_map<std::string, std::string> AnalysisService::open_document_overlays() const {
    std::unordered_map<std::string, std::string> overlays;
    for (const auto &uri : store_.all_uris()) {
        const auto *document = store_.get(uri);
        const auto path = path_from_uri(uri);
        if (document == nullptr || !path.has_value()) {
            continue;
        }
        overlays.emplace(normalized_path_key(*path), document->text);
    }
    return overlays;
}

std::string AnalysisService::open_document_overlay_revision_set_for_paths(
    const std::vector<std::filesystem::path> &paths) const {
    std::unordered_set<std::string> path_keys;
    path_keys.reserve(paths.size());
    for (const auto &path : paths) {
        path_keys.insert(std::filesystem::path(normalized_path_key(path)).generic_string());
    }

    std::vector<std::string> uris;
    uris.reserve(paths.size());
    for (const auto &uri : store_.all_uris()) {
        const auto path = path_from_uri(uri);
        if (!path.has_value()) {
            continue;
        }
        if (path_keys.contains(
                std::filesystem::path(normalized_path_key(*path)).generic_string())) {
            uris.push_back(uri);
        }
    }
    std::sort(uris.begin(), uris.end());
    uris.erase(std::unique(uris.begin(), uris.end()), uris.end());

    std::string key;
    for (const auto &uri : uris) {
        const auto revision = store_.revision(uri);
        const auto hash = store_.content_hash(uri);
        if (!revision.has_value() || !hash.has_value()) {
            continue;
        }
        key += uri;
        key += "@";
        key += std::to_string(*revision);
        key += ":";
        key += std::to_string(*hash);
        key += ";";
    }
    return key;
}

std::string AnalysisService::open_document_overlay_revision_set_for_snapshot(
    const LspAnalysisSnapshot &snapshot) const {
    std::vector<std::filesystem::path> paths;
    paths.reserve(snapshot.sources.size() +
                  (snapshot.workspace_index == nullptr
                       ? std::size_t{0}
                       : snapshot.workspace_index->source_units().size()));
    for (const auto &source : snapshot.sources) {
        if (!source.path.empty()) {
            paths.push_back(source.path);
        }
    }
    if (snapshot.workspace_index != nullptr) {
        for (const auto &source_unit : snapshot.workspace_index->source_units()) {
            if (source_unit.valid && !source_unit.path.empty()) {
                paths.push_back(source_unit.path);
            }
        }
    }
    return open_document_overlay_revision_set_for_paths(paths);
}

} // namespace ahfl::lsp
