set(AHFL_EXAMPLES_DIR "${PROJECT_SOURCE_DIR}/examples")
set(AHFL_TESTS_DIR "${PROJECT_SOURCE_DIR}/tests")
set(AHFL_EXPR_TEMPORAL_PACKAGE_SOURCE
    "${AHFL_TESTS_DIR}/integration/package_golden/ok_expr_temporal/ir/expr_temporal.ahfl"
)

find_package(Python3 COMPONENTS Interpreter REQUIRED)

add_test(NAME ahfl.architecture.boundaries
    COMMAND ${Python3_EXECUTABLE} "${PROJECT_SOURCE_DIR}/scripts/check-architecture.py"
)

add_test(NAME ahfl.runtime.native_grpc_gate
    COMMAND ${Python3_EXECUTABLE} "${PROJECT_SOURCE_DIR}/scripts/check-native-grpc-gate.py"
)

add_test(NAME ahfl.runtime.transport_gate_smoke
    COMMAND ${Python3_EXECUTABLE} "${PROJECT_SOURCE_DIR}/tests/scripts/transport_gate_smoke.py"
            "${PROJECT_SOURCE_DIR}/scripts/check-native-grpc-gate.py"
)

add_test(NAME ahfl.docs.ir_sync_gate
    COMMAND ${Python3_EXECUTABLE} "${PROJECT_SOURCE_DIR}/scripts/check-ir-doc-sync.py"
)

add_test(NAME ahfl.docs.rfc_check
    COMMAND ${Python3_EXECUTABLE} "${PROJECT_SOURCE_DIR}/scripts/check-rfc.py"
)

add_test(NAME ahfl.docs.rfc_check_smoke
    COMMAND ${Python3_EXECUTABLE} "${PROJECT_SOURCE_DIR}/tests/scripts/rfc_check_smoke.py"
            "${PROJECT_SOURCE_DIR}/scripts/check-rfc.py"
)

add_test(NAME ahfl.product.scope_freeze
    COMMAND ${Python3_EXECUTABLE} "${PROJECT_SOURCE_DIR}/scripts/check-product-scope-freeze.py"
)

add_test(NAME ahfl.product.scope_freeze_smoke
    COMMAND ${Python3_EXECUTABLE} "${PROJECT_SOURCE_DIR}/tests/scripts/product_scope_freeze_smoke.py"
            "${PROJECT_SOURCE_DIR}/scripts/check-product-scope-freeze.py"
            "${PROJECT_SOURCE_DIR}"
)

add_test(NAME ahfl.product.beta_gate_contract
    COMMAND ${Python3_EXECUTABLE} "${PROJECT_SOURCE_DIR}/scripts/check-beta-gate.py"
)

add_test(NAME ahfl.product.beta_gate_smoke
    COMMAND ${Python3_EXECUTABLE} "${PROJECT_SOURCE_DIR}/tests/scripts/beta_gate_smoke.py"
            "${PROJECT_SOURCE_DIR}/scripts/check-beta-gate.py"
            "${PROJECT_SOURCE_DIR}"
)

add_test(NAME ahfl.product.controlled_pilot_gate_contract
    COMMAND ${Python3_EXECUTABLE}
            "${PROJECT_SOURCE_DIR}/scripts/check-controlled-pilot-gate.py"
            --root "${PROJECT_SOURCE_DIR}"
)

add_test(NAME ahfl.product.controlled_pilot_gate_smoke
    COMMAND ${Python3_EXECUTABLE}
            "${PROJECT_SOURCE_DIR}/tests/scripts/controlled_pilot_gate_smoke.py"
            "${PROJECT_SOURCE_DIR}/scripts/check-controlled-pilot-gate.py"
)

add_test(NAME ahfl.product.production_confidence_gate_contract
    COMMAND ${Python3_EXECUTABLE}
            "${PROJECT_SOURCE_DIR}/scripts/check-production-confidence-gate.py"
            --root "${PROJECT_SOURCE_DIR}"
)

add_test(NAME ahfl.product.production_confidence_gate_smoke
    COMMAND ${Python3_EXECUTABLE}
            "${PROJECT_SOURCE_DIR}/tests/scripts/production_confidence_gate_smoke.py"
            "${PROJECT_SOURCE_DIR}/scripts/check-production-confidence-gate.py"
)

add_test(NAME ahfl.reference_workflow.long_soak_smoke
    COMMAND ${Python3_EXECUTABLE}
            "${PROJECT_SOURCE_DIR}/tests/scripts/reference_workflow_long_soak.py"
            --worker $<TARGET_FILE:ahfl_reference_workflow_recovery_worker>
            --repo-root "${PROJECT_SOURCE_DIR}"
            --work-dir "${CMAKE_BINARY_DIR}/runtime/reference-workflow-long-soak-smoke"
            --minimum-seconds 2
            --minimum-iterations 3
            --contract-kind smoke
            --evidence-path
            "${PROJECT_SOURCE_DIR}/build/release-evidence/production-confidence/long-soak-smoke.json"
)

