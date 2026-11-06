# Normal builds validate this project's checked-in data and authored resources.
find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter)
get_filename_component(KH3AP_NATIVE_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(_native_checkers
    "scripts/generate_catalogs.py"
    "authoring/kh02/runtime_metadata.py"
    "authoring/kh02/asset_compatibility.py"
    "authoring/kh02/profiles.py"
    "authoring/kh3/generate_character_profiles.py"
    "authoring/kh3/map_overlay/build_chest_icon.py"
    "authoring/kh3/now_playing/build_movie.py"
    "authoring/kh3/movie_resources.py")
foreach(_checker IN LISTS _native_checkers)
    execute_process(COMMAND "${Python3_EXECUTABLE}" "${KH3AP_NATIVE_ROOT}/${_checker}" --check
        COMMAND_ERROR_IS_FATAL ANY)
endforeach()
file(GLOB_RECURSE _native_authored_inputs CONFIGURE_DEPENDS
    "${KH3AP_NATIVE_ROOT}/authoring/*.py"
    "${KH3AP_NATIVE_ROOT}/authoring/*.json"
    "${KH3AP_NATIVE_ROOT}/authoring/*.as"
    "${KH3AP_NATIVE_ROOT}/authoring/*.png"
    "${KH3AP_NATIVE_ROOT}/authoring/*.abc"
    "${KH3AP_NATIVE_ROOT}/authoring/*.swf")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${_native_authored_inputs}
    "${KH3AP_NATIVE_ROOT}/scripts/generate_catalogs.py"
    "${KH3AP_NATIVE_ROOT}/scripts/runtime_ui_catalogs.py"
    "${KH3AP_NATIVE_ROOT}/data/runtime-inputs.json")
