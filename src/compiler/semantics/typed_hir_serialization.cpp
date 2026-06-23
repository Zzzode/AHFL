#include "ahfl/compiler/semantics/typed_hir_serialization.hpp"

#include "ahfl/compiler/semantics/type_context.hpp"

#include "base/json/json_value.hpp"

#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ahfl {

namespace {

using Json = json::JsonValue;

[[nodiscard]] std::unique_ptr<Json> j_int(std::uint64_t value) {
    return Json::make_int(static_cast<std::int64_t>(value));
}

[[nodiscard]] std::unique_ptr<Json> j_enum(auto value) {
    return j_int(static_cast<std::uint64_t>(value));
}

[[nodiscard]] std::unique_ptr<Json> j_source_id(std::optional<SourceId> id) {
    if (!id.has_value()) {
        return Json::make_null();
    }
    return j_int(id->value);
}

[[nodiscard]] std::unique_ptr<Json> j_symbol_id(SymbolId id) {
    return j_int(id.value);
}

[[nodiscard]] std::unique_ptr<Json> j_optional_symbol_id(std::optional<SymbolId> id) {
    if (!id.has_value()) {
        return Json::make_null();
    }
    return j_symbol_id(*id);
}

template <typename E>
[[nodiscard]] std::unique_ptr<Json> j_optional_enum(std::optional<E> value) {
    if (!value.has_value()) {
        return Json::make_null();
    }
    return j_enum(*value);
}

[[nodiscard]] std::unique_ptr<Json> j_range(SourceRange range) {
    auto object = Json::make_object();
    object->set("begin", j_int(range.begin_offset));
    object->set("end", j_int(range.end_offset));
    return object;
}

[[nodiscard]] std::unique_ptr<Json> j_string_array(const std::vector<std::string> &values) {
    auto array = Json::make_array();
    for (const auto &value : values) {
        array->push(Json::make_string(value));
    }
    return array;
}

[[nodiscard]] std::unique_ptr<Json> j_symbol_array(const std::vector<SymbolId> &values) {
    auto array = Json::make_array();
    for (const auto value : values) {
        array->push(j_symbol_id(value));
    }
    return array;
}

[[nodiscard]] std::unique_ptr<Json> j_range_array(const std::vector<SourceRange> &values) {
    auto array = Json::make_array();
    for (const auto value : values) {
        array->push(j_range(value));
    }
    return array;
}

[[nodiscard]] std::unique_ptr<Json> j_index_array(const std::vector<std::uint32_t> &values) {
    auto array = Json::make_array();
    for (const auto value : values) {
        array->push(j_int(value));
    }
    return array;
}

[[nodiscard]] std::unique_ptr<Json> j_type(TypePtr type) {
    if (type == nullptr) {
        return Json::make_null();
    }

    return type->visit(types::Overloads{
        [](const types::AnyT &) {
            auto object = Json::make_object();
            object->set("kind", Json::make_string("Any"));
            return object;
        },
        [](const types::NeverT &) {
            auto object = Json::make_object();
            object->set("kind", Json::make_string("Never"));
            return object;
        },
        [](const types::ErrorT &) {
            auto object = Json::make_object();
            object->set("kind", Json::make_string("Error"));
            return object;
        },
        [](const types::UnitT &) {
            auto object = Json::make_object();
            object->set("kind", Json::make_string("Unit"));
            return object;
        },
        [](const types::BoolT &) {
            auto object = Json::make_object();
            object->set("kind", Json::make_string("Bool"));
            return object;
        },
        [](const types::IntT &) {
            auto object = Json::make_object();
            object->set("kind", Json::make_string("Int"));
            return object;
        },
        [](const types::FloatT &) {
            auto object = Json::make_object();
            object->set("kind", Json::make_string("Float"));
            return object;
        },
        [](const types::StringT &) {
            auto object = Json::make_object();
            object->set("kind", Json::make_string("String"));
            return object;
        },
        [](const types::BoundedStringT &value) {
            auto object = Json::make_object();
            object->set("kind", Json::make_string("BoundedString"));
            object->set("minimum", Json::make_int(value.minimum));
            object->set("maximum", Json::make_int(value.maximum));
            return object;
        },
        [](const types::UUIDT &) {
            auto object = Json::make_object();
            object->set("kind", Json::make_string("UUID"));
            return object;
        },
        [](const types::TimestampT &) {
            auto object = Json::make_object();
            object->set("kind", Json::make_string("Timestamp"));
            return object;
        },
        [](const types::DurationT &) {
            auto object = Json::make_object();
            object->set("kind", Json::make_string("Duration"));
            return object;
        },
        [](const types::DecimalT &value) {
            auto object = Json::make_object();
            object->set("kind", Json::make_string("Decimal"));
            object->set("scale", Json::make_int(value.scale));
            return object;
        },
        [](const types::StructT &value) {
            auto object = Json::make_object();
            object->set("kind", Json::make_string("Struct"));
            object->set("canonical_name", Json::make_string(value.canonical_name));
            object->set("symbol", j_optional_symbol_id(value.symbol));
            auto type_args = Json::make_array();
            for (const auto &type_arg : value.type_args) {
                type_args->push(j_type(type_arg));
            }
            object->set("type_args", std::move(type_args));
            return object;
        },
        [](const types::EnumT &value) {
            auto object = Json::make_object();
            object->set("kind", Json::make_string("Enum"));
            object->set("canonical_name", Json::make_string(value.canonical_name));
            object->set("symbol", j_optional_symbol_id(value.symbol));
            auto type_args = Json::make_array();
            for (const auto &type_arg : value.type_args) {
                type_args->push(j_type(type_arg));
            }
            object->set("type_args", std::move(type_args));
            return object;
        },
        [](const types::EnumVariantT &value) {
            auto object = Json::make_object();
            object->set("kind", Json::make_string("EnumVariant"));
            object->set("canonical_name", Json::make_string(value.canonical_name));
            object->set("symbol", j_optional_symbol_id(value.symbol));
            object->set("variant_name", Json::make_string(value.variant_name));
            auto type_args = Json::make_array();
            for (const auto &type_arg : value.type_args) {
                type_args->push(j_type(type_arg));
            }
            object->set("type_args", std::move(type_args));
            return object;
        },
        [](const types::FnT &value) {
            auto object = Json::make_object();
            object->set("kind", Json::make_string("Fn"));
            auto params_array = Json::make_array();
            for (const auto &param : value.params) {
                params_array->push(j_type(param));
            }
            object->set("params", std::move(params_array));
            object->set("return", j_type(value.return_type));
            object->set("effect", Json::make_string(std::string(to_string(value.effect))));
            return object;
        },
        [](const types::TypeVarT &value) {
            auto object = Json::make_object();
            object->set("kind", Json::make_string("TypeVar"));
            object->set("index", Json::make_int(static_cast<std::int64_t>(value.index)));
            object->set("name", Json::make_string(value.name));
            return object;
        },
    });
}

[[nodiscard]] std::unique_ptr<Json> j_struct_field(const StructFieldInfo &field) {
    auto object = Json::make_object();
    object->set("name", Json::make_string(field.name));
    object->set("type", j_type(field.type));
    object->set("has_default", Json::make_bool(field.has_default));
    object->set("default_value_range", j_range(field.default_value_range));
    object->set("declaration_range", j_range(field.declaration_range));
    return object;
}

[[nodiscard]] std::unique_ptr<Json> j_param(const ParamTypeInfo &param) {
    auto object = Json::make_object();
    object->set("name", Json::make_string(param.name));
    object->set("type", j_type(param.type));
    object->set("declaration_range", j_range(param.declaration_range));
    return object;
}

[[nodiscard]] std::unique_ptr<Json> j_capability_effect(const CapabilityEffectTypeInfo &effect) {
    auto object = Json::make_object();
    object->set("declared", Json::make_bool(effect.declared));
    object->set("effect_kind", Json::make_int(effect.effect_kind));
    object->set("receipt_mode", Json::make_int(effect.receipt_mode));
    object->set("retry_mode", Json::make_int(effect.retry_mode));
    object->set("domain", Json::make_string(effect.domain));
    object->set("idempotency_key", Json::make_string(effect.idempotency_key));
    object->set("timeout", Json::make_string(effect.timeout));
    object->set("compensation", Json::make_string(effect.compensation));
    object->set("policies", j_string_array(effect.policies));
    object->set("source_range", j_range(effect.source_range));
    return object;
}

[[nodiscard]] std::unique_ptr<Json> j_state_policy(const StatePolicyItemInfo &policy) {
    auto object = Json::make_object();
    object->set("kind", j_enum(policy.kind));
    object->set("value", Json::make_string(policy.value));
    object->set("retry_on_targets", j_string_array(policy.retry_on_targets));
    return object;
}

[[nodiscard]] std::unique_ptr<Json> j_payload(const TypedDeclPayload &payload) {
    auto object = Json::make_object();
    if (std::holds_alternative<std::monostate>(payload)) {
        object->set("kind", Json::make_string("None"));
        object->set("value", Json::make_null());
        return object;
    }

    if (const auto *info = std::get_if<ModuleDeclInfo>(&payload)) {
        auto value = Json::make_object();
        value->set("name", Json::make_string(info->name));
        value->set("declaration_range", j_range(info->declaration_range));
        object->set("kind", Json::make_string("Module"));
        object->set("value", std::move(value));
        return object;
    }

    if (const auto *info = std::get_if<ImportDeclInfo>(&payload)) {
        auto value = Json::make_object();
        value->set("target_module", Json::make_string(info->target_module));
        value->set("alias", Json::make_string(info->alias));
        value->set("declaration_range", j_range(info->declaration_range));
        object->set("kind", Json::make_string("Import"));
        object->set("value", std::move(value));
        return object;
    }

    if (const auto *info = std::get_if<ConstDeclInfo>(&payload)) {
        auto value = Json::make_object();
        value->set("canonical_name", Json::make_string(info->canonical_name));
        value->set("local_name", Json::make_string(info->local_name));
        value->set("type", j_type(info->type));
        value->set("type_range", j_range(info->type_range));
        value->set("value_range", j_range(info->value_range));
        value->set("declaration_range", j_range(info->declaration_range));
        object->set("kind", Json::make_string("Const"));
        object->set("value", std::move(value));
        return object;
    }

    if (const auto *info = std::get_if<TypeAliasDeclInfo>(&payload)) {
        auto value = Json::make_object();
        value->set("symbol", j_symbol_id(info->symbol));
        value->set("canonical_name", Json::make_string(info->canonical_name));
        value->set("local_name", Json::make_string(info->local_name));
        value->set("type_param_names", j_string_array(info->type_param_names));
        value->set("aliased_type", j_type(info->aliased_type));
        value->set("aliased_type_range", j_range(info->aliased_type_range));
        value->set("declaration_range", j_range(info->declaration_range));
        object->set("kind", Json::make_string("TypeAlias"));
        object->set("value", std::move(value));
        return object;
    }

    if (const auto *info = std::get_if<StructTypeInfo>(&payload)) {
        auto value = Json::make_object();
        value->set("symbol", j_symbol_id(info->symbol));
        value->set("canonical_name", Json::make_string(info->canonical_name));
        value->set("type_param_names", j_string_array(info->type_param_names));
        auto fields = Json::make_array();
        for (const auto &field : info->fields) {
            fields->push(j_struct_field(field));
        }
        value->set("fields", std::move(fields));
        value->set("declaration_range", j_range(info->declaration_range));
        object->set("kind", Json::make_string("Struct"));
        object->set("value", std::move(value));
        return object;
    }

    if (const auto *info = std::get_if<EnumTypeInfo>(&payload)) {
        auto value = Json::make_object();
        value->set("symbol", j_symbol_id(info->symbol));
        value->set("canonical_name", Json::make_string(info->canonical_name));
        value->set("type_param_names", j_string_array(info->type_param_names));
        auto variants = Json::make_array();
        for (const auto &variant : info->variants) {
            auto variant_json = Json::make_object();
            variant_json->set("name", Json::make_string(variant.name));
            // P1 (ADT): positional payload type list. Empty for legacy
            // payload-less variants; round-trips with the read side below.
            auto payload_json = Json::make_array();
            for (const auto slot : variant.payload) {
                payload_json->push(j_type(slot));
            }
            variant_json->set("payload", std::move(payload_json));
            variant_json->set("declaration_range", j_range(variant.declaration_range));
            variants->push(std::move(variant_json));
        }
        value->set("variants", std::move(variants));
        value->set("declaration_range", j_range(info->declaration_range));
        object->set("kind", Json::make_string("Enum"));
        object->set("value", std::move(value));
        return object;
    }

    if (const auto *info = std::get_if<CapabilityTypeInfo>(&payload)) {
        auto value = Json::make_object();
        value->set("symbol", j_symbol_id(info->symbol));
        value->set("canonical_name", Json::make_string(info->canonical_name));
        auto params = Json::make_array();
        for (const auto &param : info->params) {
            params->push(j_param(param));
        }
        value->set("params", std::move(params));
        value->set("return_type", j_type(info->return_type));
        value->set("declaration_range", j_range(info->declaration_range));
        value->set("effect", j_capability_effect(info->effect));
        object->set("kind", Json::make_string("Capability"));
        object->set("value", std::move(value));
        return object;
    }

    if (const auto *info = std::get_if<PredicateTypeInfo>(&payload)) {
        auto value = Json::make_object();
        value->set("symbol", j_symbol_id(info->symbol));
        value->set("canonical_name", Json::make_string(info->canonical_name));
        auto params = Json::make_array();
        for (const auto &param : info->params) {
            params->push(j_param(param));
        }
        value->set("params", std::move(params));
        value->set("declaration_range", j_range(info->declaration_range));
        object->set("kind", Json::make_string("Predicate"));
        object->set("value", std::move(value));
        return object;
    }

    if (const auto *info = std::get_if<AgentTypeInfo>(&payload)) {
        auto value = Json::make_object();
        value->set("symbol", j_symbol_id(info->symbol));
        value->set("canonical_name", Json::make_string(info->canonical_name));
        value->set("input_type", j_type(info->input_type));
        value->set("context_type", j_type(info->context_type));
        value->set("output_type", j_type(info->output_type));
        value->set("capability_symbols", j_symbol_array(info->capability_symbols));
        value->set("declaration_range", j_range(info->declaration_range));
        value->set("input_type_range", j_range(info->input_type_range));
        value->set("context_type_range", j_range(info->context_type_range));
        value->set("output_type_range", j_range(info->output_type_range));
        value->set("states", j_string_array(info->states));
        value->set("initial_state", Json::make_string(info->initial_state));
        value->set("final_states", j_string_array(info->final_states));
        auto transitions = Json::make_array();
        for (const auto &transition : info->transitions) {
            auto transition_json = Json::make_object();
            transition_json->set("from_state", Json::make_string(transition.from_state));
            transition_json->set("to_state", Json::make_string(transition.to_state));
            transitions->push(std::move(transition_json));
        }
        value->set("transitions", std::move(transitions));
        auto quota = Json::make_array();
        for (const auto &item : info->quota) {
            auto item_json = Json::make_object();
            item_json->set("name", Json::make_string(item.name));
            item_json->set("value", Json::make_string(item.value));
            quota->push(std::move(item_json));
        }
        value->set("quota", std::move(quota));
        object->set("kind", Json::make_string("Agent"));
        object->set("value", std::move(value));
        return object;
    }

    if (const auto *info = std::get_if<WorkflowTypeInfo>(&payload)) {
        auto value = Json::make_object();
        value->set("symbol", j_symbol_id(info->symbol));
        value->set("canonical_name", Json::make_string(info->canonical_name));
        value->set("input_type", j_type(info->input_type));
        value->set("output_type", j_type(info->output_type));
        value->set("declaration_range", j_range(info->declaration_range));
        value->set("input_type_range", j_range(info->input_type_range));
        value->set("output_type_range", j_range(info->output_type_range));
        auto nodes = Json::make_array();
        for (const auto &node : info->nodes) {
            auto node_json = Json::make_object();
            node_json->set("name", Json::make_string(node.name));
            node_json->set("target_name", Json::make_string(node.target_name));
            node_json->set("target_symbol", j_symbol_id(node.target_symbol));
            node_json->set("after", j_string_array(node.after));
            node_json->set("source_range", j_range(node.source_range));
            node_json->set("input_expr_range", j_range(node.input_expr_range));
            node_json->set("target_range", j_range(node.target_range));
            nodes->push(std::move(node_json));
        }
        value->set("nodes", std::move(nodes));
        value->set("safety_ranges", j_range_array(info->safety_ranges));
        value->set("liveness_ranges", j_range_array(info->liveness_ranges));
        value->set("return_value_range", j_range(info->return_value_range));
        object->set("kind", Json::make_string("Workflow"));
        object->set("value", std::move(value));
        return object;
    }

    if (const auto *info = std::get_if<FlowTypeInfo>(&payload)) {
        auto value = Json::make_object();
        value->set("symbol", j_symbol_id(info->symbol));
        value->set("target_name", Json::make_string(info->target_name));
        value->set("target_symbol", j_symbol_id(info->target_symbol));
        value->set("target_range", j_range(info->target_range));
        auto handlers = Json::make_array();
        for (const auto &handler : info->state_handlers) {
            auto handler_json = Json::make_object();
            handler_json->set("state_name", Json::make_string(handler.state_name));
            auto policies = Json::make_array();
            for (const auto &policy : handler.policy) {
                policies->push(j_state_policy(policy));
            }
            handler_json->set("policy", std::move(policies));
            handler_json->set("body_range", j_range(handler.body_range));
            handler_json->set("source_range", j_range(handler.source_range));
            handlers->push(std::move(handler_json));
        }
        value->set("state_handlers", std::move(handlers));
        value->set("declaration_range", j_range(info->declaration_range));
        object->set("kind", Json::make_string("Flow"));
        object->set("value", std::move(value));
        return object;
    }

    if (const auto *info = std::get_if<ContractTypeInfo>(&payload)) {
        auto value = Json::make_object();
        value->set("symbol", j_symbol_id(info->symbol));
        value->set("target_name", Json::make_string(info->target_name));
        value->set("target_symbol", j_symbol_id(info->target_symbol));
        value->set("target_range", j_range(info->target_range));
        auto clauses = Json::make_array();
        for (const auto &clause : info->clauses) {
            auto clause_json = Json::make_object();
            clause_json->set("clause_kind", Json::make_int(clause.clause_kind));
            clause_json->set("is_temporal", Json::make_bool(clause.is_temporal));
            clause_json->set("is_wildcard", Json::make_bool(clause.is_wildcard));
            clause_json->set("expr_range", j_range(clause.expr_range));
            clause_json->set("source_range", j_range(clause.source_range));
            clause_json->set("has_decreases", Json::make_bool(clause.has_decreases));
            clause_json->set("decreases_is_wildcard",
                             Json::make_bool(clause.decreases_is_wildcard));
            clause_json->set("decreases_range", j_range(clause.decreases_range));
            auto decrs = Json::make_array();
            for (const auto &d : clause.decreases_exprs) {
                decrs->push(j_range(d.expr_range));
            }
            clause_json->set("decreases_exprs", std::move(decrs));
            clauses->push(std::move(clause_json));
        }
        value->set("clauses", std::move(clauses));
        value->set("declaration_range", j_range(info->declaration_range));
        object->set("kind", Json::make_string("Contract"));
        object->set("value", std::move(value));
        return object;
    }

    if (const auto *info = std::get_if<FnTypeInfo>(&payload)) {
        // P2c (RFC §3.2.2 / §3.2.3 / §6): top-level fn signature payload.
        // Mirrors the capability signature surface (params, return type) plus
        // the generic type-parameter names and the three-state effect clause.
        auto value = Json::make_object();
        value->set("symbol", j_symbol_id(info->symbol));
        value->set("canonical_name", Json::make_string(info->canonical_name));
        value->set("local_name", Json::make_string(info->local_name));
        auto params = Json::make_array();
        for (const auto &param : info->params) {
            params->push(j_param(param));
        }
        value->set("params", std::move(params));
        value->set("return_type", j_type(info->return_type));
        value->set("return_type_range", j_range(info->return_type_range));
        auto type_params = Json::make_array();
        for (const auto &type_param : info->type_param_names) {
            type_params->push(Json::make_string(type_param));
        }
        value->set("type_param_names", std::move(type_params));
        // Effect clause: kind is the ast::EffectClauseKind int (0=Pure,
        // 1=Nondet, 2=Capability) per FnEffectClauseInfo.
        auto effect = Json::make_object();
        effect->set("kind", Json::make_int(static_cast<std::int64_t>(info->effect.kind)));
        auto capabilities = Json::make_array();
        for (const auto capability : info->effect.capabilities) {
            capabilities->push(j_symbol_id(capability));
        }
        effect->set("capabilities", std::move(capabilities));
        effect->set("source_range", j_range(info->effect.source_range));
        value->set("effect", std::move(effect));
        value->set("has_body", Json::make_bool(info->has_body));
        value->set("declaration_range", j_range(info->declaration_range));
        if (info->builtin_name.has_value()) {
            value->set("builtin_name", Json::make_string(*info->builtin_name));
        } else {
            value->set("builtin_name", Json::make_null());
        }
        value->set("body_block_index", j_int(info->body_block_index));
        object->set("kind", Json::make_string("Fn"));
        object->set("value", std::move(value));
        return object;
    }

    object->set("kind", Json::make_string("None"));
    object->set("value", Json::make_null());
    return object;
}

[[nodiscard]] std::unique_ptr<Json> j_symbol(const Symbol &symbol) {
    auto object = Json::make_object();
    object->set("id", j_symbol_id(symbol.id));
    object->set("namespace", j_enum(symbol.name_space));
    object->set("kind", j_enum(symbol.kind));
    object->set("local_name", Json::make_string(symbol.local_name));
    object->set("canonical_name", Json::make_string(symbol.canonical_name));
    object->set("module_name", Json::make_string(symbol.module_name));
    object->set("source_id", j_source_id(symbol.source_id));
    object->set("declaration_range", j_range(symbol.declaration_range));
    return object;
}

[[nodiscard]] std::unique_ptr<Json> j_reference(const ResolvedReference &reference) {
    auto object = Json::make_object();
    object->set("kind", j_enum(reference.kind));
    object->set("text", Json::make_string(reference.text));
    object->set("source_id", j_source_id(reference.source_id));
    object->set("range", j_range(reference.range));
    object->set("target", j_symbol_id(reference.target));
    return object;
}

// P2c (RFC §3.5): serialize a recorded fn call site consumed by the
// monomorphization pass.
[[nodiscard]] std::unique_ptr<Json> j_fn_call_site(const TypedProgram::FnCallSiteRecord &site) {
    auto object = Json::make_object();
    object->set("fn_symbol", j_symbol_id(site.fn_symbol));
    object->set("call_range", j_range(site.call_range));
    object->set("source_id", j_source_id(site.source_id));
    auto type_args = Json::make_array();
    for (const auto type_arg : site.type_args) {
        type_args->push(j_type(type_arg));
    }
    object->set("type_args", std::move(type_args));
    return object;
}

[[nodiscard]] std::unique_ptr<Json> j_import(const ImportBinding &binding) {
    auto object = Json::make_object();
    object->set("alias", Json::make_string(binding.alias));
    object->set("target_module", Json::make_string(binding.target_module));
    object->set("source_id", j_source_id(binding.source_id));
    object->set("declaration_range", j_range(binding.declaration_range));
    return object;
}

[[nodiscard]] std::unique_ptr<Json> j_const_dependency(const ConstDependencyEdge &edge) {
    auto object = Json::make_object();
    object->set("source", j_symbol_id(edge.source));
    object->set("target", j_symbol_id(edge.target));
    object->set("reference_range", j_range(edge.reference_range));
    object->set("source_id", j_source_id(edge.source_id));
    return object;
}

[[nodiscard]] std::unique_ptr<Json> j_expr_child(const TypedExprChild &child) {
    auto object = Json::make_object();
    object->set("role", j_enum(child.role));
    object->set("name", Json::make_string(child.name));
    object->set("expr_index", j_int(child.expr_index));
    return object;
}

[[nodiscard]] std::unique_ptr<Json> j_const_value(const ConstValue &value) {
    auto object = Json::make_object();
    object->set("kind", j_enum(value.kind));
    object->set("scalar", Json::make_string(value.scalar));
    object->set("symbol", j_optional_symbol_id(value.symbol));
    object->set("child_names", j_string_array(value.child_names));
    auto children = Json::make_array();
    for (const auto &child : value.children) {
        children->push(j_const_value(child));
    }
    object->set("children", std::move(children));
    return object;
}

[[nodiscard]] std::unique_ptr<Json> j_optional_const_value(const std::optional<ConstValue> &value) {
    if (!value.has_value()) {
        return Json::make_null();
    }
    return j_const_value(*value);
}

[[nodiscard]] std::unique_ptr<Json> j_expr(const TypedExpr &expr) {
    auto object = Json::make_object();
    object->set("kind", j_enum(expr.kind));
    object->set("range", j_range(expr.range));
    object->set("source_id", j_source_id(expr.source_id));
    object->set("node_id", j_int(expr.node_id));
    object->set("type", j_type(expr.type));
    object->set("effect", j_enum(expr.effect));
    object->set("is_pure", Json::make_bool(expr.is_pure));
    object->set("resolved_symbol", j_optional_symbol_id(expr.resolved_symbol));
    object->set("semantic_name", Json::make_string(expr.semantic_name));
    object->set("call_target_kind", j_optional_enum(expr.call_target_kind));
    object->set("path_root", Json::make_string(expr.path_root));
    object->set("path_root_kind", j_enum(expr.path_root_kind));
    object->set("member_path", j_string_array(expr.member_path));
    auto children = Json::make_array();
    for (const auto &child : expr.children) {
        children->push(j_expr_child(child));
    }
    object->set("children", std::move(children));
    object->set("bool_value", Json::make_bool(expr.bool_value));
    object->set("unary_op", j_enum(expr.unary_op));
    object->set("binary_op", j_enum(expr.binary_op));
    object->set("literal_spelling", Json::make_string(expr.literal_spelling));
    object->set("member_name", Json::make_string(expr.member_name));
    object->set("lambda_params", j_string_array(expr.lambda_params));
    object->set("const_value", j_optional_const_value(expr.const_value));
    return object;
}

[[nodiscard]] std::unique_ptr<Json> j_decl(const TypedDecl &decl) {
    auto object = Json::make_object();
    object->set("kind", j_enum(decl.kind));
    object->set("symbol", j_symbol_id(decl.symbol));
    object->set("range", j_range(decl.range));
    object->set("source_id", j_source_id(decl.source_id));
    object->set("associated_agent_symbol", j_optional_symbol_id(decl.associated_agent_symbol));
    object->set("type", j_type(decl.type));
    object->set("payload", j_payload(decl.payload));
    return object;
}

[[nodiscard]] std::unique_ptr<Json> j_block(const TypedBlock &block) {
    auto object = Json::make_object();
    object->set("range", j_range(block.range));
    object->set("source_id", j_source_id(block.source_id));
    object->set("statement_indexes", j_index_array(block.statement_indexes));
    return object;
}

[[nodiscard]] std::unique_ptr<Json> j_statement(const TypedStatement &stmt) {
    auto object = Json::make_object();
    object->set("kind", j_enum(stmt.kind));
    object->set("range", j_range(stmt.range));
    object->set("source_id", j_source_id(stmt.source_id));
    object->set("node_id", j_int(stmt.node_id));
    object->set("children_expr_index", j_index_array(stmt.children_expr_index));
    object->set("target_name", Json::make_string(stmt.target_name));
    object->set("goto_target_state", Json::make_string(stmt.goto_target_state));
    object->set("then_block_index", j_int(stmt.then_block_index));
    object->set("else_block_index", j_int(stmt.else_block_index));
    object->set("let_type_ref_strategy", j_enum(stmt.let_type_ref_strategy));
    object->set("let_type", j_type(stmt.let_type));
    object->set("assign_target_root_kind", j_enum(stmt.assign_target_root_kind));
    object->set("assert_message", Json::make_string(stmt.assert_message));
    return object;
}

[[nodiscard]] std::unique_ptr<Json> j_temporal(const TypedTemporalExpr &expr) {
    auto object = Json::make_object();
    object->set("kind", j_enum(expr.kind));
    object->set("range", j_range(expr.range));
    object->set("source_id", j_source_id(expr.source_id));
    object->set("node_id", j_int(expr.node_id));
    object->set("children_index", j_index_array(expr.children_index));
    object->set("op", j_enum(expr.op));
    object->set("payload_spelling", Json::make_string(expr.payload_spelling));
    return object;
}

template <typename T, typename Fn>
[[nodiscard]] std::unique_ptr<Json> j_array(const std::vector<T> &values, Fn fn) {
    auto array = Json::make_array();
    for (const auto &value : values) {
        array->push(fn(value));
    }
    return array;
}

class Reader {
  public:
    [[nodiscard]] bool ok() const noexcept {
        return ok_;
    }

