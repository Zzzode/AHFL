#include "ahfl/compiler/ir/verify.hpp"

#include "ahfl/base/support/overloaded.hpp"
#include "ahfl/compiler/ir/analysis.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>

namespace ahfl::ir {
namespace {

[[nodiscard]] bool has_any_analysis(const AnalysisBundle &analyses) noexcept {
    return analyses.source_program_revision != 0 || !analyses.state_handler_summaries.empty() ||
           !analyses.workflow_node_input_summaries.empty() ||
           !analyses.workflow_return_summaries.empty() || !analyses.formal_observations.empty();
}

[[nodiscard]] std::string symbol_ref_name(const SymbolRef &symbol) {
    if (!symbol.canonical_name.empty()) {
        return symbol.canonical_name;
    }
    if (!symbol.local_name.empty()) {
        return symbol.local_name;
    }
    return "<unknown>";
}

[[nodiscard]] std::string_view symbol_ref_kind_name(SymbolRefKind kind) noexcept {
    switch (kind) {
    case SymbolRefKind::Unknown:
        return "unknown";
    case SymbolRefKind::Type:
        return "type";
    case SymbolRefKind::Const:
        return "const";
    case SymbolRefKind::Capability:
        return "capability";
    case SymbolRefKind::Predicate:
        return "predicate";
    case SymbolRefKind::Agent:
        return "agent";
    case SymbolRefKind::Workflow:
        return "workflow";
    case SymbolRefKind::Function:
        return "function";
    }
    return "unknown";
}

[[nodiscard]] bool is_backend_ready_mode(IrVerificationMode mode) noexcept {
    return mode == IrVerificationMode::BackendReady ||
           mode == IrVerificationMode::SerializedArtifact;
}

[[nodiscard]] bool contains_sentinel(std::string_view value) noexcept {
    return value.find("<missing-") != std::string_view::npos ||
           value.find("<invalid-") != std::string_view::npos;
}

[[nodiscard]] bool canonical_matches_decl_name(std::string_view canonical,
                                               std::string_view decl_name) noexcept {
    if (canonical == decl_name) {
        return true;
    }
    const auto pos = canonical.rfind("::");
    return pos != std::string_view::npos && canonical.substr(pos + 2) == decl_name;
}

[[nodiscard]] std::string
analysis_key(std::string_view owner, std::size_t index, std::string_view name = {}) {
    std::string key;
    key.reserve(owner.size() + name.size() + 32);
    key += owner;
    key += '\x1f';
    key += std::to_string(index);
    key += '\x1f';
    key += name;
    return key;
}

struct SymbolIdentity {
    std::string name;
    SymbolRefKind kind{SymbolRefKind::Unknown};
};

class ProgramVerifier {
  public:
    ProgramVerifier(const Program &program, IrVerificationMode mode)
        : program_(program), mode_(mode) {}

    [[nodiscard]] VerificationResult run() {
        verify_expression_arena();
        collect_decl_symbol_identities();
        verify_declarations();
        verify_analysis_phase();
        return std::move(result_);
    }

  private:
    void add(VerificationSeverity severity, std::string path, std::string message) {
        result_.diagnostics.push_back(VerificationDiagnostic{
            .severity = severity,
            .path = std::move(path),
            .message = std::move(message),
        });
    }

    void add_error(std::string path, std::string message) {
        add(VerificationSeverity::Error, std::move(path), std::move(message));
    }

    void add_warning(std::string path, std::string message) {
        add(VerificationSeverity::Warning, std::move(path), std::move(message));
    }

    void verify_expression_arena() {
        std::unordered_set<std::uint32_t> expr_ids;
        expr_ids.reserve(program_.expr_arena.size());
        const auto expressions = program_.all_exprs();
        for (std::uint32_t index = 0; index < expressions.size(); ++index) {
            const auto *expr = expressions[index];
            if (expr == nullptr) {
                add_error("expr_arena[" + std::to_string(index) + "]",
                          "arena expression pointer is null");
                continue;
            }
            if (!expr_ids.insert(expr->id).second) {
                add_error("expr_arena[" + std::to_string(index) + "]",
                          "duplicate expression id " + std::to_string(expr->id));
            }
            verify_source_range(expr->source_range,
                                "expr_arena[" + std::to_string(index) + "]",
                                "expression source range");
            verify_type_ref(expr->resolved_type,
                            "expr_arena[" + std::to_string(index) + "].resolved_type");
            verify_expr_children(*expr, "expr_arena[" + std::to_string(index) + "]");
        }
    }

    void collect_decl_symbol_identities() {
        // RFC 0027 P6/P7/P8 (KR6.13-F): one handler per Decl alternative,
        // generated from decl_nodes.def. Declarations that define no addressable
        // symbol (module / import / contract / flow / impl) name the
        // explicitly-named COLLECT_DECL_LEAF no-op; the rest route to a per-node
        // collector. No unnamed catch-all, so a new declaration node is a
        // COMPILE ERROR here until it is routed (CLAUDE.md Principle 5).
#define COLLECT_DECL_LEAF(Name) [this](const Name &) {},
#define COLLECT_DECL_SYMBOL(Name)                                                               \
    [this](const Name &value) { collect_decl_symbol_identity(value); },

#define COLLECT_ModuleDecl(Name) COLLECT_DECL_LEAF(Name)
#define COLLECT_ImportDecl(Name) COLLECT_DECL_LEAF(Name)
#define COLLECT_ConstDecl(Name) COLLECT_DECL_SYMBOL(Name)
#define COLLECT_TypeAliasDecl(Name) COLLECT_DECL_SYMBOL(Name)
#define COLLECT_StructDecl(Name) COLLECT_DECL_SYMBOL(Name)
#define COLLECT_EnumDecl(Name) COLLECT_DECL_SYMBOL(Name)
#define COLLECT_CapabilityDecl(Name) COLLECT_DECL_SYMBOL(Name)
#define COLLECT_PredicateDecl(Name) COLLECT_DECL_SYMBOL(Name)
#define COLLECT_AgentDecl(Name) COLLECT_DECL_SYMBOL(Name)
#define COLLECT_ContractDecl(Name) COLLECT_DECL_LEAF(Name)
#define COLLECT_FlowDecl(Name) COLLECT_DECL_LEAF(Name)
#define COLLECT_WorkflowDecl(Name) COLLECT_DECL_SYMBOL(Name)
#define COLLECT_FnDecl(Name) COLLECT_DECL_SYMBOL(Name)
#define COLLECT_TraitDecl(Name) COLLECT_DECL_SYMBOL(Name)
#define COLLECT_ImplDecl(Name) COLLECT_DECL_LEAF(Name)
#define COLLECT_InstanceDecl(Name) COLLECT_DECL_SYMBOL(Name)
#define HANDLE_DECL_NODE(Name) COLLECT_##Name(Name)
        for (const auto &decl : program_.declarations) {
            std::visit(
                Overloaded{
#include "ahfl/compiler/ir/decl_nodes.def"
                },
                decl);
        }
#undef HANDLE_DECL_NODE
#undef COLLECT_ModuleDecl
#undef COLLECT_ImportDecl
#undef COLLECT_ConstDecl
#undef COLLECT_TypeAliasDecl
#undef COLLECT_StructDecl
#undef COLLECT_EnumDecl
#undef COLLECT_CapabilityDecl
#undef COLLECT_PredicateDecl
#undef COLLECT_AgentDecl
#undef COLLECT_ContractDecl
#undef COLLECT_FlowDecl
#undef COLLECT_WorkflowDecl
#undef COLLECT_FnDecl
#undef COLLECT_TraitDecl
#undef COLLECT_ImplDecl
#undef COLLECT_InstanceDecl
#undef COLLECT_DECL_SYMBOL
#undef COLLECT_DECL_LEAF
    }

    void collect_decl_symbol_identity(const ConstDecl &decl) {
        collect_symbol_identity(decl.symbol_ref, "const " + decl.name);
    }
    void collect_decl_symbol_identity(const TypeAliasDecl &decl) {
        collect_symbol_identity(decl.symbol_ref, "type " + decl.name);
    }
    void collect_decl_symbol_identity(const StructDecl &decl) {
        collect_symbol_identity(decl.symbol_ref, "struct " + decl.name);
    }
    void collect_decl_symbol_identity(const EnumDecl &decl) {
        collect_symbol_identity(decl.symbol_ref, "enum " + decl.name);
    }
    void collect_decl_symbol_identity(const CapabilityDecl &decl) {
        collect_symbol_identity(decl.symbol_ref, "capability " + decl.name);
    }
    void collect_decl_symbol_identity(const PredicateDecl &decl) {
        collect_symbol_identity(decl.symbol_ref, "predicate " + decl.name);
    }
    void collect_decl_symbol_identity(const AgentDecl &decl) {
        collect_symbol_identity(decl.symbol_ref, "agent " + decl.name);
    }
    void collect_decl_symbol_identity(const WorkflowDecl &decl) {
        collect_symbol_identity(decl.symbol_ref, "workflow " + decl.name);
    }
    // P2c (RFC §3.2.2): collect a fn declaration's self symbol reference.
    void collect_decl_symbol_identity(const FnDecl &decl) {
        collect_symbol_identity(decl.symbol_ref, "fn " + decl.name);
    }
    // KR6.13-F: a trait declaration's `symbol_ref` is the trait's own symbol.
    // The generic catch-all that used to route every unlisted declaration to a
    // no-op silently skipped it (the class this slice removes); listing it here
    // ties the trait's self symbol into the cross-declaration identity check,
    // exactly as the fn / agent / workflow self symbols already are.
    void collect_decl_symbol_identity(const TraitDecl &decl) {
        collect_symbol_identity(decl.symbol_ref, "trait " + decl.name);
    }

    void collect_decl_symbol_identity(const InstanceDecl &decl) {
        collect_symbol_identity(decl.symbol_ref, "instance " + decl.name);
    }

    void collect_symbol_identity(const SymbolRef &symbol, const std::string &path) {
        if (!symbol.id.has_value()) {
            return;
        }
        auto [iter, inserted] = symbol_identities_.emplace(
            *symbol.id, SymbolIdentity{.name = symbol_ref_name(symbol), .kind = symbol.kind});
        if (!inserted && iter->second.name != symbol_ref_name(symbol)) {
            add_error(path,
                      "symbol id " + std::to_string(*symbol.id) + " is also bound to " +
                          iter->second.name);
        }
        if (!inserted && iter->second.kind != symbol.kind) {
            add_error(path,
                      "symbol id " + std::to_string(*symbol.id) + " has inconsistent kind " +
                          std::string(symbol_ref_kind_name(symbol.kind)) + " vs " +
                          std::string(symbol_ref_kind_name(iter->second.kind)));
        }
    }

    void verify_declarations() {
        std::unordered_set<std::uint32_t> provenance_ids;
        provenance_ids.reserve(program_.declarations.size());
        for (std::uint32_t index = 0; index < program_.declarations.size(); ++index) {
            std::visit(
                [this, &provenance_ids, index](const auto &value) {
                    const auto path = declaration_path(value, index);
                    verify_provenance(value.provenance, path, provenance_ids);
                    verify_decl(value, path);
                },
                program_.declarations[index]);
        }
    }

    template <typename DeclT>
    [[nodiscard]] std::string declaration_path(const DeclT &decl, std::uint32_t index) const {
        if constexpr (requires { decl.name; }) {
            if (!decl.name.empty()) {
                return "decl[" + std::to_string(index) + "](" + decl.name + ")";
            }
        }
        return "decl[" + std::to_string(index) + "]";
    }

    void verify_provenance(const DeclarationProvenance &provenance,
                           const std::string &path,
                           std::unordered_set<std::uint32_t> &ids) {
        if (!ids.insert(provenance.id).second) {
            add_error(path, "duplicate declaration provenance id " + std::to_string(provenance.id));
        }
        verify_source_range(provenance.source_range, path + ".provenance", "source range");
    }

    void verify_decl(const ModuleDecl & /*decl*/, const std::string & /*path*/) {}

    void verify_decl(const ImportDecl & /*decl*/, const std::string & /*path*/) {}

