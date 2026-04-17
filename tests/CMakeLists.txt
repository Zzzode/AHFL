set(AHFL_EXAMPLES_DIR "${PROJECT_SOURCE_DIR}/examples")
set(AHFL_TESTS_DIR "${PROJECT_SOURCE_DIR}/tests")

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
include("${CMAKE_CURRENT_LIST_DIR}/cmake/LabelTests.cmake")
