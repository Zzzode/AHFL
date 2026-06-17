if(NOT DEFINED AHFLC)
    message(FATAL_ERROR "AHFLC is required")
endif()

if(NOT DEFINED INPUT_FILE)
    message(FATAL_ERROR "INPUT_FILE is required")
endif()

if(NOT DEFINED WORK_DIR)
    message(FATAL_ERROR "WORK_DIR is required")
endif()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

set(trace_file "${WORK_DIR}/trace.jsonl")
set(metrics_file "${WORK_DIR}/metrics.jsonl")
set(log_file "${WORK_DIR}/structured-log.jsonl")
set(memory_file "${WORK_DIR}/memory-report.json")

execute_process(
    COMMAND "${AHFLC}" emit summary
            "--trace-export" "${trace_file}"
            "--metrics-export" "${metrics_file}"
            "--structured-log" "${log_file}"
            "--memory-report" "${memory_file}"
            "${INPUT_FILE}"
    RESULT_VARIABLE ahflc_result
    OUTPUT_VARIABLE ahflc_stdout
    ERROR_VARIABLE ahflc_stderr
)

if(NOT ahflc_result EQUAL 0)
    message(FATAL_ERROR "expected ahflc emit summary observability run to succeed\n${ahflc_stdout}${ahflc_stderr}")
endif()

if(ahflc_stdout MATCHES "traceId|ahfl\\.cli|ahflc command completed")
    message(FATAL_ERROR "observability output leaked into stdout artifact\n${ahflc_stdout}")
endif()

if(NOT EXISTS "${trace_file}")
    message(FATAL_ERROR "trace export file was not created")
endif()
if(NOT EXISTS "${metrics_file}")
    message(FATAL_ERROR "metrics export file was not created")
endif()
if(NOT EXISTS "${log_file}")
    message(FATAL_ERROR "structured log file was not created")
endif()
if(NOT EXISTS "${memory_file}")
    message(FATAL_ERROR "memory report file was not created")
endif()

file(READ "${trace_file}" trace_content)
if(NOT trace_content MATCHES "\"name\":\"ahflc\\.command\"")
    message(FATAL_ERROR "trace export missing ahflc.command span\n${trace_content}")
endif()
if(NOT trace_content MATCHES "\"key\":\"command\",\"value\":\"emit-summary\"")
    message(FATAL_ERROR "trace export missing command attribute\n${trace_content}")
endif()
if(NOT trace_content MATCHES "\"key\":\"exit_code\",\"value\":\"0\"")
    message(FATAL_ERROR "trace export missing success exit_code attribute\n${trace_content}")
endif()

file(READ "${metrics_file}" metrics_content)
if(NOT metrics_content MATCHES "\"name\":\"ahfl\\.cli\\.duration_ms\"")
    message(FATAL_ERROR "metrics export missing duration metric\n${metrics_content}")
endif()
if(NOT metrics_content MATCHES "\"name\":\"ahfl\\.cli\\.exit_code\",\"value\":0")
    message(FATAL_ERROR "metrics export missing success exit_code metric\n${metrics_content}")
endif()
if(NOT metrics_content MATCHES "\"command\":\"emit-summary\"")
    message(FATAL_ERROR "metrics export missing command label\n${metrics_content}")
endif()

file(READ "${log_file}" log_content)
if(NOT log_content MATCHES "\"level\":\"INFO\"")
    message(FATAL_ERROR "structured log missing INFO level\n${log_content}")
endif()
if(NOT log_content MATCHES "\"message\":\"ahflc command completed\"")
    message(FATAL_ERROR "structured log missing completion message\n${log_content}")
endif()
if(NOT log_content MATCHES "\"command\":\"emit-summary\"")
    message(FATAL_ERROR "structured log missing command field\n${log_content}")
endif()
if(NOT log_content MATCHES "\"exit_code\":\"0\"")
    message(FATAL_ERROR "structured log missing success exit_code field\n${log_content}")
endif()
if(NOT log_content MATCHES "\"duration_ms\":\"")
    message(FATAL_ERROR "structured log missing duration_ms field\n${log_content}")
endif()

file(READ "${memory_file}" memory_content)
if(NOT memory_content MATCHES "\"schema\":\"ahfl\\.memory_report\\.v0\"")
    message(FATAL_ERROR "memory report missing schema\n${memory_content}")
endif()
if(NOT memory_content MATCHES "\"command\":\"emit-summary\"")
    message(FATAL_ERROR "memory report missing command field\n${memory_content}")
endif()
if(NOT memory_content MATCHES "\"exit_code\":0")
    message(FATAL_ERROR "memory report missing success exit_code\n${memory_content}")
endif()
if(NOT memory_content MATCHES "\"available\":true")
    message(FATAL_ERROR "memory report missing available=true\n${memory_content}")
endif()
if(NOT memory_content MATCHES "\"source_bytes\":[1-9][0-9]*")
    message(FATAL_ERROR "memory report missing source_bytes\n${memory_content}")
endif()
if(NOT memory_content MATCHES "\"proxy_peak_bytes\":[1-9][0-9]*")
    message(FATAL_ERROR "memory report missing proxy_peak_bytes\n${memory_content}")
endif()

message(STATUS "trace export:\n${trace_content}\nmetrics export:\n${metrics_content}\nstructured log:\n${log_content}\nmemory report:\n${memory_content}")
