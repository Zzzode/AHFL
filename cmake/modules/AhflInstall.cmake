include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# Public headers
install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/ahfl
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.hpp"
)

install(DIRECTORY ${PROJECT_SOURCE_DIR}/third_party/antlr4/runtime/src/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/ahfl/third_party/antlr4
    FILES_MATCHING PATTERN "*.h"
)

install(DIRECTORY ${PROJECT_SOURCE_DIR}/src/compiler/syntax/parser/generated/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/ahfl/compiler/syntax/parser/generated
    FILES_MATCHING PATTERN "*.h"
)

# Libraries
option(AHFL_INSTALL_INTERNAL_TARGETS
    "Install the full internal AHFL target graph for local development packaging"
    OFF)

set(AHFL_INSTALL_TARGETS
    ahfl_base_public
)

set(AHFL_INTERNAL_INSTALL_TARGETS
    antlr4_runtime
    ahfl_base_support
    ahfl_base_json
    ahfl_compiler_syntax_parser
    ahfl_compiler_syntax
    ahfl_compiler_syntax_recovery
    ahfl_compiler_frontend
    ahfl_compiler_semantics
    ahfl_compiler_ir
    ahfl_compiler_assurance
    ahfl_compiler_handoff
    ahfl_pipeline_execution
    ahfl_pipeline_observation
    ahfl_pipeline_persistence
    ahfl_pipeline_durable_store_import_core
    ahfl_pipeline_durable_store_import_providers
    ahfl_pipeline_durable_store_import_artifacts
    ahfl_pipeline_durable_store_import
    ahfl_compiler_backend_smv
    ahfl_compiler_backend_pipeline_common
    ahfl_compiler_backend_pipeline_observation
    ahfl_compiler_backend_pipeline_persistence
    ahfl_compiler_backend_pipeline_execution
    ahfl_compiler_backend_pipeline_handoff
    ahfl_compiler_backend_assurance
    ahfl_compiler_backend_summary
    ahfl_compiler_backend_infra_wasm
    ahfl_compiler_backend_infra_k8s_crd
    ahfl_compiler_backend_infra_openapi_spec
    ahfl_compiler_backend_infra_terraform_gen
    ahfl_compiler_backends
    ahfl_compiler_passes
    ahfl_compiler_core
    ahfl_runtime_evaluator
    ahfl_runtime_engine
    ahfl_runtime_provider_llm
    ahfl_runtime_provider_secret
    ahfl_verification_formal
    ahfl_runtime_bundle
    ahfl_runtime
    ahfl_tooling_lsp
    ahfl_tooling_formatter
    ahfl_tooling_repl
    ahfl_tooling_dap
    ahfl_tooling_telemetry
    ahfl_tooling_profiling
    ahfl_tooling_abi
    ahfl_tooling_incremental
    ahfl_tooling_testing
    ahfl_tooling_package
    ahfl_tooling_bundle
    ahfl_tooling
    ahfl_compiler
)

if(AHFL_INSTALL_INTERNAL_TARGETS)
    list(APPEND AHFL_INSTALL_TARGETS ${AHFL_INTERNAL_INSTALL_TARGETS})
endif()

foreach(_target IN LISTS AHFL_INSTALL_TARGETS)
    if(TARGET ${_target})
        list(APPEND AHFL_RESOLVED_INSTALL_TARGETS ${_target})
    endif()
endforeach()

install(TARGETS
    ${AHFL_RESOLVED_INSTALL_TARGETS}
    EXPORT AHFLTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# Export config
install(EXPORT AHFLTargets
    FILE AHFLTargets.cmake
    NAMESPACE AHFL::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/AHFL
)

# Package config
configure_package_config_file(
    "${PROJECT_SOURCE_DIR}/cmake/AHFLConfig.cmake.in"
    "${PROJECT_BINARY_DIR}/AHFLConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/AHFL
)

write_basic_package_version_file(
    "${PROJECT_BINARY_DIR}/AHFLConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    "${PROJECT_BINARY_DIR}/AHFLConfig.cmake"
    "${PROJECT_BINARY_DIR}/AHFLConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/AHFL
)
