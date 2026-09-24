#include "ahfl/compiler/ir/core_json.hpp"

#include "ahfl/compiler/ir/core_verify.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <memory>
#include <unordered_set>
#include <variant>
#include <vector>

#include "ahfl/base/support/overloaded.hpp"
#include "base/json/json_value.hpp"
#include "base/support/json.hpp"

// ---------------------------------------------------------------------------
// Core-IR layered IR-JSON projection (RFC 0026 P9 / KR6.9).
//
// This file is the ONE writer/reader pair for `CoreProgram`. It mirrors the
// single-layer `ir_json.cpp` shape (a streaming printer over the shared
// `PrettyJsonWriter` lexical base, and a `JsonValue`-driven reader) but follows
// the APPROVED design `docs/design/core-ir-p9-layered-json.zh.md` exactly:
//   * envelope + fixed table order (§1, §4);
//   * `kind` discriminators resolved from the X-macro .def tables (§3);
//   * hash-consed value_types written in arena order (§6.1) and REBUILT by
//     interning on read, with the identity-remap assert (§6.2);
//   * fail-closed admission with typed `CoreJsonDiagnostic`s (§7, §9);
//   * `core_program_equal` — the R2 structural-identity obligation (§7).
//
// The writer and reader are co-located so the schema cannot drift between them.
// ---------------------------------------------------------------------------

namespace ahfl::ir::core {

namespace {

using ahfl::json::JsonValue;

// ===========================================================================
// §3 — discriminator name tables (all resolved from the .def X-lists; no
// hand-written `if (kind == "...")` chain on either side).
// ===========================================================================

[[nodiscard]] std::string_view type_decl_kind_name(CoreTypeDecl::Kind kind) {
    switch (kind) {
    case CoreTypeDecl::Kind::Struct:
        return "struct";
    case CoreTypeDecl::Kind::Enum:
        return "enum";
    }
    return "struct";
}

[[nodiscard]] bool parse_type_decl_kind(std::string_view s, CoreTypeDecl::Kind &out) {
    if (s == "struct") {
        out = CoreTypeDecl::Kind::Struct;
        return true;
    }
    if (s == "enum") {
        out = CoreTypeDecl::Kind::Enum;
        return true;
    }
    return false;
}

[[nodiscard]] std::string_view variant_payload_kind_name(CoreTypeDecl::VariantPayload::Kind kind) {
    switch (kind) {
    case CoreTypeDecl::VariantPayload::Kind::Unit:
        return "unit";
    case CoreTypeDecl::VariantPayload::Kind::Tuple:
        return "tuple";
    case CoreTypeDecl::VariantPayload::Kind::Struct:
        return "struct";
    }
    return "unit";
}

[[nodiscard]] bool parse_variant_payload_kind(std::string_view s,
                                              CoreTypeDecl::VariantPayload::Kind &out) {
    if (s == "unit") {
        out = CoreTypeDecl::VariantPayload::Kind::Unit;
        return true;
    }
    if (s == "tuple") {
        out = CoreTypeDecl::VariantPayload::Kind::Tuple;
        return true;
    }
    if (s == "struct") {
        out = CoreTypeDecl::VariantPayload::Kind::Struct;
        return true;
    }
    return false;
}

[[nodiscard]] std::string_view member_template_kind_name(CoreMemberTypeTemplateKind kind) {
    switch (kind) {
    case CoreMemberTypeTemplateKind::Concrete:
        return "concrete";
    case CoreMemberTypeTemplateKind::Param:
        return "param";
    case CoreMemberTypeTemplateKind::Nominal:
        return "nominal";
    case CoreMemberTypeTemplateKind::Fn:
        return "fn";
    }
    return "concrete";
}

[[nodiscard]] bool parse_member_template_kind(std::string_view s, CoreMemberTypeTemplateKind &out) {
    if (s == "concrete") {
        out = CoreMemberTypeTemplateKind::Concrete;
        return true;
    }
    if (s == "param") {
        out = CoreMemberTypeTemplateKind::Param;
        return true;
    }
    if (s == "nominal") {
        out = CoreMemberTypeTemplateKind::Nominal;
        return true;
    }
    if (s == "fn") {
        out = CoreMemberTypeTemplateKind::Fn;
        return true;
    }
    return false;
}

[[nodiscard]] std::string_view variance_name(CoreVariance v) {
    switch (v) {
    case CoreVariance::Invariant:
        return "invariant";
    case CoreVariance::Covariant:
        return "covariant";
    case CoreVariance::Contravariant:
        return "contravariant";
    }
    return "invariant";
}

[[nodiscard]] bool parse_variance_name(std::string_view s, CoreVariance &out) {
    if (s == "invariant") {
        out = CoreVariance::Invariant;
        return true;
    }
    if (s == "covariant") {
        out = CoreVariance::Covariant;
        return true;
    }
    if (s == "contravariant") {
        out = CoreVariance::Contravariant;
        return true;
    }
    return false;
}

[[nodiscard]] std::string_view nominal_role_name(CoreNominalRole role) {
    switch (role) {
    case CoreNominalRole::Ordinary:
        return "ordinary";
    case CoreNominalRole::Option:
        return "option";
    case CoreNominalRole::Result:
        return "result";
    case CoreNominalRole::List:
        return "list";
    case CoreNominalRole::Set:
        return "set";
    case CoreNominalRole::Map:
        return "map";
    }
    return "ordinary";
}

[[nodiscard]] bool parse_nominal_role(std::string_view s, CoreNominalRole &out) {
    if (s == "ordinary") {
        out = CoreNominalRole::Ordinary;
        return true;
    }
    if (s == "option") {
        out = CoreNominalRole::Option;
        return true;
    }
    if (s == "result") {
        out = CoreNominalRole::Result;
        return true;
    }
    if (s == "list") {
        out = CoreNominalRole::List;
        return true;
    }
    if (s == "set") {
        out = CoreNominalRole::Set;
        return true;
    }
    if (s == "map") {
        out = CoreNominalRole::Map;
        return true;
    }
    return false;
}

[[nodiscard]] std::string_view context_kind_name(CoreAgentDecl::ContextKind kind) {
    switch (kind) {
    case CoreAgentDecl::ContextKind::Unit:
        return "unit";
    case CoreAgentDecl::ContextKind::Struct:
        return "struct";
    }
    return "unit";
}

[[nodiscard]] bool parse_context_kind(std::string_view s, CoreAgentDecl::ContextKind &out) {
    if (s == "unit") {
        out = CoreAgentDecl::ContextKind::Unit;
        return true;
    }
    if (s == "struct") {
        out = CoreAgentDecl::ContextKind::Struct;
        return true;
    }
    return false;
}

[[nodiscard]] std::string_view literal_kind_name(CoreLiteralKind kind) {
    switch (kind) {
    case CoreLiteralKind::Bool:
        return "bool";
    case CoreLiteralKind::Integer:
        return "integer";
    case CoreLiteralKind::Float:
        return "float";
    case CoreLiteralKind::Decimal:
        return "decimal";
    case CoreLiteralKind::String:
        return "string";
    case CoreLiteralKind::Duration:
        return "duration";
    case CoreLiteralKind::Unit:
        return "unit";
    }
    return "unit";
}

[[nodiscard]] bool parse_literal_kind(std::string_view s, CoreLiteralKind &out) {
    if (s == "bool") {
        out = CoreLiteralKind::Bool;
        return true;
    }
    if (s == "integer") {
        out = CoreLiteralKind::Integer;
        return true;
    }
    if (s == "float") {
        out = CoreLiteralKind::Float;
        return true;
    }
    if (s == "decimal") {
        out = CoreLiteralKind::Decimal;
        return true;
    }
    if (s == "string") {
        out = CoreLiteralKind::String;
        return true;
    }
    if (s == "duration") {
        out = CoreLiteralKind::Duration;
        return true;
    }
    if (s == "unit") {
        out = CoreLiteralKind::Unit;
        return true;
    }
    return false;
}

[[nodiscard]] std::string_view path_root_name(CorePathRoot root) {
    switch (root) {
    case CorePathRoot::Input:
        return "input";
    case CorePathRoot::Context:
        return "context";
    case CorePathRoot::Local:
        return "local";
    case CorePathRoot::Identifier:
        return "identifier";
    case CorePathRoot::WorkflowInput:
        return "workflow_input";
    case CorePathRoot::WorkflowNodeOutput:
        return "workflow_node_output";
    }
    return "identifier";
}

[[nodiscard]] bool parse_path_root(std::string_view s, CorePathRoot &out) {
    if (s == "input") {
        out = CorePathRoot::Input;
        return true;
    }
    if (s == "context") {
        out = CorePathRoot::Context;
        return true;
    }
    if (s == "local") {
        out = CorePathRoot::Local;
        return true;
    }
    if (s == "identifier") {
        out = CorePathRoot::Identifier;
        return true;
    }
    if (s == "workflow_input") {
        out = CorePathRoot::WorkflowInput;
        return true;
    }
    if (s == "workflow_node_output") {
        out = CorePathRoot::WorkflowNodeOutput;
        return true;
    }
    return false;
}

[[nodiscard]] std::string_view unary_op_name(CoreUnaryOp op) {
    switch (op) {
    case CoreUnaryOp::Not:
        return "not";
    case CoreUnaryOp::Neg:
        return "neg";
    }
    return "not";
}

[[nodiscard]] bool parse_unary_op(std::string_view s, CoreUnaryOp &out) {
    if (s == "not") {
        out = CoreUnaryOp::Not;
        return true;
    }
    if (s == "neg") {
        out = CoreUnaryOp::Neg;
        return true;
    }
    return false;
}

[[nodiscard]] std::string_view binary_op_name(CoreBinaryOp op) {
    switch (op) {
    case CoreBinaryOp::Add:
        return "add";
    case CoreBinaryOp::Sub:
        return "sub";
    case CoreBinaryOp::Mul:
        return "mul";
    case CoreBinaryOp::Div:
        return "div";
    case CoreBinaryOp::Mod:
        return "mod";
    case CoreBinaryOp::Eq:
        return "eq";
    case CoreBinaryOp::Ne:
        return "ne";
    case CoreBinaryOp::Lt:
        return "lt";
    case CoreBinaryOp::Le:
        return "le";
    case CoreBinaryOp::Gt:
        return "gt";
    case CoreBinaryOp::Ge:
        return "ge";
    case CoreBinaryOp::And:
        return "and";
    case CoreBinaryOp::Or:
        return "or";
    }
    return "add";
}

[[nodiscard]] bool parse_binary_op(std::string_view s, CoreBinaryOp &out) {
    if (s == "add") {
        out = CoreBinaryOp::Add;
    } else if (s == "sub") {
        out = CoreBinaryOp::Sub;
    } else if (s == "mul") {
        out = CoreBinaryOp::Mul;
    } else if (s == "div") {
        out = CoreBinaryOp::Div;
    } else if (s == "mod") {
        out = CoreBinaryOp::Mod;
    } else if (s == "eq") {
        out = CoreBinaryOp::Eq;
    } else if (s == "ne") {
        out = CoreBinaryOp::Ne;
    } else if (s == "lt") {
        out = CoreBinaryOp::Lt;
    } else if (s == "le") {
        out = CoreBinaryOp::Le;
    } else if (s == "gt") {
        out = CoreBinaryOp::Gt;
    } else if (s == "ge") {
        out = CoreBinaryOp::Ge;
    } else if (s == "and") {
        out = CoreBinaryOp::And;
    } else if (s == "or") {
        out = CoreBinaryOp::Or;
    } else {
        return false;
    }
    return true;
}

[[nodiscard]] std::string_view coercion_op_name(CoreCoercionOpKind kind) {
    switch (kind) {
    case CoreCoercionOpKind::IntWiden:
        return "int_widen";
    case CoreCoercionOpKind::StringWiden:
        return "string_widen";
    case CoreCoercionOpKind::CapacityWiden:
        return "capacity_widen";
    case CoreCoercionOpKind::TypeArg:
        return "type_arg";
    case CoreCoercionOpKind::FnParam:
        return "fn_param";
    case CoreCoercionOpKind::FnReturn:
        return "fn_return";
    }
    return "int_widen";
}

[[nodiscard]] bool parse_coercion_op(std::string_view s, CoreCoercionOpKind &out) {
    if (s == "int_widen") {
        out = CoreCoercionOpKind::IntWiden;
    } else if (s == "string_widen") {
        out = CoreCoercionOpKind::StringWiden;
    } else if (s == "capacity_widen") {
        out = CoreCoercionOpKind::CapacityWiden;
    } else if (s == "type_arg") {
        out = CoreCoercionOpKind::TypeArg;
    } else if (s == "fn_param") {
        out = CoreCoercionOpKind::FnParam;
    } else if (s == "fn_return") {
        out = CoreCoercionOpKind::FnReturn;
    } else {
        return false;
    }
    return true;
}

[[nodiscard]] std::string_view collection_op_name(CoreCollectionOpKind kind) {
    switch (kind) {
    case CoreCollectionOpKind::Len:
        return "len";
    case CoreCollectionOpKind::ElementGet:
        return "element_get";
    case CoreCollectionOpKind::ElementSet:
        return "element_set";
    }
    return "len";
}

[[nodiscard]] bool parse_collection_op(std::string_view s, CoreCollectionOpKind &out) {
    if (s == "len") {
        out = CoreCollectionOpKind::Len;
        return true;
    }
    if (s == "element_get") {
        out = CoreCollectionOpKind::ElementGet;
        return true;
    }
    if (s == "element_set") {
        out = CoreCollectionOpKind::ElementSet;
        return true;
    }
    return false;
}

[[nodiscard]] std::string_view trap_kind_name(CoreTrapKind kind) {
    switch (kind) {
    case CoreTrapKind::NonExhaustiveMatch:
        return "non_exhaustive_match";
    }
    return "non_exhaustive_match";
}

[[nodiscard]] bool parse_trap_kind(std::string_view s, CoreTrapKind &out) {
    if (s == "non_exhaustive_match") {
        out = CoreTrapKind::NonExhaustiveMatch;
        return true;
    }
    return false;
}

[[nodiscard]] std::string_view capture_mode_name(CoreCaptureMode mode) {
    switch (mode) {
    case CoreCaptureMode::ByValue:
        return "by_value";
    }
    return "by_value";
}

[[nodiscard]] bool parse_capture_mode(std::string_view s, CoreCaptureMode &out) {
    if (s == "by_value") {
        out = CoreCaptureMode::ByValue;
        return true;
    }
    return false;
}

[[nodiscard]] std::string_view symbol_ref_kind_name(ir::SymbolRefKind kind) {
    switch (kind) {
    case ir::SymbolRefKind::Unknown:
        return "unknown";
    case ir::SymbolRefKind::Type:
        return "type";
    case ir::SymbolRefKind::Const:
        return "const";
    case ir::SymbolRefKind::Capability:
        return "capability";
    case ir::SymbolRefKind::Predicate:
        return "predicate";
    case ir::SymbolRefKind::Agent:
        return "agent";
    case ir::SymbolRefKind::Workflow:
        return "workflow";
    case ir::SymbolRefKind::Function:
        return "function";
    }
    return "unknown";
}

[[nodiscard]] bool parse_symbol_ref_kind(std::string_view s, ir::SymbolRefKind &out) {
    if (s == "unknown") {
        out = ir::SymbolRefKind::Unknown;
    } else if (s == "type") {
        out = ir::SymbolRefKind::Type;
    } else if (s == "const") {
        out = ir::SymbolRefKind::Const;
    } else if (s == "capability") {
        out = ir::SymbolRefKind::Capability;
    } else if (s == "predicate") {
        out = ir::SymbolRefKind::Predicate;
    } else if (s == "agent") {
        out = ir::SymbolRefKind::Agent;
    } else if (s == "workflow") {
        out = ir::SymbolRefKind::Workflow;
    } else if (s == "function") {
        out = ir::SymbolRefKind::Function;
    } else {
        return false;
    }
    return true;
}

[[nodiscard]] std::string_view capability_effect_kind_name(ir::CapabilityEffectKind kind) {
    switch (kind) {
    case ir::CapabilityEffectKind::Unknown:
        return "unknown";
    case ir::CapabilityEffectKind::Read:
        return "read";
    case ir::CapabilityEffectKind::ExternalSideEffect:
        return "external_side_effect";
    case ir::CapabilityEffectKind::DurableWrite:
        return "durable_write";
    case ir::CapabilityEffectKind::FinancialWrite:
        return "financial_write";
    }
    return "unknown";
}

[[nodiscard]] bool parse_capability_effect_kind(std::string_view s,
                                                ir::CapabilityEffectKind &out) {
    if (s == "unknown") {
        out = ir::CapabilityEffectKind::Unknown;
    } else if (s == "read") {
        out = ir::CapabilityEffectKind::Read;
    } else if (s == "external_side_effect") {
        out = ir::CapabilityEffectKind::ExternalSideEffect;
    } else if (s == "durable_write") {
        out = ir::CapabilityEffectKind::DurableWrite;
    } else if (s == "financial_write") {
        out = ir::CapabilityEffectKind::FinancialWrite;
    } else {
        return false;
    }
    return true;
}

/// §4 — reverse lookup of a `kind` string in an X-macro-generated wire-name
/// table. Returns the `variant_index` when found.
[[nodiscard]] std::optional<std::size_t>
wire_name_index(const auto &table, std::string_view name) {
    for (std::size_t i = 0; i < table.size(); ++i) {
        if (table[i] == name) {
            return i;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::string_view value_type_wire_name(std::size_t index) {
    return core_value_type_wire_name(index);
}

[[nodiscard]] std::string_view expr_wire_name(std::size_t index) {
    return core_expr_node_wire_name(index);
}

[[nodiscard]] std::string_view pattern_wire_name(std::size_t index) {
    return core_pattern_node_wire_name(index);
}

[[nodiscard]] std::string_view stmt_wire_name(std::size_t index) {
    return core_stmt_node_wire_name(index);
}

// ===========================================================================
// Writer — RFC 0026 P9 §2 lexical rules over the shared PrettyJsonWriter base.
// ===========================================================================

class CoreJsonPrinter final : private PrettyJsonWriter {
  public:
    explicit CoreJsonPrinter(std::ostream &out, std::string *error)
        : PrettyJsonWriter(out), error_(error) {}

    /// Render the document into the bound stream. A REQUIRED-valid id left
    /// kInvalid (§2) latches `failed()`; the caller must NOT publish the stream
    /// in that case (see `print_core_ir_json`, which renders into a scratch
    /// buffer and suppresses the whole artifact).
    void print(const CoreProgram &program) {
        // Fail-closed audit BEFORE emitting anything: a REQUIRED-valid id left
        // kInvalid (§2) is a writer error, so the document is suppressed rather
        // than publishing the kInvalid sentinel. The ONE legal serialized
        // kInvalid is `field_nominal_types` (sparse navigation-only, §4).
        audit_required_ids(program);
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(program.format_version); });
            field("layer", [&]() { write_string("core"); });
            field("types", [&]() { print_types(program.types, 1); });
            field("value_types", [&]() { print_value_types(program.value_types, 1); });
            field("capabilities", [&]() { print_capabilities(program.capabilities, 1); });
            field("agents", [&]() { print_agents(program.agents, 1); });
            field("flows", [&]() { print_flows(program.flows, 1); });
            field("workflows", [&]() { print_workflows(program.workflows, 1); });
            field("instances", [&]() { print_instances(program.instances, 1); });
            field("fns", [&]() { print_fns(program.fns, 1); });
        });
        out() << '\n';
    }

    /// True once a REQUIRED-valid id was found kInvalid anywhere in the walk
    /// (the up-front audit or a per-field writer). The rendered bytes are then
    /// not a valid artifact and must not be published.
    [[nodiscard]] bool failed() const noexcept {
        return failed_;
    }

  private:
    // --- write helpers (PrettyJsonWriter supplies print_object/print_array and
    // its indent / newline / write_string members). ---

    void fail(std::string message) {
        failed_ = true;
        if (error_ != nullptr && error_->empty()) {
            *error_ = std::move(message);
        }
    }