    void verify_decl(const ConstDecl &decl, const std::string &path) {
        verify_required_expr_ref(decl.value, path + ".value");
        verify_type_ref(decl.type_ref, path + ".type_ref");
        verify_symbol_ref(decl.symbol_ref, path + ".symbol_ref", SymbolRefKind::Const, decl.name);
    }

    void verify_decl(const TypeAliasDecl &decl, const std::string &path) {
        verify_type_ref(decl.aliased_type_ref, path + ".aliased_type_ref");
        verify_symbol_ref(decl.symbol_ref, path + ".symbol_ref", SymbolRefKind::Type, decl.name);
    }

    // RFC 0026 P4 (coercion): a generic nominal declaration's variance metadata
    // must be internally consistent BEFORE Core lowering (Codex P1-1): the
    // per-parameter variance vector MUST be exactly `type_param_count` long, and
    // every entry must be a legal Variance enumerator. The length check is
    // UNCONDITIONAL (Codex re-review P0): `type_param_count == 0` with an empty
    // vector is naturally legal (0 == 0), so a monomorphic legacy artifact still
    // passes, but a generic nominal (`type_param_count > 0`) with an empty vector
    // is REJECTED - the coercion verifier indexes variances by parameter position
    // and cannot accept a missing vector. This is a lower-neutral structural check
    // ONLY; builtin descriptor exact-match (arity/variance vs the std SSOT) is
    // intentionally NOT duplicated here - that lives in the Core consumption gate
    // so the AHFL-IR verifier never takes a reverse dependency on the
    // execution-layer `ir::core::builtin_nominal_table()`.
    void verify_variance_metadata(std::uint32_t type_param_count,
                                  const std::vector<Variance> &variances,
                                  const std::string &path) {
        // BackendReady-only: Structural mode stays permissive for partial / legacy
        // IR whose variance vectors are not yet materialized. A backend consuming
        // the IR (coercion verifier / Core lowering) requires the vector present.
        if (!is_backend_ready_mode(mode_)) {
            return;
        }
        if (variances.size() != type_param_count) {
            add_error(path + ".type_param_variances",
                      "variance vector length (" + std::to_string(variances.size()) +
                          ") must equal type_param_count (" + std::to_string(type_param_count) +
                          ")");
        }
        for (std::uint32_t index = 0; index < variances.size(); ++index) {
            const auto raw = static_cast<std::underlying_type_t<Variance>>(variances[index]);
            const bool legal = variances[index] == Variance::Invariant ||
                               variances[index] == Variance::Covariant ||
                               variances[index] == Variance::Contravariant;
            if (!legal) {
                add_error(path + ".type_param_variances[" + std::to_string(index) + "]",
                          "illegal variance enumerator value " + std::to_string(raw));
            }
        }
    }

    [[nodiscard]] std::optional<std::uint32_t> member_nominal_arity(const TypeRef &base) const {
        const auto same_identity = [](const SymbolRef &lhs, const SymbolRef &rhs) {
            if (lhs.kind != rhs.kind) {
                return false;
            }
            if (lhs.id.has_value() && rhs.id.has_value()) {
                return *lhs.id == *rhs.id;
            }
            return !lhs.canonical_name.empty() && lhs.canonical_name == rhs.canonical_name;
        };
        for (const auto &declaration : program_.declarations) {
            if (base.kind == TypeRefKind::Struct) {
                if (const auto *decl = std::get_if<StructDecl>(&declaration);
                    decl != nullptr && same_identity(base.nominal_ref, decl->symbol_ref)) {
                    return decl->type_param_count;
                }
            } else if (base.kind == TypeRefKind::Enum) {
                if (const auto *decl = std::get_if<EnumDecl>(&declaration);
                    decl != nullptr && same_identity(base.nominal_ref, decl->symbol_ref)) {
                    return decl->type_param_count;
                }
            }
        }
        // A resolved stdlib nominal may be referenced while its declaration is
        // intentionally not inlined into this Program. Core's builtin SSOT gate
        // performs the authoritative arity check for that case.
        return std::nullopt;
    }

    void verify_member_type_templates(const std::vector<MemberTypeTemplateNode> &nodes,
                                      const std::vector<std::uint32_t> &roots,
                                      std::uint32_t type_param_count,
                                      const std::string &path) {
        if (!is_backend_ready_mode(mode_)) {
            return;
        }

        std::vector<bool> reachable(nodes.size(), false);
        std::vector<std::uint32_t> worklist;
        const auto add_reference = [&](std::uint32_t child,
                                       std::uint32_t parent,
                                       const std::string &child_path) {
            if (child >= nodes.size()) {
                add_error(child_path,
                          "member template node id " + std::to_string(child) +
                              " is out of range for arena size " + std::to_string(nodes.size()));
                return;
            }
            if (child >= parent) {
                add_error(child_path,
                          "member template children must precede their parent in postorder");
            }
        };

        for (std::uint32_t index = 0; index < roots.size(); ++index) {
            const auto root = roots[index];
            const auto root_path = path + ".roots[" + std::to_string(index) + "]";
            if (root >= nodes.size()) {
                add_error(root_path,
                          "member template root " + std::to_string(root) +
                              " is out of range for arena size " + std::to_string(nodes.size()));
            } else {
                worklist.push_back(root);
            }
        }

        while (!worklist.empty()) {
            const auto id = worklist.back();
            worklist.pop_back();
            if (reachable[id]) {
                continue;
            }
            reachable[id] = true;
            const auto &node = nodes[id];
            for (const auto child : node.children) {
                if (child < nodes.size()) {
                    worklist.push_back(child);
                }
            }
            if (node.fn_return < nodes.size()) {
                worklist.push_back(node.fn_return);
            }
        }

        const TypeRef empty_type_ref{};
        for (std::uint32_t index = 0; index < nodes.size(); ++index) {
            const auto &node = nodes[index];
            const auto node_path = path + ".nodes[" + std::to_string(index) + "]";
            const auto raw = static_cast<std::underlying_type_t<MemberTypeTemplateKind>>(node.kind);
            const bool legal = node.kind == MemberTypeTemplateKind::Concrete ||
                               node.kind == MemberTypeTemplateKind::Param ||
                               node.kind == MemberTypeTemplateKind::Nominal ||
                               node.kind == MemberTypeTemplateKind::Fn;
            if (!legal) {
                add_error(node_path + ".kind",
                          "illegal member template kind value " + std::to_string(raw));
                continue;
            }
            if (!reachable[index]) {
                add_error(node_path, "orphan member template node is not reachable from any root");
            }

            switch (node.kind) {
            case MemberTypeTemplateKind::Concrete:
                verify_type_ref(node.type_ref, node_path + ".type_ref");
                if (node.type_ref.kind == TypeRefKind::Unresolved) {
                    add_error(node_path + ".type_ref", "concrete template type is unresolved");
                }
                if (node.param_index != 0 || !node.children.empty() ||
                    node.fn_return != kInvalidMemberTypeTemplateNode) {
                    add_error(node_path, "concrete template carries fields for another kind");
                }
                break;
            case MemberTypeTemplateKind::Param:
                if (!type_refs_equal(node.type_ref, empty_type_ref) || !node.children.empty() ||
                    node.fn_return != kInvalidMemberTypeTemplateNode) {
                    add_error(node_path, "parameter template carries fields for another kind");
                }
                if (node.param_index >= type_param_count) {
                    add_error(node_path + ".param_index",
                              "parameter index " + std::to_string(node.param_index) +
                                  " is out of range for declaration arity " +
                                  std::to_string(type_param_count));
                }
                break;
            case MemberTypeTemplateKind::Nominal:
                verify_type_ref(node.type_ref, node_path + ".type_ref");
                if ((node.type_ref.kind != TypeRefKind::Struct &&
                     node.type_ref.kind != TypeRefKind::Enum) ||
                    !node.type_ref.variant_name.empty() || !node.type_ref.params.empty() ||
                    node.type_ref.first != nullptr || node.type_ref.second != nullptr) {
                    add_error(node_path + ".type_ref",
                              "nominal template base must be a resolved Struct/Enum without "
                              "embedded type arguments");
                }
                if (node.param_index != 0 || node.fn_return != kInvalidMemberTypeTemplateNode) {
                    add_error(node_path, "nominal template carries fields for another kind");
                }
                if (const auto arity = member_nominal_arity(node.type_ref);
                    arity.has_value() && node.children.size() != *arity) {
                    add_error(node_path + ".children",
                              "nominal template argument count (" +
                                  std::to_string(node.children.size()) +
                                  ") must equal referenced declaration arity (" +
                                  std::to_string(*arity) + ")");
                }
                for (std::uint32_t child_index = 0; child_index < node.children.size();
                     ++child_index) {
                    add_reference(node.children[child_index],
                                  index,
                                  node_path + ".children[" + std::to_string(child_index) + "]");
                }
                break;
            case MemberTypeTemplateKind::Fn:
                if (!type_refs_equal(node.type_ref, empty_type_ref) || node.param_index != 0) {
                    add_error(node_path, "function template carries fields for another kind");
                }
                for (std::uint32_t child_index = 0; child_index < node.children.size();
                     ++child_index) {
                    add_reference(node.children[child_index],
                                  index,
                                  node_path + ".children[" + std::to_string(child_index) + "]");
                }
                if (node.fn_return == kInvalidMemberTypeTemplateNode) {
                    add_error(node_path + ".return", "function template is missing return node");
                } else {
                    add_reference(node.fn_return, index, node_path + ".return");
                }
                break;
            }
        }
    }

    void verify_decl(const StructDecl &decl, const std::string &path) {
        verify_symbol_ref(decl.symbol_ref, path + ".symbol_ref", SymbolRefKind::Type, decl.name);
        verify_variance_metadata(decl.type_param_count, decl.type_param_variances, path);
        if (is_backend_ready_mode(mode_) &&
            decl.field_type_template_roots.size() != decl.fields.size()) {
            add_error(path + ".field_type_template_roots",
                      "field template root count (" +
                          std::to_string(decl.field_type_template_roots.size()) +
                          ") must equal field count (" + std::to_string(decl.fields.size()) + ")");
        }
        verify_member_type_templates(decl.member_type_templates,
                                     decl.field_type_template_roots,
                                     decl.type_param_count,
                                     path + ".member_type_templates");
        for (std::uint32_t index = 0; index < decl.fields.size(); ++index) {
            const auto field_path = path + ".fields[" + std::to_string(index) + "]";
            verify_optional_expr_ref(decl.fields[index].default_value, field_path + ".default");
            verify_type_ref(decl.fields[index].type_ref, field_path + ".type_ref");
            verify_source_range(decl.fields[index].source_range, field_path, "source range");
        }
    }

