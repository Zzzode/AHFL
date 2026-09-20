# The two compiler benches share bench_harness.hpp, which includes
# "base/support/json.hpp" for the opt-in ahfl.bench-report.v1 artifact (KR7.3).
# That header lives under src/ and is exposed here via the same private include
# other src-internal consumers use.
add_executable(ahfl_bench_compile_time
    bench/compile_time.cpp
)
target_link_libraries(ahfl_bench_compile_time
    PRIVATE
        ahfl_compiler_ir
)
target_include_directories(ahfl_bench_compile_time PRIVATE ${PROJECT_SOURCE_DIR}/src)
ahfl_apply_project_warnings(ahfl_bench_compile_time)

add_executable(ahfl_bench_memory_usage
    bench/memory_usage.cpp
)
target_link_libraries(ahfl_bench_memory_usage
    PRIVATE
        ahfl_compiler_ir
)
target_include_directories(ahfl_bench_memory_usage PRIVATE ${PROJECT_SOURCE_DIR}/src)
ahfl_apply_project_warnings(ahfl_bench_memory_usage)

add_executable(ahfl_bench_smv_size
    bench/smv_size.cpp
)
target_link_libraries(ahfl_bench_smv_size
    PRIVATE
        ahfl_base_public
)
ahfl_apply_project_warnings(ahfl_bench_smv_size)

add_executable(ahfl_bench_ir_pipeline
    bench/ir_pipeline.cpp
)
target_link_libraries(ahfl_bench_ir_pipeline
    PRIVATE
        ahfl_compiler_ir_opt
)
target_include_directories(ahfl_bench_ir_pipeline PRIVATE ${PROJECT_SOURCE_DIR}/src)
ahfl_apply_project_warnings(ahfl_bench_ir_pipeline)

# KR7.3: compile-time / memory-proxy trend gate. Beyond the static per-budget
# ceilings asserted by the two binaries above, this drives both through their
# opt-in machine-readable report (ahfl.bench-report.v1) and compares every
# budget against the committed baseline (config/benchmark-baseline.json): any
# structural drift (source_bytes / typed_exprs / typed_statements / ir_decls /
# ir_exprs) fails exactly, and the memory proxy fails beyond a relative
# tolerance. It emits a trend artifact (ahfl.benchmark-trend.v1) so a release
# process can chart drift, converting the ceiling into a release-blocking trend
# guard. duration_us is report-only (wall-clock); the static ceilings remain the
# wall-clock bound. The baseline is the single source of truth for the budget
# set — extend coverage by adding a budget to the baseline, not by editing here.
add_test(NAME ahflc.quality.benchmark_trend
    COMMAND ${Python3_EXECUTABLE}
            "${AHFL_TESTS_DIR}/scripts/benchmark_trend_gate.py"
            $<TARGET_FILE:ahfl_bench_compile_time>
            $<TARGET_FILE:ahfl_bench_memory_usage>
            "${PROJECT_SOURCE_DIR}/config/benchmark-baseline.json"
            "${CMAKE_CURRENT_BINARY_DIR}/benchmark-trend.json"
)
set_tests_properties(ahflc.quality.benchmark_trend PROPERTIES
    PASS_REGULAR_EXPRESSION "Benchmark trend gate passed"
    FAIL_REGULAR_EXPRESSION "regressed|drifted|no baseline entry|disagree"
)

add_test(NAME ahfl.bench.compile_time COMMAND $<TARGET_FILE:ahfl_bench_compile_time>)
add_test(NAME ahfl.bench.memory_usage COMMAND $<TARGET_FILE:ahfl_bench_memory_usage>)
add_test(NAME ahfl.bench.smv_size COMMAND $<TARGET_FILE:ahfl_bench_smv_size>)
add_test(NAME ahfl.bench.ir_pipeline COMMAND $<TARGET_FILE:ahfl_bench_ir_pipeline>)