    [[nodiscard]] const Json *field(const Json &object, std::string_view key) {
        if (object.kind != json::Kind::Object) {
            ok_ = false;
            return nullptr;
        }
        const auto *value = object.get(key);
        if (value == nullptr) {
            ok_ = false;
        }
        return value;
    }

    [[nodiscard]] std::string string_field(const Json &object, std::string_view key) {
        const auto *value = field(object, key);
        if (value == nullptr) {
            return {};
        }
        const auto text = value->as_string();
        if (!text.has_value()) {
            ok_ = false;
            return {};
        }
        return std::string{*text};
    }

    [[nodiscard]] bool bool_field(const Json &object, std::string_view key) {
        const auto *value = field(object, key);
        if (value == nullptr) {
            return false;
        }
        const auto result = value->as_bool();
        if (!result.has_value()) {
            ok_ = false;
            return false;
        }
        return *result;
    }

    [[nodiscard]] std::int64_t int_field(const Json &object, std::string_view key) {
        const auto *value = field(object, key);
        if (value == nullptr) {
            return 0;
        }
        const auto result = value->as_int();
        if (!result.has_value()) {
            ok_ = false;
            return 0;
        }
        return *result;
    }

    [[nodiscard]] std::uint64_t uint_field(const Json &object, std::string_view key) {
        return uint_value(field(object, key));
    }