    /// Walk EVERY REQUIRED-valid id slot and report the first `kInvalid`. This is
    /// the writer's fail-closed gate: doing it up front means the error message
    /// names the field and the whole document is suppressed before any byte is
    /// published. It is not the ONLY gate — every `write_required_*` writer also
    /// latches, so a slot added later without an audit entry still fails closed
    /// (as `null`, never as a `"key": ,` fragment or the `4294967295` sentinel).
    ///
    /// NOT audited (kInvalid is LEGAL there, §4): the `field_nominal_types`
    /// elements, a projection step's trailing `result_type`, a struct-literal
    /// `CoreConstructExpr::variant`, an unresolved `CorePathExpr::root_type`, and
    /// every optional/kind-masked field the writer omits by presence.
    void audit_required_ids(const CoreProgram &program) {
        const auto require_type = [&](CoreTypeId id, const char *what) {
            if (id.value == CoreTypeId::kInvalid) {
                fail(std::string("required type id '") + what + "' is kInvalid");
            }
        };
        const auto require_value_type = [&](CoreValueTypeId id, const char *what) {
            if (id.value == CoreValueTypeId::kInvalid) {
                fail(std::string("required value-type id '") + what + "' is kInvalid");
            }
        };
        const auto require_template_node = [&](CoreMemberTypeTemplateNodeId id, const char *what) {
            if (id.value == CoreMemberTypeTemplateNodeId::kInvalid) {
                fail(std::string("required member-template id '") + what + "' is kInvalid");
            }
        };
        for (const auto &type : program.types) {
            for (const auto &root : type.field_type_template_roots) {
                require_template_node(root, "type.field_type_template_roots");
            }
            for (const auto &payload : type.variant_payloads) {
                for (const auto &root : payload.slot_type_template_roots) {
                    require_template_node(root, "variant_payload.slot_type_template_roots");
                }
            }
            for (const auto &node : type.member_type_templates) {
                using K = CoreMemberTypeTemplateKind;
                switch (node.kind) {
                case K::Concrete:
                    require_value_type(node.concrete, "member_template.concrete");
                    break;
                case K::Param:
                    break;
                case K::Nominal:
                    require_type(node.nominal, "member_template.nominal");
                    for (const auto &child : node.children) {
                        require_template_node(child, "member_template.children");
                    }
                    break;
                case K::Fn:
                    require_template_node(node.fn_return, "member_template.fn_return");
                    for (const auto &child : node.children) {
                        require_template_node(child, "member_template.children");
                    }
                    break;
                }
            }
        }
        for (const auto &cap : program.capabilities) {
            for (const auto &param : cap.param_types) {
                require_value_type(param, "capability.param_types");
            }
            require_value_type(cap.return_type, "capability.return_type");
        }
        for (const auto &agent : program.agents) {
            require_type(agent.input_type, "agent.input");
            require_type(agent.output_type, "agent.output");
            if (agent.context_kind == CoreAgentDecl::ContextKind::Struct) {
                require_type(agent.context_type, "agent.context");
            }
        }
        for (const auto &flow : program.flows) {
            if (flow.target.value == CoreAgentId::kInvalid) {
                fail("required id 'flow.agent' is kInvalid");
            }
        }
        for (const auto &vt : program.value_types) {
            // The program-global arena's OWN required child ids (§6.1): a
            // kInvalid child would otherwise serialize as the `4294967295`
            // sentinel. `CoreVtInt::bounds` / `CoreVtString::length_bounds` /
            // `CoreVtDecimal::scale` are plain scalars and carry no id.
            std::visit(
                Overloaded{
                    [&](const CoreVtNominal &n) {
                        require_type(n.base, "value_type.nominal.base");
                        for (const auto &arg : n.args) {
                            require_value_type(arg, "value_type.nominal.args");
                        }
                    },
                    [&](const CoreVtTuple &n) {
                        for (const auto &el : n.elements) {
                            require_value_type(el, "value_type.tuple.elements");
                        }
                    },
                    [&](const CoreVtFn &n) {
                        for (const auto &param : n.params) {
                            require_value_type(param, "value_type.fn.params");
                        }
                        require_value_type(n.ret, "value_type.fn.ret");
                    },
                    [&](const CoreVtClosure &n) {
                        require_value_type(n.signature, "value_type.closure.signature");
                        for (const auto &capture : n.captures) {
                            require_value_type(capture.value_type,
                                               "value_type.closure.captures");
                        }
                    },
                    [](const auto &) {},
                },
                vt.node);
        }
        for (const auto &workflow : program.workflows) {
            if (workflow.id.value == CoreWorkflowId::kInvalid) {
                fail("required id 'workflow.id' is kInvalid");
            }
            require_type(workflow.input_type, "workflow.input");
            require_type(workflow.output_type, "workflow.output");
            for (const auto &node : workflow.nodes) {
                if (node.target_instance.value == CoreInstanceId::kInvalid) {
                    fail("required instance id 'workflow_node.target' is kInvalid");
                }
            }
        }
        for (const auto &instance : program.instances) {
            for (const auto &dispatch : instance.dispatch_types) {
                require_value_type(dispatch, "instance.dispatch_types");
            }
            std::visit(
                Overloaded{
                    [&](const CoreCapabilityInstance &p) {
                        if (p.base.value == CoreCapabilityId::kInvalid) {
                            fail("required id 'instance.payload.base' is kInvalid");
                        }
                    },
                    [&](const CorePredicateInstance &) {},
                    [&](const CoreAgentInstance &p) {
                        if (p.base.value == CoreAgentId::kInvalid) {
                            fail("required id 'instance.payload.base' is kInvalid");
                        }
                        require_type(p.input_type, "instance.payload.input_type");
                        if (p.context_kind == CoreAgentDecl::ContextKind::Struct) {
                            require_type(p.context_type, "instance.payload.context_type");
                        }
                        require_type(p.output_type, "instance.payload.output_type");
                    },
                    [&](const CoreWorkflowInstance &p) {
                        if (p.base.value == CoreWorkflowId::kInvalid) {
                            fail("required id 'instance.payload.base' is kInvalid");
                        }
                        require_type(p.input_type, "instance.payload.input_type");
                        require_type(p.output_type, "instance.payload.output_type");
                    },
                    [&](const CoreFnInstance &) {},
                },
                instance.payload);
        }
    }

    void write_index(std::size_t value) { out() << value; }
    void write_u32(std::uint32_t v) { out() << v; }
    void write_i64(std::int64_t v) { out() << v; }
    void write_u64(std::uint64_t v) { out() << v; }
    void write_bool(bool v) { out() << (v ? "true" : "false"); }
    void write_null() { out() << "null"; }

    /// Write a REQUIRED-valid typed id (a `CoreXxxId`) as a bare integer. A
    /// `kInvalid` value is never emitted here (§2): it is a lowering-ERROR /
    /// pre-verify state that a verifier-clean program never carries, so it is a
    /// writer error. `null` (not an empty fragment) keeps the scratch buffer
    /// syntactically well formed even though the caller suppresses it.
    template <typename Id> void write_required_id(Id id, const char *what) {
        if (id.value == Id::kInvalid) {
            fail(std::string("required id '") + what +
                 "' is kInvalid; a verifier-clean program never carries one");
            write_null();
            return;
        }
        write_index(id.value);
    }

    void write_string_array(const std::vector<std::string> &values, int indent_level) {
        print_array(indent_level, [&](const auto &item) {
            for (const auto &v : values) {
                item([&]() { write_string(v); });
            }
        });
    }

