#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUTPUT_DIR="${ROOT_DIR}/src/parser/generated"
LOCK_FILE="${ROOT_DIR}/grammar/antlr-toolchain.lock"
WORK_DIR="$(mktemp -d)"
ANTLR_JAR="${ANTLR_JAR:-}"
CHECK_ONLY=0

cleanup() {
    rm -rf "${WORK_DIR}"
}

trap cleanup EXIT

if [[ "${1:-}" == "--check" ]]; then
    CHECK_ONLY=1
    shift
fi

if [[ $# -ne 0 ]]; then
    echo "usage: ANTLR_JAR=/path/to/antlr-4.x-complete.jar $0 [--check]" >&2
    exit 2
fi

lock_value() {
    local key="$1"
    sed -n "s/^${key}=//p" "${LOCK_FILE}" | head -n 1
}

if [[ ! -f "${LOCK_FILE}" ]]; then
    echo "error: missing ANTLR toolchain lock file: ${LOCK_FILE}" >&2
    exit 1
fi

EXPECTED_ANTLR_VERSION="$(lock_value version)"
EXPECTED_ANTLR_SHA256="$(lock_value sha256)"

if [[ -z "${EXPECTED_ANTLR_VERSION}" || -z "${EXPECTED_ANTLR_SHA256}" ]]; then
    echo "error: ANTLR toolchain lock must define version and sha256" >&2
    exit 1
fi

if [[ -z "${ANTLR_JAR}" ]]; then
    echo "error: set ANTLR_JAR to an antlr-4.x-complete.jar path" >&2
    exit 1
fi

if [[ ! -f "${ANTLR_JAR}" ]]; then
    echo "error: ANTLR_JAR does not exist: ${ANTLR_JAR}" >&2
    exit 1
fi

if ! command -v java >/dev/null 2>&1; then
    echo "error: java is required to run the ANTLR generator" >&2
    exit 1
fi

if command -v shasum >/dev/null 2>&1; then
    ACTUAL_ANTLR_SHA256="$(shasum -a 256 "${ANTLR_JAR}" | awk '{print $1}')"
elif command -v sha256sum >/dev/null 2>&1; then
    ACTUAL_ANTLR_SHA256="$(sha256sum "${ANTLR_JAR}" | awk '{print $1}')"
else
    echo "error: shasum or sha256sum is required to verify ANTLR_JAR" >&2
    exit 1
fi

if [[ "${ACTUAL_ANTLR_SHA256}" != "${EXPECTED_ANTLR_SHA256}" ]]; then
    echo "error: ANTLR_JAR sha256 mismatch" >&2
    echo "expected: ${EXPECTED_ANTLR_SHA256}" >&2
    echo "actual:   ${ACTUAL_ANTLR_SHA256}" >&2
    exit 1
fi

if ! command -v perl >/dev/null 2>&1; then
    echo "error: perl is required to normalize generated parser files" >&2
    exit 1
fi

mkdir -p "${OUTPUT_DIR}"

(
    cd "${ROOT_DIR}"
    java -jar "${ANTLR_JAR}" \
        -Dlanguage=Cpp \
        -visitor \
        -no-listener \
        -o "${WORK_DIR}" \
        grammar/AHFL.g4
)

if ! grep -q "by ANTLR ${EXPECTED_ANTLR_VERSION}" "${WORK_DIR}/grammar/AHFLParser.cpp"; then
    echo "error: generated parser does not report ANTLR ${EXPECTED_ANTLR_VERSION}" >&2
    exit 1
fi

GENERATED_DIR="${WORK_DIR}/generated"
mkdir -p "${GENERATED_DIR}"
cp "${WORK_DIR}/grammar"/AHFL*.cpp "${WORK_DIR}/grammar"/AHFL*.h "${GENERATED_DIR}/"
perl -pi -e 's/[ \t]+$//' "${GENERATED_DIR}"/AHFL*.cpp "${GENERATED_DIR}"/AHFL*.h

if [[ "${CHECK_ONLY}" -eq 1 ]]; then
    if ! diff -ru "${OUTPUT_DIR}" "${GENERATED_DIR}"; then
        echo "error: checked-in generated parser is stale" >&2
        exit 1
    fi
    echo "checked generated C++ parser in ${OUTPUT_DIR}"
    exit 0
fi

cp "${GENERATED_DIR}"/AHFL*.cpp "${GENERATED_DIR}"/AHFL*.h "${OUTPUT_DIR}/"

echo "regenerated C++ parser into ${OUTPUT_DIR}"