    [[nodiscard]] std::uint32_t u32_field(const Json &object, std::string_view key) {
        const auto value = uint_field(object, key);
        if (value > std::numeric_limits<std::uint32_t>::max()) {
            ok_ = false;
            return 0;
        }
        return static_cast<std::uint32_t>(value);
    }

    [[nodiscard]] std::uint64_t uint_value(const Json *value) {
        if (value == nullptr) {
            ok_ = false;
            return 0;
        }
        const auto result = value->as_int();
        if (!result.has_value() || *result < 0) {
            ok_ = false;
            return 0;
        }
        return static_cast<std::uint64_t>(*result);
    }

    [[nodiscard]] SourceRange range_field(const Json &object, std::string_view key) {
        return range_value(field(object, key));
    }

    [[nodiscard]] SourceRange range_value(const Json *value) {
        if (value == nullptr || value->kind != json::Kind::Object) {
            ok_ = false;
            return {};
        }
        return SourceRange{
            .begin_offset = static_cast<std::size_t>(uint_field(*value, "begin")),
            .end_offset = static_cast<std::size_t>(uint_field(*value, "end")),
        };
    }

    [[nodiscard]] SourceId source_id_value(const Json *value) {
        return SourceId{static_cast<std::size_t>(uint_value(value))};
    }