    void write_source_range(SourceRange range, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("begin_offset", [&]() { write_index(range.begin_offset); });
            field("end_offset", [&]() { write_index(range.end_offset); });
        });
    }

    template <typename Field>
    void write_source_range_field(const Field &field, const SourceRangeOpt &range,
                                  int indent_level) {
        if (range.has_value()) {
            field("source_range", [&]() { write_source_range(*range, indent_level); });
        }
    }

    void write_symbol_ref(const ir::SymbolRef &ref, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("kind", [&]() { write_string(symbol_ref_kind_name(ref.kind)); });
            field("canonical_name", [&]() { write_string(ref.canonical_name); });
            if (!ref.local_name.empty()) {
                field("local_name", [&]() { write_string(ref.local_name); });
            }
            if (!ref.module_name.empty()) {
                field("module_name", [&]() { write_string(ref.module_name); });
            }
            if (ref.id.has_value()) {
                field("id", [&]() { write_index(*ref.id); });
            }
        });
    }

    // --- §4 program-global tables ---

    void print_types(const std::vector<CoreTypeDecl> &types, int indent_level) {
        print_array(indent_level, [&](const auto &item) {
            for (const auto &type : types) {
                item([&]() { print_type_decl(type, indent_level + 1); });
            }
        });
    }

    void print_type_decl(const CoreTypeDecl &type, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("kind", [&]() { write_string(type_decl_kind_name(type.kind)); });
            field("name", [&]() { write_string(type.name); });
            field("fields", [&]() { write_string_array(type.fields, indent_level + 1); });
            field("field_nominal_types", [&]() {
                // The ONE legal serialized `kInvalid` (§4): a navigation-only
                // sparse slot encodes as JSON `null`, never `4294967295`.
                print_array(indent_level + 1, [&](const auto &array_item) {
                    for (const auto &nominal : type.field_nominal_types) {
                        array_item([&]() {
                            if (nominal.value == CoreTypeId::kInvalid) {
                                write_null();
                            } else {
                                write_index(nominal.value);
                            }
                        });
                    }
                });
            });
            field("field_has_default", [&]() {
                print_array(indent_level + 1, [&](const auto &array_item) {
                    for (const bool flag : type.field_has_default) {
                        array_item([&]() { write_bool(flag); });
                    }
                });
            });
            field("variants", [&]() { write_string_array(type.variants, indent_level + 1); });
            field("variant_payloads", [&]() {
                print_array(indent_level + 1, [&](const auto &payload_item) {
                    for (const auto &payload : type.variant_payloads) {
                        payload_item([&]() { print_variant_payload(payload, indent_level + 2); });
                    }
                });
            });
            field("member_type_templates", [&]() {
                print_member_templates(type.member_type_templates, indent_level + 1);
            });
            field("field_type_template_roots", [&]() {
                print_array(indent_level + 1, [&](const auto &root_item) {
                    for (const auto &root : type.field_type_template_roots) {
                        root_item([&]() { write_required_template_root(root); });
                    }
                });
            });
            field("type_param_count", [&]() { write_u32(type.type_param_count); });
            field("variances", [&]() {
                print_array(indent_level + 1, [&](const auto &variance_item) {
                    for (const auto v : type.variances) {
                        variance_item([&]() { write_string(variance_name(v)); });
                    }
                });
            });
            field("role", [&]() { write_string(nominal_role_name(type.role)); });
            field("symbol_ref", [&]() { write_symbol_ref(type.symbol_ref, indent_level + 1); });
            write_source_range_field(field, type.source_range, indent_level + 1);
        });
    }

    void write_required_template_root(CoreMemberTypeTemplateNodeId id) {
        if (id.value == CoreMemberTypeTemplateNodeId::kInvalid) {
            fail("member template root is kInvalid");
            write_null();
            return;
        }
        write_index(id.value);
    }

    void print_variant_payload(const CoreTypeDecl::VariantPayload &payload, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("kind", [&]() { write_string(variant_payload_kind_name(payload.kind)); });
            field("slot_type_template_roots", [&]() {
                print_array(indent_level + 1, [&](const auto &root_item) {
                    for (const auto &root : payload.slot_type_template_roots) {
                        root_item([&]() { write_required_template_root(root); });
                    }
                });
            });
            field("field_names", [&]() { write_string_array(payload.field_names, indent_level + 1); });
        });
    }

    void print_member_templates(const std::vector<CoreMemberTypeTemplateNode> &nodes,
                                int indent_level) {
        print_array(indent_level, [&](const auto &item) {
            for (const auto &node : nodes) {
                item([&]() { print_member_template(node, indent_level + 1); });
            }
        });
    }

    void print_member_template(const CoreMemberTypeTemplateNode &node, int indent_level) {
        // Per-kind field mask (§4): emit only the fields the kind uses, matching
        // the mask the Core verifier enforces. The reader enforces the SAME mask.
        using K = CoreMemberTypeTemplateKind;
        print_object(indent_level, [&](const auto &field) {
            field("kind", [&]() { write_string(member_template_kind_name(node.kind)); });
            if (node.kind == K::Concrete) {
                field("concrete",
                      [&]() { write_required_value_type_id(node.concrete, "concrete"); });
            } else if (node.kind == K::Param) {
                field("param_index", [&]() { write_u32(node.param_index); });
            } else if (node.kind == K::Nominal) {
                field("nominal",
                      [&]() { write_required_type_id(node.nominal, "member_template.nominal"); });
                if (node.capacity.has_value()) {
                    field("capacity", [&]() { write_u64(*node.capacity); });
                }
                field("children", [&]() {
                    print_array(indent_level + 1, [&](const auto &child_item) {
                        for (const auto &child : node.children) {
                            child_item([&]() { write_required_template_root(child); });
                        }
                    });
                });
            } else { // Fn
                field("children", [&]() {
                    print_array(indent_level + 1, [&](const auto &child_item) {
                        for (const auto &child : node.children) {
                            child_item([&]() { write_required_template_root(child); });
                        }
                    });
                });
                field("fn_return", [&]() { write_required_template_root(node.fn_return); });
            }
        });
    }

    void print_value_types(const std::vector<CoreValueType> &value_types, int indent_level) {
        print_array(indent_level, [&](const auto &item) {
            for (const auto &vt : value_types) {
                item([&]() { print_value_type(vt, indent_level + 1); });
            }
        });
    }

    void print_value_type(const CoreValueType &vt, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("kind", [&]() { write_string(value_type_wire_name(vt.node.index())); });
            std::visit(
                Overloaded{
                    [&](const CoreVtUnit &) {},
                    [&](const CoreVtNever &) {},
                    [&](const CoreVtBool &) {},
                    [&](const CoreVtInt &n) {
                        if (n.bounds.has_value()) {
                            field("bounds", [&]() { write_bounds(*n.bounds, indent_level + 1); });
                        }
                    },
                    [&](const CoreVtFloat &) {},
                    [&](const CoreVtString &n) {
                        if (n.length_bounds.has_value()) {
                            field("length_bounds",
                                  [&]() { write_bounds(*n.length_bounds, indent_level + 1); });
                        }
                    },
                    [&](const CoreVtDecimal &n) { field("scale", [&]() { write_i64(n.scale); }); },
                    [&](const CoreVtDuration &) {},
                    [&](const CoreVtTimestamp &) {},
                    [&](const CoreVtUuid &) {},
                    [&](const CoreVtNominal &n) {
                        field("base", [&]() { write_index(n.base.value); });
                        field("args", [&]() {
                            print_array(indent_level + 1, [&](const auto &arg_item) {
                                for (const auto &arg : n.args) {
                                    arg_item([&]() { write_index(arg.value); });
                                }
                            });
                        });
                        if (n.capacity.has_value()) {
                            field("capacity", [&]() { write_u64(*n.capacity); });
                        }
                    },
                    [&](const CoreVtTuple &n) {
                        field("elements", [&]() {
                            print_array(indent_level + 1, [&](const auto &el_item) {
                                for (const auto &el : n.elements) {
                                    el_item([&]() { write_index(el.value); });
                                }
                            });
                        });
                    },
                    [&](const CoreVtFn &n) {
                        field("params", [&]() {
                            print_array(indent_level + 1, [&](const auto &p_item) {
                                for (const auto &p : n.params) {
                                    p_item([&]() { write_index(p.value); });
                                }
                            });
                        });
                        field("ret", [&]() { write_index(n.ret.value); });
                    },
                    [&](const CoreVtClosure &n) {
                        field("signature", [&]() { write_index(n.signature.value); });
                        field("captures", [&]() {
                            print_array(indent_level + 1, [&](const auto &c_item) {
                                for (const auto &capture : n.captures) {
                                    c_item([&]() {
                                        print_object(indent_level + 2, [&](const auto &c_field) {
                                            c_field("value_type", [&]() {
                                                write_index(capture.value_type.value);
                                            });
                                            c_field("mode", [&]() {
                                                write_string(capture_mode_name(capture.mode));
                                            });
                                        });
                                    });
                                }
                            });
                        });
                    },
                },
                vt.node);
        });
    }

    void write_bounds(const std::pair<std::int64_t, std::int64_t> &bounds, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("minimum", [&]() { write_i64(bounds.first); });
            field("maximum", [&]() { write_i64(bounds.second); });
        });
    }

    void print_capabilities(const std::vector<CoreCapabilityDecl> &capabilities, int indent_level) {
        print_array(indent_level, [&](const auto &item) {
            for (const auto &cap : capabilities) {
                item([&]() { print_capability(cap, indent_level + 1); });
            }
        });
    }

    void print_capability(const CoreCapabilityDecl &cap, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("name", [&]() { write_string(cap.name); });
            field("symbol_ref", [&]() { write_symbol_ref(cap.symbol_ref, indent_level + 1); });
            field("effect_kind",
                  [&]() { write_string(capability_effect_kind_name(cap.effect_kind)); });
            field("param_types", [&]() {
                print_array(indent_level + 1, [&](const auto &param_item) {
                    for (const auto &param : cap.param_types) {
                        param_item([
                            &]() { write_required_value_type_id(param, "capability.param_types"); });
                    }
                });
            });
            field("return_type", [&]() {
                write_required_value_type_id(cap.return_type, "capability.return_type");
            });
            write_source_range_field(field, cap.source_range, indent_level + 1);
        });
    }

    void print_agents(const std::vector<CoreAgentDecl> &agents, int indent_level) {
        print_array(indent_level, [&](const auto &item) {
            for (const auto &agent : agents) {
                item([&]() { print_agent(agent, indent_level + 1); });
            }
        });
    }

    void print_agent(const CoreAgentDecl &agent, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("name", [&]() { write_string(agent.name); });
            field("symbol_ref", [&]() { write_symbol_ref(agent.symbol_ref, indent_level + 1); });
            field("states", [&]() { write_string_array(agent.states, indent_level + 1); });
            field("initial", [&]() { write_index(agent.initial.value); });
            field("finals", [&]() {
                print_array(indent_level + 1, [&](const auto &final_item) {
                    for (const auto &fin : agent.finals) {
                        final_item([&]() { write_index(fin.value); });
                    }
                });
            });
            field("transitions", [&]() {
                print_array(indent_level + 1, [&](const auto &t_item) {
                    for (const auto &transition : agent.transitions) {
                        t_item([&]() {
                            print_object(indent_level + 2, [&](const auto &t_field) {
                                t_field("from", [&]() { write_index(transition.from.value); });
                                t_field("to", [&]() { write_index(transition.to.value); });
                            });
                        });
                    }
                });
            });
            field("input_type", [&]() { write_required_type_id(agent.input_type, "agent.input"); });
            // `context_type` is emitted ONLY for a Struct context (§4); a Unit
            // context has kInvalid and the whole field is omitted.
            if (agent.context_kind == CoreAgentDecl::ContextKind::Struct) {
                field("context_type", [&]() { write_index(agent.context_type.value); });
            }
            field("output_type",
                  [&]() { write_required_type_id(agent.output_type, "agent.output"); });
            field("context_kind",
                  [&]() { write_string(context_kind_name(agent.context_kind)); });
            field("capabilities", [&]() {
                print_array(indent_level + 1, [&](const auto &cap_item) {
                    for (const auto &cap : agent.capabilities) {
                        cap_item([&]() { write_index(cap.value); });
                    }
                });
            });
            write_source_range_field(field, agent.source_range, indent_level + 1);
        });
    }

    void write_required_type_id(CoreTypeId id, const char *what) {
        if (id.value == CoreTypeId::kInvalid) {
            fail(std::string("required type id '") + what + "' is kInvalid");
            write_null();
            return;
        }
        write_index(id.value);
    }

    void write_required_value_type_id(CoreValueTypeId id, const char *what) {
        if (id.value == CoreValueTypeId::kInvalid) {
            fail(std::string("required value-type id '") + what + "' is kInvalid");
            write_null();
            return;
        }
        write_index(id.value);
    }

    // --- §5 per-body arenas ---

    void print_flows(const std::vector<CoreFlowDecl> &flows, int indent_level) {
        print_array(indent_level, [&](const auto &item) {
            for (const auto &flow : flows) {
                item([&]() { print_flow(flow, indent_level + 1); });
            }
        });
    }

    void print_flow(const CoreFlowDecl &flow, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("agent", [&]() { write_required_id(flow.target, "flow.agent"); });
            field("agent_name", [&]() { write_string(flow.agent_name); });
            field("target_ref", [&]() { write_symbol_ref(flow.target_ref, indent_level + 1); });
            field("value_count", [&]() { write_u32(flow.storage.value_count); });
            field("exprs", [&]() { print_exprs(flow.storage.exprs, indent_level + 1); });
            field("value_types", [&]() {
                print_array(indent_level + 1, [&](const auto &vt_item) {
                    for (const auto &vt : flow.storage.value_types) {
                        vt_item([&]() { write_index(vt.value); });
                    }
                });
            });
            field("coercion_plans", [&]() {
                print_coercion_plans(flow.storage.coercion_plans, indent_level + 1);
            });
            field("patterns", [&]() { print_patterns(flow.storage.patterns, indent_level + 1); });
            field("states", [&]() {
                print_array(indent_level + 1, [&](const auto &state_item) {
                    for (const auto &state : flow.states) {
                        state_item([&]() { print_flow_state(state, indent_level + 2); });
                    }
                });
            });
        });
    }

    // RFC 0026 FB-1: outlined fn bodies. The wire field layout keeps each
    // CoreBodyStorage's five arenas inline (one object per fn, same rule as
    // flow/workflow), plus the 1:1 instance link, pre-bound params, body
    // region, and display name.
    void print_fns(const std::vector<CoreFnDecl> &fns, int indent_level) {
        print_array(indent_level, [&](const auto &item) {
            for (const auto &fn : fns) {
                item([&]() { print_fn(fn, indent_level + 1); });
            }
        });
    }

    void print_fn(const CoreFnDecl &fn, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("id", [&]() { write_required_id(fn.id, "fn.id"); });
            field("instance", [&]() { write_required_id(fn.instance, "fn.instance"); });
            field("origin", [&]() { write_symbol_ref(fn.origin, indent_level + 1); });
            field("params", [&]() {
                print_array(indent_level + 1, [&](const auto &p) {
                    for (const CoreValueId id : fn.params) {
                        p([&]() { write_index(id.value); });
                    }
                });
            });
            // FB-3a1: the fn body's DECLARED env capture signature (program-
            // global logical value types, env-slot order); empty for an ordinary
            // fn. Mapped through the reader's value-type remap like every other
            // value-type id.
            field("captures", [&]() {
                print_array(indent_level + 1, [&](const auto &c) {
                    for (const CoreValueTypeId cap : fn.captures) {
                        c([&]() { write_index(cap.value); });
                    }
                });
            });
            // FB-3a2: the pre-bound in-body SSA values for the declared env
            // slots, parallel to `captures` (body-local value ids, like params).
            field("env_bindings", [&]() {
                print_array(indent_level + 1, [&](const auto &b) {
                    for (const CoreValueId slot : fn.env_bindings) {
                        b([&]() { write_index(slot.value); });
                    }
                });
            });
            field("value_count", [&]() { write_u32(fn.storage.value_count); });
            field("exprs", [&]() { print_exprs(fn.storage.exprs, indent_level + 1); });
            field("value_types", [&]() {
                print_array(indent_level + 1, [&](const auto &vt_item) {
                    for (const auto &vt : fn.storage.value_types) {
                        vt_item([&]() { write_index(vt.value); });
                    }
                });
            });
            field("coercion_plans",
                  [&]() { print_coercion_plans(fn.storage.coercion_plans, indent_level + 1); });
            field("patterns", [&]() { print_patterns(fn.storage.patterns, indent_level + 1); });
            field("body", [&]() { print_region(fn.body, indent_level + 1); });
            field("name", [&]() { write_string(fn.name); });
            write_source_range_field(field, fn.source_range, indent_level + 1);
        });
    }

    void print_flow_state(const CoreFlowState &state, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("state", [&]() { write_index(state.state.value); });
            field("state_name", [&]() { write_string(state.state_name); });
            field("policy", [&]() { print_policy(state.policy, indent_level + 1); });
            field("body", [&]() { print_region(state.body, indent_level + 1); });
        });
    }

    void print_policy(const CoreStatePolicy &policy, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            if (policy.retry_limit.has_value()) {
                field("retry_limit", [&]() { write_string(*policy.retry_limit); });
            }
            if (!policy.retry_on.empty()) {
                field("retry_on", [&]() { write_string_array(policy.retry_on, indent_level + 1); });
            }
            if (policy.timeout.has_value()) {
                field("timeout", [&]() { write_string(*policy.timeout); });
            }
        });
    }

    void print_workflows(const std::vector<CoreWorkflowDecl> &workflows, int indent_level) {
        print_array(indent_level, [&](const auto &item) {
            for (const auto &workflow : workflows) {
                item([&]() { print_workflow(workflow, indent_level + 1); });
            }
        });
    }

    void print_workflow(const CoreWorkflowDecl &workflow, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("id", [&]() { write_required_id(workflow.id, "workflow.id"); });
            field("name", [&]() { write_string(workflow.name); });
            field("symbol_ref", [&]() { write_symbol_ref(workflow.symbol_ref, indent_level + 1); });
            field("input_type",
                  [&]() { write_required_type_id(workflow.input_type, "workflow.input"); });
            field("output_type",
                  [&]() { write_required_type_id(workflow.output_type, "workflow.output"); });
            field("value_count", [&]() { write_u32(workflow.storage.value_count); });
            field("exprs", [&]() { print_exprs(workflow.storage.exprs, indent_level + 1); });
            field("value_types", [&]() {
                print_array(indent_level + 1, [&](const auto &vt_item) {
                    for (const auto &vt : workflow.storage.value_types) {
                        vt_item([&]() { write_index(vt.value); });
                    }
                });
            });
            field("coercion_plans", [&]() {
                print_coercion_plans(workflow.storage.coercion_plans, indent_level + 1);
            });
            field("patterns", [&]() { print_patterns(workflow.storage.patterns, indent_level + 1); });
            field("nodes", [&]() {
                print_array(indent_level + 1, [&](const auto &node_item) {
                    for (const auto &node : workflow.nodes) {
                        node_item([&]() { print_workflow_node(node, indent_level + 2); });
                    }
                });
            });
            if (workflow.return_region) {
                field("return_region",
                      [&]() { print_region(*workflow.return_region, indent_level + 1); });
            }
        });
    }

    void print_workflow_node(const CoreWorkflowNode &node, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("id", [&]() { write_index(node.id.value); });
            field("target_instance",
                  [&]() { write_required_id(node.target_instance, "workflow_node.target"); });
            field("node_name", [&]() { write_string(node.node_name); });
            field("target_ref", [&]() { write_symbol_ref(node.target_ref, indent_level + 1); });
            field("after", [&]() {
                print_array(indent_level + 1, [&](const auto &after_item) {
                    for (const auto &dep : node.after) {
                        after_item([&]() { write_index(dep.value); });
                    }
                });
            });
            if (node.input_region) {
                field("input_region", [&]() { print_region(*node.input_region, indent_level + 1); });
            }
        });
    }

    void print_exprs(const std::vector<CoreExpr> &exprs, int indent_level) {
        print_array(indent_level, [&](const auto &item) {
            for (const auto &expr : exprs) {
                item([&]() { print_expr(expr, indent_level + 1); });
            }
        });
    }

    void print_expr(const CoreExpr &expr, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("kind", [&]() { write_string(expr_wire_name(expr.node.index())); });
            write_source_range_field(field, expr.source_range, indent_level + 1);
            field("result_type", [&]() { write_index(expr.result_type.value); });
            std::visit(
                Overloaded{
                    [&](const CoreLiteralExpr &n) {
                        field("literal_kind", [&]() { write_string(literal_kind_name(n.kind)); });
                        field("spelling", [&]() { write_string(n.spelling); });
                    },
                    [&](const CoreValueRefExpr &n) {
                        field("value", [&]() { write_index(n.value.value); });
                    },
                    [&](const CorePathExpr &n) { print_path_expr(n, field, indent_level + 1); },
                    [&](const CoreQualifiedExpr &n) {
                        field("name", [&]() { write_string(n.name); });
                        field("type_id", [&]() { write_index(n.type_id.value); });
                        field("variant", [&]() { write_index(n.variant.value); });
                        field("resolved", [&]() { write_bool(n.resolved); });
                    },
                    [&](const CoreUnaryExpr &n) {
                        field("op", [&]() { write_string(unary_op_name(n.op)); });
                        field("operand", [&]() { write_index(n.operand.value); });
                    },
                    [&](const CoreBinaryExpr &n) {
                        field("op", [&]() { write_string(binary_op_name(n.op)); });
                        field("lhs", [&]() { write_index(n.lhs.value); });
                        field("rhs", [&]() { write_index(n.rhs.value); });
                    },
                    [&](const CoreConstructExpr &n) {
                        field("type_name", [&]() { write_string(n.type_name); });
                        field("variant_name", [&]() { write_string(n.variant_name); });
                        field("is_enum_variant", [&]() { write_bool(n.is_enum_variant); });
                        field("type_id", [&]() { write_index(n.type_id.value); });
                        field("variant", [&]() { write_index(n.variant.value); });
                        field("resolved", [&]() { write_bool(n.resolved); });
                        field("args", [&]() {
                            print_array(indent_level + 1, [&](const auto &arg_item) {
                                for (const auto &arg : n.args) {
                                    arg_item([&]() {
                                        print_object(indent_level + 2, [&](const auto &a_field) {
                                            a_field("field",
                                                    [&]() { write_index(arg.field.value); });
                                            a_field("value",
                                                    [&]() { write_index(arg.value.value); });
                                        });
                                    });
                                }
                            });
                        });
                    },
                    [&](const CoreCoerceExpr &n) {
                        field("operand", [&]() { write_index(n.operand.value); });
                        field("plan", [&]() { write_index(n.plan.value); });
                    },
                    [&](const CoreCollectionExpr &n) {
                        field("op", [&]() { write_string(collection_op_name(n.op)); });
                        field("base", [&]() { write_index(n.base.value); });
                        field("index", [&]() { write_index(n.index.value); });
                        field("value", [&]() { write_index(n.value.value); });
                    },
                    [&](const CoreUnsupportedExpr &n) {
                        // The node's own range is always the enclosing CoreExpr's
                        // range (both are the source expr's range in every
                        // lowering site), and the wrapper already emitted
                        // `source_range`; a second one would be a duplicate JSON
                        // key. The reader restores it from the wrapper.
                        field("source_kind", [&]() { write_string(n.source_kind); });
                    },
                    [&](const CoreCallExpr &n) {
                        field("callee", [&]() { write_index(n.callee.value); });
                        field("args", [&]() {
                            print_array(indent_level + 1, [&](const auto &arg_item) {
                                for (const CoreValueId arg : n.args) {
                                    arg_item([&]() { write_index(arg.value); });
                                }
                            });
                        });
                    },
                    [&](const CoreClosureExpr &n) {
                        field("fn", [&]() { write_index(n.fn.value); });
                        field("env", [&]() {
                            print_array(indent_level + 1, [&](const auto &env_item) {
                                for (const CoreValueId env : n.env) {
                                    env_item([&]() { write_index(env.value); });
                                }
                            });
                        });
                    },
                    [&](const CoreCallClosureExpr &n) {
                        field("callee", [&]() { write_index(n.callee.value); });
                        field("args", [&]() {
                            print_array(indent_level + 1, [&](const auto &arg_item) {
                                for (const CoreValueId arg : n.args) {
                                    arg_item([&]() { write_index(arg.value); });
                                }
                            });
                        });
                    },
                },
                expr.node);
        });
    }

    void print_path_expr(const CorePathExpr &n, const auto &field, int indent_level) {
        field("root", [&]() { write_string(path_root_name(n.root)); });
        field("root_name", [&]() { write_string(n.root_name); });
        field("members", [&]() { write_string_array(n.members, indent_level); });
        field("root_type", [&]() { write_index(n.root_type.value); });
        field("projection", [&]() {
            print_array(indent_level, [&](const auto &step_item) {
                for (const auto &step : n.projection) {
                    step_item([&]() {
                        print_object(indent_level + 1, [&](const auto &s_field) {
                            s_field("owner_type", [&]() { write_index(step.owner_type.value); });
                            s_field("field", [&]() { write_index(step.field.value); });
                            s_field("result_type", [&]() { write_index(step.result_type.value); });
                        });
                    });
                }
            });
        });
        field("projection_resolved", [&]() { write_bool(n.projection_resolved); });
        if (n.has_local) {
            field("has_local", [&]() { write_bool(true); });
            field("local", [&]() { write_index(n.local.value); });
        }
        if (n.workflow_node.value != CoreWorkflowNodeId::kInvalid) {
            field("workflow_node", [&]() { write_index(n.workflow_node.value); });
        }
    }

    void print_coercion_plans(const std::vector<CoreCoercionPlanNode> &plans, int indent_level) {
        print_array(indent_level, [&](const auto &item) {
            for (const auto &plan : plans) {
                item([&]() {
                    print_object(indent_level + 1, [&](const auto &field) {
                        field("source", [&]() { write_index(plan.source.value); });
                        field("result", [&]() { write_index(plan.result.value); });
                        field("ops", [&]() {
                            print_array(indent_level + 2, [&](const auto &op_item) {
                                for (const auto &op : plan.ops) {
                                    op_item([&]() {
                                        print_object(indent_level + 3, [&](const auto &op_field) {
                                            op_field("kind", [&]() {
                                                write_string(coercion_op_name(op.kind));
                                            });
                                            op_field("arg_index", [&]() {
                                                write_u32(op.arg_index);
                                            });
                                            if (op.child.value != CoreCoercionPlanId::kInvalid) {
                                                op_field("child", [&]() {
                                                    write_index(op.child.value);
                                                });
                                            }
                                        });
                                    });
                                }
                            });
                        });
                    });
                });
            }
        });
    }

    void print_patterns(const std::vector<CorePattern> &patterns, int indent_level) {
        print_array(indent_level, [&](const auto &item) {
            for (const auto &pattern : patterns) {
                item([&]() { print_pattern(pattern, indent_level + 1); });
            }
        });
    }

    void print_pattern(const CorePattern &pattern, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("kind", [&]() { write_string(pattern_wire_name(pattern.node.index())); });
            write_source_range_field(field, pattern.source_range, indent_level + 1);
            std::visit(
                Overloaded{
                    [&](const CoreWildcardPat &) {},
                    [&](const CoreLiteralPat &n) {
                        field("literal_kind", [&]() { write_string(literal_kind_name(n.kind)); });
                        field("spelling", [&]() { write_string(n.spelling); });
                    },
                    [&](const CoreIntRangePat &n) {
                        field("start", [&]() { write_i64(n.start); });
                        field("end", [&]() { write_i64(n.end); });
                    },
                    [&](const CoreBindingPat &n) {
                        field("binding", [&]() { write_index(n.binding.value); });
                        if (n.has_nested) {
                            field("has_nested", [&]() { write_bool(true); });
                            field("nested", [&]() { write_index(n.nested.value); });
                        }
                    },
                    [&](const CoreVariantPat &n) {
                        field("owner_enum", [&]() { write_index(n.owner_enum.value); });
                        field("variant", [&]() { write_index(n.variant.value); });
                        if (!n.tuple_subpatterns.empty()) {
                            field("tuple_subpatterns", [&]() {
                                print_array(indent_level + 1, [&](const auto &sub_item) {
                                    for (const auto &sub : n.tuple_subpatterns) {
                                        sub_item([&]() { write_index(sub.value); });
                                    }
                                });
                            });
                        }
                        if (!n.struct_fields.empty()) {
                            field("struct_fields", [&]() {
                                print_array(indent_level + 1, [&](const auto &sf_item) {
                                    for (const auto &sf : n.struct_fields) {
                                        sf_item([&]() {
                                            print_object(indent_level + 2,
                                                         [&](const auto &sf_field) {
                                                             sf_field("slot", [&]() {
                                                                 write_index(sf.slot.value);
                                                             });
                                                             sf_field("pattern", [&]() {
                                                                 write_index(sf.pattern.value);
                                                             });
                                                         });
                                        });
                                    }
                                });
                            });
                        }
                        field("has_rest", [&]() { write_bool(n.has_rest); });
                    },
                    [&](const CoreTuplePat &n) {
                        field("elements", [&]() {
                            print_array(indent_level + 1, [&](const auto &el_item) {
                                for (const auto &el : n.elements) {
                                    el_item([&]() { write_index(el.value); });
                                }
                            });
                        });
                    },
                    [&](const CoreOrPat &n) {
                        field("alternatives", [&]() {
                            print_array(indent_level + 1, [&](const auto &alt_item) {
                                for (const auto &alt : n.alternatives) {
                                    alt_item([&]() { write_index(alt.value); });
                                }
                            });
                        });
                    },
                },
                pattern.node);
        });
    }

    void print_region(const CoreRegion &region, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("statements", [&]() {
                print_array(indent_level + 1, [&](const auto &stmt_item) {
                    for (const auto &stmt : region.statements) {
                        stmt_item([&]() { print_stmt(stmt, indent_level + 2); });
                    }
                });
            });
        });
    }

    void print_stmt(const CoreStmt &stmt, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("kind", [&]() { write_string(stmt_wire_name(stmt.node.index())); });
            write_source_range_field(field, stmt.source_range, indent_level + 1);
            std::visit(
                Overloaded{
                    [&](const CoreLetStmt &n) {
                        field("result", [&]() { write_index(n.result.value); });
                        field("expr", [&]() { write_index(n.expr.value); });
                    },
                    [&](const CoreCapabilityCallStmt &n) {
                        field("result", [&]() { write_index(n.result.value); });
                        field("capability", [&]() { write_index(n.capability.value); });
                        field("callee_name", [&]() { write_string(n.callee_name); });
                        field("args", [&]() {
                            print_array(indent_level + 1, [&](const auto &arg_item) {
                                for (const auto &arg : n.args) {
                                    arg_item([&]() { write_index(arg.value); });
                                }
                            });
                        });
                    },
                    [&](const CoreCallStmt &n) {
                        field("result", [&]() { write_index(n.result.value); });
                        field("callee", [&]() { write_index(n.callee.value); });
                        field("args", [&]() {
                            print_array(indent_level + 1, [&](const auto &arg_item) {
                                for (const auto &arg : n.args) {
                                    arg_item([&]() { write_index(arg.value); });
                                }
                            });
                        });
                    },
                    [&](const CoreStoreStmt &n) {
                        field("place", [&]() { print_place(n.place, indent_level + 1); });
                        field("value", [&]() { write_index(n.value.value); });
                    },
                    [&](const CoreIfStmt &n) {
                        field("condition", [&]() { write_index(n.condition.value); });
                        if (n.then_region) {
                            field("then_region",
                                  [&]() { print_region(*n.then_region, indent_level + 1); });
                        }
                        if (n.else_region) {
                            field("else_region",
                                  [&]() { print_region(*n.else_region, indent_level + 1); });
                        }
                    },
                    [&](const CoreGotoStmt &n) {
                        field("target", [&]() { write_index(n.target.value); });
                        field("target_name", [&]() { write_string(n.target_name); });
                    },
                    [&](const CoreReturnStmt &n) {
                        field("has_value", [&]() { write_bool(n.has_value); });
                        if (n.has_value) {
                            field("value", [&]() { write_index(n.value.value); });
                        }
                    },
                    [&](const CoreYieldStmt &n) {
                        field("has_value", [&]() { write_bool(n.has_value); });
                        if (n.has_value) {
                            field("value", [&]() { write_index(n.value.value); });
                        }
                    },
                    [&](const CoreTrapStmt &n) {
                        field("trap_kind", [&]() { write_string(trap_kind_name(n.kind)); });
                    },
                    [&](const CoreMatchStmt &n) {
                        field("scrutinee", [&]() { write_index(n.scrutinee.value); });
                        field("has_result", [&]() { write_bool(n.has_result); });
                        if (n.has_result) {
                            field("result", [&]() { write_index(n.result.value); });
                        }
                        field("arms", [&]() {
                            print_array(indent_level + 1, [&](const auto &arm_item) {
                                for (const auto &arm : n.arms) {
                                    arm_item([&]() { print_match_arm(arm, indent_level + 2); });
                                }
                            });
                        });
                        if (n.fallback_region) {
                            field("fallback_region",
                                  [&]() { print_region(*n.fallback_region, indent_level + 1); });
                        }
                    },
                },
                stmt.node);
        });
    }

    void print_match_arm(const CoreMatchArm &arm, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("pattern", [&]() { write_index(arm.pattern.value); });
            field("bindings", [&]() {
                print_array(indent_level + 1, [&](const auto &b_item) {
                    for (const auto &binding : arm.bindings) {
                        b_item([&]() {
                            print_object(indent_level + 2, [&](const auto &b_field) {
                                b_field("value", [&]() { write_index(binding.value.value); });
                            });
                        });
                    }
                });
            });
            if (arm.guard_region) {
                field("guard_region",
                      [&]() { print_region(*arm.guard_region, indent_level + 1); });
            }
            if (arm.body) {
                field("body", [&]() { print_region(*arm.body, indent_level + 1); });
            }
        });
    }

    void print_place(const CorePlace &place, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("root", [&]() { write_string(path_root_name(place.root)); });
            field("root_name", [&]() { write_string(place.root_name); });
            field("members", [&]() { write_string_array(place.members, indent_level + 1); });
            field("root_type", [&]() { write_index(place.root_type.value); });
            field("projection", [&]() {
                print_array(indent_level + 1, [&](const auto &step_item) {
                    for (const auto &step : place.projection) {
                        step_item([&]() {
                            print_object(indent_level + 2, [&](const auto &s_field) {
                                s_field("owner_type",
                                        [&]() { write_index(step.owner_type.value); });
                                s_field("field", [&]() { write_index(step.field.value); });
                                s_field("result_type",
                                        [&]() { write_index(step.result_type.value); });
                            });
                        });
                    }
                });
            });
            field("projection_resolved", [&]() { write_bool(place.projection_resolved); });
        });
    }

    void print_instances(const std::vector<CoreInstanceDecl> &instances, int indent_level) {
        print_array(indent_level, [&](const auto &item) {
            for (const auto &instance : instances) {
                item([&]() { print_instance(instance, indent_level + 1); });
            }
        });
    }

    void print_instance(const CoreInstanceDecl &instance, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("id", [&]() { write_index(instance.id.value); });
            field("instance_key", [&]() { write_string(instance.instance_key); });
            field("origin", [&]() { write_symbol_ref(instance.origin, indent_level + 1); });
            field("dispatch_types", [&]() {
                print_array(indent_level + 1, [&](const auto &dt_item) {
                    for (const auto &dt : instance.dispatch_types) {
                        dt_item([
                            &]() { write_required_value_type_id(dt, "instance.dispatch_types"); });
                    }
                });
            });
            field("payload", [&]() {
                print_object(indent_level + 1, [&](const auto &p_field) {
                    p_field("kind", [&]() {
                        write_string(
                            core_instance_payload_wire_name(instance.payload.index()));
                    });
                    std::visit(
                        Overloaded{
                            [&](const CoreCapabilityInstance &p) {
                                p_field("base", [&]() {
                                    write_required_id(p.base, "instance.payload.base");
                                });
                            },
                            [&](const CorePredicateInstance &) {},
                            [&](const CoreAgentInstance &p) {
                                p_field("base", [&]() {
                                    write_required_id(p.base, "instance.payload.base");
                                });
                                p_field("input_type", [&]() {
                                    write_required_type_id(p.input_type,
                                                           "instance.payload.input_type");
                                });
                                p_field("context_kind", [&]() {
                                    write_string(context_kind_name(p.context_kind));
                                });
                                if (p.context_kind == CoreAgentDecl::ContextKind::Struct) {
                                    p_field("context_type", [&]() {
                                        write_required_type_id(p.context_type,
                                                               "instance.payload.context_type");
                                    });
                                }
                                p_field("output_type", [&]() {
                                    write_required_type_id(p.output_type,
                                                           "instance.payload.output_type");
                                });
                            },
                            [&](const CoreWorkflowInstance &p) {
                                p_field("base", [&]() {
                                    write_required_id(p.base, "instance.payload.base");
                                });
                                p_field("input_type", [&]() {
                                    write_required_type_id(p.input_type,
                                                           "instance.payload.input_type");
                                });
                                p_field("output_type", [&]() {
                                    write_required_type_id(p.output_type,
                                                           "instance.payload.output_type");
                                });
                            },
                            [&](const CoreFnInstance &) {},
                        },
                        instance.payload);
                });
            });
        });
    }

    // PrettyJsonWriter's `out_` stream, exposed for bare-token writes (the
    // trailing newline after the root object, and bare integers).
    [[nodiscard]] std::ostream &out() { return out_; }

    std::string *error_{nullptr};
    bool failed_{false};
};

