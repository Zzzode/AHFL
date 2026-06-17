#pragma once

#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/ir/lowering.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"
#include "ahfl/compiler/semantics/validate.hpp"

#include <filesystem>
#include <sstream>
#include <string>

namespace ahfl::bench {

inline std::string make_benchmark_source(std::size_t let_count, std::size_t branch_count) {
    std::ostringstream out;
    out << "module bench::quality_gate;\n\n";
    out << "struct Request {\n  value: String;\n}\n\n";
    out << "struct Context {\n  value: String = \"\";\n}\n\n";
    out << "struct Response {\n  value: String;\n}\n\n";
    out << "agent BenchAgent {\n";
    out << "  input: Request;\n";
    out << "  context: Context;\n";
    out << "  output: Response;\n";
    out << "  states: [Done];\n";
    out << "  initial: Done;\n";
    out << "  final: [Done];\n";
    out << "  capabilities: [];\n";
    out << "}\n\n";
    out << "flow for BenchAgent {\n";
    out << "  state Done {\n";
    for (std::size_t index = 0; index < let_count; ++index) {
        out << "    let flag" << index << ": Bool = true;\n";
    }
    for (std::size_t index = 0; index < branch_count; ++index) {
        out << "    if true {\n";
        out << "      let then_flag" << index << ": Bool = true;\n";
        out << "    } else {\n";
        out << "      let else_flag" << index << ": Bool = false;\n";
        out << "    }\n";
    }
    out << "    return Response { value: input.value };\n";
    out << "  }\n";
    out << "}\n";
    return out.str();
}

inline SourceGraph make_source_graph(ParseResult parse_result) {
    SourceId source_id{0};
    SourceGraph graph;
    graph.entry_sources.push_back(source_id);
    graph.module_to_source.emplace("bench::quality_gate", source_id);
    graph.sources.push_back(SourceUnit{
        .id = source_id,
        .path = std::filesystem::path{"bench/quality_gate.ahfl"},
        .module_name = "bench::quality_gate",
        .module_range = {},
        .source = std::move(parse_result.source),
        .program = std::move(parse_result.program),
        .imports = {},
    });
    return graph;
}

struct CompileArtifacts {
    std::string source;
    SourceGraph graph;
    ResolveResult resolve_result;
    TypeCheckResult type_result;
    ValidationResult validation_result;
    ir::Program ir_program;
};

inline CompileArtifacts compile_benchmark_source(std::size_t let_count, std::size_t branch_count) {
    CompileArtifacts artifacts;
    artifacts.source = make_benchmark_source(let_count, branch_count);

    Frontend frontend;
    auto parse_result = frontend.parse_text("bench/quality_gate.ahfl", artifacts.source);
    artifacts.graph = make_source_graph(std::move(parse_result));

    Resolver resolver;
    artifacts.resolve_result = resolver.resolve(artifacts.graph);

    TypeChecker checker;
    artifacts.type_result = checker.check(artifacts.graph, artifacts.resolve_result);

    Validator validator;
    artifacts.validation_result =
        validator.validate(artifacts.graph, artifacts.resolve_result, artifacts.type_result);

    artifacts.ir_program =
        lower_program_ir(artifacts.graph, artifacts.resolve_result, artifacts.type_result);

    return artifacts;
}

} // namespace ahfl::bench