    [[nodiscard]] std::optional<SourceId> optional_source_id_field(const Json &object,
                                                                   std::string_view key) {
        const auto *value = field(object, key);
        if (value == nullptr || value->is_null()) {
            return std::nullopt;
        }
        return source_id_value(value);
    }

    [[nodiscard]] SymbolId symbol_id_value(const Json *value) {
        return SymbolId{static_cast<std::size_t>(uint_value(value))};
    }

    [[nodiscard]] SymbolId symbol_id_field(const Json &object, std::string_view key) {
        return symbol_id_value(field(object, key));
    }

    [[nodiscard]] std::optional<SymbolId> optional_symbol_id_field(const Json &object,
                                                                   std::string_view key) {
        const auto *value = field(object, key);
        if (value == nullptr || value->is_null()) {
            return std::nullopt;
        }
        return symbol_id_value(value);
    }

    template <typename E>
    [[nodiscard]] std::optional<E> optional_enum_field(const Json &object, std::string_view key) {
        const auto *value = field(object, key);
        if (value == nullptr || value->is_null()) {
            return std::nullopt;
        }
        return static_cast<E>(uint_value(value));
    }

    [[nodiscard]] TypePtr type_field(const Json &object, std::string_view key) {
        return type_value(field(object, key));
    }

    [[nodiscard]] EffectJudgement effect_judgement_field(const Json &object, std::string_view key) {
        const auto effect = string_field(object, key);
        if (effect == "Pure") {
            return EffectJudgement::make_pure();
        }
        if (effect == "Nondet") {
            return EffectJudgement::make_nondet();
        }
        if (effect == "CapabilitySet") {
            return EffectJudgement::make_capability_set({});
        }
        if (effect == "Bottom") {
            return EffectJudgement::make_bottom();
        }
        ok_ = false;
        return EffectJudgement::make_bottom();
    }

    [[nodiscard]] std::vector<TypePtr> type_array_field(const Json &object, std::string_view key) {
        std::vector<TypePtr> values;
        const auto *array = field(object, key);
        if (array == nullptr || array->is_null()) {
            return values;
        }
        if (array->kind != json::Kind::Array) {
            ok_ = false;
            return values;
        }
        values.reserve(array->array_items.size());
        for (const auto &item : array->array_items) {
            values.push_back(type_value(item.get()));
        }
        return values;
    }

    [[nodiscard]] TypePtr type_value(const Json *value) {
        if (value == nullptr) {
            ok_ = false;
            return nullptr;
        }
        if (value->is_null()) {
            return nullptr;
        }
        if (value->kind != json::Kind::Object) {
            ok_ = false;
            return nullptr;
        }

        auto &types = TypeContext::global();
        const auto kind = string_field(*value, "kind");
        if (kind == "Any")
            return types.make(TypeKind::Any);
        if (kind == "Never")
            return types.make(TypeKind::Never);
        if (kind == "Error")
            return types.error_type();
        if (kind == "Unit")
            return types.make(TypeKind::Unit);
        if (kind == "Bool")
            return types.make(TypeKind::Bool);
        if (kind == "Int")
            return types.make(TypeKind::Int);
        if (kind == "Float")
            return types.make(TypeKind::Float);
        if (kind == "String")
            return types.string();
        if (kind == "BoundedString") {
            return types.bounded_string(int_field(*value, "minimum"), int_field(*value, "maximum"));
        }
        if (kind == "UUID")
            return types.make(TypeKind::UUID);
        if (kind == "Timestamp")
            return types.make(TypeKind::Timestamp);
        if (kind == "Duration")
            return types.make(TypeKind::Duration);
        if (kind == "Decimal")
            return types.decimal(int_field(*value, "scale"));
        if (kind == "Struct") {
            return types.struct_type(string_field(*value, "canonical_name"),
                                     optional_symbol_id_field(*value, "symbol"),
                                     type_array_field(*value, "type_args"));
        }
        if (kind == "Enum") {
            return types.enum_type(string_field(*value, "canonical_name"),
                                   optional_symbol_id_field(*value, "symbol"),
                                   type_array_field(*value, "type_args"));
        }
        if (kind == "EnumVariant") {
            return types.enum_variant_type(string_field(*value, "canonical_name"),
                                           string_field(*value, "variant_name"),
                                           optional_symbol_id_field(*value, "symbol"),
                                           type_array_field(*value, "type_args"));
        }
        if (kind == "Fn")
            return types.fn(type_array_field(*value, "params"),
                            type_field(*value, "return"),
                            effect_judgement_field(*value, "effect"));
        if (kind == "TypeVar")
            return types.type_var(u32_field(*value, "index"), string_field(*value, "name"));

        ok_ = false;
        return nullptr;
    }