// ===========================================================================
// Reader — RFC 0026 P9 §6.2/§7/§9. Hard admission boundary: never a partially
// populated program, never a bare nullopt; a typed `CoreJsonDiagnostic` per
// failure carrying the document's range where it supplies one.
// ===========================================================================

/// The maximum region-nesting depth the reader will recurse to (§5). The reader
/// is an untrusted-input admission boundary: the recursive region walks in
/// `core_region_exit` / `verify_region` carry no bound today, so a
/// whitespace-cheap source with ~10^5 nested `if`s would otherwise overflow the
/// native stack. This is deliberately generous (far deeper than any authored
/// program) while still bounding the descent.
inline constexpr std::size_t kMaxRegionNestingDepth = 1024;

class CoreJsonReader final {
  public:
    explicit CoreJsonReader(const JsonValue &root) : root_(&root) {}

    [[nodiscard]] bool ok() const noexcept { return diagnostics_.empty(); }

    [[nodiscard]] std::vector<CoreJsonDiagnostic> take_diagnostics() {
        return std::move(diagnostics_);
    }

    /// Read the whole envelope into `program`. Returns false (with diagnostics)
    /// on any failure; the caller discards the partially-built program in that
    /// case, so no partial artifact escapes.
    [[nodiscard]] bool read(CoreProgram &program);

  private:
    // --- diagnostics -------------------------------------------------------

    // A stable code is required (Principle 5). `never_fail` guards against
    // unbounded diagnostic growth on a deeply malformed document.
    void error(std::string code, std::string message, SourceRangeOpt range = std::nullopt) {
        if (diagnostics_.size() >= kMaxDiagnostics) {
            return;
        }
        diagnostics_.push_back(
            CoreJsonDiagnostic{std::move(code), std::move(message), range});
    }

    static constexpr std::size_t kMaxDiagnostics = 64;

    // Codes are namespaced `core.json.*` so a caller can distinguish them from
    // the `core.verify.*` a final verification emits (RFC 0026 P9 §7).
    static constexpr std::string_view kNotJson = "core.json.NOT_JSON";
    static constexpr std::string_view kBadEnvelope = "core.json.BAD_ENVELOPE";
    static constexpr std::string_view kBadFormatVersion = "core.json.BAD_FORMAT_VERSION";
    static constexpr std::string_view kBadLayer = "core.json.BAD_LAYER";
    static constexpr std::string_view kMissingField = "core.json.MISSING_FIELD";
    static constexpr std::string_view kWrongType = "core.json.WRONG_TYPE";
    static constexpr std::string_view kUnknownKind = "core.json.UNKNOWN_KIND";
    static constexpr std::string_view kUnknownField = "core.json.UNKNOWN_FIELD";
    static constexpr std::string_view kFieldMaskViolation = "core.json.FIELD_MASK_VIOLATION";
    static constexpr std::string_view kInvalidId = "core.json.INVALID_ID";
    static constexpr std::string_view kOutOfRange = "core.json.OUT_OF_RANGE";
    static constexpr std::string_view kForwardValueType = "core.json.FORWARD_VALUE_TYPE";
    static constexpr std::string_view kNonCanonicalArena = "core.json.NONCANONICAL_ARENA";
    static constexpr std::string_view kDuplicateInstanceKey = "core.json.DUPLICATE_INSTANCE_KEY";
    static constexpr std::string_view kValueCountMismatch = "core.json.VALUE_COUNT_MISMATCH";
    static constexpr std::string_view kRegionTooDeep = "core.json.REGION_TOO_DEEP";
    static constexpr std::string_view kVerifyFailed = "core.json.VERIFY_FAILED";

    // --- range helper ------------------------------------------------------

    [[nodiscard]] static SourceRangeOpt node_range(const JsonValue &v) {
        if (v.begin_offset == 0 && v.end_offset == 0) {
            return std::nullopt;
        }
        return SourceRange{v.begin_offset, v.end_offset};
    }

    // --- primitive readers -------------------------------------------------
    //
    // Every helper records a typed diagnostic and returns a sentinel on failure;
    // `ok()` (no diagnostics) is the single success signal — matching the
    // single-layer reader's `ok_` pattern but with a code per cause (§7).

    [[nodiscard]] const JsonValue *field(const JsonValue &obj, std::string_view key) {
        return obj.get(key);
    }

    [[nodiscard]] std::optional<std::string> req_string(const JsonValue &obj, std::string_view key) {
        const auto *f = obj.get(key);
        if (f == nullptr) {
            error(std::string(kMissingField),
                  "missing required string field '" + std::string(key) + "'", node_range(obj));
            return std::nullopt;
        }
        const auto value = f->as_string();
        if (!value.has_value()) {
            error(std::string(kWrongType),
                  "field '" + std::string(key) + "' must be a string", node_range(*f));
            return std::nullopt;
        }
        return std::string(*value);
    }

    [[nodiscard]] std::optional<std::string> opt_string(const JsonValue &obj, std::string_view key) {
        const auto *f = obj.get(key);
        if (f == nullptr) {
            return std::string{};
        }
        const auto value = f->as_string();
        if (!value.has_value()) {
            error(std::string(kWrongType),
                  "field '" + std::string(key) + "' must be a string", node_range(*f));
            return std::nullopt;
        }
        return std::string(*value);
    }

    [[nodiscard]] std::optional<bool> req_bool(const JsonValue &obj, std::string_view key) {
        const auto *f = obj.get(key);
        if (f == nullptr) {
            error(std::string(kMissingField),
                  "missing required bool field '" + std::string(key) + "'", node_range(obj));
            return std::nullopt;
        }
        const auto value = f->as_bool();
        if (!value.has_value()) {
            error(std::string(kWrongType),
                  "field '" + std::string(key) + "' must be a bool", node_range(*f));
            return std::nullopt;
        }
        return *value;
    }

    [[nodiscard]] std::optional<bool> opt_bool(const JsonValue &obj, std::string_view key,
                                               bool fallback) {
        const auto *f = obj.get(key);
        if (f == nullptr) {
            return fallback;
        }
        const auto value = f->as_bool();
        if (!value.has_value()) {
            error(std::string(kWrongType),
                  "field '" + std::string(key) + "' must be a bool", node_range(*f));
            return std::nullopt;
        }
        return *value;
    }

    /// A required unsigned integer in the u32 domain, as a bare JSON integer
    /// (never a string; §2). A negative / over-range / non-integer token is a
    /// hard failure — no silent signed truncation.
    [[nodiscard]] std::optional<std::uint32_t> req_u32(const JsonValue &obj, std::string_view key) {
        const auto *f = obj.get(key);
        if (f == nullptr) {
            error(std::string(kMissingField),
                  "missing required index field '" + std::string(key) + "'", node_range(obj));
            return std::nullopt;
        }
        const auto value = f->as_int();
        if (!value.has_value() || *value < 0 ||
            *value > static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max())) {
            error(std::string(kWrongType),
                  "field '" + std::string(key) + "' must be a u32 integer", node_range(*f));
            return std::nullopt;
        }
        return static_cast<std::uint32_t>(*value);
    }

    [[nodiscard]] std::optional<std::uint32_t> opt_u32(const JsonValue &obj, std::string_view key,
                                                       std::uint32_t fallback) {
        const auto *f = obj.get(key);
        if (f == nullptr) {
            return fallback;
        }
        const auto value = f->as_int();
        if (!value.has_value() || *value < 0 ||
            *value > static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max())) {
            error(std::string(kWrongType),
                  "field '" + std::string(key) + "' must be a u32 integer", node_range(*f));
            return std::nullopt;
        }
        return static_cast<std::uint32_t>(*value);
    }

    [[nodiscard]] std::optional<std::uint64_t> req_u64(const JsonValue &obj, std::string_view key) {
        const auto *f = obj.get(key);
        if (f == nullptr) {
            error(std::string(kMissingField),
                  "missing required integer field '" + std::string(key) + "'", node_range(obj));
            return std::nullopt;
        }
        const auto value = f->as_uint();
        if (!value.has_value()) {
            error(std::string(kWrongType),
                  "field '" + std::string(key) + "' must be a non-negative integer", node_range(*f));
            return std::nullopt;
        }
        return *value;
    }

    [[nodiscard]] std::optional<std::int64_t> req_i64(const JsonValue &obj, std::string_view key) {
        const auto *f = obj.get(key);
        if (f == nullptr) {
            error(std::string(kMissingField),
                  "missing required integer field '" + std::string(key) + "'", node_range(obj));
            return std::nullopt;
        }
        const auto value = f->as_int();
        if (!value.has_value()) {
            error(std::string(kWrongType),
                  "field '" + std::string(key) + "' must be an integer", node_range(*f));
            return std::nullopt;
        }
        return *value;
    }

    /// A required `{"begin_offset","end_offset"}` object -> SourceRange.
    [[nodiscard]] std::optional<SourceRange> req_source_range(const JsonValue &obj,
                                                              std::string_view key) {
        const auto *f = obj.get(key);
        if (f == nullptr) {
            return std::nullopt;
        }
        if (!f->is_object()) {
            error(std::string(kWrongType),
                  "field '" + std::string(key) + "' must be an object", node_range(*f));
            return std::nullopt;
        }
        if (!check_fields(*f, {"begin_offset", "end_offset"}, "source range")) {
            return std::nullopt;
        }
        const auto begin = req_u64(*f, "begin_offset");
        const auto end = req_u64(*f, "end_offset");
        if (!begin.has_value() || !end.has_value()) {
            return std::nullopt;
        }
        return SourceRange{static_cast<std::size_t>(*begin), static_cast<std::size_t>(*end)};
    }

    [[nodiscard]] std::optional<SourceRangeOpt> opt_source_range(const JsonValue &obj,
                                                                 std::string_view key) {
        if (obj.get(key) == nullptr) {
            return SourceRangeOpt{};
        }
        const auto range = req_source_range(obj, key);
        if (!range.has_value()) {
            return std::nullopt;
        }
        return SourceRangeOpt{*range};
    }

    [[nodiscard]] std::optional<std::vector<std::string>> req_string_array(const JsonValue &obj,
                                                                          std::string_view key) {
        const auto *f = obj.get(key);
        if (f == nullptr) {
            error(std::string(kMissingField),
                  "missing required array field '" + std::string(key) + "'", node_range(obj));
            return std::nullopt;
        }
        if (!f->is_array()) {
            error(std::string(kWrongType),
                  "field '" + std::string(key) + "' must be an array", node_range(*f));
            return std::nullopt;
        }
        std::vector<std::string> out;
        out.reserve(f->array_items.size());
        for (const auto &item : f->array_items) {
            const auto value = item->as_string();
            if (!value.has_value()) {
                error(std::string(kWrongType),
                      "field '" + std::string(key) + "' must be an array of strings",
                      node_range(*item));
                return std::nullopt;
            }
            out.emplace_back(*value);
        }
        return out;
    }

    /// A required array of bare u32 indices.
    [[nodiscard]] std::optional<std::vector<std::uint32_t>>
    req_u32_array(const JsonValue &obj, std::string_view key) {
        const auto *f = obj.get(key);
        if (f == nullptr) {
            error(std::string(kMissingField),
                  "missing required array field '" + std::string(key) + "'", node_range(obj));
            return std::nullopt;
        }
        if (!f->is_array()) {
            error(std::string(kWrongType),
                  "field '" + std::string(key) + "' must be an array", node_range(*f));
            return std::nullopt;
        }
        std::vector<std::uint32_t> out;
        out.reserve(f->array_items.size());
        for (const auto &item : f->array_items) {
            const auto value = item->as_int();
            if (!value.has_value() || *value < 0 ||
                *value > static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max())) {
                error(std::string(kWrongType),
                      "field '" + std::string(key) + "' must be an array of u32 integers",
                      node_range(*item));
                return std::nullopt;
            }
            out.push_back(static_cast<std::uint32_t>(*value));
        }
        return out;
    }

    [[nodiscard]] std::optional<ir::SymbolRef> read_symbol_ref(const JsonValue &obj) {
        if (!obj.is_object()) {
            error(std::string(kWrongType), "symbol_ref must be an object", node_range(obj));
            return std::nullopt;
        }
        // The unknown-field gate applies INSIDE a symbol_ref too (§7 "no
        // missing/extra per-kind field"): a hand-editable symbol_ref with a
        // stray member (e.g. a `canonical` typo for `canonical_name`) must be
        // rejected, not silently admitted.
        if (!check_fields(obj, {"kind", "canonical_name", "local_name", "module_name", "id"},
                          "symbol_ref")) {
            return std::nullopt;
        }
        ir::SymbolRef ref;
        const auto kind = req_string(obj, "kind");
        if (!kind.has_value() || !parse_symbol_ref_kind(*kind, ref.kind)) {
            error(std::string(kUnknownKind),
                  "symbol_ref has an unknown kind '" + kind.value_or("") + "'", node_range(obj));
            return std::nullopt;
        }
        const auto canonical = req_string(obj, "canonical_name");
        if (!canonical.has_value()) {
            return std::nullopt;
        }
        ref.canonical_name = *canonical;
        const auto local = opt_string(obj, "local_name");
        const auto module = opt_string(obj, "module_name");
        if (!local.has_value() || !module.has_value()) {
            return std::nullopt;
        }
        ref.local_name = *local;
        ref.module_name = *module;
        if (const auto *id = obj.get("id"); id != nullptr) {
            const auto value = id->as_uint();
            if (!value.has_value()) {
                error(std::string(kWrongType), "symbol_ref.id must be a non-negative integer",
                      node_range(*id));
                return std::nullopt;
            }
            ref.id = static_cast<std::size_t>(*value);
        }
        return ref;
    }

    // --- envelope ----------------------------------------------------------

    [[nodiscard]] bool read_envelope_fields(const JsonValue &obj, CoreProgram &program);

    // Field-presence tracking: every object reader checks its key set against
    // the per-kind allowed set, so an unknown / extra field is rejected rather
    // than silently ignored (§7 "no missing/extra per-kind field").

    [[nodiscard]] bool check_fields(const JsonValue &obj,
                                    std::initializer_list<std::string_view> allowed,
                                    std::string_view what) {
        for (const auto &member : obj.object_fields) {
            const std::string &key = member.first;
            bool found = false;
            for (const auto allowed_key : allowed) {
                if (key == allowed_key) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                error(std::string(kUnknownField),
                      std::string(what) + " has an unknown field '" + key + "'", node_range(obj));
                return false;
            }
        }
        return true;
    }

    // --- per-table readers -------------------------------------------------

    [[nodiscard]] bool read_types(const JsonValue &array, CoreProgram &program);
    [[nodiscard]] bool read_type_decl(const JsonValue &obj, CoreTypeDecl &out);
    [[nodiscard]] bool read_variant_payload(const JsonValue &obj,
                                            CoreTypeDecl::VariantPayload &out);
    [[nodiscard]] bool read_member_type_templates(const JsonValue &array,
                                                 std::vector<CoreMemberTypeTemplateNode> &out);
    [[nodiscard]] bool read_value_types(const JsonValue &array, CoreProgram &program);
    [[nodiscard]] bool read_value_type(const JsonValue &obj, CoreValueTypeNode &out);
    [[nodiscard]] bool read_capabilities(const JsonValue &array, CoreProgram &program);
    [[nodiscard]] bool read_agents(const JsonValue &array, CoreProgram &program);
    [[nodiscard]] bool read_flows(const JsonValue &array, CoreProgram &program);
    [[nodiscard]] bool read_workflows(const JsonValue &array, CoreProgram &program);
    [[nodiscard]] bool read_instances(const JsonValue &array, CoreProgram &program);
    [[nodiscard]] bool read_fns(const JsonValue &array, CoreProgram &program);

    // --- body readers ------------------------------------------------------

    [[nodiscard]] bool read_exprs(const JsonValue &array, std::vector<CoreExpr> &out);
    [[nodiscard]] bool read_expr(const JsonValue &obj, CoreExpr &out);
    [[nodiscard]] bool read_patterns(const JsonValue &array, std::vector<CorePattern> &out);
    [[nodiscard]] bool read_pattern(const JsonValue &obj, CorePattern &out);
    [[nodiscard]] bool read_coercion_plans(const JsonValue &array,
                                          std::vector<CoreCoercionPlanNode> &out);
    [[nodiscard]] bool read_region(const JsonValue &obj, CoreRegion &out, std::size_t depth);
    [[nodiscard]] bool read_stmt(const JsonValue &obj, CoreStmt &out, std::size_t depth);
    [[nodiscard]] bool read_match_arm(const JsonValue &obj, CoreMatchArm &out, std::size_t depth);
    [[nodiscard]] bool read_place(const JsonValue &obj, CorePlace &out);
    [[nodiscard]] bool read_path_root_and_projection(const JsonValue &obj, CorePathRoot &root,
                                                     std::string &root_name,
                                                     std::vector<std::string> &members,
                                                     CoreTypeId &root_type,
                                                     std::vector<CoreProjectionStep> &projection,
                                                     bool &projection_resolved);

    /// §6.2 — the serialized value-type index -> freshly minted id remap. A
    /// reference is resolved through this so a serialized id is never trusted.
    [[nodiscard]] std::optional<CoreValueTypeId> map_value_type_id(std::uint32_t serialized,
                                                                   const JsonValue &node) {
        if (serialized == CoreValueTypeId::kInvalid) {
            return CoreValueTypeId{}; // the kInvalid sentinel round-trips as itself
        }
        if (serialized >= value_type_remap_.size()) {
            error(std::string(kOutOfRange),
                  "value-type reference " + std::to_string(serialized) +
                      " is out of range for the program-global arena of size " +
                      std::to_string(value_type_remap_.size()),
                  node_range(node));
            return std::nullopt;
        }
        return value_type_remap_[serialized];
    }

    [[nodiscard]] std::optional<std::vector<CoreValueTypeId>>
    map_value_type_id_array(const JsonValue &obj, std::string_view key) {
        const auto raw = req_u32_array(obj, key);
        if (!raw.has_value()) {
            return std::nullopt;
        }
        std::vector<CoreValueTypeId> out;
        out.reserve(raw->size());
        for (const auto serialized : *raw) {
            if (serialized >= value_type_remap_.size()) {
                error(std::string(kOutOfRange),
                      "field '" + std::string(key) + "' references out-of-range value-type id " +
                          std::to_string(serialized),
                      node_range(obj));
                return std::nullopt;
            }
            out.push_back(value_type_remap_[serialized]);
        }
        return out;
    }

    [[nodiscard]] std::optional<CoreValueTypeId> map_single_value_type_id(const JsonValue &obj,
                                                                         std::string_view key) {
        const auto raw = req_u32(obj, key);
        if (!raw.has_value()) {
            return std::nullopt;
        }
        return map_value_type_id(*raw, obj);
    }

    const JsonValue *root_;
    std::vector<CoreJsonDiagnostic> diagnostics_;
    /// serialized value-type slot -> freshly minted arena id (§6.2).
    std::vector<CoreValueTypeId> value_type_remap_;
    /// The freshly built program-global arena, interned through the shared
    /// hash-cons. Local so a failed parse never touches the caller's program.
    std::vector<CoreValueType> rebuilt_value_types_;
};

bool CoreJsonReader::read(CoreProgram &program) {
    if (!root_->is_object()) {
        error(std::string(kBadEnvelope), "the document root must be a JSON object",
              node_range(*root_));
        return false;
    }
    if (!read_envelope_fields(*root_, program)) {
        return false;
    }
    if (!ok()) {
        return false;
    }
    // Final structural gate (§9): every accepted parse is verifier-clean, so the
    // >30 `core.verify.*` codes become wire validation for free. A failure here
    // means the document was shape-valid but not a legal Core program.
    const auto verify_result = verify_core_program(program);
    if (!verify_result.ok()) {
        for (const auto &d : verify_result.diagnostics) {
            if (d.severity == CoreDiagnosticSeverity::Error) {
                error(std::string(kVerifyFailed),
                      "parsed program fails Core verification: " + d.code + ": " + d.message,
                      d.source_range);
            }
        }
        return false;
    }
    return ok();
}

