# The runtime and standalone audio tests compile the same reviewed decoder.
include(FetchContent)
include("${CMAKE_CURRENT_LIST_DIR}/verify-reviewed-dependency.cmake")
set(KH3AP_PINNED_STB_REPOSITORY "https://github.com/nothings/stb.git")
set(KH3AP_PINNED_STB_COMMIT "${KH3AP_REVIEWED_STB_COMMIT}")
FetchContent_Declare(stb
    GIT_REPOSITORY "${KH3AP_PINNED_STB_REPOSITORY}"
    GIT_TAG "${KH3AP_PINNED_STB_COMMIT}")
FetchContent_MakeAvailable(stb)
kh3ap_verify_reviewed_dependency("stb" "${stb_SOURCE_DIR}" "${KH3AP_PINNED_STB_COMMIT}")
if(NOT TARGET kh3ap_vorbis)
    # Keep vendor diagnostics separate from the warning policy for our wrapper.
    add_library(kh3ap_vorbis STATIC "${stb_SOURCE_DIR}/stb_vorbis.c")
    set_source_files_properties("${stb_SOURCE_DIR}/stb_vorbis.c" PROPERTIES LANGUAGE CXX)
    target_compile_definitions(kh3ap_vorbis PRIVATE STB_VORBIS_NO_STDIO STB_VORBIS_NO_PUSHDATA_API)
    target_include_directories(kh3ap_vorbis SYSTEM INTERFACE "${stb_SOURCE_DIR}")
    if(MSVC)
        target_compile_options(kh3ap_vorbis PRIVATE /w)
    else()
        target_compile_options(kh3ap_vorbis PRIVATE -w)
    endif()
endif()