    [[nodiscard]] std::vector<std::string> string_array_field(const Json &object,
                                                              std::string_view key) {
        std::vector<std::string> values;
        const auto *array = field(object, key);
        if (array == nullptr || array->kind != json::Kind::Array) {
            ok_ = false;
            return values;
        }
        values.reserve(array->array_items.size());
        for (const auto &item : array->array_items) {
            const auto text = item->as_string();
            if (!text.has_value()) {
                ok_ = false;
                continue;
            }
            values.emplace_back(*text);
        }
        return values;
    }

    [[nodiscard]] std::optional<std::string> optional_string_field(const Json &object,
                                                                   std::string_view key) {
        const auto *value = object.get(key);
        if (value == nullptr || value->is_null()) {
            return std::nullopt;
        }
        const auto text = value->as_string();
        if (!text.has_value()) {
            ok_ = false;
            return std::nullopt;
        }
        return std::string{*text};
    }

    [[nodiscard]] std::uint32_t
    optional_u32_field(const Json &object, std::string_view key, std::uint32_t default_value) {
        const auto *value = object.get(key);
        if (value == nullptr || value->is_null()) {
            return default_value;
        }
        const auto integer = uint_value(value);
        if (integer > std::numeric_limits<std::uint32_t>::max()) {
            ok_ = false;
            return default_value;
        }
        return static_cast<std::uint32_t>(integer);
    }

    [[nodiscard]] std::vector<SymbolId> symbol_array_field(const Json &object,
                                                           std::string_view key) {
        std::vector<SymbolId> values;
        const auto *array = field(object, key);
        if (array == nullptr || array->kind != json::Kind::Array) {
            ok_ = false;
            return values;
        }
        values.reserve(array->array_items.size());
        for (const auto &item : array->array_items) {
            values.push_back(symbol_id_value(item.get()));
        }
        return values;
    }

    [[nodiscard]] std::vector<SourceRange> range_array_field(const Json &object,
                                                             std::string_view key) {
        std::vector<SourceRange> values;
        const auto *array = field(object, key);
        if (array == nullptr || array->kind != json::Kind::Array) {
            ok_ = false;
            return values;
        }
        values.reserve(array->array_items.size());
        for (const auto &item : array->array_items) {
            values.push_back(range_value(item.get()));
        }
        return values;
    }

    [[nodiscard]] std::vector<std::uint32_t> index_array_field(const Json &object,
                                                               std::string_view key) {
        std::vector<std::uint32_t> values;
        const auto *array = field(object, key);
        if (array == nullptr || array->kind != json::Kind::Array) {
            ok_ = false;
            return values;
        }
        values.reserve(array->array_items.size());
        for (const auto &item : array->array_items) {
            const auto value = uint_value(item.get());
            if (value > std::numeric_limits<std::uint32_t>::max()) {
                ok_ = false;
                values.push_back(0);
                continue;
            }
            values.push_back(static_cast<std::uint32_t>(value));
        }
        return values;
    }