bool CoreJsonReader::read_envelope_fields(const JsonValue &obj, CoreProgram &program) {
    if (!check_fields(obj,
                      {"format_version", "layer", "types", "value_types", "capabilities",
                       "agents", "flows", "workflows", "instances", "fns"},
                      "core envelope")) {
        return false;
    }
    const auto format_version = req_string(obj, "format_version");
    if (!format_version.has_value()) {
        return false;
    }
    if (*format_version != kCoreFormatVersion) {
        error(std::string(kBadFormatVersion),
              "format_version must be '" + std::string(kCoreFormatVersion) + "', got '" +
                  *format_version + "'",
              node_range(obj));
        return false;
    }

    const auto layer = req_string(obj, "layer");
    if (!layer.has_value()) {
        return false;
    }
    // The `layer` discriminator is a deliberate cheap first check (§1): reject a
    // document handed to the wrong reader before parsing any table.
    if (*layer != "core") {
        error(std::string(kBadLayer), "layer must be 'core', got '" + *layer + "'",
              node_range(obj));
        return false;
    }

    const auto require_array = [&](std::string_view key) -> const JsonValue * {
        const auto *f = obj.get(key);
        if (f == nullptr) {
            error(std::string(kMissingField),
                  "missing required table '" + std::string(key) + "'", node_range(obj));
            return nullptr;
        }
        if (!f->is_array()) {
            error(std::string(kWrongType),
                  "table '" + std::string(key) + "' must be an array", node_range(*f));
            return nullptr;
        }
        return f;
    };

    program.format_version = *format_version;
    // `value_types` is read FIRST (§6.2): its rebuilt index-remap must exist
    // before ANY value-type reference elsewhere in the document resolves —
    // including `CoreMemberTypeTemplateNode::concrete` inside `types`. Reading
    // the arena first is safe: a `CoreVtNominal::base` is a bare `CoreTypeId`
    // table position that needs no validation here (the final verify pass
    // range-checks it once the whole program is assembled).
    const auto *types_array = require_array("types");
    const auto *value_types_array = require_array("value_types");
    if (types_array == nullptr || value_types_array == nullptr) {
        return false;
    }
    if (!read_value_types(*value_types_array, program) || !read_types(*types_array, program)) {
        return false;
    }
    if (!ok()) {
        return false;
    }

    const auto *capabilities_array = require_array("capabilities");
    const auto *agents_array = require_array("agents");
    const auto *flows_array = require_array("flows");
    const auto *workflows_array = require_array("workflows");
    const auto *instances_array = require_array("instances");
    const auto *fns_array = require_array("fns");
    if (capabilities_array == nullptr || agents_array == nullptr || flows_array == nullptr ||
        workflows_array == nullptr || instances_array == nullptr || fns_array == nullptr) {
        return false;
    }
    if (!read_capabilities(*capabilities_array, program) || !read_agents(*agents_array, program) ||
        !read_flows(*flows_array, program) || !read_workflows(*workflows_array, program) ||
        !read_instances(*instances_array, program) || !read_fns(*fns_array, program)) {
        // `fns` is read last: it needs the instances table to rebuild the
        // derived CoreFnInstance::body back-links.
        return false;
    }
    if (!ok()) {
        return false;
    }

    // Instance `id` is a real field; it must equal the array position so the two
    // identities cannot disagree (position IS the CoreInstanceId, §4).
    for (std::uint32_t i = 0; i < program.instances.size(); ++i) {
        if (program.instances[i].id.value != i) {
            error(std::string(kOutOfRange),
                  "instance #" + std::to_string(i) + " has id " +
                      std::to_string(program.instances[i].id.value) +
                      " which disagrees with its table position",
                  std::nullopt);
            return false;
        }
    }

    return ok();
}

bool CoreJsonReader::read_types(const JsonValue &array, CoreProgram &program) {
    program.types.clear();
    program.types.reserve(array.array_items.size());
    for (const auto &item : array.array_items) {
        CoreTypeDecl decl;
        if (!read_type_decl(*item, decl)) {
            return false;
        }
        program.types.push_back(std::move(decl));
    }
    return ok();
}

bool CoreJsonReader::read_type_decl(const JsonValue &obj, CoreTypeDecl &out) {
    if (!obj.is_object()) {
        error(std::string(kWrongType), "a type declaration must be an object", node_range(obj));
        return false;
    }
    if (!check_fields(obj,
                      {"kind", "name", "fields", "field_nominal_types", "field_has_default",
                       "variants", "variant_payloads", "member_type_templates",
                       "field_type_template_roots", "type_param_count", "variances", "role",
                       "symbol_ref", "source_range"},
                      "type declaration")) {
        return false;
    }
    const auto kind = req_string(obj, "kind");
    if (!kind.has_value() || !parse_type_decl_kind(*kind, out.kind)) {
        error(std::string(kUnknownKind),
              "type declaration has an unknown kind '" + kind.value_or("") + "'", node_range(obj));
        return false;
    }
    const auto name = req_string(obj, "name");
    if (!name.has_value()) {
        return false;
    }
    out.name = *name;

    const auto fields = req_string_array(obj, "fields");
    if (!fields.has_value()) {
        return false;
    }
    out.fields = *fields;

    // field_nominal_types: an array PARALLEL to fields whose element is an
    // in-range CoreTypeId or JSON null (= kInvalid, the ONE legal serialized
    // kInvalid). Its length is verifier-pinned to fields.size().
    const auto *fnt = obj.get("field_nominal_types");
    if (fnt == nullptr) {
        error(std::string(kMissingField), "type declaration is missing 'field_nominal_types'",
              node_range(obj));
        return false;
    }
    if (!fnt->is_array()) {
        error(std::string(kWrongType), "'field_nominal_types' must be an array", node_range(*fnt));
        return false;
    }
    out.field_nominal_types.clear();
    out.field_nominal_types.reserve(fnt->array_items.size());
    for (const auto &item : fnt->array_items) {
        if (item->is_null()) {
            out.field_nominal_types.push_back(CoreTypeId{});
            continue;
        }
        const auto value = item->as_int();
        if (!value.has_value() || *value < 0 ||
            *value > static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max())) {
            error(std::string(kInvalidId),
                  "'field_nominal_types' entries must be a u32 integer or null", node_range(*item));
            return false;
        }
        out.field_nominal_types.push_back(CoreTypeId{static_cast<std::uint32_t>(*value)});
    }

    const auto *fhd = obj.get("field_has_default");
    if (fhd == nullptr) {
        error(std::string(kMissingField), "type declaration is missing 'field_has_default'",
              node_range(obj));
        return false;
    }
    if (!fhd->is_array()) {
        error(std::string(kWrongType), "'field_has_default' must be an array", node_range(*fhd));
        return false;
    }
    out.field_has_default.clear();
    out.field_has_default.reserve(fhd->array_items.size());
    for (const auto &item : fhd->array_items) {
        const auto value = item->as_bool();
        if (!value.has_value()) {
            error(std::string(kWrongType), "'field_has_default' entries must be bools",
                  node_range(*item));
            return false;
        }
        out.field_has_default.push_back(*value);
    }

    const auto variants = req_string_array(obj, "variants");
    if (!variants.has_value()) {
        return false;
    }
    out.variants = *variants;

    const auto *payloads = obj.get("variant_payloads");
    if (payloads == nullptr || !payloads->is_array()) {
        error(std::string(kMissingField), "type declaration needs an array 'variant_payloads'",
              node_range(obj));
        return false;
    }
    out.variant_payloads.clear();
    out.variant_payloads.reserve(payloads->array_items.size());
    for (const auto &item : payloads->array_items) {
        CoreTypeDecl::VariantPayload payload;
        if (!read_variant_payload(*item, payload)) {
            return false;
        }
        out.variant_payloads.push_back(std::move(payload));
    }

    const auto *templates = obj.get("member_type_templates");
    if (templates == nullptr || !templates->is_array()) {
        error(std::string(kMissingField), "type declaration needs an array 'member_type_templates'",
              node_range(obj));
        return false;
    }
    if (!read_member_type_templates(*templates, out.member_type_templates)) {
        return false;
    }

    const auto roots = req_u32_array(obj, "field_type_template_roots");
    if (!roots.has_value()) {
        return false;
    }
    out.field_type_template_roots.clear();
    out.field_type_template_roots.reserve(roots->size());
    for (const auto raw : *roots) {
        out.field_type_template_roots.push_back(CoreMemberTypeTemplateNodeId{raw});
    }

    const auto type_param_count = req_u32(obj, "type_param_count");
    if (!type_param_count.has_value()) {
        return false;
    }
    out.type_param_count = *type_param_count;

    const auto *variances = obj.get("variances");
    if (variances == nullptr || !variances->is_array()) {
        error(std::string(kMissingField), "type declaration needs an array 'variances'",
              node_range(obj));
        return false;
    }
    out.variances.clear();
    for (const auto &item : variances->array_items) {
        const auto spelling = item->as_string();
        CoreVariance variance{};
        if (!spelling.has_value() || !parse_variance_name(*spelling, variance)) {
            error(std::string(kUnknownKind),
                  "unknown variance spelling '" + std::string(spelling.value_or("")) + "'", node_range(*item));
            return false;
        }
        out.variances.push_back(variance);
    }

    const auto role = req_string(obj, "role");
    if (!role.has_value() || !parse_nominal_role(*role, out.role)) {
        error(std::string(kUnknownKind),
              "type declaration has an unknown role '" + role.value_or("") + "'", node_range(obj));
        return false;
    }

    const auto *symbol = obj.get("symbol_ref");
    if (symbol == nullptr) {
        error(std::string(kMissingField), "type declaration is missing 'symbol_ref'",
              node_range(obj));
        return false;
    }
    const auto ref = read_symbol_ref(*symbol);
    if (!ref.has_value()) {
        return false;
    }
    out.symbol_ref = *ref;

    const auto range = opt_source_range(obj, "source_range");
    if (!range.has_value()) {
        return false;
    }
    out.source_range = *range;
    return ok();
}

bool CoreJsonReader::read_variant_payload(const JsonValue &obj,
                                          CoreTypeDecl::VariantPayload &out) {
    if (!obj.is_object()) {
        error(std::string(kWrongType), "a variant payload must be an object", node_range(obj));
        return false;
    }
    if (!check_fields(obj, {"kind", "slot_type_template_roots", "field_names"},
                      "variant payload")) {
        return false;
    }
    const auto kind = req_string(obj, "kind");
    if (!kind.has_value() || !parse_variant_payload_kind(*kind, out.kind)) {
        error(std::string(kUnknownKind),
              "variant payload has an unknown kind '" + kind.value_or("") + "'", node_range(obj));
        return false;
    }
    const auto roots = req_u32_array(obj, "slot_type_template_roots");
    if (!roots.has_value()) {
        return false;
    }
    out.slot_type_template_roots.clear();
    out.slot_type_template_roots.reserve(roots->size());
    for (const auto raw : *roots) {
        out.slot_type_template_roots.push_back(CoreMemberTypeTemplateNodeId{raw});
    }
    const auto names = req_string_array(obj, "field_names");
    if (!names.has_value()) {
        return false;
    }
    out.field_names = *names;
    return ok();
}

bool CoreJsonReader::read_member_type_templates(
    const JsonValue &array, std::vector<CoreMemberTypeTemplateNode> &out) {
    out.clear();
    out.reserve(array.array_items.size());
    for (const auto &item : array.array_items) {
        if (!item->is_object()) {
            error(std::string(kWrongType), "a member type template must be an object",
                  node_range(*item));
            return false;
        }
        CoreMemberTypeTemplateNode node;
        // Per-kind field mask: the reader accepts EXACTLY the fields the kind
        // uses, matching the writer and the Core verifier's own mask (§4/§7).
        const auto kind_str = req_string(*item, "kind");
        if (!kind_str.has_value() || !parse_member_template_kind(*kind_str, node.kind)) {
            error(std::string(kUnknownKind),
                  "member template has an unknown kind '" + kind_str.value_or("") + "'",
                  node_range(*item));
            return false;
        }
        switch (node.kind) {
        case CoreMemberTypeTemplateKind::Concrete:
            if (!check_fields(*item, {"kind", "concrete"}, "concrete member template")) {
                return false;
            }
            break;
        case CoreMemberTypeTemplateKind::Param:
            if (!check_fields(*item, {"kind", "param_index"}, "param member template")) {
                return false;
            }
            break;
        case CoreMemberTypeTemplateKind::Nominal:
            if (!check_fields(*item, {"kind", "nominal", "capacity", "children"},
                              "nominal member template")) {
                return false;
            }
            break;
        case CoreMemberTypeTemplateKind::Fn:
            if (!check_fields(*item, {"kind", "children", "fn_return"}, "fn member template")) {
                return false;
            }
            break;
        }

        if (node.kind == CoreMemberTypeTemplateKind::Concrete) {
            const auto concrete = map_single_value_type_id(*item, "concrete");
            if (!concrete.has_value()) {
                return false;
            }
            node.concrete = *concrete;
        } else if (node.kind == CoreMemberTypeTemplateKind::Param) {
            const auto index = req_u32(*item, "param_index");
            if (!index.has_value()) {
                return false;
            }
            node.param_index = *index;
        } else if (node.kind == CoreMemberTypeTemplateKind::Nominal) {
            const auto nominal = req_u32(*item, "nominal");
            if (!nominal.has_value()) {
                return false;
            }
            node.nominal = CoreTypeId{*nominal};
            if (item->get("capacity") != nullptr) {
                const auto capacity = req_u64(*item, "capacity");
                if (!capacity.has_value()) {
                    return false;
                }
                node.capacity = *capacity;
            }
            const auto children = req_u32_array(*item, "children");
            if (!children.has_value()) {
                return false;
            }
            node.children.clear();
            for (const auto raw : *children) {
                node.children.push_back(CoreMemberTypeTemplateNodeId{raw});
            }
        } else { // Fn
            const auto children = req_u32_array(*item, "children");
            const auto fn_return = req_u32(*item, "fn_return");
            if (!children.has_value() || !fn_return.has_value()) {
                return false;
            }
            node.children.clear();
            for (const auto raw : *children) {
                node.children.push_back(CoreMemberTypeTemplateNodeId{raw});
            }
            node.fn_return = CoreMemberTypeTemplateNodeId{*fn_return};
        }
        out.push_back(std::move(node));
    }
    return ok();
}

bool CoreJsonReader::read_value_types(const JsonValue &array, CoreProgram &program) {
    // §6.2 — REBUILD the arena by interning. A single forward pass suffices
    // because children always precede parents; a child index >= its own index is
    // a fail-closed read error (the same rule the verifier applies). After
    // rebuilding, the freshly minted id for serialized slot `i` MUST be `i`, or
    // the input was not in canonical arena order and is rejected (§6.2 rule 4).
    rebuilt_value_types_.clear();
    value_type_remap_.assign(array.array_items.size(), CoreValueTypeId{});
    CoreValueTypeArena arena(rebuilt_value_types_);

    for (std::size_t i = 0; i < array.array_items.size(); ++i) {
        const auto &item = array.array_items[i];
        CoreValueTypeNode node;
        if (!read_value_type(*item, node)) {
            return false;
        }
        std::string reason;
        const auto minted = arena.intern(node, &reason);
        if (!minted.has_value()) {
            error(std::string(kNonCanonicalArena),
                  "value-type arena could not intern slot " + std::to_string(i) + ": " + reason,
                  node_range(*item));
            return false;
        }
        if (minted->value != i) {
            // Identity-remap assert (§6.2 rule 4): a reordered-but-structurally-
            // valid document (or a hand-edited one) is rejected.
            error(std::string(kNonCanonicalArena),
                  "value-type slot " + std::to_string(i) +
                      " is not in canonical arena order (interning mints id " +
                      std::to_string(minted->value) +
                      "); the document is reordered or a duplicate",
                  node_range(*item));
            return false;
        }
        value_type_remap_[i] = *minted;
    }
    program.value_types = rebuilt_value_types_;
    return ok();
}

bool CoreJsonReader::read_value_type(const JsonValue &obj, CoreValueTypeNode &out) {
    if (!obj.is_object()) {
        error(std::string(kWrongType), "a value type must be an object", node_range(obj));
        return false;
    }
    const auto kind = req_string(obj, "kind");
    if (!kind.has_value()) {
        return false;
    }
    const auto index = wire_name_index(core_value_type_detail::kCoreValueTypeWireNames, *kind);
    if (!index.has_value()) {
        error(std::string(kUnknownKind), "unknown value-type kind '" + *kind + "'", node_range(obj));
        return false;
    }

    // Structural nodes carry child IDS; a child reference is resolved through
    // the remap built so far, so a FORWARD/self reference is rejected here (the
    // serialized id has no minted mapping yet) — exactly the rule §6.2 applies.
    const auto map_child_id = [&](std::uint32_t serialized) -> std::optional<CoreValueTypeId> {
        if (serialized >= value_type_remap_.size()) {
            error(std::string(kOutOfRange),
                  "value-type child id " + std::to_string(serialized) + " is out of range",
                  node_range(obj));
            return std::nullopt;
        }
        if (value_type_remap_[serialized].value == CoreValueTypeId::kInvalid) {
            error(std::string(kForwardValueType),
                  "value-type child id " + std::to_string(serialized) +
                      " is a forward/self reference (not interned earlier)",
                  node_range(obj));
            return std::nullopt;
        }
        return value_type_remap_[serialized];
    };
    const auto read_child_array = [&](std::string_view key,
                                      std::vector<CoreValueTypeId> &into) -> bool {
        const auto raw = req_u32_array(obj, key);
        if (!raw.has_value()) {
            return false;
        }
        into.clear();
        for (const auto serialized : *raw) {
            const auto mapped = map_child_id(serialized);
            if (!mapped.has_value()) {
                return false;
            }
            into.push_back(*mapped);
        }
        return true;
    };

    switch (index.value()) {
    case 0: // Unit
        if (!check_fields(obj, {"kind"}, "unit value type")) {
            return false;
        }
        out = CoreVtUnit{};
        break;
    case 1: // Never
        if (!check_fields(obj, {"kind"}, "never value type")) {
            return false;
        }
        out = CoreVtNever{};
        break;
    case 2: // Bool
        if (!check_fields(obj, {"kind"}, "bool value type")) {
            return false;
        }
        out = CoreVtBool{};
        break;
    case 3: { // Int
        if (!check_fields(obj, {"kind", "bounds"}, "int value type")) {
            return false;
        }
        CoreVtInt node;
        if (const auto *bounds = obj.get("bounds"); bounds != nullptr) {
            if (!bounds->is_object()) {
                error(std::string(kWrongType), "'bounds' must be an object", node_range(*bounds));
                return false;
            }
            if (!check_fields(*bounds, {"minimum", "maximum"}, "int bounds")) {
                return false;
            }
            const auto min = req_i64(*bounds, "minimum");
            const auto max = req_i64(*bounds, "maximum");
            if (!min.has_value() || !max.has_value()) {
                return false;
            }
            node.bounds = std::pair{*min, *max};
        }
        out = node;
        break;
    }
    case 4: // Float
        if (!check_fields(obj, {"kind"}, "float value type")) {
            return false;
        }
        out = CoreVtFloat{};
        break;
    case 5: { // String
        if (!check_fields(obj, {"kind", "length_bounds"}, "string value type")) {
            return false;
        }
        CoreVtString node;
        if (const auto *bounds = obj.get("length_bounds"); bounds != nullptr) {
            if (!bounds->is_object()) {
                error(std::string(kWrongType), "'length_bounds' must be an object",
                      node_range(*bounds));
                return false;
            }
            if (!check_fields(*bounds, {"minimum", "maximum"}, "string length bounds")) {
                return false;
            }
            const auto min = req_i64(*bounds, "minimum");
            const auto max = req_i64(*bounds, "maximum");
            if (!min.has_value() || !max.has_value()) {
                return false;
            }
            node.length_bounds = std::pair{*min, *max};
        }
        out = node;
        break;
    }
    case 6: { // Decimal
        if (!check_fields(obj, {"kind", "scale"}, "decimal value type")) {
            return false;
        }
        CoreVtDecimal node;
        const auto scale = req_i64(obj, "scale");
        if (!scale.has_value()) {
            return false;
        }
        node.scale = *scale;
        out = node;
        break;
    }
    case 7: // Duration
        if (!check_fields(obj, {"kind"}, "duration value type")) {
            return false;
        }
        out = CoreVtDuration{};
        break;
    case 8: // Timestamp
        if (!check_fields(obj, {"kind"}, "timestamp value type")) {
            return false;
        }
        out = CoreVtTimestamp{};
        break;
    case 9: // Uuid
        if (!check_fields(obj, {"kind"}, "uuid value type")) {
            return false;
        }
        out = CoreVtUuid{};
        break;
    case 10: { // Nominal
        if (!check_fields(obj, {"kind", "base", "args", "capacity"}, "nominal value type")) {
            return false;
        }
        CoreVtNominal node;
        const auto base = req_u32(obj, "base");
        if (!base.has_value()) {
            return false;
        }
        // `base` is a CoreTypeId (a table position, NOT a value-type id) and is
        // therefore NOT remapped (§6.3).
        node.base = CoreTypeId{*base};
        if (!read_child_array("args", node.args)) {
            return false;
        }
        if (obj.get("capacity") != nullptr) {
            const auto capacity = req_u64(obj, "capacity");
            if (!capacity.has_value()) {
                return false;
            }
            node.capacity = *capacity;
        }
        out = node;
        break;
    }
    case 11: { // Tuple
        if (!check_fields(obj, {"kind", "elements"}, "tuple value type")) {
            return false;
        }
        CoreVtTuple node;
        if (!read_child_array("elements", node.elements)) {
            return false;
        }
        out = node;
        break;
    }
    case 12: { // Fn
        if (!check_fields(obj, {"kind", "params", "ret"}, "fn value type")) {
            return false;
        }
        CoreVtFn node;
        if (!read_child_array("params", node.params)) {
            return false;
        }
        const auto ret = req_u32(obj, "ret");
        if (!ret.has_value()) {
            return false;
        }
        const auto mapped = map_child_id(*ret);
        if (!mapped.has_value()) {
            return false;
        }
        node.ret = *mapped;
        out = node;
        break;
    }
    case 13: { // Closure
        if (!check_fields(obj, {"kind", "signature", "captures"}, "closure value type")) {
            return false;
        }
        CoreVtClosure node;
        const auto signature = req_u32(obj, "signature");
        if (!signature.has_value()) {
            return false;
        }
        const auto mapped_signature = map_child_id(*signature);
        if (!mapped_signature.has_value()) {
            return false;
        }
        node.signature = *mapped_signature;
        const auto *captures = obj.get("captures");
        if (captures == nullptr || !captures->is_array()) {
            error(std::string(kMissingField), "closure needs an array 'captures'", node_range(obj));
            return false;
        }
        for (const auto &capture : captures->array_items) {
            if (!capture->is_object()) {
                error(std::string(kWrongType), "a closure capture must be an object",
                      node_range(*capture));
                return false;
            }
            if (!check_fields(*capture, {"value_type", "mode"}, "closure capture")) {
                return false;
            }
            const auto serialized = req_u32(*capture, "value_type");
            const auto mode_str = req_string(*capture, "mode");
            if (!serialized.has_value() || !mode_str.has_value()) {
                return false;
            }
            CoreClosureCapture capture_out;
            const auto captured_type = map_child_id(*serialized);
            if (!captured_type.has_value()) {
                return false;
            }
            capture_out.value_type = *captured_type;
            if (!parse_capture_mode(*mode_str, capture_out.mode)) {
                error(std::string(kUnknownKind),
                      "closure capture has an unknown mode '" + *mode_str + "'",
                      node_range(*capture));
                return false;
            }
            node.captures.push_back(capture_out);
        }
        out = node;
        break;
    }
    default:
        error(std::string(kUnknownKind), "unknown value-type kind '" + *kind + "'", node_range(obj));
        return false;
    }
    return ok();
}

