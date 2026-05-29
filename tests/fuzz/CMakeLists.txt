option(AHFL_ENABLE_FUZZING "Enable fuzz testing targets" OFF)

if(AHFL_ENABLE_FUZZING)
    # When fuzzing is enabled, use libFuzzer
    set(FUZZ_COMPILE_FLAGS -fsanitize=fuzzer,address -fno-omit-frame-pointer)
    set(FUZZ_LINK_FLAGS -fsanitize=fuzzer,address)

    add_executable(fuzz_parser fuzz/fuzz_parser.cpp)
    target_link_libraries(fuzz_parser PRIVATE ahfl_compiler_syntax)
    target_compile_options(fuzz_parser PRIVATE ${FUZZ_COMPILE_FLAGS})
    target_link_options(fuzz_parser PRIVATE ${FUZZ_LINK_FLAGS})
    ahfl_apply_project_warnings(fuzz_parser)

    add_executable(fuzz_typecheck fuzz/fuzz_typecheck.cpp)
    target_link_libraries(fuzz_typecheck PRIVATE ahfl_compiler_ir)
    target_compile_options(fuzz_typecheck PRIVATE ${FUZZ_COMPILE_FLAGS})
    target_link_options(fuzz_typecheck PRIVATE ${FUZZ_LINK_FLAGS})
    ahfl_apply_project_warnings(fuzz_typecheck)

    add_executable(fuzz_smv_emitter fuzz/fuzz_smv_emitter.cpp)
    target_link_libraries(fuzz_smv_emitter PRIVATE ahfl_verification_formal)
    target_compile_options(fuzz_smv_emitter PRIVATE ${FUZZ_COMPILE_FLAGS})
    target_link_options(fuzz_smv_emitter PRIVATE ${FUZZ_LINK_FLAGS})
    ahfl_apply_project_warnings(fuzz_smv_emitter)
else()
    # Compile as regular executables for CI validation
    add_executable(fuzz_parser_check fuzz/fuzz_parser.cpp)
    target_link_libraries(fuzz_parser_check PRIVATE ahfl_compiler_syntax)
    target_compile_definitions(fuzz_parser_check PRIVATE AHFL_FUZZ_STANDALONE=1)
    ahfl_apply_project_warnings(fuzz_parser_check)

    add_executable(fuzz_typecheck_check fuzz/fuzz_typecheck.cpp)
    target_link_libraries(fuzz_typecheck_check PRIVATE ahfl_compiler_ir)
    target_compile_definitions(fuzz_typecheck_check PRIVATE AHFL_FUZZ_STANDALONE=1)
    ahfl_apply_project_warnings(fuzz_typecheck_check)

    add_executable(fuzz_smv_emitter_check fuzz/fuzz_smv_emitter.cpp)
    target_link_libraries(fuzz_smv_emitter_check PRIVATE ahfl_verification_formal)
    target_compile_definitions(fuzz_smv_emitter_check PRIVATE AHFL_FUZZ_STANDALONE=1)
    target_include_directories(fuzz_smv_emitter_check PRIVATE ${PROJECT_SOURCE_DIR}/src)
    ahfl_apply_project_warnings(fuzz_smv_emitter_check)

    add_test(NAME ahfl.fuzz.parser_check COMMAND $<TARGET_FILE:fuzz_parser_check>)
    add_test(NAME ahfl.fuzz.typecheck_check COMMAND $<TARGET_FILE:fuzz_typecheck_check>)
    add_test(NAME ahfl.fuzz.smv_emitter_check COMMAND $<TARGET_FILE:fuzz_smv_emitter_check>)
endif()