  private:
    bool ok_{true};
};

[[nodiscard]] StructFieldInfo read_struct_field(Reader &reader, const Json &object) {
    return StructFieldInfo{
        .name = reader.string_field(object, "name"),
        .type = reader.type_field(object, "type"),
        .has_default = reader.bool_field(object, "has_default"),
        .default_value_range = reader.range_field(object, "default_value_range"),
        .declaration_range = reader.range_field(object, "declaration_range"),
    };
}

[[nodiscard]] ParamTypeInfo read_param(Reader &reader, const Json &object) {
    return ParamTypeInfo{
        .name = reader.string_field(object, "name"),
        .type = reader.type_field(object, "type"),
        .declaration_range = reader.range_field(object, "declaration_range"),
    };
}

[[nodiscard]] std::vector<StructFieldInfo>
read_struct_fields(Reader &reader, const Json &object, std::string_view key) {
    std::vector<StructFieldInfo> values;
    const auto *array = reader.field(object, key);
    if (array == nullptr || array->kind != json::Kind::Array) {
        return values;
    }
    values.reserve(array->array_items.size());
    for (const auto &item : array->array_items) {
        values.push_back(read_struct_field(reader, *item));
    }
    return values;
}

[[nodiscard]] std::vector<ParamTypeInfo>
read_params(Reader &reader, const Json &object, std::string_view key) {
    std::vector<ParamTypeInfo> values;
    const auto *array = reader.field(object, key);
    if (array == nullptr || array->kind != json::Kind::Array) {
        return values;
    }
    values.reserve(array->array_items.size());
    for (const auto &item : array->array_items) {
        values.push_back(read_param(reader, *item));
    }
    return values;
}

[[nodiscard]] CapabilityEffectTypeInfo read_capability_effect(Reader &reader, const Json &object) {
    return CapabilityEffectTypeInfo{
        .declared = reader.bool_field(object, "declared"),
        .effect_kind = static_cast<int>(reader.int_field(object, "effect_kind")),
        .receipt_mode = static_cast<int>(reader.int_field(object, "receipt_mode")),
        .retry_mode = static_cast<int>(reader.int_field(object, "retry_mode")),
        .domain = reader.string_field(object, "domain"),
        .idempotency_key = reader.string_field(object, "idempotency_key"),
        .timeout = reader.string_field(object, "timeout"),
        .compensation = reader.string_field(object, "compensation"),
        .policies = reader.string_array_field(object, "policies"),
        .source_range = reader.range_field(object, "source_range"),
    };
}

[[nodiscard]] StatePolicyItemInfo read_state_policy(Reader &reader, const Json &object) {
    return StatePolicyItemInfo{
        .kind = static_cast<StatePolicyKind>(reader.uint_field(object, "kind")),
        .value = reader.string_field(object, "value"),
        .retry_on_targets = reader.string_array_field(object, "retry_on_targets"),
    };
}

[[nodiscard]] std::vector<StatePolicyItemInfo>
read_state_policies(Reader &reader, const Json &object, std::string_view key) {
    std::vector<StatePolicyItemInfo> values;
    const auto *array = reader.field(object, key);
    if (array == nullptr || array->kind != json::Kind::Array) {
        return values;
    }
    values.reserve(array->array_items.size());
    for (const auto &item : array->array_items) {
        values.push_back(read_state_policy(reader, *item));
    }
    return values;
}

[[nodiscard]] TypedDeclPayload read_payload(Reader &reader, const Json &object) {
    const auto kind = reader.string_field(object, "kind");
    const auto *value = reader.field(object, "value");
    if (kind == "None" || value == nullptr || value->is_null()) {
        return std::monostate{};
    }

    if (kind == "Module") {
        return ModuleDeclInfo{
            .name = reader.string_field(*value, "name"),
            .declaration_range = reader.range_field(*value, "declaration_range"),
        };
    }

    if (kind == "Import") {
        return ImportDeclInfo{
            .target_module = reader.string_field(*value, "target_module"),
            .alias = reader.string_field(*value, "alias"),
            .declaration_range = reader.range_field(*value, "declaration_range"),
        };
    }

    if (kind == "Const") {
        return ConstDeclInfo{
            .canonical_name = reader.string_field(*value, "canonical_name"),
            .local_name = reader.string_field(*value, "local_name"),
            .type = reader.type_field(*value, "type"),
            .type_range = reader.range_field(*value, "type_range"),
            .value_range = reader.range_field(*value, "value_range"),
            .declaration_range = reader.range_field(*value, "declaration_range"),
        };
    }

    if (kind == "TypeAlias") {
        return TypeAliasDeclInfo{
            .symbol = reader.symbol_id_field(*value, "symbol"),
            .canonical_name = reader.string_field(*value, "canonical_name"),
            .local_name = reader.string_field(*value, "local_name"),
            .type_param_names = value->get("type_param_names") == nullptr
                                    ? std::vector<std::string>{}
                                    : reader.string_array_field(*value, "type_param_names"),
            .aliased_type = reader.type_field(*value, "aliased_type"),
            .aliased_type_range = reader.range_field(*value, "aliased_type_range"),
            .declaration_range = reader.range_field(*value, "declaration_range"),
        };
    }

    if (kind == "Struct") {
        StructTypeInfo info{
            .symbol = reader.symbol_id_field(*value, "symbol"),
            .canonical_name = reader.string_field(*value, "canonical_name"),
            .type_param_names = value->get("type_param_names") == nullptr
                                    ? std::vector<std::string>{}
                                    : reader.string_array_field(*value, "type_param_names"),
            .fields = read_struct_fields(reader, *value, "fields"),
            .declaration_range = reader.range_field(*value, "declaration_range"),
            .field_index_ = {},
        };
        info.rebuild_field_index();
        return info;
    }

    if (kind == "Enum") {
        EnumTypeInfo info{
            .symbol = reader.symbol_id_field(*value, "symbol"),
            .canonical_name = reader.string_field(*value, "canonical_name"),
            .type_param_names = value->get("type_param_names") == nullptr
                                    ? std::vector<std::string>{}
                                    : reader.string_array_field(*value, "type_param_names"),
            .variants = {},
            .declaration_range = reader.range_field(*value, "declaration_range"),
            .variant_set_ = {},
        };
        const auto *variants = reader.field(*value, "variants");
        if (variants != nullptr && variants->kind == json::Kind::Array) {
            info.variants.reserve(variants->array_items.size());
            for (const auto &item : variants->array_items) {
                EnumVariantInfo variant{
                    .name = reader.string_field(*item, "name"),
                    .declaration_range = reader.range_field(*item, "declaration_range"),
                };
                // P1 (ADT): read optional positional payload list. Absent for
                // legacy serialized enums (treated as no payload).
                if (const auto *payload = reader.field(*item, "payload");
                    payload != nullptr && payload->kind == json::Kind::Array) {
                    variant.payload.reserve(payload->array_items.size());
                    for (const auto &slot : payload->array_items) {
                        variant.payload.push_back(reader.type_value(slot.get()));
                    }
                }
                info.variants.push_back(std::move(variant));
            }
        }
        info.rebuild_variant_index();
        return info;
    }

    if (kind == "Capability") {
        return CapabilityTypeInfo{
            .symbol = reader.symbol_id_field(*value, "symbol"),
            .canonical_name = reader.string_field(*value, "canonical_name"),
            .params = read_params(reader, *value, "params"),
            .return_type = reader.type_field(*value, "return_type"),
            .declaration_range = reader.range_field(*value, "declaration_range"),
            .effect = read_capability_effect(reader, *reader.field(*value, "effect")),
        };
    }

    if (kind == "Predicate") {
        return PredicateTypeInfo{
            .symbol = reader.symbol_id_field(*value, "symbol"),
            .canonical_name = reader.string_field(*value, "canonical_name"),
            .params = read_params(reader, *value, "params"),
            .declaration_range = reader.range_field(*value, "declaration_range"),
        };
    }

    if (kind == "Agent") {
        AgentTypeInfo info{
            .symbol = reader.symbol_id_field(*value, "symbol"),
            .canonical_name = reader.string_field(*value, "canonical_name"),
            .input_type = reader.type_field(*value, "input_type"),
            .context_type = reader.type_field(*value, "context_type"),
            .output_type = reader.type_field(*value, "output_type"),
            .capability_symbols = reader.symbol_array_field(*value, "capability_symbols"),
            .declaration_range = reader.range_field(*value, "declaration_range"),
            .input_type_range = reader.range_field(*value, "input_type_range"),
            .context_type_range = reader.range_field(*value, "context_type_range"),
            .output_type_range = reader.range_field(*value, "output_type_range"),
            .states = reader.string_array_field(*value, "states"),
            .initial_state = reader.string_field(*value, "initial_state"),
            .final_states = reader.string_array_field(*value, "final_states"),
            .transitions = {},
            .quota = {},
        };
        const auto *transitions = reader.field(*value, "transitions");
        if (transitions != nullptr && transitions->kind == json::Kind::Array) {
            info.transitions.reserve(transitions->array_items.size());
            for (const auto &item : transitions->array_items) {
                info.transitions.push_back(AgentTransitionInfo{
                    .from_state = reader.string_field(*item, "from_state"),
                    .to_state = reader.string_field(*item, "to_state"),
                });
            }
        }
        const auto *quota = reader.field(*value, "quota");
        if (quota != nullptr && quota->kind == json::Kind::Array) {
            info.quota.reserve(quota->array_items.size());
            for (const auto &item : quota->array_items) {
                info.quota.push_back(AgentQuotaInfo{
                    .name = reader.string_field(*item, "name"),
                    .value = reader.string_field(*item, "value"),
                });
            }
        }
        return info;
    }

    if (kind == "Workflow") {
        WorkflowTypeInfo info{
            .symbol = reader.symbol_id_field(*value, "symbol"),
            .canonical_name = reader.string_field(*value, "canonical_name"),
            .input_type = reader.type_field(*value, "input_type"),
            .output_type = reader.type_field(*value, "output_type"),
            .declaration_range = reader.range_field(*value, "declaration_range"),
            .input_type_range = reader.range_field(*value, "input_type_range"),
            .output_type_range = reader.range_field(*value, "output_type_range"),
            .nodes = {},
            .safety_ranges = {},
            .liveness_ranges = {},
            .return_value_range = {},
        };
        const auto *nodes = reader.field(*value, "nodes");
        if (nodes != nullptr && nodes->kind == json::Kind::Array) {
            info.nodes.reserve(nodes->array_items.size());
            for (const auto &item : nodes->array_items) {
                info.nodes.push_back(WorkflowNodeInfo{
                    .name = reader.string_field(*item, "name"),
                    .target_name = reader.string_field(*item, "target_name"),
                    .target_symbol = reader.symbol_id_field(*item, "target_symbol"),
                    .after = reader.string_array_field(*item, "after"),
                    .source_range = reader.range_field(*item, "source_range"),
                    .input_expr_range = reader.range_field(*item, "input_expr_range"),
                    .target_range = reader.range_field(*item, "target_range"),
                });
            }
        }
        info.safety_ranges = reader.range_array_field(*value, "safety_ranges");
        info.liveness_ranges = reader.range_array_field(*value, "liveness_ranges");
        info.return_value_range = reader.range_field(*value, "return_value_range");
        return info;
    }

    if (kind == "Flow") {
        FlowTypeInfo info{
            .symbol = reader.symbol_id_field(*value, "symbol"),
            .target_name = reader.string_field(*value, "target_name"),
            .target_symbol = reader.symbol_id_field(*value, "target_symbol"),
            .target_range = reader.range_field(*value, "target_range"),
            .state_handlers = {},
            .declaration_range = reader.range_field(*value, "declaration_range"),
        };
        const auto *handlers = reader.field(*value, "state_handlers");
        if (handlers != nullptr && handlers->kind == json::Kind::Array) {
            info.state_handlers.reserve(handlers->array_items.size());
            for (const auto &item : handlers->array_items) {
                info.state_handlers.push_back(FlowStateHandlerInfo{
                    .state_name = reader.string_field(*item, "state_name"),
                    .policy = read_state_policies(reader, *item, "policy"),
                    .body_range = reader.range_field(*item, "body_range"),
                    .source_range = reader.range_field(*item, "source_range"),
                });
            }
        }
        return info;
    }

    if (kind == "Contract") {
        ContractTypeInfo info{
            .symbol = reader.symbol_id_field(*value, "symbol"),
            .target_name = reader.string_field(*value, "target_name"),
            .target_symbol = reader.symbol_id_field(*value, "target_symbol"),
            .target_range = reader.range_field(*value, "target_range"),
            .clauses = {},
            .declaration_range = reader.range_field(*value, "declaration_range"),
        };
        const auto *clauses = reader.field(*value, "clauses");
        if (clauses != nullptr && clauses->kind == json::Kind::Array) {
            info.clauses.reserve(clauses->array_items.size());
            for (const auto &item : clauses->array_items) {
                ContractClauseInfo clause_info{
                    .clause_kind = static_cast<int>(reader.int_field(*item, "clause_kind")),
                    .is_temporal = reader.bool_field(*item, "is_temporal"),
                    .is_wildcard = reader.bool_field(*item, "is_wildcard"),
                    .expr_range = reader.range_field(*item, "expr_range"),
                    .source_range = reader.range_field(*item, "source_range"),
                    // P4.S3: decreases metadata; defaults to the zero-value sentinel
                    // (false / empty / false) when restoring snapshots produced before
                    // this plumbing was introduced.
                    .has_decreases = reader.bool_field(*item, "has_decreases"),
                    .decreases_exprs = {},
                    .decreases_is_wildcard = reader.bool_field(*item, "decreases_is_wildcard"),
                    .decreases_range = reader.range_field(*item, "decreases_range"),
                };
                if (const auto *decr_arr = reader.field(*item, "decreases_exprs");
                    decr_arr != nullptr && decr_arr->kind == json::Kind::Array) {
                    clause_info.decreases_exprs.reserve(decr_arr->array_items.size());
                    for (const auto &decr_item : decr_arr->array_items) {
                        clause_info.decreases_exprs.push_back(DecreasesExprInfo{
                            .expr_range = reader.range_value(decr_item.get()),
                        });
                    }
                }
                info.clauses.push_back(std::move(clause_info));
            }
        }
        return info;
    }

    if (kind == "Fn") {
        // P2c (RFC §3.2.2): round-trip a fn signature payload.
        FnTypeInfo info{
            .symbol = reader.symbol_id_field(*value, "symbol"),
            .canonical_name = reader.string_field(*value, "canonical_name"),
            .local_name = reader.string_field(*value, "local_name"),
            .params = {},
            .return_type = reader.type_field(*value, "return_type"),
            .return_type_range = reader.range_field(*value, "return_type_range"),
            .type_param_names = {},
            .effect =
                FnEffectClauseInfo{
                    .kind =
                        static_cast<int>(reader.int_field(*reader.field(*value, "effect"), "kind")),
                    .capabilities = {},
                    .source_range =
                        reader.range_field(*reader.field(*value, "effect"), "source_range"),
                },
            .has_body = reader.bool_field(*value, "has_body"),
            .declaration_range = reader.range_field(*value, "declaration_range"),
            .builtin_name = reader.optional_string_field(*value, "builtin_name"),
            .body_block_index = reader.optional_u32_field(*value, "body_block_index", UINT32_MAX),
        };
        if (const auto *params = reader.field(*value, "params");
            params != nullptr && params->kind == json::Kind::Array) {
            info.params.reserve(params->array_items.size());
            for (const auto &item : params->array_items) {
                info.params.push_back(ParamTypeInfo{
                    .name = reader.string_field(*item, "name"),
                    .type = reader.type_field(*item, "type"),
                    .declaration_range = reader.range_field(*item, "declaration_range"),
                });
            }
        }
        if (const auto *type_params = reader.field(*value, "type_param_names");
            type_params != nullptr) {
            info.type_param_names = reader.string_array_field(*value, "type_param_names");
        }
        if (const auto *effect = reader.field(*value, "effect"); effect != nullptr) {
            if (const auto *capabilities = reader.field(*effect, "capabilities");
                capabilities != nullptr && capabilities->kind == json::Kind::Array) {
                info.effect.capabilities.reserve(capabilities->array_items.size());
                for (const auto &item : capabilities->array_items) {
                    info.effect.capabilities.push_back(reader.symbol_id_value(item.get()));
                }
            }
        }
        return info;
    }

    return std::monostate{};
}

[[nodiscard]] Symbol read_symbol(Reader &reader, const Json &object) {
    return Symbol{
        .id = reader.symbol_id_field(object, "id"),
        .name_space = static_cast<SymbolNamespace>(reader.uint_field(object, "namespace")),
        .kind = static_cast<SymbolKind>(reader.uint_field(object, "kind")),
        .local_name = reader.string_field(object, "local_name"),
        .canonical_name = reader.string_field(object, "canonical_name"),
        .module_name = reader.string_field(object, "module_name"),
        .source_id = reader.optional_source_id_field(object, "source_id"),
        .declaration_range = reader.range_field(object, "declaration_range"),
    };
}

[[nodiscard]] ResolvedReference read_reference(Reader &reader, const Json &object) {
    return ResolvedReference{
        .kind = static_cast<ReferenceKind>(reader.uint_field(object, "kind")),
        .text = reader.string_field(object, "text"),
        .source_id = reader.optional_source_id_field(object, "source_id"),
        .range = reader.range_field(object, "range"),
        .target = reader.symbol_id_field(object, "target"),
    };
}

[[nodiscard]] ImportBinding read_import(Reader &reader, const Json &object) {
    return ImportBinding{
        .alias = reader.string_field(object, "alias"),
        .target_module = reader.string_field(object, "target_module"),
        .source_id = reader.optional_source_id_field(object, "source_id"),
        .declaration_range = reader.range_field(object, "declaration_range"),
    };
}

[[nodiscard]] ConstDependencyEdge read_const_dependency(Reader &reader, const Json &object) {
    return ConstDependencyEdge{
        .source = reader.symbol_id_field(object, "source"),
        .target = reader.symbol_id_field(object, "target"),
        .reference_range = reader.range_field(object, "reference_range"),
        .source_id = reader.optional_source_id_field(object, "source_id"),
    };
}

[[nodiscard]] TypedProgram::FnCallSiteRecord read_fn_call_site(Reader &reader, const Json &object) {
    return TypedProgram::FnCallSiteRecord{
        .fn_symbol = reader.symbol_id_field(object, "fn_symbol"),
        .call_range = reader.range_field(object, "call_range"),
        .source_id = reader.optional_source_id_field(object, "source_id"),
        .type_args = reader.type_array_field(object, "type_args"),
    };
}

[[nodiscard]] TypedExprChild read_expr_child(Reader &reader, const Json &object) {
    return TypedExprChild{
        .role = static_cast<TypedExprChildRole>(reader.uint_field(object, "role")),
        .name = reader.string_field(object, "name"),
        .expr_index = reader.u32_field(object, "expr_index"),
    };
}

[[nodiscard]] ConstValue read_const_value(Reader &reader, const Json &object) {
    ConstValue value{
        .kind = static_cast<ConstValueKind>(reader.uint_field(object, "kind")),
        .scalar = reader.string_field(object, "scalar"),
        .symbol = reader.optional_symbol_id_field(object, "symbol"),
        .child_names = reader.string_array_field(object, "child_names"),
        .children = {},
    };

    const auto *children = reader.field(object, "children");
    if (children != nullptr && children->kind == json::Kind::Array) {
        value.children.reserve(children->array_items.size());
        for (const auto &item : children->array_items) {
            value.children.push_back(read_const_value(reader, *item));
        }
    }
    return value;
}

[[nodiscard]] std::optional<ConstValue> read_optional_const_value(Reader &reader,
                                                                  const Json &object) {
    const auto *value = object.get("const_value");
    if (value == nullptr || value->is_null()) {
        return std::nullopt;
    }
    if (value->kind != json::Kind::Object) {
        return std::nullopt;
    }
    return read_const_value(reader, *value);
}

[[nodiscard]] TypedExpr read_expr(Reader &reader, const Json &object) {
    TypedExpr expr{
        .kind = static_cast<ast::ExprSyntaxKind>(reader.uint_field(object, "kind")),
        .range = reader.range_field(object, "range"),
        .source_id = reader.optional_source_id_field(object, "source_id"),
        .node_id = reader.uint_field(object, "node_id"),
        .type = reader.type_field(object, "type"),
        .effect = static_cast<ExprEffect>(reader.uint_field(object, "effect")),
        .is_pure = reader.bool_field(object, "is_pure"),
        .resolved_symbol = reader.optional_symbol_id_field(object, "resolved_symbol"),
        .semantic_name = reader.string_field(object, "semantic_name"),
        .call_target_kind =
            reader.template optional_enum_field<TypedCallTargetKind>(object, "call_target_kind"),
        .path_root = reader.string_field(object, "path_root"),
        .path_root_kind =
            static_cast<AssignTargetRootKind>(reader.uint_field(object, "path_root_kind")),
        .member_path = reader.string_array_field(object, "member_path"),
        .children = {},
        .bool_value = reader.bool_field(object, "bool_value"),
        .unary_op = static_cast<ast::ExprUnaryOp>(reader.uint_field(object, "unary_op")),
        .binary_op = static_cast<ast::ExprBinaryOp>(reader.uint_field(object, "binary_op")),
        .literal_spelling = reader.string_field(object, "literal_spelling"),
        .member_name = reader.string_field(object, "member_name"),
        .lambda_params = reader.string_array_field(object, "lambda_params"),
        .const_value = read_optional_const_value(reader, object),
    };

    const auto *children = reader.field(object, "children");
    if (children != nullptr && children->kind == json::Kind::Array) {
        expr.children.reserve(children->array_items.size());
        for (const auto &item : children->array_items) {
            expr.children.push_back(read_expr_child(reader, *item));
        }
    }
    return expr;
}

[[nodiscard]] TypedDecl read_decl(Reader &reader, const Json &object) {
    return TypedDecl{
        .kind = static_cast<ast::NodeKind>(reader.uint_field(object, "kind")),
        .symbol = reader.symbol_id_field(object, "symbol"),
        .range = reader.range_field(object, "range"),
        .source_id = reader.optional_source_id_field(object, "source_id"),
        .associated_agent_symbol =
            reader.optional_symbol_id_field(object, "associated_agent_symbol"),
        .type = reader.type_field(object, "type"),
        .payload = read_payload(reader, *reader.field(object, "payload")),
    };
}

[[nodiscard]] TypedBlock read_block(Reader &reader, const Json &object) {
    return TypedBlock{
        .range = reader.range_field(object, "range"),
        .source_id = reader.optional_source_id_field(object, "source_id"),
        .statement_indexes = reader.index_array_field(object, "statement_indexes"),
    };
}

[[nodiscard]] TypedStatement read_statement(Reader &reader, const Json &object) {
    return TypedStatement{
        .kind = static_cast<TypedStmtKind>(reader.uint_field(object, "kind")),
        .range = reader.range_field(object, "range"),
        .source_id = reader.optional_source_id_field(object, "source_id"),
        .node_id = reader.uint_field(object, "node_id"),
        .children_expr_index = reader.index_array_field(object, "children_expr_index"),
        .target_name = reader.string_field(object, "target_name"),
        .goto_target_state = reader.string_field(object, "goto_target_state"),
        .then_block_index = reader.u32_field(object, "then_block_index"),
        .else_block_index = reader.u32_field(object, "else_block_index"),
        .let_type_ref_strategy =
            static_cast<LetTypeRefStrategy>(reader.uint_field(object, "let_type_ref_strategy")),
        .let_type = reader.type_field(object, "let_type"),
        .assign_target_root_kind =
            static_cast<AssignTargetRootKind>(reader.uint_field(object, "assign_target_root_kind")),
        .assert_message = reader.string_field(object, "assert_message"),
    };
}

[[nodiscard]] TypedTemporalExpr read_temporal(Reader &reader, const Json &object) {
    return TypedTemporalExpr{
        .kind = static_cast<TypedTemporalKind>(reader.uint_field(object, "kind")),
        .range = reader.range_field(object, "range"),
        .source_id = reader.optional_source_id_field(object, "source_id"),
        .node_id = reader.uint_field(object, "node_id"),
        .children_index = reader.index_array_field(object, "children_index"),
        .op = static_cast<TypedTemporalOp>(reader.uint_field(object, "op")),
        .payload_spelling = reader.string_field(object, "payload_spelling"),
    };
}

template <typename T, typename Fn>
[[nodiscard]] std::vector<T>
read_array(Reader &reader, const Json &object, std::string_view key, Fn fn) {
    std::vector<T> values;
    const auto *array = reader.field(object, key);
    if (array == nullptr || array->kind != json::Kind::Array) {
        return values;
    }
    values.reserve(array->array_items.size());
    for (const auto &item : array->array_items) {
        values.push_back(fn(reader, *item));
    }
    return values;
}

} // namespace