bool CoreJsonReader::read_capabilities(const JsonValue &array, CoreProgram &program) {
    program.capabilities.clear();
    program.capabilities.reserve(array.array_items.size());
    for (const auto &item : array.array_items) {
        if (!item->is_object()) {
            error(std::string(kWrongType), "a capability declaration must be an object",
                  node_range(*item));
            return false;
        }
        if (!check_fields(*item,
                          {"name", "symbol_ref", "effect_kind", "param_types", "return_type",
                           "source_range"},
                          "capability declaration")) {
            return false;
        }
        CoreCapabilityDecl decl;
        const auto name = req_string(*item, "name");
        if (!name.has_value()) {
            return false;
        }
        decl.name = *name;
        const auto *symbol = item->get("symbol_ref");
        if (symbol == nullptr) {
            error(std::string(kMissingField), "capability is missing 'symbol_ref'",
                  node_range(*item));
            return false;
        }
        const auto ref = read_symbol_ref(*symbol);
        if (!ref.has_value()) {
            return false;
        }
        decl.symbol_ref = *ref;
        const auto effect = req_string(*item, "effect_kind");
        if (!effect.has_value() || !parse_capability_effect_kind(*effect, decl.effect_kind)) {
            error(std::string(kUnknownKind),
                  "capability has an unknown effect_kind '" + effect.value_or("") + "'",
                  node_range(*item));
            return false;
        }
        const auto params = map_value_type_id_array(*item, "param_types");
        if (!params.has_value()) {
            return false;
        }
        decl.param_types = *params;
        const auto ret = map_single_value_type_id(*item, "return_type");
        if (!ret.has_value()) {
            return false;
        }
        decl.return_type = *ret;
        const auto range = opt_source_range(*item, "source_range");
        if (!range.has_value()) {
            return false;
        }
        decl.source_range = *range;
        program.capabilities.push_back(std::move(decl));
    }
    return ok();
}

bool CoreJsonReader::read_agents(const JsonValue &array, CoreProgram &program) {
    program.agents.clear();
    program.agents.reserve(array.array_items.size());
    for (const auto &item : array.array_items) {
        if (!item->is_object()) {
            error(std::string(kWrongType), "an agent declaration must be an object",
                  node_range(*item));
            return false;
        }
        if (!check_fields(*item,
                          {"name", "symbol_ref", "states", "initial", "finals", "transitions",
                           "input_type", "context_type", "output_type", "context_kind",
                           "capabilities", "source_range"},
                          "agent declaration")) {
            return false;
        }
        CoreAgentDecl decl;
        const auto name = req_string(*item, "name");
        if (!name.has_value()) {
            return false;
        }
        decl.name = *name;
        const auto *symbol = item->get("symbol_ref");
        if (symbol == nullptr) {
            error(std::string(kMissingField), "agent is missing 'symbol_ref'", node_range(*item));
            return false;
        }
        const auto ref = read_symbol_ref(*symbol);
        if (!ref.has_value()) {
            return false;
        }
        decl.symbol_ref = *ref;
        const auto states = req_string_array(*item, "states");
        if (!states.has_value()) {
            return false;
        }
        decl.states = *states;
        const auto initial = req_u32(*item, "initial");
        if (!initial.has_value()) {
            return false;
        }
        decl.initial = CoreStateId{*initial};
        const auto finals = req_u32_array(*item, "finals");
        if (!finals.has_value()) {
            return false;
        }
        decl.finals.clear();
        for (const auto raw : *finals) {
            decl.finals.push_back(CoreStateId{raw});
        }
        const auto *transitions = item->get("transitions");
        if (transitions == nullptr || !transitions->is_array()) {
            error(std::string(kMissingField), "agent needs an array 'transitions'",
                  node_range(*item));
            return false;
        }
        decl.transitions.clear();
        for (const auto &transition : transitions->array_items) {
            if (!transition->is_object()) {
                error(std::string(kWrongType), "a transition must be an object",
                      node_range(*transition));
                return false;
            }
            if (!check_fields(*transition, {"from", "to"}, "transition")) {
                return false;
            }
            const auto from = req_u32(*transition, "from");
            const auto to = req_u32(*transition, "to");
            if (!from.has_value() || !to.has_value()) {
                return false;
            }
            decl.transitions.push_back(CoreTransition{CoreStateId{*from}, CoreStateId{*to}});
        }
        const auto input_type = req_u32(*item, "input_type");
        const auto output_type = req_u32(*item, "output_type");
        if (!input_type.has_value() || !output_type.has_value()) {
            return false;
        }
        decl.input_type = CoreTypeId{*input_type};
        decl.output_type = CoreTypeId{*output_type};
        const auto context_kind = req_string(*item, "context_kind");
        if (!context_kind.has_value() || !parse_context_kind(*context_kind, decl.context_kind)) {
            error(std::string(kUnknownKind),
                  "agent has an unknown context_kind '" + context_kind.value_or("") + "'",
                  node_range(*item));
            return false;
        }
        // `context_type` is present iff the context is a Struct (§4); a Unit
        // context has kInvalid and the field is absent. Requiring the field's
        // presence to agree with the kind keeps the two encodings from drifting.
        const auto *context_type = item->get("context_type");
        if (decl.context_kind == CoreAgentDecl::ContextKind::Struct) {
            if (context_type == nullptr) {
                error(std::string(kMissingField),
                      "agent has a struct context but no 'context_type'", node_range(*item));
                return false;
            }
            const auto value = req_u32(*item, "context_type");
            if (!value.has_value()) {
                return false;
            }
            decl.context_type = CoreTypeId{*value};
        } else if (context_type != nullptr) {
            error(std::string(kFieldMaskViolation),
                  "agent has a unit context but carries a 'context_type' field",
                  node_range(*item));
            return false;
        }
        const auto capabilities = req_u32_array(*item, "capabilities");
        if (!capabilities.has_value()) {
            return false;
        }
        decl.capabilities.clear();
        for (const auto raw : *capabilities) {
            decl.capabilities.push_back(CoreCapabilityId{raw});
        }
        const auto range = opt_source_range(*item, "source_range");
        if (!range.has_value()) {
            return false;
        }
        decl.source_range = *range;
        program.agents.push_back(std::move(decl));
    }
    return ok();
}

bool CoreJsonReader::read_flows(const JsonValue &array, CoreProgram &program) {
    program.flows.clear();
    program.flows.reserve(array.array_items.size());
    for (const auto &item : array.array_items) {
        if (!item->is_object()) {
            error(std::string(kWrongType), "a flow must be an object", node_range(*item));
            return false;
        }
        if (!check_fields(*item,
                          {"agent", "agent_name", "target_ref", "value_count", "exprs",
                           "value_types", "coercion_plans", "patterns", "states"},
                          "flow declaration")) {
            return false;
        }
        CoreFlowDecl decl;
        const auto agent = req_u32(*item, "agent");
        if (!agent.has_value()) {
            return false;
        }
        decl.target = CoreAgentId{*agent};
        const auto agent_name = req_string(*item, "agent_name");
        if (!agent_name.has_value()) {
            return false;
        }
        decl.agent_name = *agent_name;
        const auto *target_ref = item->get("target_ref");
        if (target_ref == nullptr) {
            error(std::string(kMissingField), "flow is missing 'target_ref'", node_range(*item));
            return false;
        }
        const auto ref = read_symbol_ref(*target_ref);
        if (!ref.has_value()) {
            return false;
        }
        decl.target_ref = *ref;
        const auto value_count = req_u32(*item, "value_count");
        if (!value_count.has_value()) {
            return false;
        }
        decl.storage.value_count = *value_count;
        const auto *exprs = item->get("exprs");
        if (exprs == nullptr || !exprs->is_array()) {
            error(std::string(kMissingField), "flow needs an array 'exprs'", node_range(*item));
            return false;
        }
        if (!read_exprs(*exprs, decl.storage.exprs)) {
            return false;
        }
        const auto value_types = map_value_type_id_array(*item, "value_types");
        if (!value_types.has_value()) {
            return false;
        }
        decl.storage.value_types = *value_types;
        // §5 — the per-body value_types table is DENSE (size == value_count).
        if (decl.storage.value_types.size() != decl.storage.value_count) {
            error(std::string(kValueCountMismatch),
                  "flow value_types size " + std::to_string(decl.storage.value_types.size()) +
                      " disagrees with value_count " + std::to_string(decl.storage.value_count),
                  node_range(*item));
            return false;
        }
        const auto *plans = item->get("coercion_plans");
        if (plans == nullptr || !plans->is_array()) {
            error(std::string(kMissingField), "flow needs an array 'coercion_plans'",
                  node_range(*item));
            return false;
        }
        if (!read_coercion_plans(*plans, decl.storage.coercion_plans)) {
            return false;
        }
        const auto *patterns = item->get("patterns");
        if (patterns == nullptr || !patterns->is_array()) {
            error(std::string(kMissingField), "flow needs an array 'patterns'", node_range(*item));
            return false;
        }
        if (!read_patterns(*patterns, decl.storage.patterns)) {
            return false;
        }
        const auto *states = item->get("states");
        if (states == nullptr || !states->is_array()) {
            error(std::string(kMissingField), "flow needs an array 'states'", node_range(*item));
            return false;
        }
        for (const auto &state : states->array_items) {
            if (!state->is_object()) {
                error(std::string(kWrongType), "a flow state must be an object",
                      node_range(*state));
                return false;
            }
            if (!check_fields(*state, {"state", "state_name", "policy", "body"}, "flow state")) {
                return false;
            }
            CoreFlowState out;
            const auto state_id = req_u32(*state, "state");
            const auto state_name = req_string(*state, "state_name");
            if (!state_id.has_value() || !state_name.has_value()) {
                return false;
            }
            out.state = CoreStateId{*state_id};
            out.state_name = *state_name;
            const auto *policy = state->get("policy");
            if (policy == nullptr || !policy->is_object()) {
                error(std::string(kMissingField), "flow state needs a 'policy' object",
                      node_range(*state));
                return false;
            }
            if (!check_fields(*policy, {"retry_limit", "retry_on", "timeout"}, "state policy")) {
                return false;
            }
            if (policy->get("retry_limit") != nullptr) {
                const auto retry_limit = req_string(*policy, "retry_limit");
                if (!retry_limit.has_value()) {
                    return false;
                }
                out.policy.retry_limit = *retry_limit;
            }
            if (policy->get("retry_on") != nullptr) {
                const auto retry_on = req_string_array(*policy, "retry_on");
                if (!retry_on.has_value()) {
                    return false;
                }
                out.policy.retry_on = *retry_on;
            }
            if (policy->get("timeout") != nullptr) {
                const auto timeout = req_string(*policy, "timeout");
                if (!timeout.has_value()) {
                    return false;
                }
                out.policy.timeout = *timeout;
            }
            const auto *body = state->get("body");
            if (body == nullptr) {
                error(std::string(kMissingField), "flow state is missing 'body'",
                      node_range(*state));
                return false;
            }
            if (!read_region(*body, out.body, /*depth=*/0)) {
                return false;
            }
            decl.states.push_back(std::move(out));
        }
        program.flows.push_back(std::move(decl));
    }
    return ok();
}

bool CoreJsonReader::read_workflows(const JsonValue &array, CoreProgram &program) {
    program.workflows.clear();
    program.workflows.reserve(array.array_items.size());
    for (const auto &item : array.array_items) {
        if (!item->is_object()) {
            error(std::string(kWrongType), "a workflow must be an object", node_range(*item));
            return false;
        }
        if (!check_fields(*item,
                          {"id", "name", "symbol_ref", "input_type", "output_type", "value_count",
                           "exprs", "value_types", "coercion_plans", "patterns", "nodes",
                           "return_region"},
                          "workflow declaration")) {
            return false;
        }
        CoreWorkflowDecl decl;
        const auto id = req_u32(*item, "id");
        if (!id.has_value()) {
            return false;
        }
        decl.id = CoreWorkflowId{*id};
        const auto name = req_string(*item, "name");
        if (!name.has_value()) {
            return false;
        }
        decl.name = *name;
        const auto *symbol = item->get("symbol_ref");
        if (symbol == nullptr) {
            error(std::string(kMissingField), "workflow is missing 'symbol_ref'",
                  node_range(*item));
            return false;
        }
        const auto ref = read_symbol_ref(*symbol);
        if (!ref.has_value()) {
            return false;
        }
        decl.symbol_ref = *ref;
        const auto input_type = req_u32(*item, "input_type");
        const auto output_type = req_u32(*item, "output_type");
        if (!input_type.has_value() || !output_type.has_value()) {
            return false;
        }
        decl.input_type = CoreTypeId{*input_type};
        decl.output_type = CoreTypeId{*output_type};
        const auto value_count = req_u32(*item, "value_count");
        if (!value_count.has_value()) {
            return false;
        }
        decl.storage.value_count = *value_count;
        const auto *exprs = item->get("exprs");
        if (exprs == nullptr || !exprs->is_array()) {
            error(std::string(kMissingField), "workflow needs an array 'exprs'",
                  node_range(*item));
            return false;
        }
        if (!read_exprs(*exprs, decl.storage.exprs)) {
            return false;
        }
        const auto value_types = map_value_type_id_array(*item, "value_types");
        if (!value_types.has_value()) {
            return false;
        }
        decl.storage.value_types = *value_types;
        if (decl.storage.value_types.size() != decl.storage.value_count) {
            error(std::string(kValueCountMismatch),
                  "workflow value_types size " + std::to_string(decl.storage.value_types.size()) +
                      " disagrees with value_count " + std::to_string(decl.storage.value_count),
                  node_range(*item));
            return false;
        }
        const auto *plans = item->get("coercion_plans");
        if (plans == nullptr || !plans->is_array()) {
            error(std::string(kMissingField), "workflow needs an array 'coercion_plans'",
                  node_range(*item));
            return false;
        }
        if (!read_coercion_plans(*plans, decl.storage.coercion_plans)) {
            return false;
        }
        const auto *patterns = item->get("patterns");
        if (patterns == nullptr || !patterns->is_array()) {
            error(std::string(kMissingField), "workflow needs an array 'patterns'",
                  node_range(*item));
            return false;
        }
        if (!read_patterns(*patterns, decl.storage.patterns)) {
            return false;
        }
        const auto *nodes = item->get("nodes");
        if (nodes == nullptr || !nodes->is_array()) {
            error(std::string(kMissingField), "workflow needs an array 'nodes'",
                  node_range(*item));
            return false;
        }
        for (const auto &node : nodes->array_items) {
            if (!node->is_object()) {
                error(std::string(kWrongType), "a workflow node must be an object",
                      node_range(*node));
                return false;
            }
            if (!check_fields(*node,
                              {"id", "target_instance", "node_name", "target_ref", "after",
                               "input_region"},
                              "workflow node")) {
                return false;
            }
            CoreWorkflowNode out;
            const auto node_id = req_u32(*node, "id");
            const auto target = req_u32(*node, "target_instance");
            const auto node_name = req_string(*node, "node_name");
            if (!node_id.has_value() || !target.has_value() || !node_name.has_value()) {
                return false;
            }
            out.id = CoreWorkflowNodeId{*node_id};
            out.target_instance = CoreInstanceId{*target};
            out.node_name = *node_name;
            const auto *target_ref = node->get("target_ref");
            if (target_ref == nullptr) {
                error(std::string(kMissingField), "workflow node is missing 'target_ref'",
                      node_range(*node));
                return false;
            }
            const auto node_ref = read_symbol_ref(*target_ref);
            if (!node_ref.has_value()) {
                return false;
            }
            out.target_ref = *node_ref;
            const auto after = req_u32_array(*node, "after");
            if (!after.has_value()) {
                return false;
            }
            out.after.clear();
            for (const auto raw : *after) {
                out.after.push_back(CoreWorkflowNodeId{raw});
            }
            const auto *input_region = node->get("input_region");
            if (input_region == nullptr) {
                error(std::string(kMissingField), "workflow node is missing 'input_region'",
                      node_range(*node));
                return false;
            }
            out.input_region = std::make_unique<CoreRegion>();
            if (!read_region(*input_region, *out.input_region, /*depth=*/0)) {
                return false;
            }
            decl.nodes.push_back(std::move(out));
        }
        if (const auto *return_region = item->get("return_region"); return_region != nullptr) {
            decl.return_region = std::make_unique<CoreRegion>();
            if (!read_region(*return_region, *decl.return_region, /*depth=*/0)) {
                return false;
            }
        }
        program.workflows.push_back(std::move(decl));
    }
    return ok();
}

bool CoreJsonReader::read_instances(const JsonValue &array, CoreProgram &program) {
    program.instances.clear();
    program.instances.reserve(array.array_items.size());
    std::unordered_set<std::string> keys;
    for (const auto &item : array.array_items) {
        if (!item->is_object()) {
            error(std::string(kWrongType), "an instance must be an object", node_range(*item));
            return false;
        }
        if (!check_fields(*item, {"id", "instance_key", "origin", "dispatch_types", "payload"},
                          "instance declaration")) {
            return false;
        }
        CoreInstanceDecl decl;
        const auto id = req_u32(*item, "id");
        if (!id.has_value()) {
            return false;
        }
        decl.id = CoreInstanceId{*id};
        const auto key = req_string(*item, "instance_key");
        if (!key.has_value()) {
            return false;
        }
        decl.instance_key = *key;
        if (!keys.insert(decl.instance_key).second) {
            error(std::string(kDuplicateInstanceKey),
                  "duplicate instance_key '" + decl.instance_key + "'", node_range(*item));
            return false;
        }
        const auto *origin = item->get("origin");
        if (origin == nullptr) {
            error(std::string(kMissingField), "instance is missing 'origin'", node_range(*item));
            return false;
        }
        const auto ref = read_symbol_ref(*origin);
        if (!ref.has_value()) {
            return false;
        }
        decl.origin = *ref;
        const auto dispatch = map_value_type_id_array(*item, "dispatch_types");
        if (!dispatch.has_value()) {
            return false;
        }
        decl.dispatch_types = *dispatch;

        const auto *payload = item->get("payload");
        if (payload == nullptr || !payload->is_object()) {
            error(std::string(kMissingField), "instance needs a 'payload' object",
                  node_range(*item));
            return false;
        }
        const auto payload_kind = req_string(*payload, "kind");
        if (!payload_kind.has_value()) {
            return false;
        }
        const auto payload_index =
            wire_name_index(core_node_detail::kCoreInstancePayloadWireNames, *payload_kind);
        if (!payload_index.has_value()) {
            error(std::string(kUnknownKind),
                  "instance payload has an unknown kind '" + *payload_kind + "'",
                  node_range(*payload));
            return false;
        }
        switch (payload_index.value()) {
        case 0: { // capability
            if (!check_fields(*payload, {"kind", "base"}, "capability instance payload")) {
                return false;
            }
            const auto base = req_u32(*payload, "base");
            if (!base.has_value()) {
                return false;
            }
            decl.payload = CoreCapabilityInstance{CoreCapabilityId{*base}};
            break;
        }
        case 1: // predicate
            if (!check_fields(*payload, {"kind"}, "predicate instance payload")) {
                return false;
            }
            decl.payload = CorePredicateInstance{};
            break;
        case 2: { // agent
            if (!check_fields(*payload,
                              {"kind", "base", "input_type", "context_kind", "context_type",
                               "output_type"},
                              "agent instance payload")) {
                return false;
            }
            CoreAgentInstance agent;
            const auto base = req_u32(*payload, "base");
            const auto input_type = req_u32(*payload, "input_type");
            const auto output_type = req_u32(*payload, "output_type");
            const auto context_kind = req_string(*payload, "context_kind");
            if (!base.has_value() || !input_type.has_value() || !output_type.has_value() ||
                !context_kind.has_value() ||
                !parse_context_kind(*context_kind, agent.context_kind)) {
                error(std::string(kUnknownKind), "agent instance has an invalid shell",
                      node_range(*payload));
                return false;
            }
            agent.base = CoreAgentId{*base};
            agent.input_type = CoreTypeId{*input_type};
            agent.output_type = CoreTypeId{*output_type};
            const auto *context_type = payload->get("context_type");
            if (agent.context_kind == CoreAgentDecl::ContextKind::Struct) {
                if (context_type == nullptr) {
                    error(std::string(kMissingField),
                          "agent instance has a struct context but no 'context_type'",
                          node_range(*payload));
                    return false;
                }
                const auto value = req_u32(*payload, "context_type");
                if (!value.has_value()) {
                    return false;
                }
                agent.context_type = CoreTypeId{*value};
            } else if (context_type != nullptr) {
                error(std::string(kFieldMaskViolation),
                      "agent instance has a unit context but carries a 'context_type' field",
                      node_range(*payload));
                return false;
            }
            decl.payload = agent;
            break;
        }
        case 3: { // workflow
            if (!check_fields(*payload, {"kind", "base", "input_type", "output_type"},
                              "workflow instance payload")) {
                return false;
            }
            CoreWorkflowInstance workflow;
            const auto base = req_u32(*payload, "base");
            const auto input_type = req_u32(*payload, "input_type");
            const auto output_type = req_u32(*payload, "output_type");
            if (!base.has_value() || !input_type.has_value() || !output_type.has_value()) {
                return false;
            }
            workflow.base = CoreWorkflowId{*base};
            workflow.input_type = CoreTypeId{*input_type};
            workflow.output_type = CoreTypeId{*output_type};
            decl.payload = workflow;
            break;
        }
        case 4: // fn
            if (!check_fields(*payload, {"kind"}, "fn instance payload")) {
                return false;
            }
            decl.payload = CoreFnInstance{};
            break;
        default:
            error(std::string(kUnknownKind), "unknown instance payload kind", node_range(*payload));
            return false;
        }
        program.instances.push_back(std::move(decl));
    }
    return ok();
}

