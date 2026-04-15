#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUTPUT_DIR="${ROOT_DIR}/src/parser/generated"
WORK_DIR="$(mktemp -d)"
ANTLR_JAR="${ANTLR_JAR:-}"

cleanup() {
    rm -rf "${WORK_DIR}"
}

trap cleanup EXIT

if [[ -z "${ANTLR_JAR}" ]]; then
    echo "error: set ANTLR_JAR to an antlr-4.x-complete.jar path" >&2
    exit 1
fi

if [[ ! -f "${ANTLR_JAR}" ]]; then
    echo "error: ANTLR_JAR does not exist: ${ANTLR_JAR}" >&2
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

cp "${WORK_DIR}/grammar"/AHFL*.cpp "${WORK_DIR}/grammar"/AHFL*.h "${OUTPUT_DIR}/"

echo "regenerated C++ parser into ${OUTPUT_DIR}"
