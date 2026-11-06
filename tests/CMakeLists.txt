cmake_minimum_required(VERSION 3.20)
project(kh3_randomizer_regressions LANGUAGES CXX)
enable_testing()
add_executable(character_unlocks_test character_unlocks_test.cpp)
target_include_directories(character_unlocks_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../KH3APProbeCpp")
target_compile_features(character_unlocks_test PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(character_unlocks_test PRIVATE /UNDEBUG)
else()
    target_compile_options(character_unlocks_test PRIVATE -UNDEBUG)
endif()
add_test(NAME character_unlocks COMMAND character_unlocks_test)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(space_idle_source "${CMAKE_CURRENT_BINARY_DIR}/space_idle_under_test.h")
add_custom_command(OUTPUT "${space_idle_source}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/generate_space_idle_test.py" "${space_idle_source}"
    DEPENDS generate_space_idle_test.py "${CMAKE_CURRENT_SOURCE_DIR}/../KH3APProbeCpp/kh3ap/features/world/space/space_service.cpp"
    VERBATIM)

set(KH3AP_PROBE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../KH3APProbeCpp")
set(KH3AP_ROOT "${KH3AP_PROBE_ROOT}/kh3ap")
foreach(mode IN ITEMS 0 1)
    add_executable(context_capabilities_${mode}_test context_capabilities_test.cpp
        "${KH3AP_ROOT}/bridge/serialization/payload_serializer.cpp")
    target_include_directories(context_capabilities_${mode}_test PRIVATE "${KH3AP_PROBE_ROOT}")
    target_compile_features(context_capabilities_${mode}_test PRIVATE cxx_std_20)
    target_compile_definitions(context_capabilities_${mode}_test PRIVATE KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS=${mode})
    if(MSVC)
        target_compile_options(context_capabilities_${mode}_test PRIVATE /UNDEBUG)
    else()
        target_compile_options(context_capabilities_${mode}_test PRIVATE -UNDEBUG)
    endif()
    add_test(NAME context_capabilities_${mode} COMMAND context_capabilities_${mode}_test)
    add_test(NAME player_menus_build_mode_${mode}
        COMMAND "${CMAKE_COMMAND}" "-DCOMPILER=${CMAKE_CXX_COMPILER}" "-DMSVC=${MSVC}"
            "-DMODE=${mode}" "-DSOURCE=${KH3AP_ROOT}/features/world/space/save_space_service.cpp"
            "-DMAP_SOURCE=${KH3AP_ROOT}/app/composition/menus_wiring.cpp"
            "-DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/save_space_mode_${mode}.cpp"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/test-save-space-build-mode.cmake")
endforeach()
add_executable(bounded_read_test bounded_read_test.cpp)
target_include_directories(bounded_read_test PRIVATE "${KH3AP_PROBE_ROOT}")
target_compile_features(bounded_read_test PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(bounded_read_test PRIVATE /W4 /WX /UNDEBUG)
else()
    target_compile_options(bounded_read_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
endif()
add_test(NAME bounded_read COMMAND bounded_read_test)
set(KH3AP_MUSIC "${KH3AP_ROOT}/features/music")
if(WIN32)
    include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/kh3-audio-dependency.cmake")
    add_executable(custom_audio_test custom_audio_test.cpp
        "${KH3AP_MUSIC}/audio/custom_audio.cpp" "${KH3AP_MUSIC}/audio/ogg_decoder.cpp")
    target_include_directories(custom_audio_test PRIVATE "${KH3AP_PROBE_ROOT}")
    target_include_directories(custom_audio_test SYSTEM PRIVATE "${stb_SOURCE_DIR}")
    target_compile_features(custom_audio_test PRIVATE cxx_std_20)
    target_link_libraries(custom_audio_test PRIVATE kh3ap_vorbis)
    add_executable(pipe_security_test pipe_security_test.cpp)
    target_include_directories(pipe_security_test PRIVATE "${KH3AP_PROBE_ROOT}")
    target_compile_features(pipe_security_test PRIVATE cxx_std_20)
    target_link_libraries(pipe_security_test PRIVATE advapi32)
    add_executable(revision_hooks_test revision_hooks_test.cpp)
    target_include_directories(revision_hooks_test PRIVATE
        native_revision_stubs native_wake_stubs "${KH3AP_PROBE_ROOT}")
    target_compile_features(revision_hooks_test PRIVATE cxx_std_20)
    add_executable(kh02_capture_path_test kh02_capture_path_test.cpp)
    target_include_directories(kh02_capture_path_test PRIVATE "${KH3AP_PROBE_ROOT}")
    target_compile_features(kh02_capture_path_test PRIVATE cxx_std_20)
    foreach(_security_target IN ITEMS custom_audio_test pipe_security_test revision_hooks_test kh02_capture_path_test)
        if(MSVC)
            target_compile_options(${_security_target} PRIVATE /W4 /WX /UNDEBUG /utf-8)
        else()
            target_compile_options(${_security_target} PRIVATE -Wall -Wextra -Werror -UNDEBUG)
        endif()
        add_test(NAME ${_security_target} COMMAND ${_security_target})
    endforeach()
endif()
set(KH3AP_COLORS "${KH3AP_ROOT}/features/colors")
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
foreach(settings_mode IN ITEMS 0 1)
    add_executable(settings_header_${settings_mode}_test
        settings_header_test.cpp settings_header_storage_fixture.cpp)
    target_include_directories(settings_header_${settings_mode}_test PRIVATE "${KH3AP_PROBE_ROOT}")
    target_compile_definitions(settings_header_${settings_mode}_test PRIVATE
        KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS=${settings_mode})
    add_test(NAME settings_header_${settings_mode} COMMAND settings_header_${settings_mode}_test)
endforeach()
add_executable(options_callback_state_test options_callback_state_test.cpp)
target_include_directories(options_callback_state_test PRIVATE "${KH3AP_PROBE_ROOT}")
add_executable(wake_hook_test wake_hook_test.cpp)
target_include_directories(wake_hook_test PRIVATE native_wake_stubs "${KH3AP_PROBE_ROOT}")
foreach(_callback_test IN ITEMS options_callback_state_test wake_hook_test)
    if(MSVC)
        target_compile_options(${_callback_test} PRIVATE /UNDEBUG /EHsc)
    else()
        target_compile_options(${_callback_test} PRIVATE -UNDEBUG)
    endif()
endforeach()
add_test(NAME options_callback_state COMMAND options_callback_state_test)
add_test(NAME wake_hook COMMAND wake_hook_test)
add_executable(character_profile_test character_profile_test.cpp)
target_include_directories(character_profile_test PRIVATE "${KH3AP_PROBE_ROOT}")
if(MSVC)
    target_compile_options(character_profile_test PRIVATE /UNDEBUG)
else()
    target_compile_options(character_profile_test PRIVATE -UNDEBUG)
endif()
add_test(NAME character_profile COMMAND character_profile_test)
add_test(NAME character_native_table_current
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/../authoring/kh3/generate_character_profiles.py" --check)
if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    add_executable(character_record_lookup_test character_record_lookup_test.cpp)
    target_include_directories(character_record_lookup_test PRIVATE "${KH3AP_PROBE_ROOT}")
    # The executable contract assertions must also run in Release builds.
    if(MSVC)
        target_compile_options(character_record_lookup_test PRIVATE /UNDEBUG)
    endif()
    add_test(NAME character_record_lookup COMMAND character_record_lookup_test)
endif()
add_executable(gate_opener_test gate_opener_test.cpp)
target_include_directories(gate_opener_test PRIVATE "${KH3AP_PROBE_ROOT}")
add_test(NAME gate_opener COMMAND gate_opener_test)
include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/kh3-json-dependency.cmake")
add_executable(kh02_asset_index_test kh02_asset_index_test.cpp)
target_include_directories(kh02_asset_index_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(kh02_asset_index_test PRIVATE kh3ap_json_headers)
add_test(NAME kh02_asset_index COMMAND kh02_asset_index_test)
if(WIN32)
    foreach(_target IN ITEMS kh02_package_adapter_test kh02_package_adapter_cli kh02_asset_provider_smoke)
        add_executable(${_target} ${_target}.cpp
            "${KH3AP_ROOT}/features/kh02/transport/kh02_package_adapter.cpp"
            "${KH3AP_ROOT}/features/kh02/transport/kh02_package_cache.cpp"
            "${KH3AP_ROOT}/features/kh02/transport/kh02_index_state.cpp")
        target_include_directories(${_target} PRIVATE "${KH3AP_PROBE_ROOT}"
            "${CMAKE_CURRENT_SOURCE_DIR}/../include" "${CMAKE_CURRENT_SOURCE_DIR}/..")
        target_link_libraries(${_target} PRIVATE kh3ap_json_headers bcrypt)
        target_compile_options(${_target} PRIVATE /UNDEBUG)
    endforeach()
    target_sources(kh02_asset_provider_smoke PRIVATE
        "${KH3AP_ROOT}/features/kh02/transport/kh02_companion_process.cpp"
        "${KH3AP_ROOT}/features/kh02/transport/kh02_remote_pipe_transport.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../shared/kh02/kh02_control_pipe.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../shared/kh02/kh02_executable_verification.cpp")
    target_link_libraries(kh02_asset_provider_smoke PRIVATE ole32 advapi32 user32)
    target_compile_definitions(kh02_asset_provider_smoke PRIVATE KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS=1)
    add_test(NAME kh02_package_adapter COMMAND kh02_package_adapter_test)
endif()
if(MSVC)
    target_compile_options(kh02_asset_index_test PRIVATE /UNDEBUG)
else()
    target_compile_options(kh02_asset_index_test PRIVATE -UNDEBUG)
endif()
add_test(NAME reviewed_dependency
    COMMAND "${CMAKE_COMMAND}"
        "-DVERIFY_MODULE=${CMAKE_CURRENT_SOURCE_DIR}/../cmake/verify-reviewed-dependency.cmake"
        "-DJSON_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/../build-json-deps/nlohmann_json-src"
        "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/test-reviewed-dependency.cmake")

if(MSVC)
    add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:/W4>" "$<$<COMPILE_LANGUAGE:CXX>:/utf-8>"
        "$<$<COMPILE_LANGUAGE:CXX>:/UNDEBUG>")
else()
    add_compile_options(-Wall -Wextra -UNDEBUG)
endif()

set(map_ready_source "${CMAKE_CURRENT_BINARY_DIR}/map_ready_under_test.h" "${CMAKE_CURRENT_BINARY_DIR}/ability_ready_under_test.h")
add_custom_command(OUTPUT ${map_ready_source}
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/generate_map_ready_test.py" "${CMAKE_CURRENT_BINARY_DIR}/map_ready_under_test.h"
    DEPENDS generate_map_ready_test.py "${KH3AP_ROOT}/features/player/context/player_context_readiness.cpp" "${KH3AP_ROOT}/features/player/controller_abilities/refresh.cpp"
    VERBATIM)
add_executable(map_ready_test map_ready_test.cpp ${map_ready_source})
target_include_directories(map_ready_test PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${KH3AP_PROBE_ROOT}")
add_test(NAME map_ready COMMAND map_ready_test)

set(flag_recovery_sources "${CMAKE_CURRENT_BINARY_DIR}/vbonus_recovery_under_test.h" "${CMAKE_CURRENT_BINARY_DIR}/event_recovery_under_test.h")
add_custom_command(OUTPUT ${flag_recovery_sources}
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/generate_flag_recovery_test.py" "${CMAKE_CURRENT_BINARY_DIR}"
    DEPENDS generate_flag_recovery_test.py
        "${KH3AP_ROOT}/features/story/progression/vbonus_tracking_service.cpp"
        "${KH3AP_ROOT}/features/story/progression/event_tracking_service.cpp"
    VERBATIM)
add_executable(flag_recovery_test flag_recovery_test.cpp ${flag_recovery_sources})
target_include_directories(flag_recovery_test PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
add_test(NAME flag_recovery COMMAND flag_recovery_test)

add_executable(tutorial_lookup_test tutorial_lookup_test.cpp
    "${KH3AP_ROOT}/features/tutorials/tutorial_function_lookup.cpp")
target_include_directories(tutorial_lookup_test PRIVATE function_lookup_stubs "${KH3AP_PROBE_ROOT}")
add_test(NAME tutorial_lookup COMMAND tutorial_lookup_test)

add_executable(tutorial_patch_batch_test tutorial_patch_batch_test.cpp)
target_include_directories(tutorial_patch_batch_test PRIVATE "${KH3AP_PROBE_ROOT}")
add_test(NAME tutorial_patch_batch COMMAND tutorial_patch_batch_test)

add_executable(treasure_retry_test treasure_retry_test.cpp
    "${KH3AP_ROOT}/features/treasure/treasure_retry_queues.cpp")
target_include_directories(treasure_retry_test PRIVATE function_lookup_stubs "${KH3AP_PROBE_ROOT}")
add_test(NAME treasure_retry COMMAND treasure_retry_test)

if(WIN32)
    add_executable(portal_result_writer_test portal_result_writer_test.cpp
        "${KH3AP_ROOT}/features/world/portals/portal_result_writer.cpp"
        "${KH3AP_ROOT}/bridge/serialization/json.cpp"
        "${KH3AP_ROOT}/bridge/utilities.cpp")
    target_include_directories(portal_result_writer_test PRIVATE "${KH3AP_PROBE_ROOT}")
    target_link_libraries(portal_result_writer_test PRIVATE kh3ap_json_headers)
    add_test(NAME portal_result_writer COMMAND portal_result_writer_test)
endif()

set(remaining_outputs)
foreach(name workshop party color_discovery color_weapons coordinates journal_level_actors)
    list(APPEND remaining_outputs "${CMAKE_CURRENT_BINARY_DIR}/remaining/${name}_remaining_under_test.h")
endforeach()
add_custom_command(OUTPUT ${remaining_outputs}
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/generate_remaining_events_test.py" "${CMAKE_CURRENT_BINARY_DIR}/remaining"
    DEPENDS generate_remaining_events_test.py
        "${KH3AP_ROOT}/features/moogle_shop/workshop_service.cpp"
        "${KH3AP_ROOT}/features/party/party_reconciliation.cpp"
        "${KH3AP_COLORS}/service/discovery.cpp"
        "${KH3AP_COLORS}/service/links.cpp"
        "${KH3AP_ROOT}/features/journal/ingredient_journal_map_service.cpp"
    VERBATIM)
add_executable(remaining_events_test remaining_events_test.cpp ${remaining_outputs})
target_include_directories(remaining_events_test PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/remaining" "${KH3AP_PROBE_ROOT}")
add_test(NAME remaining_events COMMAND remaining_events_test)
add_executable(journal_level_actors_test journal_level_actors_test.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/remaining/journal_level_actors_remaining_under_test.h")
target_include_directories(journal_level_actors_test PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/remaining")
if(MSVC)
    target_compile_options(journal_level_actors_test PRIVATE /UNDEBUG)
else()
    target_compile_options(journal_level_actors_test PRIVATE -UNDEBUG)
endif()
add_test(NAME journal_level_actors COMMAND journal_level_actors_test)
add_executable(revision_stub_test revision_stub_test.cpp)
target_include_directories(revision_stub_test PRIVATE "${KH3AP_PROBE_ROOT}")
add_test(NAME revision_stub COMMAND revision_stub_test)

add_executable(space_idle_test space_idle_test.cpp "${space_idle_source}")
target_include_directories(space_idle_test PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${KH3AP_PROBE_ROOT}")
add_test(NAME space_idle COMMAND space_idle_test)

set(qol_events_source "${CMAKE_CURRENT_BINARY_DIR}/quality_of_life_events_under_test.h")
add_custom_command(OUTPUT "${qol_events_source}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/generate_quality_of_life_events_test.py" "${qol_events_source}"
    DEPENDS generate_quality_of_life_events_test.py "${KH3AP_ROOT}/features/story/quality_of_life_service.cpp"
    VERBATIM)
add_executable(quality_of_life_events_test quality_of_life_events_test.cpp "${qol_events_source}"
    "${KH3AP_ROOT}/bridge/serialization/json.cpp" "${KH3AP_ROOT}/bridge/serialization/json_fields.cpp")
target_include_directories(quality_of_life_events_test PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${KH3AP_PROBE_ROOT}")
target_link_libraries(quality_of_life_events_test PRIVATE kh3ap_json_headers)
add_test(NAME quality_of_life_events COMMAND quality_of_life_events_test)

set(bootstrap_work_source "${CMAKE_CURRENT_BINARY_DIR}/world_bootstrap_work_under_test.h")
set(bootstrap_ui_sources "${CMAKE_CURRENT_BINARY_DIR}/proof_ui_state_under_test.h"
    "${CMAKE_CURRENT_BINARY_DIR}/proof_ui_facade_under_test.h")
add_custom_command(OUTPUT "${bootstrap_work_source}" ${bootstrap_ui_sources}
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/generate_world_bootstrap_work_test.py" "${bootstrap_work_source}"
    DEPENDS generate_world_bootstrap_work_test.py "${KH3AP_ROOT}/features/story/bootstrap/world_bootstrap_service.cpp"
        "${KH3AP_ROOT}/features/story/bootstrap/proof_ui_lifecycle.cpp"
    VERBATIM)
add_executable(world_bootstrap_work_test world_bootstrap_work_test.cpp "${bootstrap_work_source}" ${bootstrap_ui_sources})
target_include_directories(world_bootstrap_work_test PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${KH3AP_PROBE_ROOT}")
add_test(NAME world_bootstrap_work COMMAND world_bootstrap_work_test)

set(chest_maintenance_source "${CMAKE_CURRENT_BINARY_DIR}/chest_maintenance_under_test.h")
add_custom_command(OUTPUT "${chest_maintenance_source}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/generate_chest_maintenance_test.py" "${chest_maintenance_source}"
    DEPENDS generate_chest_maintenance_test.py "${KH3AP_ROOT}/features/treasure/music_chest/music_chest_service.cpp"
        "${KH3AP_ROOT}/features/treasure/music_chest/music_chest_pause.cpp"
        "${KH3AP_ROOT}/features/treasure/music_chest/music_chest.h"
    VERBATIM)
add_executable(chest_maintenance_test chest_maintenance_test.cpp "${chest_maintenance_source}")
target_include_directories(chest_maintenance_test PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${KH3AP_PROBE_ROOT}")
add_test(NAME chest_maintenance COMMAND chest_maintenance_test)

add_executable(tutorial_patch_lifecycle_test tutorial_patch_lifecycle_test.cpp)
target_include_directories(tutorial_patch_lifecycle_test PRIVATE "${KH3AP_PROBE_ROOT}")
add_test(NAME tutorial_patch_lifecycle COMMAND tutorial_patch_lifecycle_test)

set(event_services_headers)
foreach(service beat union journal emblem guardians)
    list(APPEND event_services_headers "${CMAKE_CURRENT_BINARY_DIR}/${service}_events_under_test.h")
endforeach()
add_custom_command(OUTPUT ${event_services_headers}
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/generate_event_services_test.py" "${CMAKE_CURRENT_BINARY_DIR}"
    DEPENDS generate_event_services_test.py
        "${KH3AP_ROOT}/features/story/minigames/beat_of_lead_service.cpp"
        "${KH3AP_ROOT}/features/story/minigames/union_cross_command_service.cpp"
        "${KH3AP_ROOT}/features/journal/ingredient_journal_service.cpp"
        "${KH3AP_ROOT}/features/player/emblems/lucky_emblem_tracker_service.cpp"
        "${KH3AP_ROOT}/features/story/progression/story_progress_service.cpp"
    VERBATIM)
add_executable(event_services_test event_services_test.cpp ${event_services_headers})
target_include_directories(event_services_test PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${KH3AP_PROBE_ROOT}")
target_compile_definitions(event_services_test PRIVATE KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS=0)
add_test(NAME event_services COMMAND event_services_test)

add_executable(garden_return_policy_test garden_return_policy_test.cpp)
add_executable(sead_music_events_test sead_music_events_test.cpp)
add_executable(goa_music_bytecode_test goa_music_bytecode_test.cpp)
target_include_directories(goa_music_bytecode_test PRIVATE "${KH3AP_PROBE_ROOT}")
add_test(NAME goa_music_bytecode COMMAND goa_music_bytecode_test)
set(titan_events_source "${CMAKE_CURRENT_BINARY_DIR}/titan_events_under_test.h")
add_custom_command(OUTPUT "${titan_events_source}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/generate_titan_events_test.py" "${titan_events_source}"
    DEPENDS generate_titan_events_test.py "${KH3AP_ROOT}/features/enemies/titan_arena.cpp" VERBATIM)
add_executable(titan_events_test titan_events_test.cpp "${titan_events_source}")
target_include_directories(titan_events_test PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}" "${KH3AP_PROBE_ROOT}")
add_test(NAME titan_events COMMAND titan_events_test)
add_executable(titan_object_registry_test titan_object_registry_test.cpp "${KH3AP_ROOT}/features/enemies/titan_object_registry.cpp")
target_include_directories(titan_object_registry_test PRIVATE music_registry_stubs "${KH3AP_PROBE_ROOT}")
add_test(NAME titan_object_registry COMMAND titan_object_registry_test)
add_executable(color_object_registry_test color_object_registry_test.cpp "${KH3AP_COLORS}/color_object_registry.cpp")
target_include_directories(color_object_registry_test PRIVATE music_registry_stubs "${KH3AP_PROBE_ROOT}")
add_test(NAME color_object_registry COMMAND color_object_registry_test)
add_executable(music_object_registry_test music_object_registry_test.cpp
    "${KH3AP_MUSIC}/music_object_registry.cpp")
target_include_directories(music_object_registry_test PRIVATE music_registry_stubs "${KH3AP_PROBE_ROOT}")
add_test(NAME music_object_registry COMMAND music_object_registry_test)
target_include_directories(sead_music_events_test PRIVATE "${KH3AP_PROBE_ROOT}")
add_test(NAME sead_music_events COMMAND sead_music_events_test)
target_include_directories(garden_return_policy_test PRIVATE "${KH3AP_PROBE_ROOT}")
add_test(NAME garden_return_policy COMMAND garden_return_policy_test)

add_executable(monstropolis_mapset_test monstropolis_mapset_test.cpp)
add_test(NAME monstropolis_mapset COMMAND monstropolis_mapset_test)

add_executable(battlegate_activation_test battlegate_activation_test.cpp)
add_test(NAME battlegate_activation COMMAND battlegate_activation_test)

add_executable(graveyard_backtracking_test graveyard_backtracking_test.cpp)
add_test(NAME graveyard_backtracking COMMAND graveyard_backtracking_test)

add_executable(actor_discovery_test actor_discovery_test.cpp
    "${KH3AP_ROOT}/core/objects/actor_discovery.cpp"
    "${KH3AP_ROOT}/core/objects/uobject_validity.cpp")
target_include_directories(actor_discovery_test PRIVATE discovery_stubs "${KH3AP_PROBE_ROOT}")
add_test(NAME actor_discovery COMMAND actor_discovery_test)

add_executable(battle_level_functions_test battle_level_functions_test.cpp
    "${KH3AP_ROOT}/features/grants/battle_level/battle_level_functions.cpp"
    "${KH3AP_ROOT}/core/objects/uobject_validity.cpp")
target_include_directories(battle_level_functions_test PRIVATE discovery_stubs
    "${KH3AP_PROBE_ROOT}")
add_test(NAME battle_level_functions COMMAND battle_level_functions_test)

add_executable(reflection_metadata_cache_test reflection_metadata_cache_test.cpp
    "${KH3AP_ROOT}/core/objects/uobject_validity.cpp")
target_include_directories(reflection_metadata_cache_test PRIVATE discovery_stubs "${KH3AP_PROBE_ROOT}")
add_test(NAME reflection_metadata_cache COMMAND reflection_metadata_cache_test)

add_executable(reflection_scalar_test reflection_scalar_test.cpp
    "${KH3AP_ROOT}/core/reflection/reflection_scalar.cpp")
target_include_directories(reflection_scalar_test PRIVATE discovery_stubs "${KH3AP_PROBE_ROOT}")
if(MSVC)
    target_compile_options(reflection_scalar_test PRIVATE /W4 /WX /UNDEBUG)
else()
    target_compile_options(reflection_scalar_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
endif()
add_test(NAME reflection_scalar COMMAND reflection_scalar_test)

add_executable(asset_load_helpers_test asset_load_helpers_test.cpp
    "${KH3AP_ROOT}/core/objects/asset_load_helpers.cpp"
    "${KH3AP_ROOT}/core/objects/uobject_validity.cpp"
    "${KH3AP_ROOT}/features/grants/grant_internal_call_depth.cpp")
target_include_directories(asset_load_helpers_test PRIVATE asset_stubs discovery_stubs
    "${KH3AP_PROBE_ROOT}")
add_test(NAME asset_load_helpers COMMAND asset_load_helpers_test)

add_executable(map_gfx_value_layout_test map_gfx_value_layout_test.cpp)
target_include_directories(map_gfx_value_layout_test PRIVATE "${KH3AP_PROBE_ROOT}")
add_test(NAME map_gfx_value_layout COMMAND map_gfx_value_layout_test)

set(map_gfx_binding_source "${CMAKE_CURRENT_BINARY_DIR}/map_gfx_binding_under_test.h")
add_custom_command(OUTPUT "${map_gfx_binding_source}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/generate_map_gfx_binding_test.py" "${map_gfx_binding_source}"
    DEPENDS generate_map_gfx_binding_test.py "${KH3AP_ROOT}/features/ui/map_overlay/gfx_bindings.h"
    VERBATIM)
add_executable(map_gfx_binding_test map_gfx_binding_test.cpp "${map_gfx_binding_source}"
    "${KH3AP_ROOT}/core/objects/uobject_validity.cpp")
target_include_directories(map_gfx_binding_test PRIVATE discovery_stubs "${KH3AP_PROBE_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(map_gfx_binding_test PRIVATE KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS=0)
add_test(NAME map_gfx_binding COMMAND map_gfx_binding_test)

add_executable(map_overlay_test map_overlay_test.cpp)
add_executable(map_jump_player_start_test map_jump_player_start_test.cpp)
add_test(NAME map_jump_player_start COMMAND map_jump_player_start_test)
add_executable(scene_attachment_test scene_attachment_test.cpp)
add_test(NAME scene_attachment COMMAND scene_attachment_test)
add_test(NAME map_overlay COMMAND map_overlay_test)

add_executable(minimap_layout_cache_test minimap_layout_cache_test.cpp
    "${KH3AP_ROOT}/core/objects/uobject_validity.cpp")
target_include_directories(minimap_layout_cache_test PRIVATE discovery_stubs "${KH3AP_PROBE_ROOT}")
add_test(NAME minimap_layout_cache COMMAND minimap_layout_cache_test)

add_executable(minimap_projection_cache_test minimap_projection_cache_test.cpp)
target_include_directories(minimap_projection_cache_test PRIVATE "${KH3AP_PROBE_ROOT}")
add_test(NAME minimap_projection_cache COMMAND minimap_projection_cache_test)
add_executable(map_marker_position_cache_test map_marker_position_cache_test.cpp)
target_include_directories(map_marker_position_cache_test PRIVATE "${KH3AP_PROBE_ROOT}")
add_test(NAME map_marker_position_cache COMMAND map_marker_position_cache_test)
add_executable(shop_movie_variants_test shop_movie_variants_test.cpp)
target_include_directories(shop_movie_variants_test PRIVATE "${KH3AP_PROBE_ROOT}")
include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/kh3-movie-resources.cmake")
kh3ap_attach_movie_resources(shop_movie_variants_test "${KH3AP_PROBE_ROOT}")
add_test(NAME shop_movie_variants COMMAND shop_movie_variants_test)
if(WIN32)
    add_executable(steam_parity_profile_test steam_parity_profile_test.cpp)
    target_include_directories(steam_parity_profile_test PRIVATE "${KH3AP_PROBE_ROOT}")
    add_test(NAME steam_parity_profile COMMAND steam_parity_profile_test)
endif()

add_library(music_model STATIC
    "${KH3AP_MUSIC}/shuffle/bgm_shuffle_table.cpp"
    "${KH3AP_MUSIC}/shuffle/form_theme.cpp"
    "${KH3AP_MUSIC}/audio/custom_music_library.cpp"
    "${KH3AP_ROOT}/bridge/serialization/json.cpp"
    "${KH3AP_ROOT}/bridge/serialization/json_fields.cpp")
target_include_directories(music_model PUBLIC "${KH3AP_PROBE_ROOT}")
target_link_libraries(music_model PRIVATE kh3ap_json_headers)

add_executable(bridge_json_test bridge_json_test.cpp
    "${KH3AP_ROOT}/bridge/serialization/json.cpp"
    "${KH3AP_ROOT}/bridge/serialization/json_fields.cpp"
    "${KH3AP_ROOT}/bridge/catalog/slot_data_lookup_parser.cpp"
    "${KH3AP_ROOT}/bridge/rewards/vbonus_reward_bundle.cpp"
    "${KH3AP_ROOT}/bridge/serialization/request_parser.cpp"
    "${KH3AP_ROOT}/features/player/context/context_resolver.cpp")
target_include_directories(bridge_json_test PRIVATE "${KH3AP_PROBE_ROOT}")
target_link_libraries(bridge_json_test PRIVATE kh3ap_json_headers)
add_test(NAME bridge_json COMMAND bridge_json_test)

add_executable(remap_catalog_test remap_catalog_test.cpp
    "${KH3AP_ROOT}/features/enemies/remap_catalog.cpp"
    "${KH3AP_ROOT}/bridge/serialization/json.cpp"
    "${KH3AP_ROOT}/bridge/serialization/json_fields.cpp")
target_include_directories(remap_catalog_test PRIVATE hook_stubs "${KH3AP_PROBE_ROOT}")
target_link_libraries(remap_catalog_test PRIVATE kh3ap_json_headers)
add_test(NAME remap_catalog COMMAND remap_catalog_test)

add_executable(production_hook_registry_test production_hook_registry_test.cpp
    "${KH3AP_ROOT}/hooks/production_hook_registry.cpp"
    "${KH3AP_ROOT}/core/objects/uobject_validity.cpp")
target_include_directories(production_hook_registry_test PRIVATE hook_stubs discovery_stubs
    "${KH3AP_PROBE_ROOT}")
target_compile_definitions(production_hook_registry_test PRIVATE KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS=0)
add_test(NAME production_hook_registry COMMAND production_hook_registry_test)
add_library(color_model STATIC "${KH3AP_COLORS}/color_palette.cpp")
target_include_directories(color_model PUBLIC "${KH3AP_PROBE_ROOT}")

foreach(name bgm_shuffle_table form_theme custom_music_library)
    add_executable(${name}_test ${name}_test.cpp)
    target_link_libraries(${name}_test PRIVATE music_model)
    add_test(NAME ${name} COMMAND ${name}_test)
endforeach()
foreach(name color_palette color_timing)
    add_executable(${name}_test ${name}_test.cpp)
    target_link_libraries(${name}_test PRIVATE color_model)
    add_test(NAME ${name} COMMAND ${name}_test)
endforeach()
add_executable(color_apply_state_test color_apply_state_test.cpp "${KH3AP_COLORS}/color_apply_state.cpp")
target_include_directories(color_apply_state_test PRIVATE color_stubs "${KH3AP_PROBE_ROOT}")
add_test(NAME color_apply_state COMMAND color_apply_state_test)

set(generated "${CMAKE_CURRENT_BINARY_DIR}/service_sources")
set(service_outputs
    "${generated}/music_service_under_test.h"
    "${generated}/music_service_under_test.cpp"
    "${generated}/gummi_service_under_test.h")
add_custom_command(OUTPUT ${service_outputs}
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/generate_randomizer_service_tests.py" "${generated}"
    DEPENDS generate_randomizer_service_tests.py
        "${KH3AP_MUSIC}/shuffle/bgm_shuffle_service_impl.h"
        "${KH3AP_MUSIC}/shuffle/bgm_shuffle_service.cpp"
        "${KH3AP_MUSIC}/events/sead_bgm.cpp"
        "${KH3AP_MUSIC}/shuffle/bgm_shuffle_custom.cpp"
        "${KH3AP_MUSIC}/shuffle/bgm_shuffle_forms.cpp"
        "${KH3AP_MUSIC}/shuffle/bgm_shuffle_debug.cpp"
        "${KH3AP_COLORS}/color_gummi.cpp"
    VERBATIM)
add_custom_target(randomizer_service_sources DEPENDS ${service_outputs})
add_executable(music_service_test music_service_test.cpp "${generated}/music_service_under_test.cpp")
add_executable(gummi_service_test gummi_service_test.cpp)
foreach(name music_service gummi_service)
    add_dependencies(${name}_test randomizer_service_sources)
    target_include_directories(${name}_test PRIVATE "${generated}" "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(${name}_test PRIVATE music_model color_model)
    add_test(NAME ${name} COMMAND ${name}_test)
endforeach()

# Keep engine stubs first; production headers use the same module-qualified
# paths as the DLL and need no feature-directory search paths.
get_property(KH3AP_TEST_TARGETS DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(test_target IN LISTS KH3AP_TEST_TARGETS)
    get_target_property(test_type ${test_target} TYPE)
    if(NOT test_type STREQUAL "UTILITY" AND NOT test_type STREQUAL "INTERFACE_LIBRARY")
        target_include_directories(${test_target} PRIVATE "${KH3AP_PROBE_ROOT}")
    endif()
endforeach()

add_executable(seed_ui_options_test seed_ui_options_test.cpp
    "${KH3AP_ROOT}/bridge/serialization/json.cpp" "${KH3AP_ROOT}/bridge/serialization/json_fields.cpp")
target_include_directories(seed_ui_options_test PRIVATE "${KH3AP_PROBE_ROOT}")
target_link_libraries(seed_ui_options_test PRIVATE kh3ap_json_headers)
add_test(NAME seed_ui_options COMMAND seed_ui_options_test)

add_executable(movement_auto_equip_test movement_auto_equip_test.cpp)
target_include_directories(movement_auto_equip_test PRIVATE "${KH3AP_PROBE_ROOT}")
add_test(NAME movement_auto_equip COMMAND movement_auto_equip_test)

add_executable(save_menu_commands_test save_menu_commands_test.cpp)
target_include_directories(save_menu_commands_test PRIVATE "${KH3AP_PROBE_ROOT}")
add_test(NAME save_menu_commands COMMAND save_menu_commands_test)

add_executable(save_point_text_test save_point_text_test.cpp
    "${KH3AP_ROOT}/features/story/save_points/text.cpp"
    "${KH3AP_ROOT}/bridge/serialization/json.cpp" "${KH3AP_ROOT}/bridge/serialization/json_fields.cpp"
    "${KH3AP_ROOT}/bridge/utilities.cpp")
target_include_directories(save_point_text_test PRIVATE "${KH3AP_PROBE_ROOT}")
target_link_libraries(save_point_text_test PRIVATE kh3ap_json_headers)
add_test(NAME save_point_text COMMAND save_point_text_test)

add_executable(save_point_cache_test save_point_cache_test.cpp
    "${KH3AP_ROOT}/features/story/save_points/cached_function_lookup.cpp")
target_include_directories(save_point_cache_test PRIVATE function_lookup_stubs "${KH3AP_PROBE_ROOT}")
add_test(NAME save_point_cache COMMAND save_point_cache_test)

add_executable(keyblade_style_polling_test keyblade_style_polling_test.cpp)
target_include_directories(keyblade_style_polling_test PRIVATE "${KH3AP_PROBE_ROOT}")
if(MSVC)
    target_compile_options(keyblade_style_polling_test PRIVATE /UNDEBUG)
else()
    target_compile_options(keyblade_style_polling_test PRIVATE -UNDEBUG)
endif()
add_test(NAME keyblade_style_events COMMAND keyblade_style_polling_test)

if(WIN32)
    add_executable(garden_character_restoration_test garden_character_restoration_test.cpp
        "${KH3AP_ROOT}/features/world/garden_return/character_restoration.cpp")
    target_include_directories(garden_character_restoration_test PRIVATE "${KH3AP_PROBE_ROOT}")
    add_test(NAME garden_character_restoration COMMAND garden_character_restoration_test)
endif()

add_executable(character_trap_state_test character_trap_state_test.cpp)
target_include_directories(character_trap_state_test PRIVATE "${KH3AP_PROBE_ROOT}")
if(MSVC)
    target_compile_options(character_trap_state_test PRIVATE /UNDEBUG)
else()
    target_compile_options(character_trap_state_test PRIVATE -UNDEBUG)
endif()
add_test(NAME character_trap_state COMMAND character_trap_state_test)

# Standalone regressions formerly built by external developer scripts.
if(WIN32)
    set(_bridge "${KH3AP_ROOT}/bridge")
    set(_json_sources "${_bridge}/serialization/json.cpp" "${_bridge}/serialization/json_fields.cpp")
    foreach(name IN ITEMS item_delivery_binding item_delivery_catalog item_delivery_queue
            catalog chest_manifest delivery_worker receipt_accounting
            runtime_status runtime_loot_policy save_space_policy space_access space_eclipse
            space_profile space_policy space_refresh space_ship_policy space_sphere)
        add_executable(${name}_test ${name}_test.cpp)
        target_include_directories(${name}_test PRIVATE "${KH3AP_PROBE_ROOT}")
        target_link_libraries(${name}_test PRIVATE kh3ap_json_headers)
        target_compile_options(${name}_test PRIVATE /UNDEBUG /bigobj)
        add_test(NAME ${name} COMMAND ${name}_test)
    endforeach()
    target_sources(item_delivery_binding_test PRIVATE ${_json_sources})
    target_sources(catalog_test PRIVATE "${_bridge}/catalog/catalog.cpp" "${_bridge}/serialization/json.cpp")
    foreach(name IN ITEMS chest_manifest receipt_accounting runtime_status)
        target_sources(${name}_test PRIVATE "${_bridge}/serialization/json.cpp")
    endforeach()
    target_sources(delivery_worker_test PRIVATE ${_json_sources}
        "${_bridge}/catalog/catalog.cpp" "${_bridge}/rewards/reconciliation.cpp"
        "${_bridge}/rewards/reconciliation_protocol.cpp" "${_bridge}/serialization/request_parser.cpp")
    target_sources(space_access_test PRIVATE ${_json_sources}
        "${KH3AP_ROOT}/features/world/space/space_access.cpp")

    # External clients can exercise real IPC without linking UE4SS or launching KH3.
    add_executable(pipe_transport_host pipe_transport_host.cpp ${_json_sources}
        "${_bridge}/catalog/catalog.cpp" "${_bridge}/transport/pipe_transport.cpp"
        "${_bridge}/rewards/trap_queue.cpp" "${_bridge}/rewards/reconciliation.cpp"
        "${_bridge}/rewards/reconciliation_protocol.cpp" "${_bridge}/checks/lucky_marks.cpp"
        "${_bridge}/checks/level_checks.cpp" "${_bridge}/session/deathlink.cpp"
        "${_bridge}/transport/local_socket.cpp" "${_bridge}/serialization/request_parser.cpp")
    target_include_directories(pipe_transport_host PRIVATE "${KH3AP_PROBE_ROOT}")
    target_compile_options(pipe_transport_host PRIVATE /bigobj)
    target_link_libraries(pipe_transport_host PRIVATE kh3ap_json_headers ws2_32 bcrypt advapi32)
    add_test(NAME pipe_transport_regression
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/pipe_transport_regression.py"
            "$<TARGET_FILE:pipe_transport_host>")
    set_tests_properties(pipe_transport_regression PROPERTIES TIMEOUT 45)
endif()

foreach(mode IN ITEMS 0 1)
    add_executable(command_sources_${mode}_test command_sources_test.cpp)
    target_include_directories(command_sources_${mode}_test PRIVATE "${KH3AP_PROBE_ROOT}")
    target_compile_definitions(command_sources_${mode}_test PRIVATE KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS=${mode})
    add_test(NAME command_sources_${mode} COMMAND command_sources_${mode}_test)
endforeach()

add_executable(music_decode_queue_test music_decode_queue_test.cpp)
target_include_directories(music_decode_queue_test PRIVATE "${KH3AP_PROBE_ROOT}")
add_test(NAME music_decode_queue COMMAND music_decode_queue_test)
set_tests_properties(music_decode_queue PROPERTIES TIMEOUT 15)

include("${CMAKE_CURRENT_LIST_DIR}/../cmake/kh3-test-assertions.cmake")