add_test(NAME ahfl.product.runtime_evidence_smoke
    COMMAND ${Python3_EXECUTABLE}
            "${PROJECT_SOURCE_DIR}/tests/scripts/runtime_evidence_smoke.py"
            "${PROJECT_SOURCE_DIR}/scripts/generate-beta-runtime-evidence.py"
            "${PROJECT_SOURCE_DIR}"
            "${CMAKE_BINARY_DIR}"
)

add_test(NAME ahfl.product.beta_evidence_bundle_smoke
    COMMAND ${Python3_EXECUTABLE}
            "${PROJECT_SOURCE_DIR}/tests/scripts/beta_evidence_bundle_smoke.py"
            "${PROJECT_SOURCE_DIR}/scripts/generate-beta-evidence-bundle.py"
            "${PROJECT_SOURCE_DIR}"
)

add_test(NAME ahfl.product.beta_evidence_bundle_ready
    COMMAND ${Python3_EXECUTABLE}
            "${PROJECT_SOURCE_DIR}/scripts/generate-beta-evidence-bundle.py"
            --repo-root "${PROJECT_SOURCE_DIR}"
            --build-dir "${CMAKE_BINARY_DIR}"
)

add_test(NAME ahfl.product.formatter_evidence_smoke
    COMMAND ${Python3_EXECUTABLE} "${PROJECT_SOURCE_DIR}/tests/scripts/formatter_evidence_smoke.py"
            "${PROJECT_SOURCE_DIR}/scripts/generate-beta-formatter-evidence.py"
            "${PROJECT_SOURCE_DIR}"
            "${CMAKE_BINARY_DIR}"
)

add_test(NAME ahfl.product.stdlib_container_evidence_smoke
    COMMAND ${Python3_EXECUTABLE} "${PROJECT_SOURCE_DIR}/tests/scripts/stdlib_container_evidence_smoke.py"
            "${PROJECT_SOURCE_DIR}/scripts/generate-beta-stdlib-container-evidence.py"
            "${PROJECT_SOURCE_DIR}"
            "${CMAKE_BINARY_DIR}"
)

add_test(NAME ahfl.product.install_evidence_smoke
    COMMAND ${Python3_EXECUTABLE} "${PROJECT_SOURCE_DIR}/tests/scripts/install_evidence_smoke.py"
            "${PROJECT_SOURCE_DIR}/scripts/generate-beta-install-evidence.py"
            "${PROJECT_SOURCE_DIR}"
            "${CMAKE_BINARY_DIR}"
)

add_test(NAME ahfl.product.readme_capabilities
    COMMAND ${Python3_EXECUTABLE} "${PROJECT_SOURCE_DIR}/scripts/check-readme-capabilities.py"
            --root "${PROJECT_SOURCE_DIR}"
)

add_test(NAME ahfl.product.readme_capabilities_smoke
    COMMAND ${Python3_EXECUTABLE} "${PROJECT_SOURCE_DIR}/tests/scripts/readme_capabilities_smoke.py"
            "${PROJECT_SOURCE_DIR}/scripts/check-readme-capabilities.py"
            "${PROJECT_SOURCE_DIR}"
            "${CMAKE_BINARY_DIR}"
)

add_test(NAME ahfl.security.execution_demo_secret
    COMMAND ${Python3_EXECUTABLE} "${PROJECT_SOURCE_DIR}/scripts/check-execution-demo-secret.py"
)

add_test(NAME ahfl.security.execution_demo_secret_smoke
    COMMAND ${Python3_EXECUTABLE} "${PROJECT_SOURCE_DIR}/tests/scripts/execution_demo_secret_smoke.py"
            "${PROJECT_SOURCE_DIR}/scripts/check-execution-demo-secret.py"
            "${PROJECT_SOURCE_DIR}"
)

function(ahfl_label_tests)
    set(multi_value_args LABELS TESTS)
    cmake_parse_arguments(AHFL "" "" "${multi_value_args}" ${ARGN})

    if(NOT AHFL_LABELS)
        message(FATAL_ERROR "ahfl_label_tests requires LABELS")
    endif()

    if(NOT AHFL_TESTS)
        message(FATAL_ERROR "ahfl_label_tests requires TESTS")
    endif()

    foreach(test_name IN LISTS AHFL_TESTS)
        set_property(TEST "${test_name}" APPEND PROPERTY LABELS ${AHFL_LABELS})
    endforeach()
endfunction()

include("${CMAKE_CURRENT_LIST_DIR}/cmake/TestTargets.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/ProjectTests.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/SingleFileCliTests.cmake")

include("${CMAKE_CURRENT_LIST_DIR}/fuzz/CMakeLists.txt")
include("${CMAKE_CURRENT_LIST_DIR}/bench/CMakeLists.txt")
include("${CMAKE_CURRENT_LIST_DIR}/mutation/CMakeLists.txt")

include("${CMAKE_CURRENT_LIST_DIR}/cmake/LabelTests.cmake")

get_property(ahfl_all_tests DIRECTORY PROPERTY TESTS)
foreach(test_name IN LISTS ahfl_all_tests)
    set_property(TEST "${test_name}" PROPERTY WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
endforeach()