// RFC 0026 FB-1: outlined fn bodies. `fns` is read AFTER `instances`: each fn
// is linked 1:1 to an Fn-kind instance, and reading the instances first lets us
// reconstruct the payload's derived `CoreFnInstance::body` back-link rather
// than trusting a serialized one (Principle 2: the fn table position IS the
// body identity). The verifier (run after the whole document is assembled)
// supplies every remaining link/SSA/signature gate.
bool CoreJsonReader::read_fns(const JsonValue &array, CoreProgram &program) {
    program.fns.clear();
    program.fns.reserve(array.array_items.size());
    for (const auto &item : array.array_items) {
        if (!item->is_object()) {
            error(std::string(kWrongType), "a fn must be an object", node_range(*item));
            return false;
        }
        if (!check_fields(*item,
                          {"id", "instance", "origin", "params", "captures", "env_bindings",
                           "value_count", "exprs", "value_types", "coercion_plans", "patterns",
                           "body", "name", "source_range"},
                          "fn declaration")) {
            return false;
        }
        CoreFnDecl decl;
        const auto id = req_u32(*item, "id");
        if (!id.has_value()) {
            return false;
        }
        decl.id = CoreFnId{*id};
        const auto instance = req_u32(*item, "instance");
        if (!instance.has_value()) {
            return false;
        }
        decl.instance = CoreInstanceId{*instance};
        const auto *origin = item->get("origin");
        if (origin == nullptr) {
            error(std::string(kMissingField), "fn is missing 'origin'", node_range(*item));
            return false;
        }
        const auto ref = read_symbol_ref(*origin);
        if (!ref.has_value()) {
            return false;
        }
        decl.origin = *ref;
        const auto params = req_u32_array(*item, "params");
        if (!params.has_value()) {
            return false;
        }
        decl.params.reserve(params->size());
        for (const std::uint32_t param : *params) {
            decl.params.push_back(CoreValueId{param});
        }
        // FB-3a1/FB-3a2 added the declared capture signature and the pre-bound
        // env-slot value ids to the fn object WITHOUT bumping the pre-
        // stabilization 'ahfl.core.v1' format. Both fields therefore default to
        // empty when absent so an older v1 fn object still reads; the
        // env_bindings.size()==captures.size() gate below rejects any partial /
        // inconsistent pairing.
        const auto captures_node = item->get("captures");
        if (captures_node == nullptr) {
            decl.captures.clear();
        } else {
            const auto captures = map_value_type_id_array(*item, "captures");
            if (!captures.has_value()) {
                return false;
            }
            decl.captures = *captures;
        }
        const auto env_node = item->get("env_bindings");
        if (env_node == nullptr) {
            decl.env_bindings.clear();
        } else {
            // FB-3a2: env_bindings are body-local value ids (the same
            // req_u32_array remap params use), parallel to the declared
            // captures.
            const auto env_bindings = req_u32_array(*item, "env_bindings");
            if (!env_bindings.has_value()) {
                return false;
            }
            decl.env_bindings.reserve(env_bindings->size());
            for (const std::uint32_t slot : *env_bindings) {
                decl.env_bindings.push_back(CoreValueId{slot});
            }
        }
        const auto value_count = req_u32(*item, "value_count");
        if (!value_count.has_value()) {
            return false;
        }
        decl.storage.value_count = *value_count;
        const auto *exprs = item->get("exprs");
        if (exprs == nullptr || !exprs->is_array()) {
            error(std::string(kMissingField), "fn needs an array 'exprs'", node_range(*item));
            return false;
        }
        if (!read_exprs(*exprs, decl.storage.exprs)) {
            return false;
        }
        const auto value_types = map_value_type_id_array(*item, "value_types");
        if (!value_types.has_value()) {
            return false;
        }
        decl.storage.value_types = *value_types;
        // §5 — the fn body's value_types table is DENSE (size == value_count),
        // same rule as a flow/workflow body.
        if (decl.storage.value_types.size() != decl.storage.value_count) {
            error(std::string(kValueCountMismatch),
                  "fn value_types size " + std::to_string(decl.storage.value_types.size()) +
                      " disagrees with value_count " + std::to_string(decl.storage.value_count),
                  node_range(*item));
            return false;
        }
        const auto *plans = item->get("coercion_plans");
        if (plans == nullptr || !plans->is_array()) {
            error(std::string(kMissingField), "fn needs an array 'coercion_plans'",
                  node_range(*item));
            return false;
        }
        if (!read_coercion_plans(*plans, decl.storage.coercion_plans)) {
            return false;
        }
        const auto *patterns = item->get("patterns");
        if (patterns == nullptr || !patterns->is_array()) {
            error(std::string(kMissingField), "fn needs an array 'patterns'", node_range(*item));
            return false;
        }
        if (!read_patterns(*patterns, decl.storage.patterns)) {
            return false;
        }
        const auto *body = item->get("body");
        if (body == nullptr) {
            error(std::string(kMissingField), "fn is missing 'body'", node_range(*item));
            return false;
        }
        if (!read_region(*body, decl.body, /*depth=*/0)) {
            return false;
        }
        const auto name = req_string(*item, "name");
        if (!name.has_value()) {
            return false;
        }
        decl.name = *name;
        const auto range = opt_source_range(*item, "source_range");
        if (!range.has_value()) {
            return false;
        }
        decl.source_range = *range;
        program.fns.push_back(std::move(decl));
    }
    // Rebuild each Fn instance's derived body back-link from the fn table.
    for (std::uint32_t fi = 0; fi < program.fns.size(); ++fi) {
        const CoreFnDecl &fn = program.fns[fi];
        if (fn.instance.value >= program.instances.size()) {
            // Range is reported by the verifier with the fn's own source range.
            continue;
        }
        auto *payload = std::get_if<CoreFnInstance>(&program.instances[fn.instance.value].payload);
        if (payload != nullptr) {
            payload->body = CoreFnId{fi};
        }
    }
    return ok();
}

bool CoreJsonReader::read_exprs(const JsonValue &array, std::vector<CoreExpr> &out) {
    out.clear();
    out.reserve(array.array_items.size());
    for (const auto &item : array.array_items) {
        CoreExpr expr;
        if (!read_expr(*item, expr)) {
            return false;
        }
        out.push_back(std::move(expr));
    }
    return ok();
}

bool CoreJsonReader::read_path_root_and_projection(const JsonValue &obj, CorePathRoot &root,
                                                   std::string &root_name,
                                                   std::vector<std::string> &members,
                                                   CoreTypeId &root_type,
                                                   std::vector<CoreProjectionStep> &projection,
                                                   bool &projection_resolved) {
    const auto root_str = req_string(obj, "root");
    if (!root_str.has_value() || !parse_path_root(*root_str, root)) {
        error(std::string(kUnknownKind), "unknown path root '" + root_str.value_or("") + "'",
              node_range(obj));
        return false;
    }
    const auto name = req_string(obj, "root_name");
    if (!name.has_value()) {
        return false;
    }
    root_name = *name;
    const auto member_list = req_string_array(obj, "members");
    if (!member_list.has_value()) {
        return false;
    }
    members = *member_list;
    const auto root_type_id = req_u32(obj, "root_type");
    if (!root_type_id.has_value()) {
        return false;
    }
    root_type = CoreTypeId{*root_type_id};
    const auto resolved = opt_bool(obj, "projection_resolved", true);
    if (!resolved.has_value()) {
        return false;
    }
    projection_resolved = *resolved;
    const auto *steps = obj.get("projection");
    if (steps == nullptr || !steps->is_array()) {
        error(std::string(kMissingField), "path is missing an array 'projection'", node_range(obj));
        return false;
    }
    projection.clear();
    for (const auto &step : steps->array_items) {
        if (!step->is_object()) {
            error(std::string(kWrongType), "a projection step must be an object",
                  node_range(*step));
            return false;
        }
        if (!check_fields(*step, {"owner_type", "field", "result_type"}, "projection step")) {
            return false;
        }
        const auto owner = req_u32(*step, "owner_type");
        const auto field = req_u32(*step, "field");
        const auto result = req_u32(*step, "result_type");
        if (!owner.has_value() || !field.has_value() || !result.has_value()) {
            return false;
        }
        projection.push_back(CoreProjectionStep{CoreTypeId{*owner}, CoreFieldId{*field},
                                                CoreTypeId{*result}});
    }
    return ok();
}

bool CoreJsonReader::read_expr(const JsonValue &obj, CoreExpr &out) {
    if (!obj.is_object()) {
        error(std::string(kWrongType), "an expression must be an object", node_range(obj));
        return false;
    }
    const auto kind = req_string(obj, "kind");
    if (!kind.has_value()) {
        return false;
    }
    const auto index = wire_name_index(core_node_detail::kCoreExprWireNames, *kind);
    if (!index.has_value()) {
        error(std::string(kUnknownKind), "unknown expression kind '" + *kind + "'", node_range(obj));
        return false;
    }
    const auto range = opt_source_range(obj, "source_range");
    if (!range.has_value()) {
        return false;
    }
    out.source_range = *range;
    const auto result_type = map_single_value_type_id(obj, "result_type");
    if (!result_type.has_value()) {
        return false;
    }
    out.result_type = *result_type;

    switch (index.value()) {
    case 0: { // CoreLiteralExpr
        if (!check_fields(obj,
                          {"kind", "source_range", "result_type", "literal_kind", "spelling"},
                          "literal expression")) {
            return false;
        }
        CoreLiteralExpr node;
        const auto literal_kind = req_string(obj, "literal_kind");
        const auto spelling = req_string(obj, "spelling");
        if (!literal_kind.has_value() || !spelling.has_value() ||
            !parse_literal_kind(*literal_kind, node.kind)) {
            error(std::string(kUnknownKind), "literal expression has an unknown literal_kind",
                  node_range(obj));
            return false;
        }
        node.spelling = *spelling;
        out.node = std::move(node);
        break;
    }
    case 1: { // CoreValueRefExpr
        if (!check_fields(obj, {"kind", "source_range", "result_type", "value"},
                          "value-ref expression")) {
            return false;
        }
        const auto value = req_u32(obj, "value");
        if (!value.has_value()) {
            return false;
        }
        out.node = CoreValueRefExpr{CoreValueId{*value}};
        break;
    }
    case 2: { // CorePathExpr
        if (!check_fields(obj,
                          {"kind", "source_range", "result_type", "root", "root_name", "members",
                           "root_type", "projection", "projection_resolved", "has_local", "local",
                           "workflow_node"},
                          "path expression")) {
            return false;
        }
        CorePathExpr node;
        if (!read_path_root_and_projection(obj, node.root, node.root_name, node.members,
                                           node.root_type, node.projection,
                                           node.projection_resolved)) {
            return false;
        }
        if (obj.get("has_local") != nullptr) {
            const auto has_local = req_bool(obj, "has_local");
            if (!has_local.has_value()) {
                return false;
            }
            node.has_local = *has_local;
            if (node.has_local) {
                const auto local = req_u32(obj, "local");
                if (!local.has_value()) {
                    return false;
                }
                node.local = CoreValueId{*local};
            }
        }
        if (obj.get("workflow_node") != nullptr) {
            const auto workflow_node = req_u32(obj, "workflow_node");
            if (!workflow_node.has_value()) {
                return false;
            }
            node.workflow_node = CoreWorkflowNodeId{*workflow_node};
        }
        out.node = std::move(node);
        break;
    }
    case 3: { // CoreQualifiedExpr
        if (!check_fields(obj,
                          {"kind", "source_range", "result_type", "name", "type_id", "variant",
                           "resolved"},
                          "qualified expression")) {
            return false;
        }
        CoreQualifiedExpr node;
        const auto name = req_string(obj, "name");
        const auto type_id = req_u32(obj, "type_id");
        const auto variant = req_u32(obj, "variant");
        const auto resolved = req_bool(obj, "resolved");
        if (!name.has_value() || !type_id.has_value() || !variant.has_value() ||
            !resolved.has_value()) {
            return false;
        }
        node.name = *name;
        node.type_id = CoreTypeId{*type_id};
        node.variant = CoreVariantId{*variant};
        node.resolved = *resolved;
        out.node = std::move(node);
        break;
    }
    case 4: { // CoreUnaryExpr
        if (!check_fields(obj, {"kind", "source_range", "result_type", "op", "operand"},
                          "unary expression")) {
            return false;
        }
        CoreUnaryExpr node;
        const auto op = req_string(obj, "op");
        const auto operand = req_u32(obj, "operand");
        if (!op.has_value() || !operand.has_value() || !parse_unary_op(*op, node.op)) {
            error(std::string(kUnknownKind), "unary expression has an unknown op", node_range(obj));
            return false;
        }
        node.operand = CoreExprId{*operand};
        out.node = node;
        break;
    }
    case 5: { // CoreBinaryExpr
        if (!check_fields(obj, {"kind", "source_range", "result_type", "op", "lhs", "rhs"},
                          "binary expression")) {
            return false;
        }
        CoreBinaryExpr node;
        const auto op = req_string(obj, "op");
        const auto lhs = req_u32(obj, "lhs");
        const auto rhs = req_u32(obj, "rhs");
        if (!op.has_value() || !lhs.has_value() || !rhs.has_value() ||
            !parse_binary_op(*op, node.op)) {
            error(std::string(kUnknownKind), "binary expression has an unknown op", node_range(obj));
            return false;
        }
        node.lhs = CoreExprId{*lhs};
        node.rhs = CoreExprId{*rhs};
        out.node = node;
        break;
    }
    case 6: { // CoreConstructExpr
        if (!check_fields(obj,
                          {"kind", "source_range", "result_type", "type_name", "variant_name",
                           "is_enum_variant", "type_id", "variant", "resolved", "args"},
                          "construct expression")) {
            return false;
        }
        CoreConstructExpr node;
        const auto type_name = req_string(obj, "type_name");
        const auto variant_name = req_string(obj, "variant_name");
        const auto is_enum_variant = req_bool(obj, "is_enum_variant");
        const auto type_id = req_u32(obj, "type_id");
        const auto variant = req_u32(obj, "variant");
        const auto resolved = req_bool(obj, "resolved");
        if (!type_name.has_value() || !variant_name.has_value() || !is_enum_variant.has_value() ||
            !type_id.has_value() || !variant.has_value() || !resolved.has_value()) {
            return false;
        }
        node.type_name = *type_name;
        node.variant_name = *variant_name;
        node.is_enum_variant = *is_enum_variant;
        node.type_id = CoreTypeId{*type_id};
        node.variant = CoreVariantId{*variant};
        node.resolved = *resolved;
        const auto *args = obj.get("args");
        if (args == nullptr || !args->is_array()) {
            error(std::string(kMissingField), "construct expression needs an array 'args'",
                  node_range(obj));
            return false;
        }
        for (const auto &arg : args->array_items) {
            if (!arg->is_object()) {
                error(std::string(kWrongType), "a construct arg must be an object",
                      node_range(*arg));
                return false;
            }
            if (!check_fields(*arg, {"field", "value"}, "construct arg")) {
                return false;
            }
            const auto field = req_u32(*arg, "field");
            const auto value = req_u32(*arg, "value");
            if (!field.has_value() || !value.has_value()) {
                return false;
            }
            node.args.push_back(CoreConstructArg{CoreFieldId{*field}, CoreValueId{*value}});
        }
        out.node = std::move(node);
        break;
    }
    case 7: { // CoreCoerceExpr
        if (!check_fields(obj, {"kind", "source_range", "result_type", "operand", "plan"},
                          "coerce expression")) {
            return false;
        }
        const auto operand = req_u32(obj, "operand");
        const auto plan = req_u32(obj, "plan");
        if (!operand.has_value() || !plan.has_value()) {
            return false;
        }
        out.node = CoreCoerceExpr{CoreValueId{*operand}, CoreCoercionPlanId{*plan}};
        break;
    }
    case 8: { // CoreCollectionExpr
        if (!check_fields(obj,
                          {"kind", "source_range", "result_type", "op", "base", "index", "value"},
                          "collection expression")) {
            return false;
        }
        CoreCollectionExpr node;
        const auto op = req_string(obj, "op");
        const auto base = req_u32(obj, "base");
        const auto index = req_u32(obj, "index");
        const auto value = req_u32(obj, "value");
        if (!op.has_value() || !base.has_value() || !index.has_value() || !value.has_value() ||
            !parse_collection_op(*op, node.op)) {
            error(std::string(kUnknownKind), "collection expression has an unknown op",
                  node_range(obj));
            return false;
        }
        node.base = CoreValueId{*base};
        node.index = CoreValueId{*index};
        node.value = CoreValueId{*value};
        out.node = node;
        break;
    }
    case 9: { // CoreUnsupportedExpr
        if (!check_fields(obj, {"kind", "source_range", "result_type", "source_kind"},
                          "unsupported expression")) {
            return false;
        }
        const auto source_kind = req_string(obj, "source_kind");
        if (!source_kind.has_value()) {
            return false;
        }
        // The node's own range is restored from the wrapper (the writer emits a
        // single `source_range`; a second would be a duplicate JSON key).
        out.node = CoreUnsupportedExpr{*source_kind, out.source_range};
        break;
    }
    case 10: { // CoreCallExpr
        if (!check_fields(obj, {"kind", "source_range", "result_type", "callee", "args"},
                          "direct call expression")) {
            return false;
        }
        CoreCallExpr node;
        const auto callee = req_u32(obj, "callee");
        if (!callee.has_value()) {
            return false;
        }
        node.callee = CoreInstanceId{*callee};
        const auto args = req_u32_array(obj, "args");
        if (!args.has_value()) {
            return false;
        }
        node.args.reserve(args->size());
        for (const std::uint32_t arg : *args) {
            node.args.push_back(CoreValueId{arg});
        }
        out.node = std::move(node);
        break;
    }
    case 11: { // CoreClosureExpr
        if (!check_fields(obj, {"kind", "source_range", "result_type", "fn", "env"},
                          "closure expression")) {
            return false;
        }
        CoreClosureExpr node;
        const auto fn = req_u32(obj, "fn");
        if (!fn.has_value()) {
            return false;
        }
        node.fn = CoreFnId{*fn};
        const auto env = req_u32_array(obj, "env");
        if (!env.has_value()) {
            return false;
        }
        node.env.reserve(env->size());
        for (const std::uint32_t slot : *env) {
            node.env.push_back(CoreValueId{slot});
        }
        out.node = std::move(node);
        break;
    }
    case 12: { // CoreCallClosureExpr
        if (!check_fields(obj, {"kind", "source_range", "result_type", "callee", "args"},
                          "closure call expression")) {
            return false;
        }
        CoreCallClosureExpr node;
        const auto callee = req_u32(obj, "callee");
        if (!callee.has_value()) {
            return false;
        }
        node.callee = CoreValueId{*callee};
        const auto args = req_u32_array(obj, "args");
        if (!args.has_value()) {
            return false;
        }
        node.args.reserve(args->size());
        for (const std::uint32_t arg : *args) {
            node.args.push_back(CoreValueId{arg});
        }
        out.node = std::move(node);
        break;
    }
    default:
        error(std::string(kUnknownKind), "unknown expression kind '" + *kind + "'", node_range(obj));
        return false;
    }
    return ok();
}

bool CoreJsonReader::read_patterns(const JsonValue &array, std::vector<CorePattern> &out) {
    out.clear();
    out.reserve(array.array_items.size());
    for (const auto &item : array.array_items) {
        CorePattern pattern;
        if (!read_pattern(*item, pattern)) {
            return false;
        }
        out.push_back(std::move(pattern));
    }
    return ok();
}

