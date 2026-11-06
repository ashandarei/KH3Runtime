include("${CMAKE_CURRENT_LIST_DIR}/write-if-different.cmake")
find_package(Git REQUIRED)
execute_process(
    COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
    WORKING_DIRECTORY "${UE4SS_SRC}"
    RESULT_VARIABLE _ue4ss_revision_result
    OUTPUT_VARIABLE _ue4ss_revision
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
if(NOT _ue4ss_revision_result EQUAL 0)
    message(FATAL_ERROR "UE4SS_SRC is not a Git checkout, so its pinned revision cannot be verified")
endif()

if(NOT "${_ue4ss_revision}" STREQUAL "${KH3AP_PINNED_UE4SS_COMMIT}")
    if(KH3AP_ALLOW_UNPINNED_UE4SS)
        message(WARNING
            "Using unpinned UE4SS revision ${_ue4ss_revision}; "
            "release builds require ${KH3AP_PINNED_UE4SS_COMMIT}")
    else()
        message(FATAL_ERROR
            "UE4SS_SRC is at ${_ue4ss_revision}; expected reviewed ue4ss-goa revision "
            "${KH3AP_PINNED_UE4SS_COMMIT}. Set KH3AP_ALLOW_UNPINNED_UE4SS=ON only for local research.")
    endif()
else()
    message(STATUS
        "Verified ue4ss-goa revision ${KH3AP_PINNED_UE4SS_COMMIT} "
        "(${KH3AP_PINNED_UE4SS_REPOSITORY})")
endif()

execute_process(
    COMMAND "${GIT_EXECUTABLE}" status --porcelain --untracked-files=no
    WORKING_DIRECTORY "${UE4SS_SRC}"
    RESULT_VARIABLE _ue4ss_status_result
    OUTPUT_VARIABLE _ue4ss_status
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
if(NOT _ue4ss_status_result EQUAL 0)
    message(FATAL_ERROR "Unable to inspect the ue4ss-goa dependency worktree")
endif()
if(_ue4ss_status)
    if(KH3AP_ALLOW_UNPINNED_UE4SS)
        message(WARNING "ue4ss-goa has tracked local changes; this is not a release build")
    else()
        message(FATAL_ERROR
            "ue4ss-goa has tracked local changes. Release builds require the clean pinned checkout; "
            "set KH3AP_ALLOW_UNPINNED_UE4SS=ON only for local research.")
    endif()
endif()

execute_process(
    COMMAND "${GIT_EXECUTABLE}" submodule status --recursive
    WORKING_DIRECTORY "${UE4SS_SRC}"
    RESULT_VARIABLE _ue4ss_submodule_result
    OUTPUT_VARIABLE _ue4ss_submodule_status
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
if(NOT _ue4ss_submodule_result EQUAL 0 OR _ue4ss_submodule_status MATCHES "(^|\n)[-+U]")
    message(FATAL_ERROR
        "ue4ss-goa submodules are missing or do not match the pinned revision. Run "
        "git -C \"${UE4SS_SRC}\" submodule update --init --recursive")
endif()

set(_dirty false)
if(_ue4ss_status)
    set(_dirty true)
endif()
set(_research false)
if(KH3AP_ALLOW_UNPINNED_UE4SS)
    set(_research true)
endif()
set(_release_eligible false)
if(NOT _dirty AND NOT _research AND _ue4ss_revision STREQUAL KH3AP_PINNED_UE4SS_COMMIT)
    set(_release_eligible true)
endif()
if(DEFINED PROVENANCE_FILE)
    kh3ap_write_if_different("${PROVENANCE_FILE}"
        "{\n  \"ue4ss_revision\": \"${_ue4ss_revision}\",\n  \"ue4ss_dirty\": ${_dirty},\n  \"allow_unpinned_ue4ss\": ${_research},\n  \"release_eligible\": ${_release_eligible}\n}\n")
endif()
