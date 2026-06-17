if(NOT DEFINED AHFLC)
    message(FATAL_ERROR "AHFLC is required")
endif()

if(NOT DEFINED INPUT_FILE)
    message(FATAL_ERROR "INPUT_FILE is required")
endif()

function(run_ahflc output_var)
    execute_process(
        COMMAND "${AHFLC}" ${ARGN}
        RESULT_VARIABLE result_code
        OUTPUT_VARIABLE command_output
        ERROR_VARIABLE command_error
    )
    if(NOT result_code EQUAL 0)
        message(FATAL_ERROR
            "command failed with exit code ${result_code}: ${ARGN}\n"
            "stderr:\n${command_error}"
        )
    endif()
    set(${output_var} "${command_output}" PARENT_SCOPE)
endfunction()

function(require_contains haystack needle description)
    string(FIND "${haystack}" "${needle}" needle_position)
    if(needle_position EQUAL -1)
        message(FATAL_ERROR "expected ${description} to contain:\n${needle}")
    endif()
endfunction()

function(require_not_contains haystack needle description)
    string(FIND "${haystack}" "${needle}" needle_position)
    if(NOT needle_position EQUAL -1)
        message(FATAL_ERROR "expected ${description} not to contain:\n${needle}")
    endif()
endfunction()

run_ahflc(ir_before emit ir "${INPUT_FILE}")
run_ahflc(ir_after emit ir -O "${INPUT_FILE}")

require_contains(
    "${ir_before}"
    "requires: (true && ir::pass_productization::ready(input.value))"
    "unoptimized IR"
)
require_contains(
    "${ir_after}"
    "requires: ir::pass_productization::ready(input.value)"
    "optimized IR"
)
require_not_contains(
    "${ir_after}"
    "requires: (true && ir::pass_productization::ready(input.value))"
    "optimized IR"
)
require_contains(
    "${ir_before}"
    "safety: G(G((!(running(run)) || completed(run))))"
    "unoptimized IR"
)
require_contains(
    "${ir_after}"
    "safety: G((!(running(run)) || completed(run)))"
    "optimized IR"
)

run_ahflc(smv_before emit smv "${INPUT_FILE}")
run_ahflc(smv_after emit smv -O "${INPUT_FILE}")

require_contains(
    "${smv_before}"
    "LTLSPEC G (G (agent__ir_pass_productization_PassAgent__called__ir_pass_productization_Decide))"
    "unoptimized SMV"
)
require_contains(
    "${smv_after}"
    "LTLSPEC G (agent__ir_pass_productization_PassAgent__called__ir_pass_productization_Decide)"
    "optimized SMV"
)
require_not_contains(
    "${smv_after}"
    "LTLSPEC G (G (agent__ir_pass_productization_PassAgent__called__ir_pass_productization_Decide))"
    "optimized SMV"
)
require_contains(
    "${smv_before}"
    "G (G ((! (workflow__ir_pass_productization_PassWorkflow__node__run__running) | workflow__ir_pass_productization_PassWorkflow__node__run__completed)))"
    "unoptimized SMV"
)
require_contains(
    "${smv_after}"
    "G ((! (workflow__ir_pass_productization_PassWorkflow__node__run__running) | workflow__ir_pass_productization_PassWorkflow__node__run__completed))"
    "optimized SMV"
)
