#pragma once

#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/ir/ir.hpp"
#include "ahfl/compiler/ir/lowering.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"
#include "ahfl/compiler/semantics/validate.hpp"
#include "runtime/evaluator/value.hpp"

#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace ahfl::tests::runtime_e2e {

struct TestStats {
    int test_count{0};
    int pass_count{0};

    void check(bool condition, std::string_view test_name) {
        ++test_count;
        if (condition) {
            ++pass_count;
            return;
        }
        std::cerr << "FAIL: " << test_name << "\n";
    }

    [[nodiscard]] bool passed() const noexcept {
        return pass_count == test_count;
    }
};

[[nodiscard]] inline std::optional<ir::Program>
compile_ahfl_file(const std::filesystem::path &file_path) {
    const Frontend frontend;
    const auto parse_result = frontend.parse_file(file_path);
    if (parse_result.has_errors() || !parse_result.program) {
        parse_result.diagnostics.render(std::cerr);
        return std::nullopt;
    }

    const Resolver resolver;
    const auto resolve_result = resolver.resolve(*parse_result.program);
    if (resolve_result.has_errors()) {
        resolve_result.diagnostics.render(std::cerr);
        return std::nullopt;
    }

    const TypeChecker type_checker;
    const auto type_check_result = type_checker.check(*parse_result.program, resolve_result);
    if (type_check_result.has_errors()) {
        type_check_result.diagnostics.render(std::cerr);
        return std::nullopt;
    }

    const Validator validator;
    const auto validation_result =
        validator.validate(*parse_result.program, resolve_result, type_check_result);
    if (validation_result.has_errors()) {
        validation_result.diagnostics.render(std::cerr);
        return std::nullopt;
    }

    return lower_program_ir(*parse_result.program, resolve_result, type_check_result);
}

[[nodiscard]] inline std::unordered_map<std::string, evaluator::Value> build_fields() {
    return {};
}

template <typename... Rest>
[[nodiscard]] std::unordered_map<std::string, evaluator::Value>
build_fields(std::string key, evaluator::Value value, Rest &&...rest) {
    auto fields = build_fields(std::forward<Rest>(rest)...);
    fields.emplace(std::move(key), std::move(value));
    return fields;
}

[[nodiscard]] inline const evaluator::Value *struct_field(const evaluator::StructValue &value,
                                                          std::string_view name) {
    const auto iter = value.fields.find(std::string{name});
    if (iter == value.fields.end()) {
        return nullptr;
    }
    return iter->second.get();
}

} // namespace ahfl::tests::runtime_e2e
