# Mutation testing configuration
# Run with: ./run_mutation.sh from this directory
# Requires: mull-runner or custom mutation framework

# This CMakeLists.txt serves as documentation for the mutation testing setup
# Actual mutation testing is driven by run_mutation.sh

# Copy config to build directory for convenience
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/mutation_config.json
    ${CMAKE_CURRENT_BINARY_DIR}/mutation_config.json
    COPYONLY
)
