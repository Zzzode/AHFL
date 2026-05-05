// prompt_builder.cpp - LLM Prompt 构建

#include "ahfl/llm_provider/prompt_builder.hpp"

#include <sstream>
#include <variant>

namespace ahfl::llm_provider {

PromptBuilder::PromptBuilder(const ir::Program &program) : program_(program) {}

const ir::CapabilityDecl *PromptBuilder::find_capability(const std::string &name) const {
    for (const auto &decl : program_.declarations) {
        if (auto *cap = std::get_if<ir::CapabilityDecl>(&decl)) {
            if (cap->name == name) {
                return cap;
            }
        }
    }
    return nullptr;
}

const ir::StructDecl *PromptBuilder::find_struct(const std::string &name) const {
    for (const auto &decl : program_.declarations) {
        if (auto *s = std::get_if<ir::StructDecl>(&decl)) {
            if (s->name == name) {
                return s;
            }
        }
    }
    return nullptr;
}

const ir::EnumDecl *PromptBuilder::find_enum(const std::string &name) const {
    for (const auto &decl : program_.declarations) {
        if (auto *e = std::get_if<ir::EnumDecl>(&decl)) {
            if (e->name == name) {
                return e;
            }
        }
    }
    return nullptr;
}

std::string PromptBuilder::describe_type_schema(const std::string &type_name) const {
    // 基本类型
    if (type_name == "String") {
        return "string";
    }
    if (type_name == "Int") {
        return "integer";
    }
    if (type_name == "Float") {
        return "number";
    }
    if (type_name == "Bool") {
        return "boolean";
    }

    // 枚举类型
    if (const auto *enum_decl = find_enum(type_name)) {
        std::ostringstream oss;
        oss << "enum, one of: [";
        for (std::size_t i = 0; i < enum_decl->variants.size(); ++i) {
            if (i > 0) {
                oss << ", ";
            }
            oss << "\"" << enum_decl->variants[i] << "\"";
        }
        oss << "]";
        return oss.str();
    }

    // 结构体类型
    if (const auto *struct_decl = find_struct(type_name)) {
        std::ostringstream oss;
        oss << "object with fields: { ";
        for (std::size_t i = 0; i < struct_decl->fields.size(); ++i) {
            if (i > 0) {
                oss << ", ";
            }
            const auto &field = struct_decl->fields[i];
            oss << "\"" << field.name << "\": " << describe_type_schema(field.type);
        }
        oss << " }";
        return oss.str();
    }

    return type_name;
}

std::string PromptBuilder::value_to_string(const evaluator::Value &val) const {
    std::ostringstream oss;
    std::visit(
        [&](const auto &v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, evaluator::NoneValue>) {
                oss << "none";
            } else if constexpr (std::is_same_v<T, evaluator::BoolValue>) {
                oss << (v.value ? "true" : "false");
            } else if constexpr (std::is_same_v<T, evaluator::IntValue>) {
                oss << v.value;
            } else if constexpr (std::is_same_v<T, evaluator::FloatValue>) {
                oss << v.value;
            } else if constexpr (std::is_same_v<T, evaluator::StringValue>) {
                oss << "\"" << v.value << "\"";
            } else if constexpr (std::is_same_v<T, evaluator::EnumValue>) {
                oss << v.enum_name << "." << v.variant;
            } else if constexpr (std::is_same_v<T, evaluator::StructValue>) {
                oss << v.type_name << " { ";
                bool first = true;
                for (const auto &[key, field_val] : v.fields) {
                    if (!first) {
                        oss << ", ";
                    }
                    first = false;
                    oss << key << ": ";
                    if (field_val) {
                        oss << value_to_string(*field_val);
                    }
                }
                oss << " }";
            } else if constexpr (std::is_same_v<T, evaluator::ListValue>) {
                oss << "[";
                for (std::size_t i = 0; i < v.items.size(); ++i) {
                    if (i > 0) {
                        oss << ", ";
                    }
                    if (v.items[i]) {
                        oss << value_to_string(*v.items[i]);
                    }
                }
                oss << "]";
            } else if constexpr (std::is_same_v<T, evaluator::DecimalValue>) {
                oss << v.spelling;
            } else if constexpr (std::is_same_v<T, evaluator::DurationValue>) {
                oss << v.spelling;
            } else if constexpr (std::is_same_v<T, evaluator::OptionalValue>) {
                if (v.inner) {
                    oss << "some(" << value_to_string(*v.inner) << ")";
                } else {
                    oss << "none";
                }
            }
        },
        val.node);
    return oss.str();
}

std::string PromptBuilder::build_system_prompt(const std::string &capability_name) const {
    const auto *cap = find_capability(capability_name);
    if (cap == nullptr) {
        return "You are a helpful assistant. Respond in JSON format.";
    }

    std::ostringstream oss;
    oss << "You are an AI assistant implementing the capability \"" << capability_name << "\".\n";
    oss << "You MUST respond with a valid JSON object.\n\n";

    // 参数描述
    if (!cap->params.empty()) {
        oss << "Input parameters:\n";
        for (const auto &param : cap->params) {
            oss << "  - " << param.name << ": " << describe_type_schema(param.type) << "\n";
        }
        oss << "\n";
    }

    // 返回类型描述
    oss << "Required output format (JSON):\n";
    oss << "  " << describe_type_schema(cap->return_type) << "\n\n";
    oss << "Respond ONLY with the JSON object, no extra text.";

    return oss.str();
}

std::string PromptBuilder::build_user_prompt(const std::string &capability_name,
                                              const std::vector<evaluator::Value> &args) const {
    const auto *cap = find_capability(capability_name);
    if (cap == nullptr) {
        return "No input provided.";
    }

    std::ostringstream oss;
    oss << "Execute capability \"" << capability_name << "\" with:\n";
    for (std::size_t i = 0; i < args.size() && i < cap->params.size(); ++i) {
        oss << "  " << cap->params[i].name << " = " << value_to_string(args[i]) << "\n";
    }
    return oss.str();
}

} // namespace ahfl::llm_provider
