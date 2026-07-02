if(NOT DEFINED AHFLC)
    message(FATAL_ERROR "AHFLC is required")
endif()

if(NOT DEFINED SOURCE_WORKSPACE)
    message(FATAL_ERROR "SOURCE_WORKSPACE is required")
endif()

if(NOT DEFINED SYSROOT_DIR)
    message(FATAL_ERROR "SYSROOT_DIR is required")
endif()

if(NOT DEFINED WORK_DIR)
    message(FATAL_ERROR "WORK_DIR is required")
endif()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")
file(COPY "${SOURCE_WORKSPACE}/" DESTINATION "${WORK_DIR}")

set(lockfile_path "${WORK_DIR}/ahfl.lock")
set(bad_checksum "sha256:0000000000000000000000000000000000000000000000000000000000000000")
string(CONCAT lockfile_json
    "{\n"
    "  \"format_version\": \"ahfl.lock.v1\",\n"
    "  \"resolver_version\": 1,\n"
    "  \"root_package\": \"refund-audit\",\n"
    "  \"packages\": [\n"
    "    {\n"
    "      \"id\": 0,\n"
    "      \"name\": \"std\",\n"
    "      \"version\": \"0.1.0\",\n"
    "      \"source\": \"sysroot\",\n"
    "      \"manifest\": \"${SYSROOT_DIR}/std/ahfl.toml\",\n"
    "      \"checksum\": \"${bad_checksum}\"\n"
    "    },\n"
    "    {\n"
    "      \"id\": 1,\n"
    "      \"name\": \"refund-audit\",\n"
    "      \"version\": \"0.1.0\",\n"
    "      \"source\": \"path\",\n"
    "      \"manifest\": \"packages/refund-audit/ahfl.toml\",\n"
    "      \"checksum\": \"${bad_checksum}\"\n"
    "    },\n"
    "    {\n"
    "      \"id\": 2,\n"
    "      \"name\": \"audit-core\",\n"
    "      \"version\": \"0.1.0\",\n"
    "      \"source\": \"workspace\",\n"
    "      \"manifest\": \"packages/audit-core/ahfl.toml\",\n"
    "      \"checksum\": \"${bad_checksum}\"\n"
    "    },\n"
    "    {\n"
    "      \"id\": 3,\n"
    "      \"name\": \"no-deps-app\",\n"
    "      \"version\": \"0.1.0\",\n"
    "      \"source\": \"workspace\",\n"
    "      \"manifest\": \"packages/no-deps-app/ahfl.toml\",\n"
    "      \"checksum\": \"${bad_checksum}\"\n"
    "    }\n"
    "  ],\n"
    "  \"edges\": [\n"
    "    {\"from\": 1, \"dependency\": \"audit-core\", \"to\": 2, \"source\": \"workspace\"},\n"
    "    {\"from\": 1, \"dependency\": \"std\", \"to\": 0, \"source\": \"sysroot\"},\n"
    "    {\"from\": 2, \"dependency\": \"std\", \"to\": 0, \"source\": \"sysroot\"},\n"
    "    {\"from\": 3, \"dependency\": \"std\", \"to\": 0, \"source\": \"sysroot\"}\n"
    "  ]\n"
    "}\n"
)
file(WRITE "${lockfile_path}" "${lockfile_json}")

execute_process(
    COMMAND "${AHFLC}" check
            --workspace "${WORK_DIR}/ahfl.workspace.toml"
            --package refund-audit
            --target workflow
            --sysroot "${SYSROOT_DIR}"
    RESULT_VARIABLE ahflc_result
    OUTPUT_VARIABLE ahflc_stdout
    ERROR_VARIABLE ahflc_stderr
)

string(CONCAT ahflc_output "${ahflc_stdout}" "${ahflc_stderr}")

if(ahflc_result EQUAL 0)
    message(FATAL_ERROR
        "expected ahflc check to reject workspace lockfile drift, but it succeeded\n${ahflc_output}"
    )
endif()

if(NOT ahflc_output MATCHES "field 'checksum'")
    message(FATAL_ERROR
        "expected workspace lockfile drift output to mention checksum mismatch\n${ahflc_output}"
    )
endif()

message(STATUS "${ahflc_output}")
