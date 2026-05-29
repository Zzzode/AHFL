#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build/mutation"

echo "=== AHFL Mutation Testing ==="
echo "Project root: ${PROJECT_ROOT}"
echo "Build dir: ${BUILD_DIR}"

# Check for mull-runner
if ! command -v mull-runner &> /dev/null; then
    echo "WARNING: mull-runner not found. Install with:"
    echo "  brew install mull (macOS) or see https://github.com/mull-project/mull"
    echo ""
    echo "Running in dry-run mode..."
    echo "Would test mutations in: ahfl_compiler_syntax, ahfl_compiler_ir, ahfl_verification_formal"
    echo "Against test suites: ahfl_project_parse_tests, ahfl_project_check_tests, ahfl_bmc_tests"
    exit 0
fi

# Build with coverage instrumentation
cmake -S "${PROJECT_ROOT}" -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_FLAGS="-fpass-plugin=mull-ir-frontend" \
    -G Ninja

cmake --build "${BUILD_DIR}"

# Run mutation testing
echo "Running mutation testing..."
mull-runner \
    --config "${SCRIPT_DIR}/mutation_config.json" \
    --report-dir "${BUILD_DIR}/mutation_reports" \
    "${BUILD_DIR}/tests/ahfl_project_parse_tests"

echo "=== Mutation testing complete ==="
echo "Reports available in: ${BUILD_DIR}/mutation_reports/"
