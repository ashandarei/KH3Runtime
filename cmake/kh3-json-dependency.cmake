# The probe uses one pinned decoder for bridge requests, native workers and
# checkpoints, including complete JSON validation and Unicode surrogate pairs.
if(NOT TARGET kh3ap_json_headers)
    include("${CMAKE_CURRENT_LIST_DIR}/verify-reviewed-dependency.cmake")
    set(_kh3ap_json_source "${CMAKE_CURRENT_LIST_DIR}/../build-json-deps/nlohmann_json-src")
    if(NOT EXISTS "${_kh3ap_json_source}/include/nlohmann/json.hpp")
        include(FetchContent)
        FetchContent_Declare(nlohmann_json
            GIT_REPOSITORY https://github.com/nlohmann/json.git
            GIT_TAG "${KH3AP_REVIEWED_JSON_COMMIT}"
            SOURCE_DIR "${_kh3ap_json_source}")
        set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(nlohmann_json)
    endif()
    kh3ap_verify_reviewed_dependency("nlohmann/json" "${_kh3ap_json_source}"
        "${KH3AP_REVIEWED_JSON_COMMIT}")
    add_library(kh3ap_json_headers INTERFACE)
    target_include_directories(kh3ap_json_headers SYSTEM INTERFACE "${_kh3ap_json_source}/include")
    target_compile_features(kh3ap_json_headers INTERFACE cxx_std_20)
endif()