    void verify_decl(const EnumDecl &decl, const std::string &path) {
        verify_symbol_ref(decl.symbol_ref, path + ".symbol_ref", SymbolRefKind::Type, decl.name);
        verify_variance_metadata(decl.type_param_count, decl.type_param_variances, path);
        std::vector<std::uint32_t> template_roots;
        for (std::uint32_t index = 0; index < decl.variants.size(); ++index) {
            const auto &variant = decl.variants[index];
            const auto variant_path = path + ".variants[" + std::to_string(index) + "](" +
                                      (variant.name.empty() ? "<unnamed>" : variant.name) + ")";
            if (variant.name.empty()) {
                add_error(variant_path, "enum variant is missing name");
            }
            verify_source_range(variant.source_range, variant_path, "source range");
            switch (variant.payload_kind) {
            case EnumVariantPayloadKind::Unit:
                if (!variant.payload.empty()) {
                    add_error(variant_path, "unit enum variant carries tuple payload");
                }
                if (!variant.fields.empty()) {
                    add_error(variant_path, "unit enum variant carries struct fields");
                }
                break;
            case EnumVariantPayloadKind::Tuple:
                if (variant.payload.empty()) {
                    add_error(variant_path, "tuple enum variant is missing payload");
                }
                if (!variant.fields.empty()) {
                    add_error(variant_path, "tuple enum variant carries struct fields");
                }
                break;
            case EnumVariantPayloadKind::Struct:
                if (!variant.payload.empty()) {
                    add_error(variant_path, "struct enum variant carries tuple payload");
                }
                if (variant.fields.empty()) {
                    add_error(variant_path, "struct enum variant is missing fields");
                }
                break;
            }
            const auto expected_template_roots =
                variant.payload_kind == EnumVariantPayloadKind::Tuple
                    ? variant.payload.size()
                    : (variant.payload_kind == EnumVariantPayloadKind::Struct
                           ? variant.fields.size()
                           : 0);
            if (is_backend_ready_mode(mode_) &&
                variant.payload_type_template_roots.size() != expected_template_roots) {
                add_error(variant_path + ".payload_type_template_roots",
                          "payload template root count (" +
                              std::to_string(variant.payload_type_template_roots.size()) +
                              ") must equal active payload slot count (" +
                              std::to_string(expected_template_roots) + ")");
            }
            template_roots.insert(template_roots.end(),
                                  variant.payload_type_template_roots.begin(),
                                  variant.payload_type_template_roots.end());
            for (std::uint32_t payload_index = 0; payload_index < variant.payload.size();
                 ++payload_index) {
                verify_type_ref(variant.payload[payload_index],
                                variant_path + ".payload[" + std::to_string(payload_index) + "]");
            }
            for (std::uint32_t field_index = 0; field_index < variant.fields.size();
                 ++field_index) {
                const auto &field = variant.fields[field_index];
                const auto field_path = variant_path + ".fields[" + std::to_string(field_index) +
                                        "](" + (field.name.empty() ? "<unnamed>" : field.name) +
                                        ")";
                if (field.name.empty()) {
                    add_error(field_path, "enum variant field is missing name");
                }
                verify_type_ref(field.type_ref, field_path + ".type_ref");
                verify_optional_expr_ref(field.default_value, field_path + ".default");
                verify_source_range(field.source_range, field_path, "source range");
            }
        }
        verify_member_type_templates(decl.member_type_templates,
                                     template_roots,
                                     decl.type_param_count,
                                     path + ".member_type_templates");
    }

    void verify_decl(const CapabilityDecl &decl, const std::string &path) {
        verify_symbol_ref(
            decl.symbol_ref, path + ".symbol_ref", SymbolRefKind::Capability, decl.name);
        verify_params(decl.params, path + ".params");
        verify_type_ref(decl.return_type_ref, path + ".return_type_ref");
        verify_source_range(decl.effect.source_range, path + ".effect", "source range");
    }

    void verify_decl(const PredicateDecl &decl, const std::string &path) {
        verify_symbol_ref(
            decl.symbol_ref, path + ".symbol_ref", SymbolRefKind::Predicate, decl.name);
        verify_params(decl.params, path + ".params");
    }

    void verify_decl(const AgentDecl &decl, const std::string &path) {
        verify_symbol_ref(decl.symbol_ref, path + ".symbol_ref", SymbolRefKind::Agent, decl.name);
        verify_type_ref(decl.input_type_ref, path + ".input_type_ref");
        verify_type_ref(decl.context_type_ref, path + ".context_type_ref");
        verify_type_ref(decl.output_type_ref, path + ".output_type_ref");
        for (std::uint32_t index = 0; index < decl.capability_refs.size(); ++index) {
            verify_symbol_ref(decl.capability_refs[index],
                              path + ".capability_refs[" + std::to_string(index) + "]",
                              SymbolRefKind::Capability);
        }
    }

    void verify_decl(const ContractDecl &decl, const std::string &path) {
        verify_symbol_ref(decl.target_ref, path + ".target_ref", SymbolRefKind::Agent);
        for (std::uint32_t index = 0; index < decl.clauses.size(); ++index) {
            const auto &clause = decl.clauses[index];
            const auto clause_path = path + ".clauses[" + std::to_string(index) + "]";
            verify_source_range(clause.source_range, clause_path, "source range");
            // Wildcard decreases intentionally carries no expression payload —
            // skip the value verifier in that case (the clause_kind and
            // is_wildcard fields are sufficient for downstream consumers).
            if (clause.kind == ContractClauseKind::Decreases && clause.is_wildcard) {
                continue;
            }
            std::visit(
                [this, &clause_path](const auto &value) {
                    verify_contract_clause_value(value, clause_path + ".value");
                },
                clause.value);
        }
    }

    void verify_decl(const FlowDecl &decl, const std::string &path) {
        verify_symbol_ref(decl.target_ref, path + ".target_ref", SymbolRefKind::Agent);
        for (std::uint32_t index = 0; index < decl.state_handlers.size(); ++index) {
            const auto handler_path = path + ".state_handlers[" + std::to_string(index) + "]";
            verify_source_range(
                decl.state_handlers[index].source_range, handler_path, "source range");
            verify_block(decl.state_handlers[index].body, handler_path + ".body");
        }
    }

    void verify_decl(const WorkflowDecl &decl, const std::string &path) {
        verify_symbol_ref(
            decl.symbol_ref, path + ".symbol_ref", SymbolRefKind::Workflow, decl.name);
        verify_type_ref(decl.input_type_ref, path + ".input_type_ref");
        verify_type_ref(decl.output_type_ref, path + ".output_type_ref");
        for (std::uint32_t index = 0; index < decl.nodes.size(); ++index) {
            const auto node_path = path + ".nodes[" + std::to_string(index) + "]";
            verify_required_expr_ref(decl.nodes[index].input, node_path + ".input");
            verify_symbol_ref(
                decl.nodes[index].target_ref, node_path + ".target_ref", SymbolRefKind::Agent);
            verify_source_range(decl.nodes[index].source_range, node_path, "source range");
        }
        for (std::uint32_t index = 0; index < decl.safety.size(); ++index) {
            verify_temporal_ptr(decl.safety[index],
                                path + ".safety[" + std::to_string(index) + "]");
        }
        for (std::uint32_t index = 0; index < decl.liveness.size(); ++index) {
            verify_temporal_ptr(decl.liveness[index],
                                path + ".liveness[" + std::to_string(index) + "]");
        }
        verify_required_expr_ref(decl.return_value, path + ".return_value");
    }

    // P2c/P2d: verify a top-level fn declaration. Prototypes and @builtin
    // declarations have no body; source fn definitions carry a lowered block.
    void verify_decl(const FnDecl &decl, const std::string &path) {
        verify_symbol_ref(
            decl.symbol_ref, path + ".symbol_ref", SymbolRefKind::Function, decl.name);
        verify_params(decl.params, path + ".params");
        if (decl.has_return_type) {
            verify_type_ref(decl.return_type_ref, path + ".return_type_ref");
        }
        verify_source_range(decl.effect.source_range, path + ".effect", "source range");
        for (std::uint32_t index = 0; index < decl.effect.capabilities.size(); ++index) {
            const auto capability_path =
                path + ".effect.capabilities[" + std::to_string(index) + "]";
            verify_symbol_ref(
                decl.effect.capabilities[index], capability_path, SymbolRefKind::Capability);
        }
        if (decl.has_body) {
            if (decl.body) {
                verify_block(*decl.body, path + ".body");
            } else {
                add_error(path + ".body", "function has_body is true but body is null");
            }
        } else if (decl.body) {
            add_error(path + ".body", "function body is present but has_body is false");
        }
    }

    void verify_decl(const InstanceDecl &decl, const std::string &path) {
        // InstanceDecl is a concrete instance copy of a nominal declaration.
        // symbol_ref.kind must match InstanceKind; type_ref fields that are
        // irrelevant for the given kind may be Unresolved and are skipped.
        switch (decl.kind) {
        case InstanceKind::Capability:
            verify_symbol_ref(decl.symbol_ref, path + ".symbol_ref",
                              SymbolRefKind::Capability);
            break;
        case InstanceKind::Predicate:
            verify_symbol_ref(decl.symbol_ref, path + ".symbol_ref",
                              SymbolRefKind::Predicate);
            break;
        case InstanceKind::Agent:
            verify_symbol_ref(decl.symbol_ref, path + ".symbol_ref",
                              SymbolRefKind::Agent);
            break;
        case InstanceKind::Workflow:
            verify_symbol_ref(decl.symbol_ref, path + ".symbol_ref",
                              SymbolRefKind::Workflow);
            break;
        case InstanceKind::Fn:
            verify_symbol_ref(decl.symbol_ref, path + ".symbol_ref",
                              SymbolRefKind::Function);
            break;
        case InstanceKind::Unknown:
            if (decl.symbol_ref.id.has_value() || !decl.symbol_ref.canonical_name.empty()) {
                verify_symbol_ref(decl.symbol_ref, path + ".symbol_ref",
                                  SymbolRefKind::Unknown);
            }
            break;
        }
        for (std::uint32_t index = 0; index < decl.type_args.size(); ++index) {
            verify_type_ref(decl.type_args[index],
                            path + ".type_args[" + std::to_string(index) + "]");
        }
        verify_params(decl.params, path + ".params");
        // Only validate kind-relevant type-ref fields.
        switch (decl.kind) {
        case InstanceKind::Capability:
            verify_type_ref(decl.return_type_ref, path + ".return_type_ref");
            break;
        case InstanceKind::Predicate:
            break;
        case InstanceKind::Agent:
            verify_type_ref(decl.agent_input_type_ref, path + ".agent_input_type_ref");
            verify_type_ref(decl.agent_context_type_ref, path + ".agent_context_type_ref");
            verify_type_ref(decl.agent_output_type_ref, path + ".agent_output_type_ref");
            break;
        case InstanceKind::Workflow:
            verify_type_ref(decl.workflow_input_type_ref, path + ".workflow_input_type_ref");
            verify_type_ref(decl.workflow_output_type_ref, path + ".workflow_output_type_ref");
            break;
        case InstanceKind::Fn:
            verify_type_ref(decl.return_type_ref, path + ".return_type_ref");
            break;
        case InstanceKind::Unknown:
            break;
        }
    }

    // P3 (RFC §1.3): a trait interface declaration. Its symbol lowers as a
    // Type ref (traits occupy type positions at bound/impl sites); super-traits
    // and method-signature parameter/return types are verified structurally.
    void verify_decl(const TraitDecl &decl, const std::string &path) {
        verify_symbol_ref(decl.symbol_ref, path + ".symbol_ref", SymbolRefKind::Type, decl.name);
        for (std::uint32_t index = 0; index < decl.super_traits.size(); ++index) {
            verify_symbol_ref(decl.super_traits[index],
                              path + ".super_traits[" + std::to_string(index) + "]",
                              SymbolRefKind::Type);
        }
        for (std::uint32_t index = 0; index < decl.methods.size(); ++index) {
            const auto method_path = path + ".methods[" + std::to_string(index) + "]";
            verify_params(decl.methods[index].params, method_path + ".params");
            if (decl.methods[index].has_return_type) {
                verify_type_ref(decl.methods[index].return_type_ref,
                                method_path + ".return_type_ref");
            }
            verify_source_range(decl.methods[index].source_range, method_path, "source range");
        }
    }

    // P3 (RFC §1.4): an impl declaration. The target type is always required;
    // the trait ref (Type kind) is present only for trait impls. Method refs
    // point at the lowered FnDecls that carry the bodies (Function kind).
    void verify_decl(const ImplDecl &decl, const std::string &path) {
        verify_type_ref(decl.target_type_ref, path + ".target_type_ref");
        if (!decl.is_inherent) {
            verify_symbol_ref(decl.trait_ref, path + ".trait_ref", SymbolRefKind::Type);
        }
        for (std::uint32_t index = 0; index < decl.trait_type_args.size(); ++index) {
            verify_type_ref(decl.trait_type_args[index],
                            path + ".trait_type_args[" + std::to_string(index) + "]");
        }
        for (std::uint32_t index = 0; index < decl.method_refs.size(); ++index) {
            verify_symbol_ref(decl.method_refs[index],
                              path + ".method_refs[" + std::to_string(index) + "]",
                              SymbolRefKind::Function);
        }
    }

