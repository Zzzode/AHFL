function(ahfl_add_check_test name source_file)
    add_test(NAME ${name}
        COMMAND $<TARGET_FILE:ahflc> check "${source_file}"
    )
endfunction()

function(ahfl_add_check_fail_test name source_file expected_pattern)
    add_test(NAME ${name}
        COMMAND ${CMAKE_COMMAND}
            -DAHFLC=$<TARGET_FILE:ahflc>
            -DINPUT_FILE=${source_file}
            -DEXPECTED_REGEX=${expected_pattern}
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedFailure.cmake"
    )
endfunction()

function(ahfl_add_output_test name subcommand source_file expected_file)
    add_test(NAME ${name}
        COMMAND ${CMAKE_COMMAND}
            -DAHFLC=$<TARGET_FILE:ahflc>
            -DSUBCOMMAND=${subcommand}
            -DINPUT_FILE=${source_file}
            -DEXPECTED_FILE=${expected_file}
            -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedOutput.cmake"
    )
endfunction()
