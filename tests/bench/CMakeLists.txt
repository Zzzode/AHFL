add_executable(ahfl_bench_compile_time
    bench/compile_time.cpp
)
target_link_libraries(ahfl_bench_compile_time
    PRIVATE
        ahfl_compiler_ir
)
ahfl_apply_project_warnings(ahfl_bench_compile_time)

add_executable(ahfl_bench_memory_usage
    bench/memory_usage.cpp
)
target_link_libraries(ahfl_bench_memory_usage
    PRIVATE
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

add_test(NAME ahfl.bench.compile_time COMMAND $<TARGET_FILE:ahfl_bench_compile_time>)
add_test(NAME ahfl.bench.memory_usage COMMAND $<TARGET_FILE:ahfl_bench_memory_usage>)
add_test(NAME ahfl.bench.smv_size COMMAND $<TARGET_FILE:ahfl_bench_smv_size>)
add_test(NAME ahfl.bench.ir_pipeline COMMAND $<TARGET_FILE:ahfl_bench_ir_pipeline>)
