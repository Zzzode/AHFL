# KR7.3: the shared harness for both compiler benches. bench_harness.hpp is
# dependency-free (standard library + the public fixture header); the one
# private dependency, "base/support/json.hpp", lives in bench_harness.cpp, so
# this target - and only this target - needs the src-private include root. A
# bench that includes the header does not.
add_library(ahfl_bench_harness STATIC
    bench/bench_harness.cpp
)
target_link_libraries(ahfl_bench_harness
    PRIVATE
        ahfl_base_public
)
target_include_directories(ahfl_bench_harness PRIVATE ${PROJECT_SOURCE_DIR}/src)
ahfl_apply_project_warnings(ahfl_bench_harness)

add_executable(ahfl_bench_compile_time
    bench/compile_time.cpp
)
target_link_libraries(ahfl_bench_compile_time
    PRIVATE
        ahfl_bench_harness
        ahfl_compiler_ir
)
ahfl_apply_project_warnings(ahfl_bench_compile_time)

add_executable(ahfl_bench_memory_usage
    bench/memory_usage.cpp
)
target_link_libraries(ahfl_bench_memory_usage
    PRIVATE
        ahfl_bench_harness
        ahfl_compiler_ir
)
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
    FAIL_REGULAR_EXPRESSION "regressed|drifted|no baseline entry|disagree|refused"
)

# The trend gate's own contracts, pinned: --update resets the tolerance checks
# but never the invariants (cross-bench disagreement / one-sided measurement
# refuse the update and leave the baseline byte-identical), and a malformed
# baseline entry is a diagnostic, not a traceback. Uses stub bench binaries, so
# it needs neither the benches nor the compiler built.
add_test(NAME ahflc.quality.benchmark_trend_smoke
    COMMAND ${Python3_EXECUTABLE}
            "${AHFL_TESTS_DIR}/scripts/benchmark_trend_gate_smoke.py"
)
set_tests_properties(ahflc.quality.benchmark_trend_smoke PROPERTIES
    PASS_REGULAR_EXPRESSION "benchmark trend gate smoke passed"
)

add_test(NAME ahfl.bench.compile_time COMMAND $<TARGET_FILE:ahfl_bench_compile_time>)
add_test(NAME ahfl.bench.memory_usage COMMAND $<TARGET_FILE:ahfl_bench_memory_usage>)
add_test(NAME ahfl.bench.smv_size COMMAND $<TARGET_FILE:ahfl_bench_smv_size>)
add_test(NAME ahfl.bench.ir_pipeline COMMAND $<TARGET_FILE:ahfl_bench_ir_pipeline>)