    void verify_params(const std::vector<ParamDecl> &params, const std::string &path) {
        for (std::uint32_t index = 0; index < params.size(); ++index) {
            const auto param_path = path + "[" + std::to_string(index) + "]";
            verify_type_ref(params[index].type_ref, param_path + ".type_ref");
            verify_source_range(params[index].source_range, param_path, "source range");
        }
    }

    void verify_contract_clause_value(const ExprRef &expr, const std::string &path) {
        verify_required_expr_ref(expr, path);
    }

    void verify_contract_clause_value(const TemporalExprPtr &expr, const std::string &path) {
        verify_temporal_ptr(expr, path);
    }

    void verify_block(const Block &block, const std::string &path) {
        verify_source_range(block.source_range, path, "source range");
        for (std::uint32_t index = 0; index < block.statements.size(); ++index) {
            const auto stmt_path = path + ".statements[" + std::to_string(index) + "]";
            if (!block.statements[index]) {
                add_error(stmt_path, "statement pointer is null");
                continue;
            }
            verify_statement(*block.statements[index], stmt_path);
        }
    }

    void verify_statement(const Statement &stmt, const std::string &path) {
        if (!statement_ids_.insert(stmt.id).second) {
            add_error(path, "duplicate statement id " + std::to_string(stmt.id));
        }
        verify_source_range(stmt.source_range, path, "source range");
        // RFC 0027 P6/P7/P8 (KR6.13-F): one handler per StatementNode alternative,
        // generated from stmt_nodes.def; a node without its own verifier names the
        // explicitly-named VERIFY_STMT_LEAF no-op. No unnamed catch-all: a new
        // statement node is a COMPILE ERROR here until it is routed.
#define VERIFY_STMT_LEAF(Name) [this, &path](const Name &) {},
#define VERIFY_STMT_CHECKED(Name)                                                                \
    [this, &path](const Name &value) { verify_statement_node(value, path); },

#define VERIFY_STMT_LetStatement(Name) VERIFY_STMT_CHECKED(Name)
#define VERIFY_STMT_AssignStatement(Name) VERIFY_STMT_CHECKED(Name)
#define VERIFY_STMT_IfStatement(Name) VERIFY_STMT_CHECKED(Name)
#define VERIFY_STMT_IfLetStatement(Name) VERIFY_STMT_CHECKED(Name)
#define VERIFY_STMT_GotoStatement(Name) VERIFY_STMT_CHECKED(Name)
#define VERIFY_STMT_ReturnStatement(Name) VERIFY_STMT_CHECKED(Name)
#define VERIFY_STMT_AssertStatement(Name) VERIFY_STMT_CHECKED(Name)
#define VERIFY_STMT_UnwrapStatement(Name) VERIFY_STMT_CHECKED(Name)
#define VERIFY_STMT_RequiresStatement(Name) VERIFY_STMT_CHECKED(Name)
#define VERIFY_STMT_UnreachableStatement(Name) VERIFY_STMT_CHECKED(Name)
#define VERIFY_STMT_ExprStatement(Name) VERIFY_STMT_CHECKED(Name)
#define HANDLE_STMT_NODE(Name) VERIFY_STMT_##Name(Name)
        std::visit(
            Overloaded{
#include "ahfl/compiler/ir/stmt_nodes.def"
            },
            stmt.node);
#undef HANDLE_STMT_NODE
#undef VERIFY_STMT_LetStatement
#undef VERIFY_STMT_AssignStatement
#undef VERIFY_STMT_IfStatement
#undef VERIFY_STMT_IfLetStatement
#undef VERIFY_STMT_GotoStatement
#undef VERIFY_STMT_ReturnStatement
#undef VERIFY_STMT_AssertStatement
#undef VERIFY_STMT_UnwrapStatement
#undef VERIFY_STMT_RequiresStatement
#undef VERIFY_STMT_UnreachableStatement
#undef VERIFY_STMT_ExprStatement
#undef VERIFY_STMT_CHECKED
#undef VERIFY_STMT_LEAF
    }

    [[nodiscard]] static bool same_symbol_identity(const SymbolRef &lhs,
                                                   const SymbolRef &rhs) noexcept {
        if (lhs.kind != rhs.kind) {
            return false;
        }
        if (lhs.id.has_value() && rhs.id.has_value()) {
            return *lhs.id == *rhs.id;
        }
        return !lhs.canonical_name.empty() && lhs.canonical_name == rhs.canonical_name;
    }

    [[nodiscard]] static bool same_nominal_base(const TypeRef &source,
                                                const TypeRef &target) noexcept {
        return source.kind == target.kind &&
               (source.kind == TypeRefKind::Struct || source.kind == TypeRefKind::Enum) &&
               same_symbol_identity(source.nominal_ref, target.nominal_ref);
    }

