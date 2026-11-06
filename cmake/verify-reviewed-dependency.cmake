set(KH3AP_REVIEWED_JSON_COMMIT "9cca280a4d0ccf0c08f47a99aa71d1b0e52f8d03")
# Includes the reviewed Vorbis allocation/overflow and buffer-clamp fixes from
# https://github.com/nothings/stb/issues/1928 (immutable development revision).
set(KH3AP_REVIEWED_STB_COMMIT "790a3a1d644013d4f6a2baa8787a388b7468b89b")

function(kh3ap_verify_reviewed_dependency name source revision)
    find_package(Git REQUIRED)
    execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${source}" rev-parse HEAD
        RESULT_VARIABLE revision_result OUTPUT_VARIABLE actual_revision
        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(NOT revision_result EQUAL 0 OR NOT actual_revision STREQUAL revision)
        message(FATAL_ERROR
            "${name} must be a Git checkout at reviewed revision ${revision}; "
            "found '${actual_revision}' in ${source}. Restore the reviewed dependency before building.")
    endif()
    execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${source}" status --porcelain --untracked-files=no
        RESULT_VARIABLE status_result OUTPUT_VARIABLE tracked_changes
        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(NOT status_result EQUAL 0 OR tracked_changes)
        message(FATAL_ERROR "${name} has tracked local changes or could not be inspected: ${source}")
    endif()
endfunction()

# The standalone PowerShell test hosts use the same pin and verification path.
if(DEFINED KH3AP_VERIFY_JSON_SOURCE)
    kh3ap_verify_reviewed_dependency("nlohmann/json" "${KH3AP_VERIFY_JSON_SOURCE}"
        "${KH3AP_REVIEWED_JSON_COMMIT}")
endif()