std::string serialize_typed_program_json(const TypedProgram &program) {
    auto root = Json::make_object();
    root->set("format", Json::make_string("AHFL_TYPED_HIR_V1"));
    root->set("const_value_artifact_schema",
              Json::make_string(std::string(kConstValueArtifactSchemaVersion)));
    root->set("symbols", j_array(program.symbols, j_symbol));
    root->set("references", j_array(program.references, j_reference));
    root->set("imports", j_array(program.imports, j_import));
    root->set("const_dependencies", j_array(program.const_dependencies, j_const_dependency));
    root->set("declarations", j_array(program.declarations, j_decl));
    root->set("expressions", j_array(program.expressions, j_expr));
    root->set("blocks", j_array(program.blocks, j_block));
    root->set("statements", j_array(program.statements, j_statement));
    root->set("temporal_exprs", j_array(program.temporal_exprs, j_temporal));
    // P2c (RFC §3.5): recorded fn call sites for the monomorphization pass.
    root->set("fn_call_sites", j_array(program.fn_call_sites, j_fn_call_site));
    return json::serialize_json(*root);
}

namespace {

[[nodiscard]] std::unique_ptr<Json> j_cache_metadata(const TypedProgramCacheMetadata &metadata) {
    auto object = Json::make_object();
    object->set("schema_version", Json::make_string(metadata.schema_version));
    object->set("source_graph_revision", Json::make_string(metadata.source_graph_revision));
    object->set("source_content_hash", Json::make_string(metadata.source_content_hash));
    object->set("resolver_snapshot_version", Json::make_string(metadata.resolver_snapshot_version));
    return object;
}

[[nodiscard]] std::optional<TypedProgramCacheMetadata> read_cache_metadata(Reader &reader,
                                                                           const Json &root) {
    const auto *metadata_json = reader.field(root, "metadata");
    if (metadata_json == nullptr || metadata_json->kind != json::Kind::Object) {
        return std::nullopt;
    }

    TypedProgramCacheMetadata metadata{
        .schema_version = reader.string_field(*metadata_json, "schema_version"),
        .source_graph_revision = reader.string_field(*metadata_json, "source_graph_revision"),
        .source_content_hash = reader.string_field(*metadata_json, "source_content_hash"),
        .resolver_snapshot_version =
            reader.string_field(*metadata_json, "resolver_snapshot_version"),
    };
    if (!reader.ok()) {
        return std::nullopt;
    }
    return metadata;
}

[[nodiscard]] TypedProgramCacheLoadStatus
cache_metadata_status(const TypedProgramCacheMetadata &actual,
                      const TypedProgramCacheMetadata &expected) {
    if (actual.schema_version != expected.schema_version ||
        actual.schema_version != kTypedProgramCacheSchemaVersion) {
        return TypedProgramCacheLoadStatus::SchemaMismatch;
    }
    if (actual.source_graph_revision != expected.source_graph_revision) {
        return TypedProgramCacheLoadStatus::SourceRevisionMismatch;
    }
    if (actual.source_content_hash != expected.source_content_hash) {
        return TypedProgramCacheLoadStatus::SourceContentHashMismatch;
    }
    if (actual.resolver_snapshot_version != expected.resolver_snapshot_version) {
        return TypedProgramCacheLoadStatus::ResolverSnapshotMismatch;
    }
    return TypedProgramCacheLoadStatus::Hit;
}

} // namespace

