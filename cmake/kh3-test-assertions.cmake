# Include after defining all executables in a standalone test directory.
# Keep this policy off third-party libraries and the production DLL.
get_property(_kh3ap_assertion_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
set(_kh3ap_assertion_header "${CMAKE_CURRENT_LIST_DIR}/../tests/assertions_enabled.h")
foreach(_target IN LISTS _kh3ap_assertion_targets)
    get_target_property(_type ${_target} TYPE)
    if(_type STREQUAL "EXECUTABLE")
        if(MSVC)
            target_compile_options(${_target} PRIVATE /UNDEBUG "/FI${_kh3ap_assertion_header}")
        else()
            target_compile_options(${_target} PRIVATE -UNDEBUG
                "SHELL:-include \"${_kh3ap_assertion_header}\"")
        endif()
    endif()
endforeach()