bool CoreJsonReader::read_pattern(const JsonValue &obj, CorePattern &out) {
    if (!obj.is_object()) {
        error(std::string(kWrongType), "a pattern must be an object", node_range(obj));
        return false;
    }
    const auto kind = req_string(obj, "kind");
    if (!kind.has_value()) {
        return false;
    }
    const auto index = wire_name_index(core_node_detail::kCorePatternWireNames, *kind);
    if (!index.has_value()) {
        error(std::string(kUnknownKind), "unknown pattern kind '" + *kind + "'", node_range(obj));
        return false;
    }
    const auto range = opt_source_range(obj, "source_range");
    if (!range.has_value()) {
        return false;
    }
    out.source_range = *range;

    switch (index.value()) {
    case 0: // CoreWildcardPat
        if (!check_fields(obj, {"kind", "source_range"}, "wildcard pattern")) {
            return false;
        }
        out.node = CoreWildcardPat{};
        break;
    case 1: { // CoreLiteralPat
        if (!check_fields(obj, {"kind", "source_range", "literal_kind", "spelling"},
                          "literal pattern")) {
            return false;
        }
        CoreLiteralPat node;
        const auto literal_kind = req_string(obj, "literal_kind");
        const auto spelling = req_string(obj, "spelling");
        if (!literal_kind.has_value() || !spelling.has_value() ||
            !parse_literal_kind(*literal_kind, node.kind)) {
            error(std::string(kUnknownKind), "literal pattern has an unknown literal_kind",
                  node_range(obj));
            return false;
        }
        node.spelling = *spelling;
        out.node = std::move(node);
        break;
    }
    case 2: { // CoreIntRangePat
        if (!check_fields(obj, {"kind", "source_range", "start", "end"}, "int-range pattern")) {
            return false;
        }
        const auto start = req_i64(obj, "start");
        const auto end = req_i64(obj, "end");
        if (!start.has_value() || !end.has_value()) {
            return false;
        }
        out.node = CoreIntRangePat{*start, *end};
        break;
    }
    case 3: { // CoreBindingPat
        if (!check_fields(obj, {"kind", "source_range", "binding", "has_nested", "nested"},
                          "binding pattern")) {
            return false;
        }
        CoreBindingPat node;
        const auto binding = req_u32(obj, "binding");
        if (!binding.has_value()) {
            return false;
        }
        node.binding = CorePatternBindingId{*binding};
        if (obj.get("has_nested") != nullptr) {
            const auto has_nested = req_bool(obj, "has_nested");
            if (!has_nested.has_value()) {
                return false;
            }
            node.has_nested = *has_nested;
            if (node.has_nested) {
                const auto nested = req_u32(obj, "nested");
                if (!nested.has_value()) {
                    return false;
                }
                node.nested = CorePatternId{*nested};
            }
        }
        out.node = node;
        break;
    }
    case 4: { // CoreVariantPat
        if (!check_fields(obj,
                          {"kind", "source_range", "owner_enum", "variant", "tuple_subpatterns",
                           "struct_fields", "has_rest"},
                          "variant pattern")) {
            return false;
        }
        CoreVariantPat node;
        const auto owner_enum = req_u32(obj, "owner_enum");
        const auto variant = req_u32(obj, "variant");
        if (!owner_enum.has_value() || !variant.has_value()) {
            return false;
        }
        node.owner_enum = CoreTypeId{*owner_enum};
        node.variant = CoreVariantId{*variant};
        if (obj.get("tuple_subpatterns") != nullptr) {
            const auto subs = req_u32_array(obj, "tuple_subpatterns");
            if (!subs.has_value()) {
                return false;
            }
            for (const auto raw : *subs) {
                node.tuple_subpatterns.push_back(CorePatternId{raw});
            }
        }
        if (const auto *fields = obj.get("struct_fields"); fields != nullptr) {
            if (!fields->is_array()) {
                error(std::string(kWrongType), "'struct_fields' must be an array", node_range(obj));
                return false;
            }
            for (const auto &field : fields->array_items) {
                if (!field->is_object()) {
                    error(std::string(kWrongType), "a struct-field pattern must be an object",
                          node_range(*field));
                    return false;
                }
                if (!check_fields(*field, {"slot", "pattern"}, "struct-field pattern")) {
                    return false;
                }
                const auto slot = req_u32(*field, "slot");
                const auto sub = req_u32(*field, "pattern");
                if (!slot.has_value() || !sub.has_value()) {
                    return false;
                }
                node.struct_fields.push_back(
                    CoreVariantPatField{CoreFieldId{*slot}, CorePatternId{*sub}});
            }
        }
        const auto has_rest = req_bool(obj, "has_rest");
        if (!has_rest.has_value()) {
            return false;
        }
        node.has_rest = *has_rest;
        out.node = std::move(node);
        break;
    }
    case 5: { // CoreTuplePat
        if (!check_fields(obj, {"kind", "source_range", "elements"}, "tuple pattern")) {
            return false;
        }
        CoreTuplePat node;
        const auto elements = req_u32_array(obj, "elements");
        if (!elements.has_value()) {
            return false;
        }
        for (const auto raw : *elements) {
            node.elements.push_back(CorePatternId{raw});
        }
        out.node = std::move(node);
        break;
    }
    case 6: { // CoreOrPat
        if (!check_fields(obj, {"kind", "source_range", "alternatives"}, "or pattern")) {
            return false;
        }
        CoreOrPat node;
        const auto alternatives = req_u32_array(obj, "alternatives");
        if (!alternatives.has_value()) {
            return false;
        }
        for (const auto raw : *alternatives) {
            node.alternatives.push_back(CorePatternId{raw});
        }
        out.node = std::move(node);
        break;
    }
    default:
        error(std::string(kUnknownKind), "unknown pattern kind '" + *kind + "'", node_range(obj));
        return false;
    }
    return ok();
}

bool CoreJsonReader::read_coercion_plans(const JsonValue &array,
                                         std::vector<CoreCoercionPlanNode> &out) {
    out.clear();
    out.reserve(array.array_items.size());
    for (const auto &item : array.array_items) {
        if (!item->is_object()) {
            error(std::string(kWrongType), "a coercion plan node must be an object",
                  node_range(*item));
            return false;
        }
        if (!check_fields(*item, {"source", "result", "ops"}, "coercion plan node")) {
            return false;
        }
        CoreCoercionPlanNode node;
        const auto source = map_single_value_type_id(*item, "source");
        const auto result = map_single_value_type_id(*item, "result");
        if (!source.has_value() || !result.has_value()) {
            return false;
        }
        node.source = *source;
        node.result = *result;
        const auto *ops = item->get("ops");
        if (ops == nullptr || !ops->is_array()) {
            error(std::string(kMissingField), "coercion plan node needs an array 'ops'",
                  node_range(*item));
            return false;
        }
        for (const auto &op : ops->array_items) {
            if (!op->is_object()) {
                error(std::string(kWrongType), "a coercion op must be an object", node_range(*op));
                return false;
            }
            if (!check_fields(*op, {"kind", "arg_index", "child"}, "coercion op")) {
                return false;
            }
            CoreCoercionOp op_out;
            const auto kind = req_string(*op, "kind");
            if (!kind.has_value() || !parse_coercion_op(*kind, op_out.kind)) {
                error(std::string(kUnknownKind), "coercion op has an unknown kind",
                      node_range(*op));
                return false;
            }
            const auto arg_index = opt_u32(*op, "arg_index", 0);
            if (!arg_index.has_value()) {
                return false;
            }
            op_out.arg_index = *arg_index;
            if (op->get("child") != nullptr) {
                const auto child = req_u32(*op, "child");
                if (!child.has_value()) {
                    return false;
                }
                op_out.child = CoreCoercionPlanId{*child};
            }
            node.ops.push_back(op_out);
        }
        out.push_back(std::move(node));
    }
    return ok();
}

bool CoreJsonReader::read_place(const JsonValue &obj, CorePlace &out) {
    if (!obj.is_object()) {
        error(std::string(kWrongType), "a place must be an object", node_range(obj));
        return false;
    }
    if (!check_fields(obj,
                      {"root", "root_name", "members", "root_type", "projection",
                       "projection_resolved"},
                      "place")) {
        return false;
    }
    return read_path_root_and_projection(obj, out.root, out.root_name, out.members, out.root_type,
                                         out.projection, out.projection_resolved);
}

bool CoreJsonReader::read_region(const JsonValue &obj, CoreRegion &out, std::size_t depth) {
    if (depth > kMaxRegionNestingDepth) {
        error(std::string(kRegionTooDeep),
              "region nesting exceeds the reader's depth bound (" +
                  std::to_string(kMaxRegionNestingDepth) + ")",
              node_range(obj));
        return false;
    }
    if (!obj.is_object()) {
        error(std::string(kWrongType), "a region must be an object", node_range(obj));
        return false;
    }
    if (!check_fields(obj, {"statements"}, "region")) {
        return false;
    }
    const auto *statements = obj.get("statements");
    if (statements == nullptr || !statements->is_array()) {
        error(std::string(kMissingField), "region needs an array 'statements'", node_range(obj));
        return false;
    }
    out.statements.clear();
    out.statements.reserve(statements->array_items.size());
    for (const auto &stmt : statements->array_items) {
        CoreStmt parsed;
        if (!read_stmt(*stmt, parsed, depth)) {
            return false;
        }
        out.statements.push_back(std::move(parsed));
    }
    return ok();
}

bool CoreJsonReader::read_stmt(const JsonValue &obj, CoreStmt &out, std::size_t depth) {
    if (!obj.is_object()) {
        error(std::string(kWrongType), "a statement must be an object", node_range(obj));
        return false;
    }
    const auto kind = req_string(obj, "kind");
    if (!kind.has_value()) {
        return false;
    }
    const auto index = wire_name_index(core_node_detail::kCoreStmtWireNames, *kind);
    if (!index.has_value()) {
        error(std::string(kUnknownKind), "unknown statement kind '" + *kind + "'", node_range(obj));
        return false;
    }
    const auto range = opt_source_range(obj, "source_range");
    if (!range.has_value()) {
        return false;
    }
    out.source_range = *range;

    switch (index.value()) {
    case 0: { // CoreLetStmt
        if (!check_fields(obj, {"kind", "source_range", "result", "expr"}, "let statement")) {
            return false;
        }
        const auto result = req_u32(obj, "result");
        const auto expr = req_u32(obj, "expr");
        if (!result.has_value() || !expr.has_value()) {
            return false;
        }
        out.node = CoreLetStmt{CoreValueId{*result}, CoreExprId{*expr}};
        break;
    }
    case 1: { // CoreCapabilityCallStmt
        if (!check_fields(obj,
                          {"kind", "source_range", "result", "capability", "callee_name", "args"},
                          "capability-call statement")) {
            return false;
        }
        CoreCapabilityCallStmt node;
        const auto result = req_u32(obj, "result");
        const auto capability = req_u32(obj, "capability");
        const auto callee_name = req_string(obj, "callee_name");
        if (!result.has_value() || !capability.has_value() || !callee_name.has_value()) {
            return false;
        }
        node.result = CoreValueId{*result};
        node.capability = CoreCapabilityId{*capability};
        node.callee_name = *callee_name;
        const auto args = req_u32_array(obj, "args");
        if (!args.has_value()) {
            return false;
        }
        for (const auto raw : *args) {
            node.args.push_back(CoreValueId{raw});
        }
        out.node = std::move(node);
        break;
    }
    case 2: { // CoreStoreStmt
        if (!check_fields(obj, {"kind", "source_range", "place", "value"}, "store statement")) {
            return false;
        }
        CoreStoreStmt node;
        const auto *place = obj.get("place");
        if (place == nullptr) {
            error(std::string(kMissingField), "store statement is missing 'place'",
                  node_range(obj));
            return false;
        }
        if (!read_place(*place, node.place)) {
            return false;
        }
        const auto value = req_u32(obj, "value");
        if (!value.has_value()) {
            return false;
        }
        node.value = CoreValueId{*value};
        out.node = std::move(node);
        break;
    }
    case 3: { // CoreIfStmt
        if (!check_fields(obj,
                          {"kind", "source_range", "condition", "then_region", "else_region"},
                          "if statement")) {
            return false;
        }
        CoreIfStmt node;
        const auto condition = req_u32(obj, "condition");
        if (!condition.has_value()) {
            return false;
        }
        node.condition = CoreValueId{*condition};
        const auto *then_region = obj.get("then_region");
        if (then_region == nullptr) {
            error(std::string(kMissingField), "if statement is missing 'then_region'",
                  node_range(obj));
            return false;
        }
        node.then_region = std::make_unique<CoreRegion>();
        if (!read_region(*then_region, *node.then_region, depth + 1)) {
            return false;
        }
        if (const auto *else_region = obj.get("else_region"); else_region != nullptr) {
            node.else_region = std::make_unique<CoreRegion>();
            if (!read_region(*else_region, *node.else_region, depth + 1)) {
                return false;
            }
        }
        out.node = std::move(node);
        break;
    }
    case 4: { // CoreGotoStmt
        if (!check_fields(obj, {"kind", "source_range", "target", "target_name"},
                          "goto statement")) {
            return false;
        }
        CoreGotoStmt node;
        const auto target = req_u32(obj, "target");
        const auto target_name = req_string(obj, "target_name");
        if (!target.has_value() || !target_name.has_value()) {
            return false;
        }
        node.target = CoreStateId{*target};
        node.target_name = *target_name;
        out.node = std::move(node);
        break;
    }
    case 5: { // CoreReturnStmt
        if (!check_fields(obj, {"kind", "source_range", "has_value", "value"},
                          "return statement")) {
            return false;
        }
        CoreReturnStmt node;
        const auto has_value = req_bool(obj, "has_value");
        if (!has_value.has_value()) {
            return false;
        }
        node.has_value = *has_value;
        if (node.has_value) {
            const auto value = req_u32(obj, "value");
            if (!value.has_value()) {
                return false;
            }
            node.value = CoreValueId{*value};
        }
        out.node = node;
        break;
    }
    case 6: { // CoreYieldStmt
        if (!check_fields(obj, {"kind", "source_range", "has_value", "value"}, "yield statement")) {
            return false;
        }
        CoreYieldStmt node;
        const auto has_value = req_bool(obj, "has_value");
        if (!has_value.has_value()) {
            return false;
        }
        node.has_value = *has_value;
        if (node.has_value) {
            const auto value = req_u32(obj, "value");
            if (!value.has_value()) {
                return false;
            }
            node.value = CoreValueId{*value};
        }
        out.node = node;
        break;
    }
    case 7: { // CoreTrapStmt
        if (!check_fields(obj, {"kind", "source_range", "trap_kind"}, "trap statement")) {
            return false;
        }
        CoreTrapStmt node;
        const auto trap_kind = req_string(obj, "trap_kind");
        if (!trap_kind.has_value() || !parse_trap_kind(*trap_kind, node.kind)) {
            error(std::string(kUnknownKind), "trap statement has an unknown trap_kind",
                  node_range(obj));
            return false;
        }
        out.node = node;
        break;
    }
    case 8: { // CoreMatchStmt
        if (!check_fields(obj,
                          {"kind", "source_range", "scrutinee", "has_result", "result", "arms",
                           "fallback_region"},
                          "match statement")) {
            return false;
        }
        CoreMatchStmt node;
        const auto scrutinee = req_u32(obj, "scrutinee");
        const auto has_result = req_bool(obj, "has_result");
        if (!scrutinee.has_value() || !has_result.has_value()) {
            return false;
        }
        node.scrutinee = CoreValueId{*scrutinee};
        node.has_result = *has_result;
        if (node.has_result) {
            const auto result = req_u32(obj, "result");
            if (!result.has_value()) {
                return false;
            }
            node.result = CoreValueId{*result};
        }
        const auto *arms = obj.get("arms");
        if (arms == nullptr || !arms->is_array()) {
            error(std::string(kMissingField), "match statement needs an array 'arms'",
                  node_range(obj));
            return false;
        }
        for (const auto &arm : arms->array_items) {
            CoreMatchArm parsed;
            if (!read_match_arm(*arm, parsed, depth + 1)) {
                return false;
            }
            node.arms.push_back(std::move(parsed));
        }
        // `fallback_region` is mandatory in a well-formed program (§5); the
        // writer always emits it, so a missing one is a malformed document.
        const auto *fallback = obj.get("fallback_region");
        if (fallback == nullptr) {
            error(std::string(kMissingField), "match statement is missing 'fallback_region'",
                  node_range(obj));
            return false;
        }
        node.fallback_region = std::make_unique<CoreRegion>();
        if (!read_region(*fallback, *node.fallback_region, depth + 1)) {
            return false;
        }
        out.node = std::move(node);
        break;
    }
    case 9: { // CoreCallStmt
        if (!check_fields(obj, {"kind", "source_range", "result", "callee", "args"},
                          "call statement")) {
            return false;
        }
        CoreCallStmt node;
        const auto result = req_u32(obj, "result");
        const auto callee = req_u32(obj, "callee");
        if (!result.has_value() || !callee.has_value()) {
            return false;
        }
        node.result = CoreValueId{*result};
        node.callee = CoreInstanceId{*callee};
        const auto args = req_u32_array(obj, "args");
        if (!args.has_value()) {
            return false;
        }
        for (const auto raw : *args) {
            node.args.push_back(CoreValueId{raw});
        }
        out.node = std::move(node);
        break;
    }
    default:
        error(std::string(kUnknownKind), "unknown statement kind '" + *kind + "'", node_range(obj));
        return false;
    }
    return ok();
}

bool CoreJsonReader::read_match_arm(const JsonValue &obj, CoreMatchArm &out, std::size_t depth) {
    if (!obj.is_object()) {
        error(std::string(kWrongType), "a match arm must be an object", node_range(obj));
        return false;
    }
    if (!check_fields(obj, {"pattern", "bindings", "guard_region", "body"}, "match arm")) {
        return false;
    }
    const auto pattern = req_u32(obj, "pattern");
    if (!pattern.has_value()) {
        return false;
    }
    out.pattern = CorePatternId{*pattern};
    const auto *bindings = obj.get("bindings");
    if (bindings == nullptr || !bindings->is_array()) {
        error(std::string(kMissingField), "match arm needs an array 'bindings'", node_range(obj));
        return false;
    }
    for (const auto &binding : bindings->array_items) {
        if (!binding->is_object()) {
            error(std::string(kWrongType), "a match binding must be an object",
                  node_range(*binding));
            return false;
        }
        if (!check_fields(*binding, {"value"}, "match binding")) {
            return false;
        }
        const auto value = req_u32(*binding, "value");
        if (!value.has_value()) {
            return false;
        }
        out.bindings.push_back(CorePatternBinding{CoreValueId{*value}});
    }
    if (const auto *guard = obj.get("guard_region"); guard != nullptr) {
        out.guard_region = std::make_unique<CoreRegion>();
        if (!read_region(*guard, *out.guard_region, depth)) {
            return false;
        }
    }
    // An arm body is mandatory in a well-formed program (the verifier flags a
    // missing one); the writer emits it whenever non-null.
    const auto *body = obj.get("body");
    if (body == nullptr) {
        error(std::string(kMissingField), "match arm is missing 'body'", node_range(obj));
        return false;
    }
    out.body = std::make_unique<CoreRegion>();
    if (!read_region(*body, *out.body, depth)) {
        return false;
    }
    return ok();
}

} // namespace

// ===========================================================================
// Public entry points.
// ===========================================================================

void print_core_ir_json(const CoreProgram &program, std::ostream &out, std::string *error) {
    // Render into a private scratch buffer and publish ONLY on success. A
    // REQUIRED-valid id left kInvalid (§2) is a writer error, and the contract is
    // that the caller receives NO artifact — not a truncated or malformed one. A
    // mid-document failure would otherwise leave a syntactically invalid prefix
    // in the caller's stream for any consumer that does not check `error`.
    std::ostringstream scratch;
    CoreJsonPrinter printer(scratch, error);
    printer.print(program);
    if (printer.failed()) {
        return; // suppress the whole document; `error` (if given) names the field
    }
    out << scratch.str();
}

CoreJsonParseResult parse_core_ir_json(std::string_view json) {
    CoreJsonParseResult result;
    auto parsed = ahfl::json::parse_json(json);
    if (!parsed.has_value() || *parsed == nullptr) {
        result.diagnostics.push_back(CoreJsonDiagnostic{
            std::string("core.json.NOT_JSON"),
            "the document is not valid JSON (truncated, malformed, or a duplicate object key)",
            std::nullopt});
        return result;
    }
    CoreJsonReader reader(**parsed);
    CoreProgram program;
    if (!reader.read(program)) {
        result.diagnostics = reader.take_diagnostics();
        if (result.diagnostics.empty()) {
            result.diagnostics.push_back(CoreJsonDiagnostic{
                std::string("core.json.REJECTED"), "the document was rejected", std::nullopt});
        }
        return result;
    }
    result.program = std::move(program);
    return result;
}

bool core_program_equal(const CoreProgram &a, const CoreProgram &b) noexcept {
    // Component-wise over EVERY table and body, in table order (RFC 0026 P9 §7
    // R2). `CoreValueTypeId` equality is same-owner-arena equality, so the two
    // programs must share arena numbering — which parse(print(p)) guarantees via
    // §6.2's identity remap.
    if (a.format_version != b.format_version) {
        return false;
    }
    if (a.types != b.types || a.value_types != b.value_types ||
        a.capabilities != b.capabilities || a.agents != b.agents || a.flows != b.flows ||
        a.workflows != b.workflows || a.instances != b.instances || a.fns != b.fns) {
        return false;
    }
    return true;
}

} // namespace ahfl::ir::core