    [[nodiscard]] std::optional<std::vector<Variance>>
    nominal_variances(const TypeRef &type) const {
        if (type.kind != TypeRefKind::Struct && type.kind != TypeRefKind::Enum) {
            return std::nullopt;
        }
        for (const auto &decl : program_.declarations) {
            if (const auto *value = std::get_if<StructDecl>(&decl);
                type.kind == TypeRefKind::Struct && value != nullptr &&
                same_symbol_identity(type.nominal_ref, value->symbol_ref)) {
                return value->type_param_variances;
            }
            if (const auto *value = std::get_if<EnumDecl>(&decl);
                type.kind == TypeRefKind::Enum && value != nullptr &&
                same_symbol_identity(type.nominal_ref, value->symbol_ref)) {
                return value->type_param_variances;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] bool nominal_enum_has_variant(const TypeRef &type,
                                                std::string_view variant_name) const {
        if (type.kind != TypeRefKind::Enum || variant_name.empty()) {
            return false;
        }
        for (const auto &decl : program_.declarations) {
            const auto *value = std::get_if<EnumDecl>(&decl);
            if (value == nullptr || !same_symbol_identity(type.nominal_ref, value->symbol_ref)) {
                continue;
            }
            return std::ranges::any_of(value->variants, [&](const EnumVariantDecl &variant) {
                return variant.name == variant_name;
            });
        }
        return false;
    }

    [[nodiscard]] static bool
    bounds_widen(const std::optional<std::pair<std::int64_t, std::int64_t>> &source,
                 const std::optional<std::pair<std::int64_t, std::int64_t>> &target) noexcept {
        return source.has_value() && target.has_value() && source->first >= target->first &&
               source->second <= target->second && source != target;
    }

    [[nodiscard]] static bool capacity_widen(const TypeRef &source,
                                             const TypeRef &target) noexcept {
        if (!source.collection_capacity.has_value()) {
            return false;
        }
        if (!target.collection_capacity.has_value()) {
            return true;
        }
        return *source.collection_capacity < *target.collection_capacity;
    }

    void verify_adjustment_plan(const AdjustmentPlan &plan,
                                const TypeRef &initializer_type,
                                const TypeRef &binding_type,
                                const std::string &path) {
        verify_type_ref(plan.source, path + ".source");
        verify_type_ref(plan.target, path + ".target");
        if (!type_refs_equal(plan.source, initializer_type)) {
            add_error(path + ".source", "adjustment source does not match initializer type");
        }
        if (!type_refs_equal(plan.target, binding_type)) {
            add_error(path + ".target", "adjustment target does not match let binding type");
        }
        if (type_refs_equal(plan.source, plan.target)) {
            add_error(path, "identity adjustment plan is not allowed");
        }
        if (plan.root >= plan.nodes.size()) {
            add_error(path + ".root", "adjustment root index is out of range");
            return;
        }
        if (!type_refs_equal(plan.nodes[plan.root].source, plan.source)) {
            add_error(path + ".root", "adjustment root source does not match plan source");
        }
        if (!type_refs_equal(plan.nodes[plan.root].target, plan.target)) {
            add_error(path + ".root", "adjustment root target does not match plan target");
        }

        std::vector<unsigned char> color(plan.nodes.size(), 0);
        std::vector<bool> reachable(plan.nodes.size(), false);
        std::function<void(std::uint32_t)> visit = [&](std::uint32_t id) {
            if (id >= plan.nodes.size()) {
                return;
            }
            reachable[id] = true;
            if (color[id] == 1) {
                add_error(path + ".nodes[" + std::to_string(id) + "]",
                          "adjustment plan contains a cycle");
                return;
            }
            if (color[id] == 2) {
                return;
            }
            color[id] = 1;
            for (const auto &op : plan.nodes[id].ops) {
                if (op.child != UINT32_MAX) {
                    if (op.child >= plan.nodes.size()) {
                        add_error(path + ".nodes[" + std::to_string(id) + "]",
                                  "adjustment child index is out of range");
                    } else {
                        visit(op.child);
                    }
                }
            }
            color[id] = 2;
        };
        visit(plan.root);
        for (std::uint32_t id = 0; id < plan.nodes.size(); ++id) {
            if (!reachable[id]) {
                add_error(path + ".nodes[" + std::to_string(id) + "]",
                          "adjustment node is not reachable from root");
            }
        }

        for (std::uint32_t id = 0; id < plan.nodes.size(); ++id) {
            const auto &node = plan.nodes[id];
            const auto node_path = path + ".nodes[" + std::to_string(id) + "]";
            verify_type_ref(node.source, node_path + ".source");
            verify_type_ref(node.target, node_path + ".target");

            std::unordered_set<std::uint32_t> type_args;
            std::unordered_set<std::uint32_t> fn_params;
            bool has_capacity = false;
            bool has_fn_return = false;
            bool has_scalar = false;
            bool has_variant = false;
            bool has_top_bottom = false;
            std::optional<std::pair<unsigned char, std::uint32_t>> previous_order;
            for (std::uint32_t op_index = 0; op_index < node.ops.size(); ++op_index) {
                const auto &op = node.ops[op_index];
                const auto op_path = node_path + ".ops[" + std::to_string(op_index) + "]";
                std::optional<std::pair<unsigned char, std::uint32_t>> current_order;
                switch (op.kind) {
                case AdjustmentOpKind::CapacityWiden:
                case AdjustmentOpKind::FnParam:
                    current_order = std::pair{static_cast<unsigned char>(0), op.arg_index};
                    break;
                case AdjustmentOpKind::TypeArg:
                case AdjustmentOpKind::FnReturn:
                    current_order = std::pair{static_cast<unsigned char>(1), op.arg_index};
                    break;
                case AdjustmentOpKind::IntWiden:
                case AdjustmentOpKind::StringWiden:
                case AdjustmentOpKind::VariantToEnum:
                case AdjustmentOpKind::ToAny:
                case AdjustmentOpKind::FromNever:
                    break;
                }
                if (current_order.has_value()) {
                    if (previous_order.has_value() && *current_order <= *previous_order) {
                        add_error(op_path, "adjustment operations are not in canonical order");
                    }
                    previous_order = current_order;
                }
                const bool projected = op.kind == AdjustmentOpKind::TypeArg ||
                                       op.kind == AdjustmentOpKind::FnParam ||
                                       op.kind == AdjustmentOpKind::FnReturn;
                const bool indexed =
                    op.kind == AdjustmentOpKind::TypeArg || op.kind == AdjustmentOpKind::FnParam;
                if (!indexed && op.arg_index != 0) {
                    add_error(op_path, "non-indexed adjustment has a non-zero argument index");
                }
                if (projected) {
                    if (op.child >= plan.nodes.size()) {
                        add_error(op_path, "projected adjustment has no valid child");
                        continue;
                    }
                } else if (op.child != UINT32_MAX) {
                    add_error(op_path, "leaf adjustment unexpectedly has a child");
                }

                switch (op.kind) {
                case AdjustmentOpKind::IntWiden: {
                    if (has_scalar || node.ops.size() != 1) {
                        add_error(op_path, "scalar adjustment must be the node's only operation");
                    }
                    has_scalar = true;
                    const bool to_plain = node.source.kind == TypeRefKind::BoundedInt &&
                                          node.target.kind == TypeRefKind::Int;
                    const bool bounded =
                        node.source.kind == TypeRefKind::BoundedInt &&
                        node.target.kind == TypeRefKind::BoundedInt &&
                        bounds_widen(node.source.int_bounds, node.target.int_bounds);
                    if (!to_plain && !bounded) {
                        add_error(op_path, "IntWiden does not match node source/target bounds");
                    }
                    break;
                }
                case AdjustmentOpKind::StringWiden: {
                    if (has_scalar || node.ops.size() != 1) {
                        add_error(op_path, "scalar adjustment must be the node's only operation");
                    }
                    has_scalar = true;
                    const bool to_plain = node.source.kind == TypeRefKind::BoundedString &&
                                          node.target.kind == TypeRefKind::String;
                    const bool bounded =
                        node.source.kind == TypeRefKind::BoundedString &&
                        node.target.kind == TypeRefKind::BoundedString &&
                        bounds_widen(node.source.string_bounds, node.target.string_bounds);
                    if (!to_plain && !bounded) {
                        add_error(op_path, "StringWiden does not match node source/target bounds");
                    }
                    break;
                }
                case AdjustmentOpKind::CapacityWiden:
                    if (has_capacity || !same_nominal_base(node.source, node.target) ||
                        node.source.kind != TypeRefKind::Struct ||
                        (node.source.canonical_name != "std::collections::List" &&
                         node.source.canonical_name != "std::collections::Set" &&
                         node.source.canonical_name != "std::collections::Map") ||
                        !capacity_widen(node.source, node.target)) {
                        add_error(op_path,
                                  "CapacityWiden does not match a widening nominal capacity");
                    }
                    has_capacity = true;
                    break;
                case AdjustmentOpKind::TypeArg: {
                    if (!type_args.insert(op.arg_index).second) {
                        add_error(op_path, "duplicate TypeArg adjustment position");
                    }
                    if (!same_nominal_base(node.source, node.target) ||
                        op.arg_index >= node.source.params.size() ||
                        op.arg_index >= node.target.params.size() ||
                        node.source.params[op.arg_index] == nullptr ||
                        node.target.params[op.arg_index] == nullptr) {
                        add_error(op_path, "TypeArg adjustment position is invalid for node");
                        break;
                    }
                    const auto variances = nominal_variances(node.source);
                    if (!variances.has_value() || op.arg_index >= variances->size() ||
                        (*variances)[op.arg_index] == Variance::Invariant) {
                        add_error(op_path,
                                  "TypeArg adjustment is not allowed at an invariant position");
                        break;
                    }
                    const auto &child = plan.nodes[op.child];
                    const auto &source_arg = *node.source.params[op.arg_index];
                    const auto &target_arg = *node.target.params[op.arg_index];
                    const bool correct = (*variances)[op.arg_index] == Variance::Covariant
                                             ? type_refs_equal(child.source, source_arg) &&
                                                   type_refs_equal(child.target, target_arg)
                                             : type_refs_equal(child.source, target_arg) &&
                                                   type_refs_equal(child.target, source_arg);
                    if (!correct) {
                        add_error(op_path, "TypeArg child has the wrong variance direction");
                    }
                    break;
                }
                case AdjustmentOpKind::FnParam: {
                    if (!fn_params.insert(op.arg_index).second) {
                        add_error(op_path, "duplicate FnParam adjustment position");
                    }
                    if (node.source.kind != TypeRefKind::Fn ||
                        node.target.kind != TypeRefKind::Fn ||
                        op.arg_index >= node.source.params.size() ||
                        op.arg_index >= node.target.params.size() ||
                        node.source.params[op.arg_index] == nullptr ||
                        node.target.params[op.arg_index] == nullptr) {
                        add_error(op_path, "FnParam adjustment position is invalid for node");
                        break;
                    }
                    const auto &child = plan.nodes[op.child];
                    if (!type_refs_equal(child.source, *node.target.params[op.arg_index]) ||
                        !type_refs_equal(child.target, *node.source.params[op.arg_index])) {
                        add_error(op_path, "FnParam child has the wrong contravariant direction");
                    }
                    break;
                }
                case AdjustmentOpKind::FnReturn: {
                    if (has_fn_return) {
                        add_error(op_path, "duplicate FnReturn adjustment");
                    }
                    has_fn_return = true;
                    if (node.source.kind != TypeRefKind::Fn ||
                        node.target.kind != TypeRefKind::Fn || node.source.first == nullptr ||
                        node.target.first == nullptr) {
                        add_error(op_path, "FnReturn adjustment does not match a function node");
                        break;
                    }
                    const auto &child = plan.nodes[op.child];
                    if (!type_refs_equal(child.source, *node.source.first) ||
                        !type_refs_equal(child.target, *node.target.first)) {
                        add_error(op_path, "FnReturn child has the wrong covariant direction");
                    }
                    break;
                }
                case AdjustmentOpKind::VariantToEnum:
                    if (has_variant || node.ops.size() != 1 ||
                        node.source.kind != TypeRefKind::Enum ||
                        node.target.kind != TypeRefKind::Enum ||
                        !same_nominal_base(node.source, node.target) ||
                        node.source.variant_name.empty() || !node.target.variant_name.empty() ||
                        !nominal_enum_has_variant(node.source, node.source.variant_name)) {
                        add_error(op_path,
                                  "VariantToEnum does not match a variant-to-owner boundary");
                    }
                    if (node.source.params.size() != node.target.params.size()) {
                        add_error(op_path, "VariantToEnum changes type-argument arity");
                    } else {
                        for (std::size_t i = 0; i < node.source.params.size(); ++i) {
                            if (!type_refs_equal(node.source.params[i].get(),
                                                 node.target.params[i].get())) {
                                add_error(op_path, "VariantToEnum changes a type argument");
                            }
                        }
                    }
                    has_variant = true;
                    break;
                case AdjustmentOpKind::ToAny:
                    if (has_top_bottom || node.ops.size() != 1 ||
                        node.target.kind != TypeRefKind::Any ||
                        node.source.kind == TypeRefKind::Any) {
                        add_error(op_path, "ToAny does not match a non-Any-to-Any boundary");
                    }
                    has_top_bottom = true;
                    break;
                case AdjustmentOpKind::FromNever:
                    if (has_top_bottom || node.ops.size() != 1 ||
                        node.source.kind != TypeRefKind::Never ||
                        node.target.kind == TypeRefKind::Never) {
                        add_error(op_path,
                                  "FromNever does not match a Never-to-non-Never boundary");
                    }
                    has_top_bottom = true;
                    break;
                }
            }

            if (has_scalar || has_variant || has_top_bottom) {
                continue;
            }
            if (same_nominal_base(node.source, node.target)) {
                if (node.source.params.size() != node.target.params.size()) {
                    add_error(node_path, "nominal adjustment changes type-argument arity");
                } else {
                    for (std::uint32_t i = 0; i < node.source.params.size(); ++i) {
                        if (!type_args.contains(i) &&
                            !type_refs_equal(node.source.params[i].get(),
                                             node.target.params[i].get())) {
                            add_error(node_path, "unnamed nominal type argument changes");
                        }
                    }
                }
                if (!has_capacity &&
                    node.source.collection_capacity != node.target.collection_capacity) {
                    add_error(node_path, "unnamed nominal capacity changes");
                }
            } else if (node.source.kind == TypeRefKind::Fn && node.target.kind == TypeRefKind::Fn) {
                if (node.source.params.size() != node.target.params.size()) {
                    add_error(node_path, "function adjustment changes parameter arity");
                } else {
                    for (std::uint32_t i = 0; i < node.source.params.size(); ++i) {
                        if (!fn_params.contains(i) &&
                            !type_refs_equal(node.source.params[i].get(),
                                             node.target.params[i].get())) {
                            add_error(node_path, "unnamed function parameter changes");
                        }
                    }
                }
                if (!has_fn_return &&
                    !type_refs_equal(node.source.first.get(), node.target.first.get())) {
                    add_error(node_path, "unnamed function return type changes");
                }
            } else if (!type_refs_equal(node.source, node.target)) {
                add_error(node_path, "adjustment node changes an unnamed type dimension");
            }
        }
    }

    void verify_statement_node(const LetStatement &stmt, const std::string &path) {
        verify_type_ref(stmt.type_ref, path + ".type_ref");
        verify_required_expr_ref(stmt.initializer, path + ".initializer");
        if (!is_backend_ready_mode(mode_) || !stmt.initializer ||
            stmt.initializer.get() == nullptr) {
            return;
        }
        const auto &initializer_type = stmt.initializer.get()->resolved_type;
        if (!stmt.adjustment.has_value()) {
            if (!type_refs_equal(initializer_type, stmt.type_ref)) {
                add_error(path + ".adjustment", "missing adjustment for non-identity let boundary");
            }
            return;
        }
        verify_adjustment_plan(
            *stmt.adjustment, initializer_type, stmt.type_ref, path + ".adjustment");
    }

    void verify_statement_node(const AssignStatement &stmt, const std::string &path) {
        verify_required_expr_ref(stmt.value, path + ".value");
    }

    void verify_statement_node(const IfStatement &stmt, const std::string &path) {
        verify_required_expr_ref(stmt.condition, path + ".condition");
        if (stmt.then_block) {
            verify_block(*stmt.then_block, path + ".then");
        } else {
            add_error(path + ".then", "then block is null");
        }
        if (stmt.else_block) {
            verify_block(*stmt.else_block, path + ".else");
        }
    }

    void verify_statement_node(const IfLetStatement &stmt, const std::string &path) {
        verify_match_pattern(stmt.pattern, path + ".pattern");
        verify_required_expr_ref(stmt.scrutinee, path + ".scrutinee");
        if (stmt.then_block) {
            verify_block(*stmt.then_block, path + ".then");
        } else {
            add_error(path + ".then", "then block is null");
        }
        if (stmt.else_block) {
            verify_block(*stmt.else_block, path + ".else");
        }
    }

    void verify_statement_node(const GotoStatement &stmt, const std::string &path) {
        if (!is_backend_ready_mode(mode_)) {
            return;
        }
        if (stmt.target_state.empty()) {
            add_error(path, "goto statement is missing target state");
        } else if (contains_sentinel(stmt.target_state)) {
            add_error(path, "goto statement contains sentinel target state");
        }
    }

    void verify_statement_node(const ReturnStatement &stmt, const std::string &path) {
        verify_required_expr_ref(stmt.value, path + ".value");
    }

    void verify_statement_node(const AssertStatement &stmt, const std::string &path) {
        verify_required_expr_ref(stmt.condition, path + ".condition");
        // message is optional on arity-1 assert forms.
        verify_optional_expr_ref(stmt.message, path + ".message");
    }

    void verify_statement_node(const UnwrapStatement &stmt, const std::string &path) {
        verify_required_expr_ref(stmt.operand, path + ".operand");
    }

    void verify_statement_node(const RequiresStatement &stmt, const std::string &path) {
        verify_required_expr_ref(stmt.condition, path + ".condition");
        verify_optional_expr_ref(stmt.message, path + ".message");
    }

    void verify_statement_node(const UnreachableStatement &stmt, const std::string &path) {
        // message is optional on the bare `unreachable;` form.
        verify_optional_expr_ref(stmt.message, path + ".message");
    }

    void verify_statement_node(const ExprStatement &stmt, const std::string &path) {
        verify_required_expr_ref(stmt.expr, path + ".expr");
    }

    void verify_expr_children(const Expr &expr, const std::string &path) {
        // RFC 0027 P6/P7/P8 (KR6.13-F): one handler per ExprNode alternative,
        // generated from the X-list expr_nodes.def. A node with genuinely empty
        // verifier semantics (a pure literal / path leaf — its checks, if any,
        // are its children's) is bound to the SINGLE explicitly-named
        // VERIFY_EXPR_LEAF no-op; every node that carries its own check names a
        // per-node handler below. There is no unnamed catch-all, so adding a node
        // to expr_nodes.def without listing it here is a COMPILE ERROR naming the
        // type (CLAUDE.md Principle 5) instead of a silently unchecked node.
#define VERIFY_EXPR_LEAF(Name) [this, &path](const Name &) {},
#define VERIFY_EXPR_CHECKED(Name)                                                                \
    [this, &path](const Name &value) { verify_expr_node(value, path); },

#define VERIFY_EXPR_BoolLiteralExpr(Name) VERIFY_EXPR_LEAF(Name)
#define VERIFY_EXPR_IntegerLiteralExpr(Name) VERIFY_EXPR_LEAF(Name)
#define VERIFY_EXPR_FloatLiteralExpr(Name) VERIFY_EXPR_LEAF(Name)
#define VERIFY_EXPR_DecimalLiteralExpr(Name) VERIFY_EXPR_LEAF(Name)
#define VERIFY_EXPR_StringLiteralExpr(Name) VERIFY_EXPR_LEAF(Name)
#define VERIFY_EXPR_DurationLiteralExpr(Name) VERIFY_EXPR_LEAF(Name)
#define VERIFY_EXPR_PathExpr(Name) VERIFY_EXPR_LEAF(Name)
#define VERIFY_EXPR_QualifiedValueExpr(Name) VERIFY_EXPR_CHECKED(Name)
#define VERIFY_EXPR_CallExpr(Name) VERIFY_EXPR_CHECKED(Name)
#define VERIFY_EXPR_MethodCallExpr(Name) VERIFY_EXPR_CHECKED(Name)
#define VERIFY_EXPR_LambdaExpr(Name) VERIFY_EXPR_CHECKED(Name)
#define VERIFY_EXPR_StructLiteralExpr(Name) VERIFY_EXPR_CHECKED(Name)
#define VERIFY_EXPR_UnaryExpr(Name) VERIFY_EXPR_CHECKED(Name)
#define VERIFY_EXPR_BinaryExpr(Name) VERIFY_EXPR_CHECKED(Name)
#define VERIFY_EXPR_MemberAccessExpr(Name) VERIFY_EXPR_CHECKED(Name)
#define VERIFY_EXPR_IndexAccessExpr(Name) VERIFY_EXPR_CHECKED(Name)
#define VERIFY_EXPR_MatchExpr(Name) VERIFY_EXPR_CHECKED(Name)
#define VERIFY_EXPR_UnwrapExpr(Name) VERIFY_EXPR_CHECKED(Name)
#define VERIFY_EXPR_UnitLiteralExpr(Name) VERIFY_EXPR_LEAF(Name)
#define VERIFY_EXPR_QuantifierExpr(Name) VERIFY_EXPR_CHECKED(Name)
#define HANDLE_EXPR_NODE(Name, Wire, Edges) VERIFY_EXPR_##Name(Name)
        std::visit(
            Overloaded{
#include "ahfl/compiler/ir/expr_nodes.def"
            },
            expr.node);
#undef HANDLE_EXPR_NODE
#undef VERIFY_EXPR_BoolLiteralExpr
#undef VERIFY_EXPR_IntegerLiteralExpr
#undef VERIFY_EXPR_FloatLiteralExpr
#undef VERIFY_EXPR_DecimalLiteralExpr
#undef VERIFY_EXPR_StringLiteralExpr
#undef VERIFY_EXPR_DurationLiteralExpr
#undef VERIFY_EXPR_PathExpr
#undef VERIFY_EXPR_QualifiedValueExpr
#undef VERIFY_EXPR_CallExpr
#undef VERIFY_EXPR_MethodCallExpr
#undef VERIFY_EXPR_LambdaExpr
#undef VERIFY_EXPR_StructLiteralExpr
#undef VERIFY_EXPR_UnaryExpr
#undef VERIFY_EXPR_BinaryExpr
#undef VERIFY_EXPR_MemberAccessExpr
#undef VERIFY_EXPR_IndexAccessExpr
#undef VERIFY_EXPR_MatchExpr
#undef VERIFY_EXPR_UnwrapExpr
#undef VERIFY_EXPR_UnitLiteralExpr
#undef VERIFY_EXPR_QuantifierExpr
#undef VERIFY_EXPR_CHECKED
#undef VERIFY_EXPR_LEAF
    }

    void verify_expr_node(const CallExpr &expr, const std::string &path) {
        if (is_backend_ready_mode(mode_) && contains_sentinel(expr.callee)) {
            add_error(path, "call expression contains sentinel callee");
        }
        if (is_backend_ready_mode(mode_) && expr.callee_ref.id.has_value() &&
            expr.callee_ref.kind == SymbolRefKind::Unknown) {
            add_error(path, "resolved call expression has unknown callee symbol kind");
        }
        for (std::uint32_t index = 0; index < expr.arguments.size(); ++index) {
            verify_required_expr_ref(expr.arguments[index],
                                     path + ".arguments[" + std::to_string(index) + "]");
        }
    }

    void verify_expr_node(const MethodCallExpr &expr, const std::string &path) {
        verify_required_expr_ref(expr.receiver, path + ".receiver");
        if (is_backend_ready_mode(mode_)) {
            if (expr.method.empty() || contains_sentinel(expr.method)) {
                add_error(path, "method call expression has empty or sentinel method target");
            }
            if (expr.method_ref.id.has_value() &&
                expr.method_ref.kind == SymbolRefKind::Unknown) {
                add_error(path, "resolved method call expression has unknown method symbol kind");
            }
        }
        for (std::uint32_t index = 0; index < expr.arguments.size(); ++index) {
            verify_required_expr_ref(expr.arguments[index],
                                     path + ".arguments[" + std::to_string(index) + "]");
        }
    }

    void verify_expr_node(const LambdaExpr &expr, const std::string &path) {
        if (is_backend_ready_mode(mode_)) {
            for (std::uint32_t index = 0; index < expr.params.size(); ++index) {
                if (contains_sentinel(expr.params[index])) {
                    add_error(path + ".params[" + std::to_string(index) + "]",
                              "lambda parameter contains sentinel name");
                }
            }
        }
        verify_required_expr_ref(expr.body, path + ".body");
    }

    void verify_expr_node(const StructLiteralExpr &expr, const std::string &path) {
        if (is_backend_ready_mode(mode_) && contains_sentinel(expr.type_name)) {
            add_error(path, "struct literal contains sentinel type name");
        }
        for (std::uint32_t index = 0; index < expr.fields.size(); ++index) {
            verify_required_expr_ref(expr.fields[index].value,
                                     path + ".fields[" + std::to_string(index) + "].value");
        }
    }

    void verify_expr_node(const QualifiedValueExpr &expr, const std::string &path) {
        if (is_backend_ready_mode(mode_) && contains_sentinel(expr.value)) {
            add_error(path, "qualified value expression contains sentinel payload");
        }
    }

    void verify_expr_node(const UnaryExpr &expr, const std::string &path) {
        verify_required_expr_ref(expr.operand, path + ".operand");
    }

    void verify_expr_node(const BinaryExpr &expr, const std::string &path) {
        verify_required_expr_ref(expr.lhs, path + ".lhs");
        verify_required_expr_ref(expr.rhs, path + ".rhs");
    }

    void verify_expr_node(const MemberAccessExpr &expr, const std::string &path) {
        verify_required_expr_ref(expr.base, path + ".base");
    }

    void verify_expr_node(const IndexAccessExpr &expr, const std::string &path) {
        verify_required_expr_ref(expr.base, path + ".base");
        verify_required_expr_ref(expr.index, path + ".index");
    }

    void verify_expr_node(const MatchExpr &expr, const std::string &path) {
        verify_required_expr_ref(expr.scrutinee, path + ".scrutinee");
        for (std::uint32_t index = 0; index < expr.arms.size(); ++index) {
            const auto arm_path = path + ".arms[" + std::to_string(index) + "]";
            verify_match_pattern(expr.arms[index].pattern, arm_path + ".pattern");
            verify_optional_expr_ref(expr.arms[index].guard, arm_path + ".guard");
            verify_required_expr_ref(expr.arms[index].body, arm_path + ".body");
        }
    }

    // P4-02: unwrap(operand) — operand is required; optional failure message is
    // optional (pure user-facing data; a null ref is fine and means "use default").
    void verify_expr_node(const UnwrapExpr &expr, const std::string &path) {
        verify_required_expr_ref(expr.operand, path + ".operand");
        verify_optional_expr_ref(expr.fallback_none_message, path + ".fallback_none_message");
    }

    // RFC 0024: bounded quantifier — both the collection operand and the body
    // predicate are required expression refs.
    void verify_expr_node(const QuantifierExpr &expr, const std::string &path) {
        verify_required_expr_ref(expr.collection, path + ".collection");
        verify_required_expr_ref(expr.body, path + ".body");
    }

    void verify_match_pattern(const MatchPattern &pattern, const std::string &path) {
        if (is_backend_ready_mode(mode_) && contains_sentinel(pattern.text)) {
            add_error(path, "match pattern contains sentinel text");
        }
        // RFC 0026 P4-B: the resolved `matched_type_ref` is the bridge a backend
        // consumes to type every match binding. It must be present + fully valid
        // on EVERY pattern node (BackendReady), so "AHFL BackendReady" and
        // "Core-consumable" cannot diverge (a missing bridge must fail here, not
        // only later at Core lowering).
        //   - it must be resolved (not Unresolved) and pass the full type-ref gate
        //     (nominal_ref identity / shape), the same one every other TypeRef
        //     goes through;
        //   - a nominal (Struct/Enum) matched type's nominal_ref must name the
        //     SAME symbol as the nominal-only `matched_enum`; a resolved
        //     NON-nominal (primitive/Fn/...) matched type requires matched_enum to
        //     be Unknown (a primitive scrutinee has no enum).
        // Structural (non-BackendReady) mode stays permissive for partial/legacy
        // programs; the type-ref shape recursion below still runs there.
        if (is_backend_ready_mode(mode_)) {
            if (pattern.matched_type_ref.kind == TypeRefKind::Unresolved) {
                add_error(path + ".matched_type_ref",
                          "match pattern has no resolved matched type "
                          "(BackendReady requires the P4-B typed bridge on every node)");
            }
            const auto &mt = pattern.matched_type_ref;
            const bool is_nominal =
                mt.kind == TypeRefKind::Struct || mt.kind == TypeRefKind::Enum;
            if (mt.kind != TypeRefKind::Unresolved) {
                if (is_nominal) {
                    // Same-symbol identity: id-first (both ids present -> must
                    // match), else non-empty canonical-name equality; both Type.
                    const auto &a = mt.nominal_ref;
                    const auto &b = pattern.matched_enum;
                    const bool same = a.kind == SymbolRefKind::Type &&
                                      b.kind == SymbolRefKind::Type &&
                                      (a.id.has_value() && b.id.has_value()
                                           ? *a.id == *b.id
                                           : (!a.canonical_name.empty() &&
                                              a.canonical_name == b.canonical_name));
                    if (!same) {
                        add_error(path + ".matched_type_ref",
                                  "resolved matched type nominal identity disagrees with matched_enum");
                    }
                } else if (pattern.matched_enum.kind != SymbolRefKind::Unknown) {
                    add_error(path + ".matched_type_ref",
                              "non-nominal matched type must not carry a matched_enum identity");
                }
            }
        }
        // The matched type goes through the SAME full shape/identity gate as every
        // other TypeRef (nominal_ref, refinements, nested params). Runs in every
        // mode; BackendReady adds the resolved-nominal-identity checks inside.
        verify_type_ref(pattern.matched_type_ref, path + ".matched_type_ref");
        std::visit(Overloaded{
                       [this, &path, &pattern](const LiteralPattern &p) {
                           verify_match_pattern_node(p, path, pattern.matched_enum);
                       },
                       [this, &path, &pattern](const IntRangePattern &p) {
                           verify_match_pattern_node(p, path, pattern.matched_enum);
                       },
                       [this, &path, &pattern](const VariantPattern &p) {
                           verify_match_pattern_node(p, path, pattern.matched_enum);
                       },
                       [this, &path, &pattern](const WildcardPattern &p) {
                           verify_match_pattern_node(p, path, pattern.matched_enum);
                       },
                       [this, &path, &pattern](const BindingPattern &p) {
                           verify_match_pattern_node(p, path, pattern.matched_enum);
                       },
                       [this, &path, &pattern](const TuplePattern &p) {
                           verify_match_pattern_node(p, path, pattern.matched_enum);
                       },
                       [this, &path, &pattern](const OrPattern &p) {
                           verify_match_pattern_node(p, path, pattern.matched_enum);
                       },
                   },
                   pattern.node);
    }

    // RFC 0027 P6/P7/P8 (KR6.13-F): one handler per MatchPatternNode alternative,
    // generated from pattern_nodes.def. Literal / int-range / wildcard patterns
    // carry no sub-pattern to verify, so they name the explicitly-named
    // VERIFY_PATTERN_LEAF no-op; the rest route to a per-node handler. No unnamed
    // catch-all: a new pattern node is a COMPILE ERROR here until it is routed.
    // The dispatch above always passes the matched-enum identity; the leaf
    // handlers simply ignore it.
#define VERIFY_PATTERN_LEAF(Name)                                                               \
    void verify_match_pattern_node(const Name & /*pattern*/, const std::string & /*path*/,      \
                                   const SymbolRef & /*matched_enum*/) {}

// Variant / binding / tuple / or patterns carry children and already have a
// three-argument verifier definition further down this class, so their routing
// macro is the identity — the .def expansion below must NOT redeclare them.
#define VERIFY_PATTERN_VariantPattern(Name)
#define VERIFY_PATTERN_BindingPattern(Name)
#define VERIFY_PATTERN_TuplePattern(Name)
#define VERIFY_PATTERN_OrPattern(Name)
#define VERIFY_PATTERN_LiteralPattern(Name) VERIFY_PATTERN_LEAF(Name)
#define VERIFY_PATTERN_IntRangePattern(Name) VERIFY_PATTERN_LEAF(Name)
#define VERIFY_PATTERN_WildcardPattern(Name) VERIFY_PATTERN_LEAF(Name)
#define HANDLE_PATTERN_NODE(Name) VERIFY_PATTERN_##Name(Name)
#include "ahfl/compiler/ir/pattern_nodes.def"
#undef HANDLE_PATTERN_NODE
#undef VERIFY_PATTERN_LiteralPattern
#undef VERIFY_PATTERN_IntRangePattern
#undef VERIFY_PATTERN_VariantPattern
#undef VERIFY_PATTERN_WildcardPattern
#undef VERIFY_PATTERN_BindingPattern
#undef VERIFY_PATTERN_TuplePattern
#undef VERIFY_PATTERN_OrPattern
#undef VERIFY_PATTERN_LEAF

    void verify_match_pattern_node(const VariantPattern &pattern, const std::string &path,
                                   const SymbolRef &matched_enum) {
        if (is_backend_ready_mode(mode_) && contains_sentinel(pattern.path)) {
            add_error(path, "variant pattern contains sentinel path");
        }
        // RFC 0026 (3)-3b: a variant pattern's typed identity must be resolved,
        // so a backend resolves the variant by symbol, never by parsing `path`.
        if (is_backend_ready_mode(mode_)) {
            if (pattern.owner_enum.kind != SymbolRefKind::Type ||
                pattern.owner_enum.canonical_name.empty()) {
                add_error(path,
                          "variant pattern has no resolved owner-enum identity "
                          "(BackendReady requires a typed owner enum)");
            }
            if (pattern.variant_name.empty()) {
                add_error(path, "variant pattern has an empty variant name");
            }
            // The variant's owner enum must be the SAME enum the pattern is
            // matched against — otherwise a "matched enum A, variant owner B"
            // pair could both be resolved yet inconsistent. A nested variant with
            // no matched_enum (primitive context) is exempt.
            if (matched_enum.kind == SymbolRefKind::Type &&
                pattern.owner_enum.kind == SymbolRefKind::Type &&
                matched_enum.canonical_name != pattern.owner_enum.canonical_name) {
                add_error(path,
                          "variant pattern owner enum '" + pattern.owner_enum.canonical_name +
                              "' does not match the scrutinee enum '" + matched_enum.canonical_name +
                              "'");
            }
        }
        for (std::uint32_t index = 0; index < pattern.subpatterns.size(); ++index) {
            const auto subpattern_path = path + ".subpatterns[" + std::to_string(index) + "]";
            if (!pattern.subpatterns[index]) {
                add_error(subpattern_path, "match subpattern pointer is null");
                continue;
            }
            verify_match_pattern(*pattern.subpatterns[index], subpattern_path);
        }
        // Struct-payload fields carry their own sub-patterns (a `..` rest field
        // has none); recurse into each present one.
        for (std::uint32_t index = 0; index < pattern.fields.size(); ++index) {
            const auto field_path = path + ".fields[" + std::to_string(index) + "]";
            if (pattern.fields[index].pattern) {
                verify_match_pattern(*pattern.fields[index].pattern, field_path);
            }
        }
    }

    void verify_match_pattern_node(const BindingPattern &pattern, const std::string &path,
                                   const SymbolRef & /*matched_enum*/) {
        if (is_backend_ready_mode(mode_) && contains_sentinel(pattern.name)) {
            add_error(path, "binding pattern contains sentinel name");
        }
        if (pattern.nested) {
            verify_match_pattern(*pattern.nested, path + ".nested");
        }
    }

    void verify_match_pattern_node(const TuplePattern &pattern, const std::string &path,
                                   const SymbolRef & /*matched_enum*/) {
        for (std::uint32_t index = 0; index < pattern.elements.size(); ++index) {
            const auto element_path = path + ".elements[" + std::to_string(index) + "]";
            if (!pattern.elements[index]) {
                add_error(element_path, "tuple pattern element pointer is null");
                continue;
            }
            verify_match_pattern(*pattern.elements[index], element_path);
        }
    }

    void verify_match_pattern_node(const OrPattern &pattern, const std::string &path,
                                   const SymbolRef & /*matched_enum*/) {
        for (std::uint32_t index = 0; index < pattern.branches.size(); ++index) {
            const auto branch_path = path + ".branches[" + std::to_string(index) + "]";
            if (!pattern.branches[index]) {
                add_error(branch_path, "or pattern branch pointer is null");
                continue;
            }
            verify_match_pattern(*pattern.branches[index], branch_path);
        }
    }

    void verify_expr_ref_list(const std::vector<ExprRef> &exprs, const std::string &path) {
        for (std::uint32_t index = 0; index < exprs.size(); ++index) {
            verify_required_expr_ref(exprs[index], path + "[" + std::to_string(index) + "]");
        }
    }

    void verify_temporal_ptr(const TemporalExprPtr &expr, const std::string &path) {
        if (!expr) {
            add_error(path, "temporal expression pointer is null");
            return;
        }
        verify_source_range(expr->source_range, path, "source range");
        // RFC 0027 P6/P7/P8 (KR6.13-F): one handler per TemporalExprNode
        // alternative, generated from temporal_nodes.def. Every temporal node has
        // its own verifier rule, so all 7 name VERIFY_TEMPORAL_CHECKED; the
        // explicit enumeration (rather than an unnamed generic lambda) means a new
        // temporal node is a COMPILE ERROR here until it is routed.
#define VERIFY_TEMPORAL_CHECKED(Name)                                                           \
    [this, &path](const Name &value) { verify_temporal_node(value, path); },
#define VERIFY_TEMPORAL_EmbeddedTemporalExpr(Name) VERIFY_TEMPORAL_CHECKED(Name)
#define VERIFY_TEMPORAL_CalledTemporalExpr(Name) VERIFY_TEMPORAL_CHECKED(Name)
#define VERIFY_TEMPORAL_InStateTemporalExpr(Name) VERIFY_TEMPORAL_CHECKED(Name)
#define VERIFY_TEMPORAL_RunningTemporalExpr(Name) VERIFY_TEMPORAL_CHECKED(Name)
#define VERIFY_TEMPORAL_CompletedTemporalExpr(Name) VERIFY_TEMPORAL_CHECKED(Name)
#define VERIFY_TEMPORAL_TemporalUnaryExpr(Name) VERIFY_TEMPORAL_CHECKED(Name)
#define VERIFY_TEMPORAL_TemporalBinaryExpr(Name) VERIFY_TEMPORAL_CHECKED(Name)
#define HANDLE_TEMPORAL_NODE(Name) VERIFY_TEMPORAL_##Name(Name)
        std::visit(
            Overloaded{
#include "ahfl/compiler/ir/temporal_nodes.def"
            },
            expr->node);
#undef HANDLE_TEMPORAL_NODE
#undef VERIFY_TEMPORAL_EmbeddedTemporalExpr
#undef VERIFY_TEMPORAL_CalledTemporalExpr
#undef VERIFY_TEMPORAL_InStateTemporalExpr
#undef VERIFY_TEMPORAL_RunningTemporalExpr
#undef VERIFY_TEMPORAL_CompletedTemporalExpr
#undef VERIFY_TEMPORAL_TemporalUnaryExpr
#undef VERIFY_TEMPORAL_TemporalBinaryExpr
#undef VERIFY_TEMPORAL_CHECKED
    }

    void verify_temporal_node(const EmbeddedTemporalExpr &expr, const std::string &path) {
        verify_required_expr_ref(expr.expr, path + ".expr");
    }

    void verify_temporal_node(const CalledTemporalExpr &expr, const std::string &path) {
        if (is_backend_ready_mode(mode_) && contains_sentinel(expr.capability)) {
            add_error(path, "called temporal expression contains sentinel capability");
        }
    }

    void verify_temporal_node(const InStateTemporalExpr &expr, const std::string &path) {
        if (is_backend_ready_mode(mode_) && contains_sentinel(expr.state)) {
            add_error(path, "in_state temporal expression contains sentinel state");
        }
    }

    void verify_temporal_node(const RunningTemporalExpr &expr, const std::string &path) {
        if (is_backend_ready_mode(mode_) && contains_sentinel(expr.node)) {
            add_error(path, "running temporal expression contains sentinel node");
        }
    }

    void verify_temporal_node(const CompletedTemporalExpr &expr, const std::string &path) {
        if (!is_backend_ready_mode(mode_)) {
            return;
        }
        if (contains_sentinel(expr.node)) {
            add_error(path, "completed temporal expression contains sentinel node");
        }
        if (expr.state_name.has_value() && contains_sentinel(*expr.state_name)) {
            add_error(path, "completed temporal expression contains sentinel state");
        }
    }

    void verify_temporal_node(const TemporalUnaryExpr &expr, const std::string &path) {
        verify_temporal_ptr(expr.operand, path + ".operand");
    }

    void verify_temporal_node(const TemporalBinaryExpr &expr, const std::string &path) {
        verify_temporal_ptr(expr.lhs, path + ".lhs");
        verify_temporal_ptr(expr.rhs, path + ".rhs");
    }

    void verify_required_expr_ref(ExprRef ref, const std::string &path) {
        if (!ref) {
            add_error(path, "expression reference is null");
            return;
        }
        verify_expr_ref(ref, path);
    }

    void verify_optional_expr_ref(ExprRef ref, const std::string &path) {
        if (!ref) {
            return;
        }
        verify_expr_ref(ref, path);
    }

    void verify_expr_ref(ExprRef ref, const std::string &path) {
        if (ref.index == ExprRef::kInvalid || ref.index >= program_.expr_arena.size()) {
            add_error(path, "expression reference index is out of range");
            return;
        }
        if (ref.get() == nullptr) {
            add_error(path, "expression reference pointer is null");
            return;
        }
        if (&program_.expr_arena.get(ref.index) != ref.get()) {
            add_error(path, "expression reference pointer does not match arena index");
        }
    }

    void verify_symbol_ref(const SymbolRef &symbol,
                           const std::string &path,
                           std::optional<SymbolRefKind> expected_kind = std::nullopt,
                           std::string_view declaration_name = {}) {
        if (is_backend_ready_mode(mode_)) {
            if (!symbol.id.has_value()) {
                add_error(path, "required symbol reference is missing id");
            }
            if (symbol.kind == SymbolRefKind::Unknown) {
                add_error(path, "required symbol reference has unknown kind");
            }
            if (symbol.canonical_name.empty()) {
                add_error(path, "required symbol reference is missing canonical name");
            }
            if (contains_sentinel(symbol.canonical_name) || contains_sentinel(symbol.local_name) ||
                contains_sentinel(symbol.module_name)) {
                add_error(path, "symbol reference contains sentinel identity");
            }
            if (expected_kind.has_value() && symbol.kind != *expected_kind) {
                add_error(path,
                          "symbol reference has kind " +
                              std::string(symbol_ref_kind_name(symbol.kind)) + ", expected " +
                              std::string(symbol_ref_kind_name(*expected_kind)));
            }
            if (!declaration_name.empty()) {
                if (symbol.local_name.empty()) {
                    add_error(path, "declaration symbol reference is missing local name");
                } else if (symbol.local_name != declaration_name &&
                           (symbol.canonical_name.empty() ||
                            !canonical_matches_decl_name(symbol.canonical_name,
                                                         declaration_name))) {
                    add_error(path,
                              "declaration name '" + std::string(declaration_name) +
                                  "' drifts from symbol local name '" + symbol.local_name + "'");
                }
                if (!symbol.canonical_name.empty() &&
                    !canonical_matches_decl_name(symbol.canonical_name, declaration_name)) {
                    add_error(path,
                              "declaration name '" + std::string(declaration_name) +
                                  "' drifts from symbol canonical name '" + symbol.canonical_name +
                                  "'");
                }
            }
        }
        if (!symbol.id.has_value()) {
            return;
        }
        const auto found = symbol_identities_.find(*symbol.id);
        if (found == symbol_identities_.end()) {
            return;
        }
        const auto name = symbol_ref_name(symbol);
        if (name != "<unknown>" && found->second.name != name) {
            add_error(path,
                      "symbol id " + std::to_string(*symbol.id) + " resolves to " +
                          found->second.name + ", not " + name);
        }
        if (is_backend_ready_mode(mode_) && found->second.kind != SymbolRefKind::Unknown &&
            symbol.kind != SymbolRefKind::Unknown && found->second.kind != symbol.kind) {
            add_error(path,
                      "symbol id " + std::to_string(*symbol.id) + " resolves to kind " +
                          std::string(symbol_ref_kind_name(found->second.kind)) + ", not " +
                          std::string(symbol_ref_kind_name(symbol.kind)));
        }
    }

    void verify_type_ref(const TypeRef &type, const std::string &path) {
        verify_source_range(type.source_range, path, "source range");
        if (is_backend_ready_mode(mode_)) {
            if (type.kind == TypeRefKind::Unresolved) {
                add_error(path, "required type reference is unresolved");
            }
            if (contains_sentinel(type.display_name) || contains_sentinel(type.canonical_name) ||
                contains_sentinel(type.variant_name)) {
                add_error(path, "type reference contains sentinel identity");
            }
            if ((type.kind == TypeRefKind::Struct || type.kind == TypeRefKind::Enum) &&
                type.canonical_name.empty()) {
                add_error(path, "nominal type reference is missing canonical name");
            }
        }
        verify_nominal_ref(type, path);
        switch (type.kind) {
        default:
            if (type.first) {
                verify_type_ref(*type.first, path + ".first");
            }
            if (type.second) {
                verify_type_ref(*type.second, path + ".second");
            }
            break;
        }
        for (std::size_t index = 0; index < type.params.size(); ++index) {
            if (!type.params[index]) {
                add_error(path + ".params[" + std::to_string(index) + "]",
                          "type argument reference is null");
                continue;
            }
            verify_type_ref(*type.params[index], path + ".params[" + std::to_string(index) + "]");
        }
    }

    // RFC 0026 P4 nominal-identity bridge: the resolved nominal SymbolRef a
    // Struct/Enum TypeRef must carry. This is the BackendReady gate that turns
    // the string-only nominal boundary into a symbol-identity one. Only enforced
    // in BackendReady mode (a partially-lowered program may legitimately lack
    // the bridge). Rules:
    //   - Struct/Enum: nominal_ref must be a resolved Type ref whose canonical
    //     name matches the type's; id-present refs are cross-checked against the
    //     symbol-identity map by verify_symbol_ref; a name-only ref (no id) is
    //     permitted per the lowering boundary.
    //   - every other kind: nominal_ref must be Unknown/empty (no stray identity
    //     smuggled onto a primitive / Fn / Unit / ... ref).
    void verify_nominal_ref(const TypeRef &type, const std::string &path) {
        if (!is_backend_ready_mode(mode_)) {
            return;
        }
        const bool is_nominal =
            type.kind == TypeRefKind::Struct || type.kind == TypeRefKind::Enum;
        const auto &nominal = type.nominal_ref;
        if (!is_nominal) {
            if (nominal.kind != SymbolRefKind::Unknown || !nominal.canonical_name.empty() ||
                !nominal.local_name.empty() || !nominal.module_name.empty() ||
                nominal.id.has_value()) {
                add_error(path + ".nominal_ref",
                          "non-nominal type reference carries a stray nominal identity");
            }
            return;
        }
        // Nominal (Struct/Enum) TypeRef.
        if (nominal.kind == SymbolRefKind::Unknown) {
            add_error(path + ".nominal_ref",
                      "nominal type reference is missing its resolved nominal identity");
            return;
        }
        if (nominal.kind != SymbolRefKind::Type) {
            add_error(path + ".nominal_ref",
                      "nominal identity has kind " +
                          std::string(symbol_ref_kind_name(nominal.kind)) + ", expected Type");
            return;
        }
        if (nominal.canonical_name.empty()) {
            add_error(path + ".nominal_ref", "nominal identity is missing canonical name");
        } else if (!type.canonical_name.empty() &&
                   nominal.canonical_name != type.canonical_name) {
            add_error(path + ".nominal_ref",
                      "nominal identity canonical '" + nominal.canonical_name +
                          "' drifts from type canonical '" + type.canonical_name + "'");
        }
        // When the ref carries an id, run the full symbol-identity cross-check
        // (kind + id->identity map) shared with every other SymbolRef consumer.
        // A name-only fallback (no id) is allowed by the lowering boundary.
        if (nominal.id.has_value()) {
            verify_symbol_ref(nominal, path + ".nominal_ref", SymbolRefKind::Type);
        }
    }

    void
    verify_source_range(const SourceRangeOpt &range, const std::string &path, const char *label) {
        if (!range.has_value()) {
            return;
        }
        if (range->end_offset < range->begin_offset) {
            add_error(path, std::string(label) + " has end before begin");
        }
    }

    void verify_analysis_phase() {
        if (program_.phase == ProgramPhase::Lowered) {
            if (has_any_analysis(program_.analyses)) {
                add_warning("program.phase", "lowered program carries derived analyses");
            }
            return;
        }

        if (!has_fresh_derived_analyses(program_)) {
            add_error("program.analyses",
                      "derived analyses are stale for current program analysis revision");
        }

        verify_analysis_owners_and_indexes();

        const bool has_flow_handlers = std::any_of(
            program_.declarations.begin(), program_.declarations.end(), [](const Decl &decl) {
                const auto *flow = std::get_if<FlowDecl>(&decl);
                return flow != nullptr && !flow->state_handlers.empty();
            });
        if (has_flow_handlers && program_.analyses.state_handler_summaries.empty()) {
            add_error("program.analyses", "analyzed program is missing state handler summaries");
        }

        const bool has_workflow_nodes = std::any_of(
            program_.declarations.begin(), program_.declarations.end(), [](const Decl &decl) {
                const auto *workflow = std::get_if<WorkflowDecl>(&decl);
                return workflow != nullptr && !workflow->nodes.empty();
            });
        if (has_workflow_nodes && program_.analyses.workflow_node_input_summaries.empty()) {
            add_error("program.analyses", "analyzed program is missing workflow node summaries");
        }
    }

    void verify_analysis_owners_and_indexes() {
        std::unordered_set<std::string> expected_state_handlers;
        std::unordered_set<std::string> expected_workflow_nodes;
        std::unordered_set<std::string> expected_workflow_returns;
        for (const auto &decl : program_.declarations) {
            if (const auto *flow = std::get_if<FlowDecl>(&decl); flow != nullptr) {
                const auto owner = flow->target_ref.canonical_name;
                for (std::size_t index = 0; index < flow->state_handlers.size(); ++index) {
                    expected_state_handlers.insert(
                        analysis_key(owner, index, flow->state_handlers[index].state_name));
                }
                continue;
            }
            if (const auto *workflow = std::get_if<WorkflowDecl>(&decl); workflow != nullptr) {
                const auto owner = workflow->symbol_ref.canonical_name.empty()
                                       ? workflow->name
                                       : workflow->symbol_ref.canonical_name;
                for (std::size_t index = 0; index < workflow->nodes.size(); ++index) {
                    expected_workflow_nodes.insert(
                        analysis_key(owner, index, workflow->nodes[index].name));
                }
                expected_workflow_returns.insert(analysis_key(owner, 0));
            }
        }

        verify_analysis_entries(expected_state_handlers,
                                program_.analyses.state_handler_summaries,
                                "program.analyses.state_handler_summaries",
                                [](const StateHandlerSummaryAnalysis &entry) {
                                    return analysis_key(
                                        entry.flow_target, entry.handler_index, entry.state_name);
                                });
        verify_analysis_entries(expected_workflow_nodes,
                                program_.analyses.workflow_node_input_summaries,
                                "program.analyses.workflow_node_input_summaries",
                                [](const WorkflowNodeExprSummaryAnalysis &entry) {
                                    return analysis_key(
                                        entry.workflow_name, entry.node_index, entry.node_name);
                                });
        verify_analysis_entries(expected_workflow_returns,
                                program_.analyses.workflow_return_summaries,
                                "program.analyses.workflow_return_summaries",
                                [](const WorkflowReturnExprSummaryAnalysis &entry) {
                                    return analysis_key(entry.workflow_name, 0);
                                });

        std::unordered_set<std::string> formal_observation_symbols;
        for (std::size_t index = 0; index < program_.analyses.formal_observations.size(); ++index) {
            const auto &symbol = program_.analyses.formal_observations[index].symbol;
            if (symbol.empty()) {
                add_error("program.analyses.formal_observations[" + std::to_string(index) + "]",
                          "formal observation is missing symbol");
                continue;
            }
            if (!formal_observation_symbols.insert(symbol).second) {
                add_error("program.analyses.formal_observations[" + std::to_string(index) + "]",
                          "duplicate formal observation symbol " + symbol);
            }
        }
    }

    template <typename EntryT, typename KeyFn>
    void verify_analysis_entries(const std::unordered_set<std::string> &expected,
                                 const std::vector<EntryT> &actual,
                                 const std::string &path,
                                 KeyFn key_of) {
        if (actual.size() != expected.size()) {
            add_error(path,
                      "analysis entry count " + std::to_string(actual.size()) +
                          " does not match declaration count " + std::to_string(expected.size()));
        }
        std::unordered_set<std::string> seen;
        for (std::size_t index = 0; index < actual.size(); ++index) {
            const auto key = key_of(actual[index]);
            const auto entry_path = path + "[" + std::to_string(index) + "]";
            if (!expected.contains(key)) {
                add_error(entry_path, "analysis entry has no matching declaration owner/index");
            }
            if (!seen.insert(key).second) {
                add_error(entry_path, "duplicate analysis entry for declaration owner/index");
            }
        }
    }

    const Program &program_;
    IrVerificationMode mode_;
    VerificationResult result_;
    std::unordered_map<std::size_t, SymbolIdentity> symbol_identities_;
    std::unordered_set<std::uint32_t> statement_ids_;
};

} // namespace

bool VerificationResult::has_errors() const noexcept {
    return std::any_of(diagnostics.begin(), diagnostics.end(), [](const auto &diagnostic) {
        return diagnostic.severity == VerificationSeverity::Error;
    });
}

bool VerificationResult::has_warnings() const noexcept {
    return std::any_of(diagnostics.begin(), diagnostics.end(), [](const auto &diagnostic) {
        return diagnostic.severity == VerificationSeverity::Warning;
    });
}

VerificationResult verify_ir_program(const Program &program) {
    return verify_ir_program(program, IrVerificationMode::Structural);
}

VerificationResult verify_ir_program(const Program &program, IrVerificationMode mode) {
    return ProgramVerifier(program, mode).run();
}

} // namespace ahfl::ir
