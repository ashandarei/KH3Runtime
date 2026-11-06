if(NOT DEFINED VERIFY_MODULE OR NOT DEFINED JSON_SOURCE OR NOT DEFINED TEST_ROOT)
    message(FATAL_ERROR "Dependency test requires VERIFY_MODULE, JSON_SOURCE and TEST_ROOT")
endif()
include("${VERIFY_MODULE}")
find_package(Git REQUIRED)
string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef suffix)
set(fixture "${TEST_ROOT}/dependency-${suffix}")
file(MAKE_DIRECTORY "${fixture}")
set(child "${fixture}/verify.cmake")
file(WRITE "${child}" [=[
include("${VERIFY_MODULE}")
kh3ap_verify_reviewed_dependency("test dependency" "${SOURCE}" "${REVISION}")
]=])

function(check_dependency source revision expected_result expected_message)
    execute_process(COMMAND "${CMAKE_COMMAND}" "-DVERIFY_MODULE=${VERIFY_MODULE}"
        "-DSOURCE=${source}" "-DREVISION=${revision}" -P "${child}"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors)
    if(expected_result STREQUAL "pass")
        if(NOT result EQUAL 0)
            message(FATAL_ERROR "Clean reviewed dependency was rejected: ${output}${errors}")
        endif()
    elseif(result EQUAL 0 OR NOT "${output}${errors}" MATCHES "${expected_message}")
        message(FATAL_ERROR "Invalid dependency was not rejected as expected: ${output}${errors}")
    endif()
endfunction()

check_dependency("${JSON_SOURCE}" "${KH3AP_REVIEWED_JSON_COMMIT}" pass "")
check_dependency("${JSON_SOURCE}" "0000000000000000000000000000000000000000" fail "reviewed revision")
check_dependency("${fixture}/missing" "${KH3AP_REVIEWED_JSON_COMMIT}" fail "reviewed revision")
execute_process(COMMAND "${GIT_EXECUTABLE}" clone --shared --sparse --quiet
    "${JSON_SOURCE}" "${fixture}/dirty"
    RESULT_VARIABLE clone_result OUTPUT_QUIET ERROR_VARIABLE clone_errors)
if(NOT clone_result EQUAL 0)
    message(FATAL_ERROR "Could not prepare isolated dependency fixture: ${clone_errors}")
endif()
check_dependency("${fixture}/dirty" "${KH3AP_REVIEWED_JSON_COMMIT}" pass "")
file(APPEND "${fixture}/dirty/CMakeLists.txt" "\n# isolated tracked-change test\n")
check_dependency("${fixture}/dirty" "${KH3AP_REVIEWED_JSON_COMMIT}" fail "tracked local changes")
