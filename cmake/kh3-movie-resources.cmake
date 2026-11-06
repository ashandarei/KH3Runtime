# Share the real Windows resource packaging between the DLL and decoder tests.
include_guard(GLOBAL)
if(WIN32)
    enable_language(RC)
endif()

function(kh3ap_attach_movie_resources target probe_root)
    if(WIN32)
        get_filename_component(KH3AP_MOVIE_RESOURCE_ROOT "${probe_root}" ABSOLUTE)
        set(_rc "${CMAKE_CURRENT_BINARY_DIR}/${target}_movie_resources.rc")
        configure_file("${probe_root}/resources/movie_resources.rc.in" "${_rc}" @ONLY)
        set_property(SOURCE "${_rc}" PROPERTY OBJECT_DEPENDS
            "${probe_root}/resources/now_playing_movie.swf"
            "${probe_root}/resources/map_chest_movie.swf"
            "${probe_root}/resources/kh02/kh02_movie_surface.swf"
            "${probe_root}/resources/shop_movie_patch_egs.bin"
            "${probe_root}/resources/shop_movie_patch_steam.bin"
            "${probe_root}/resources/ingredient_journal_patch.bin")
        target_sources(${target} PRIVATE "${_rc}" "${probe_root}/kh3ap/core/ui/embedded_movie_resource.cpp")
    else()
        # Portable decoder tests use the same committed binaries from disk.
        # Shipping DLLs use the Windows branch, with no external file reads.
        target_sources(${target} PRIVATE "${probe_root}/../tests/movie_resource_fixture.cpp")
        target_compile_definitions(${target} PRIVATE KH3AP_TEST_MOVIE_RESOURCE_DIR="${probe_root}/resources")
    endif()
endfunction()
