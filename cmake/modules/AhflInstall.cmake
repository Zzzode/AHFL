include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# Public headers
install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/ahfl
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.hpp"
)

# Libraries
install(TARGETS
    ahfl_support
    ahfl_support_lib
    ahfl_json
    ahfl_syntax
    ahfl_frontend_lib
    ahfl_semantics
    ahfl_ir
    ahfl_handoff
    ahfl_execution_pipeline
    ahfl_observation
    ahfl_persistence
    ahfl_durable_store_import
    ahfl_backend
    ahfl_compiler_core
    ahfl_runtime_stack
    ahfl_tooling_stack
    ahfl_compiler
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