std::optional<TypedProgram> deserialize_typed_program_json(std::string_view json_text) {
    auto parsed = json::parse_json(json_text);
    if (!parsed.has_value() || *parsed == nullptr) {
        return std::nullopt;
    }

    Reader reader;
    const auto &root = **parsed;
    if (root.kind != json::Kind::Object) {
        return std::nullopt;
    }
    if (reader.string_field(root, "format") != "AHFL_TYPED_HIR_V1") {
        return std::nullopt;
    }
    if (reader.string_field(root, "const_value_artifact_schema") !=
        kConstValueArtifactSchemaVersion) {
        return std::nullopt;
    }

    TypedProgram program;
    program.symbols = read_array<Symbol>(reader, root, "symbols", read_symbol);
    program.references = read_array<ResolvedReference>(reader, root, "references", read_reference);
    program.imports = read_array<ImportBinding>(reader, root, "imports", read_import);
    program.const_dependencies =
        read_array<ConstDependencyEdge>(reader, root, "const_dependencies", read_const_dependency);
    program.declarations = read_array<TypedDecl>(reader, root, "declarations", read_decl);
    program.expressions = read_array<TypedExpr>(reader, root, "expressions", read_expr);
    program.blocks = read_array<TypedBlock>(reader, root, "blocks", read_block);
    program.statements = read_array<TypedStatement>(reader, root, "statements", read_statement);
    program.temporal_exprs =
        read_array<TypedTemporalExpr>(reader, root, "temporal_exprs", read_temporal);
    if (root.get("fn_call_sites") != nullptr) {
        program.fn_call_sites = read_array<TypedProgram::FnCallSiteRecord>(
            reader, root, "fn_call_sites", read_fn_call_site);
    }

    if (!reader.ok()) {
        return std::nullopt;
    }

    program.rebuild_indices();
    return program;
}

std::string serialize_typed_program_cache_json(const TypedProgram &program,
                                               const TypedProgramCacheMetadata &metadata) {
    auto typed_program_json = json::parse_json(serialize_typed_program_json(program));
    auto root = Json::make_object();
    root->set("format", Json::make_string(std::string(kTypedProgramCacheSchemaVersion)));
    root->set("metadata", j_cache_metadata(metadata));
    root->set("typed_program",
              typed_program_json.has_value() ? std::move(*typed_program_json) : Json::make_null());
    return json::serialize_json(*root);
}

TypedProgramCacheLoadResult
load_typed_program_cache_json(std::string_view json_text,
                              const TypedProgramCacheMetadata &expected_metadata) {
    auto parsed = json::parse_json(json_text);
    if (!parsed.has_value() || *parsed == nullptr || (*parsed)->kind != json::Kind::Object) {
        return {.status = TypedProgramCacheLoadStatus::InvalidPayload, .program = std::nullopt};
    }

    Reader reader;
    const auto &root = **parsed;
    if (reader.string_field(root, "format") != kTypedProgramCacheSchemaVersion) {
        return {.status = TypedProgramCacheLoadStatus::SchemaMismatch, .program = std::nullopt};
    }

    auto metadata = read_cache_metadata(reader, root);
    if (!metadata.has_value()) {
        return {.status = TypedProgramCacheLoadStatus::InvalidPayload, .program = std::nullopt};
    }

    const auto metadata_status = cache_metadata_status(*metadata, expected_metadata);
    if (metadata_status != TypedProgramCacheLoadStatus::Hit) {
        return {.status = metadata_status, .program = std::nullopt};
    }

    const auto *typed_program_json = reader.field(root, "typed_program");
    if (typed_program_json == nullptr || !reader.ok()) {
        return {.status = TypedProgramCacheLoadStatus::InvalidPayload, .program = std::nullopt};
    }

    auto program = deserialize_typed_program_json(json::serialize_json(*typed_program_json));
    if (!program.has_value()) {
        return {.status = TypedProgramCacheLoadStatus::InvalidPayload, .program = std::nullopt};
    }

    return {.status = TypedProgramCacheLoadStatus::Hit, .program = std::move(program)};
}

} // namespace ahfl
