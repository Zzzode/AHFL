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

function(ahfl_add_command_fail_test name subcommand source_file expected_pattern)
    add_test(NAME ${name}
        COMMAND ${CMAKE_COMMAND}
            -DAHFLC=$<TARGET_FILE:ahflc>
            -DINPUT_FILE=${source_file}
            "-DAHFLC_ARGS=${subcommand};${source_file}"
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

# Register a single golden test for a package-manifest subcommand.
# Options:
#   PACKAGE_ONLY  — no mocks/fixture/run-id
#   FIXTURE       — input fixture name (default: fixture.request.basic)
#   RUN_ID        — run identifier (default: run-001)
#   MOCKS_STEM    — stem for the .mocks.json file (default: source_stem)
function(ahfl_add_package_output_test name subcommand source_stem expected_file)
    cmake_parse_arguments(ARG "PACKAGE_ONLY" "FIXTURE;RUN_ID;MOCKS_STEM" "" ${ARGN})

    set(manifest_file "${AHFL_TESTS_DIR}/integration/package_golden/${source_stem}/ahfl.toml")
    set(package_args "--manifest ${manifest_file} --target workflow --sysroot ${PROJECT_SOURCE_DIR}")

    if(ARG_PACKAGE_ONLY)
        set(args "${subcommand} ${package_args}")
    else()
        if(NOT ARG_FIXTURE)
            set(ARG_FIXTURE "fixture.request.basic")
        endif()
        if(NOT ARG_RUN_ID)
            set(ARG_RUN_ID "run-001")
        endif()
        if(NOT ARG_MOCKS_STEM)
            set(ARG_MOCKS_STEM "${source_stem}")
        endif()
        set(mocks_file "${AHFL_TESTS_DIR}/golden/dry_run/${ARG_MOCKS_STEM}.mocks.json")
        set(args "${subcommand} ${package_args} --capability-mocks ${mocks_file} --input-fixture ${ARG_FIXTURE} --run-id ${ARG_RUN_ID}")
    endif()

    add_test(NAME ${name}
        COMMAND ${CMAKE_COMMAND}
                "-DAHFLC=$<TARGET_FILE:ahflc>"
                "-DAHFLC_ARGS=${args}"
                "-DEXPECTED_FILE=${expected_file}"
                -P "${PROJECT_SOURCE_DIR}/cmake/RunExpectedCommandOutput.cmake"
    )
endfunction()

# Auto-discover and register all package golden tests.
# Scans tests/golden/ for files containing ".with_package." and registers
# each as a test, deriving the subcommand from the artifact suffix.
function(ahfl_discover_package_golden_tests)
    # Subcommands that only need package metadata (no mocks/fixture/run-id)
    set(_package_only_subcommands
        "emit native-json"
        "emit package-review"
        "emit execution-plan"
    )

    file(GLOB_RECURSE golden_files "${AHFL_TESTS_DIR}/golden/*with_package*")

    foreach(golden_file ${golden_files})
        get_filename_component(filename ${golden_file} NAME)

        # Extract: <variant_stem>.with_package.<artifact_suffix>
        string(REGEX MATCH "^(.+)\\.with_package\\.(.+)$" _match "${filename}")
        if(NOT _match)
            continue()
        endif()
        set(variant_stem "${CMAKE_MATCH_1}")
        set(artifact_suffix "${CMAKE_MATCH_2}")

        # Derive the subcommand from artifact_suffix.
        # Special cases first, then general rule.
        if(artifact_suffix STREQUAL "native.json")
            set(subcommand "emit native-json")
        elseif(artifact_suffix STREQUAL "review")
            set(subcommand "emit package-review")
        elseif(artifact_suffix MATCHES "\\.json$")
            string(REGEX REPLACE "\\.json$" "" _base "${artifact_suffix}")
            # Convert long-form artifact base to short-form artifact-id
            if(_base MATCHES "^durable-store-import-provider-(.+)$")
                set(subcommand "emit-provider-artifact provider/${CMAKE_MATCH_1}")
            elseif(_base MATCHES "^durable-store-import-(.+)$")
                set(subcommand "emit store/${CMAKE_MATCH_1}")
            else()
                set(subcommand "emit ${_base}")
            endif()
        else()
            # Non-json suffix: apply same prefix-stripping rules
            if(artifact_suffix MATCHES "^durable-store-import-provider-(.+)$")
                set(subcommand "emit-provider-artifact provider/${CMAKE_MATCH_1}")
            elseif(artifact_suffix MATCHES "^durable-store-import-(.+)$")
                set(subcommand "emit store/${CMAKE_MATCH_1}")
            else()
                set(subcommand "emit ${artifact_suffix}")
            endif()
        endif()

        # Auto-discovered provider goldens validate artifact output coverage,
        # including internal pipeline nodes. The user-facing default provider
        # surface is covered by command_routing and explicit public tests.
        if(subcommand MATCHES "^emit-provider-artifact provider/")
            set(subcommand "${subcommand} --show-hidden")
        endif()

        # Determine source_stem, fixture, run_id, mocks_stem from variant_stem.
        set(fixture "fixture.request.basic")
        set(run_id "run-001")
        set(source_stem "${variant_stem}")
        set(mocks_stem "${variant_stem}")

        if(variant_stem MATCHES "^(.+)\\.partial$")
            set(source_stem "${CMAKE_MATCH_1}")
            set(mocks_stem "${CMAKE_MATCH_1}.pending")
            set(fixture "fixture.request.partial")
            set(run_id "run-partial-001")
        elseif(variant_stem MATCHES "^(.+)\\.failed$")
            set(source_stem "${CMAKE_MATCH_1}")
            set(mocks_stem "${CMAKE_MATCH_1}.fail")
        endif()

        # Skip if the package fixture manifest does not exist.
        if(NOT EXISTS "${AHFL_TESTS_DIR}/integration/package_golden/${source_stem}/ahfl.toml")
            continue()
        endif()

        # Build test name from original artifact_suffix for stability.
        # This ensures LabelTests.cmake references remain valid even when
        # the CLI subcommand syntax changes.
        if(artifact_suffix STREQUAL "native.json")
            set(test_name_base "emit_native_json")
        elseif(artifact_suffix STREQUAL "review")
            set(test_name_base "emit_package_review")
        elseif(artifact_suffix MATCHES "\\.json$")
            string(REGEX REPLACE "\\.json$" "" _base "${artifact_suffix}")
            string(REPLACE "-" "_" test_name_base "emit_${_base}")
        else()
            string(REPLACE "-" "_" test_name_base "emit_${artifact_suffix}")
        endif()
        # name_stem: strip "ok_" prefix from variant_stem if present
        if(variant_stem MATCHES "^ok_(.*)")
            set(name_stem "${CMAKE_MATCH_1}")
        else()
            set(name_stem "${variant_stem}")
        endif()
        set(test_name "ahflc.${test_name_base}.${name_stem}.with_package")

        # Register the test.
        list(FIND _package_only_subcommands "${subcommand}" _pkg_only_idx)
        if(NOT _pkg_only_idx EQUAL -1)
            ahfl_add_package_output_test(
                "${test_name}"
                "${subcommand}"
                "${source_stem}"
                "${golden_file}"
                PACKAGE_ONLY
            )
        else()
            ahfl_add_package_output_test(
                "${test_name}"
                "${subcommand}"
                "${source_stem}"
                "${golden_file}"
                FIXTURE "${fixture}"
                RUN_ID "${run_id}"
                MOCKS_STEM "${mocks_stem}"
            )
        endif()
    endforeach()
endfunction()
