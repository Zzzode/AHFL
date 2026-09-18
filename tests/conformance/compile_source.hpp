#pragma once

// KR6.7 (RFC 0026 P7): the ONE compile seam for conformance test
// infrastructure.
//
// Every conformance consumer that needs a compiled AHFL program (the evaluator
// engine adapter, the wasm eligibility classifier) runs the identical
// parse -> resolve -> typecheck -> validate -> lower pipeline through this
// function. It replaced the per-driver copies the bespoke end-to-end binaries
// each carried, so the pipeline order and its diagnostic rendering exist
// exactly once. Engine adapters add their own execution on top; they never
// re-implement compilation.

#include <filesystem>
#include <optional>
#include <sstream>
#include <string>

#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/ir/lowering.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"
#include "ahfl/compiler/semantics/validate.hpp"

namespace ahfl::conformance {

/// Compiles one AHFL source file through the standard pipeline. Returns the
/// lowered AHFL IR, or `nullopt` with the rendered diagnostics in `error_out`.
[[nodiscard]] inline std::optional<ir::Program>
compile_conformance_source(const std::filesystem::path &file_path, std::string &error_out) {
    const Frontend frontend;
    const auto parse_result = frontend.parse_file(file_path);
    if (parse_result.has_errors() || !parse_result.program) {
        std::ostringstream out;
        parse_result.diagnostics.render(out);
        error_out = "parse failed:\n" + out.str();
        return std::nullopt;
    }

    const Resolver resolver;
    const auto resolve_result = resolver.resolve(*parse_result.program);
    if (resolve_result.has_errors()) {
        std::ostringstream out;
        resolve_result.diagnostics.render(out);
        error_out = "resolve failed:\n" + out.str();
        return std::nullopt;
    }

    const TypeChecker type_checker;
    const auto type_check_result = type_checker.check(*parse_result.program, resolve_result);
    if (type_check_result.has_errors()) {
        std::ostringstream out;
        type_check_result.diagnostics.render(out);
        error_out = "typecheck failed:\n" + out.str();
        return std::nullopt;
    }

    const Validator validator;
    const auto validation_result =
        validator.validate(*parse_result.program, resolve_result, type_check_result);
    if (validation_result.has_errors()) {
        std::ostringstream out;
        validation_result.diagnostics.render(out);
        error_out = "validate failed:\n" + out.str();
        return std::nullopt;
    }

    return lower_program_ir(*parse_result.program, resolve_result, type_check_result);
}

} // namespace ahfl::conformance
