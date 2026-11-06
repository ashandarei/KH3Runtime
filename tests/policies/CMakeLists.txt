cmake_minimum_required(VERSION 3.16)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(kh3-retry-point-probe-tests LANGUAGES CXX)
    enable_testing()
endif()

if(WIN32)
    add_executable(test_kh02_movie_shared kh02/test_kh02_movie_shared.cpp
        ../../shared/kh02/kh02_movie_shared.cpp)
    target_compile_features(test_kh02_movie_shared PRIVATE cxx_std_20)
    target_link_libraries(test_kh02_movie_shared PRIVATE ole32 advapi32)
    if(MSVC)
        target_compile_options(test_kh02_movie_shared PRIVATE /UNDEBUG)
    else()
        target_compile_options(test_kh02_movie_shared PRIVATE -UNDEBUG)
    endif()
    add_test(NAME test_kh02_movie_shared COMMAND test_kh02_movie_shared)
    add_executable(test_kh02_executable_verification kh02/test_kh02_executable_verification.cpp
        ../../shared/kh02/kh02_executable_verification.cpp)
    target_compile_features(test_kh02_executable_verification PRIVATE cxx_std_20)
    target_link_libraries(test_kh02_executable_verification PRIVATE bcrypt)
endif()

add_executable(test_kh3_bridge_wake test_bridge_wake.cpp)
target_compile_features(test_kh3_bridge_wake PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(test_kh3_bridge_wake PRIVATE /UNDEBUG)
else()
    target_compile_options(test_kh3_bridge_wake PRIVATE -UNDEBUG)
endif()
add_test(NAME test_kh3_bridge_wake COMMAND test_kh3_bridge_wake)

set(KH3AP_PROBE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../KH3APProbeCpp)
include("${KH3AP_PROBE_ROOT}/../cmake/kh3-json-dependency.cmake")

set(_kh3_policy_tests
    ui_input_event
    ui_control_command_catalog
    magic_command_policy
    character_style_state
    character_ascension_state
    ultima_weapon_shard_policy
    integer
    fast_start_policy
    options_input_ownership)
if(WIN32)
    list(APPEND _kh3_policy_tests request_file_watcher)
endif()
foreach(_kh3_policy_test IN LISTS _kh3_policy_tests)
    add_executable(test_kh3_${_kh3_policy_test} test_${_kh3_policy_test}.cpp)
    target_include_directories(test_kh3_${_kh3_policy_test} PRIVATE ${KH3AP_PROBE_ROOT})
    target_compile_features(test_kh3_${_kh3_policy_test} PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(test_kh3_${_kh3_policy_test} PRIVATE /UNDEBUG)
    else()
        target_compile_options(test_kh3_${_kh3_policy_test} PRIVATE -UNDEBUG)
    endif()
    add_test(NAME test_kh3_${_kh3_policy_test} COMMAND test_kh3_${_kh3_policy_test})
endforeach()

add_executable(test_kh3_runtime_autoload_profile test_autoload_profile.cpp)
target_compile_features(test_kh3_runtime_autoload_profile PRIVATE cxx_std_20)
add_test(NAME test_kh3_runtime_autoload_profile COMMAND test_kh3_runtime_autoload_profile)
add_library(kh3ap_probe_test_headers INTERFACE)
target_include_directories(kh3ap_probe_test_headers INTERFACE
        ${KH3AP_PROBE_ROOT}
        ${KH3AP_PROBE_ROOT}/..
        ${KH3AP_PROBE_ROOT}/../..
)

# These targets are created before the directory-wide link declaration.
foreach(_kh02_early_target IN ITEMS test_kh02_movie_frames test_kh02_movie_shared test_kh02_executable_verification)
    if(TARGET ${_kh02_early_target})
        target_link_libraries(${_kh02_early_target} PRIVATE kh3ap_probe_test_headers)
    endif()
endforeach()
target_link_libraries(test_kh3_bridge_wake PRIVATE kh3ap_probe_test_headers)
target_link_libraries(test_kh3_runtime_autoload_profile PRIVATE kh3ap_probe_test_headers)
link_libraries(kh3ap_probe_test_headers)

add_executable(test_lucky_emblem_persistence test_lucky_emblem_persistence.cpp
    ${KH3AP_PROBE_ROOT}/kh3ap/bridge/serialization/json.cpp)
target_compile_features(test_lucky_emblem_persistence PRIVATE cxx_std_20)
target_link_libraries(test_lucky_emblem_persistence PRIVATE kh3ap_json_headers)
if(MSVC)
    target_compile_options(test_lucky_emblem_persistence PRIVATE /UNDEBUG)
else()
    target_compile_options(test_lucky_emblem_persistence PRIVATE -UNDEBUG)
endif()
add_test(NAME test_lucky_emblem_persistence COMMAND test_lucky_emblem_persistence)

add_executable(test_cutscene_skip_view test_cutscene_skip_view.cpp)
target_compile_features(test_cutscene_skip_view PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(test_cutscene_skip_view PRIVATE /UNDEBUG)
else()
    target_compile_options(test_cutscene_skip_view PRIVATE -UNDEBUG)
endif()
add_test(NAME test_cutscene_skip_view COMMAND test_cutscene_skip_view)

add_executable(test_character_travel_policy test_character_travel_policy.cpp)
target_compile_features(test_character_travel_policy PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(test_character_travel_policy PRIVATE /UNDEBUG)
else()
    target_compile_options(test_character_travel_policy PRIVATE -UNDEBUG)
endif()
add_test(NAME test_character_travel_policy COMMAND test_character_travel_policy)

add_executable(test_kh3_runtime_profile_collector test_runtime_profile_collector.cpp)
target_compile_features(test_kh3_runtime_profile_collector PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(test_kh3_runtime_profile_collector PRIVATE /UNDEBUG)
else()
    target_compile_options(test_kh3_runtime_profile_collector PRIVATE -UNDEBUG)
endif()
find_package(Threads REQUIRED)
target_link_libraries(test_kh3_runtime_profile_collector PRIVATE Threads::Threads)
add_test(NAME test_kh3_runtime_profile_collector COMMAND test_kh3_runtime_profile_collector)

add_executable(test_kh3_runtime_profile_disabled
        test_runtime_profile_disabled.cpp
        ${KH3AP_PROBE_ROOT}/kh3ap/diagnostics/profiling/reflection_profile.cpp)
target_compile_features(test_kh3_runtime_profile_disabled PRIVATE cxx_std_20)
target_compile_definitions(test_kh3_runtime_profile_disabled PRIVATE KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS=0)
add_test(NAME test_kh3_runtime_profile_disabled COMMAND test_kh3_runtime_profile_disabled)

add_executable(test_kh3_shop_movie_patch test_shop_movie_patch.cpp)
target_compile_features(test_kh3_shop_movie_patch PRIVATE cxx_std_20)
include("${KH3AP_PROBE_ROOT}/../cmake/kh3-movie-resources.cmake")
kh3ap_attach_movie_resources(test_kh3_shop_movie_patch "${KH3AP_PROBE_ROOT}")
if(MSVC)
    target_compile_options(test_kh3_shop_movie_patch PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/UNDEBUG>")
else()
    target_compile_options(test_kh3_shop_movie_patch PRIVATE -UNDEBUG)
endif()
add_test(NAME test_kh3_shop_movie_patch COMMAND test_kh3_shop_movie_patch)

add_executable(test_kh3_keyblade_inventory_events
        ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/keyblade_inventory_events_test.cpp)
target_compile_features(test_kh3_keyblade_inventory_events PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(test_kh3_keyblade_inventory_events PRIVATE /UNDEBUG)
else()
    target_compile_options(test_kh3_keyblade_inventory_events PRIVATE -UNDEBUG)
endif()
add_test(NAME test_kh3_keyblade_inventory_events COMMAND test_kh3_keyblade_inventory_events)

add_executable(test_kh3_runtime_item_delivery
        test_item_delivery.cpp
        ${KH3AP_PROBE_ROOT}/kh3ap/features/grants/delivery/item_delivery_flow.cpp
        ${KH3AP_PROBE_ROOT}/kh3ap/features/grants/delivery/item_grant_poll_state.cpp
        ${KH3AP_PROBE_ROOT}/kh3ap/features/grants/grant_tokens.cpp)
target_compile_features(test_kh3_runtime_item_delivery PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(test_kh3_runtime_item_delivery PRIVATE /UNDEBUG)
else()
    target_compile_options(test_kh3_runtime_item_delivery PRIVATE -UNDEBUG)
endif()
add_test(NAME test_kh3_runtime_item_delivery COMMAND test_kh3_runtime_item_delivery)

add_executable(test_kh3_remind_portal_route test_remind_portal_route.cpp)
target_compile_features(test_kh3_remind_portal_route PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(test_kh3_remind_portal_route PRIVATE /UNDEBUG)
else()
    target_compile_options(test_kh3_remind_portal_route PRIVATE -UNDEBUG)
endif()
add_test(NAME test_kh3_remind_portal_route COMMAND test_kh3_remind_portal_route)

add_executable(test_kh3_runtime_link_tutorial_bypass test_link_tutorial_bypass.cpp)
add_executable(test_kh3_world_keyblade_bypass test_world_keyblade_bypass.cpp)
target_compile_features(test_kh3_world_keyblade_bypass PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(test_kh3_world_keyblade_bypass PRIVATE /UNDEBUG)
else()
    target_compile_options(test_kh3_world_keyblade_bypass PRIVATE -UNDEBUG)
endif()
add_test(NAME test_kh3_world_keyblade_bypass COMMAND test_kh3_world_keyblade_bypass)
target_compile_features(test_kh3_runtime_link_tutorial_bypass PRIVATE cxx_std_17)
add_test(NAME test_kh3_runtime_link_tutorial_bypass COMMAND test_kh3_runtime_link_tutorial_bypass)

add_executable(test_kh3_native_frame_wait_policy test_frame_wait_policy.cpp)
target_compile_features(test_kh3_native_frame_wait_policy PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(test_kh3_native_frame_wait_policy PRIVATE /UNDEBUG)
else()
    target_compile_options(test_kh3_native_frame_wait_policy PRIVATE -UNDEBUG)
endif()
add_test(NAME test_kh3_native_frame_wait_policy COMMAND test_kh3_native_frame_wait_policy)

if(WIN32 AND MSVC)
    add_executable(test_kh3_native_frame_wait_stub test_frame_wait_stub.cpp)
    target_compile_features(test_kh3_native_frame_wait_stub PRIVATE cxx_std_17)
    target_compile_options(test_kh3_native_frame_wait_stub PRIVATE /UNDEBUG)
    add_test(NAME test_kh3_native_frame_wait_stub COMMAND test_kh3_native_frame_wait_stub)
    add_executable(test_kh3_airstep_gate_stub test_airstep_gate_stub.cpp)
    target_compile_features(test_kh3_airstep_gate_stub PRIVATE cxx_std_17)
    target_compile_options(test_kh3_airstep_gate_stub PRIVATE /UNDEBUG)
    add_test(NAME test_kh3_airstep_gate_stub COMMAND test_kh3_airstep_gate_stub)
    set(KH3AP_POLYHOOK_BUILD "" CACHE PATH "Existing KH3 probe build for the real PolyHook call-site test")
    if(KH3AP_POLYHOOK_BUILD)
        add_executable(test_kh3_native_frame_wait_polyhook test_frame_wait_stub.cpp)
        target_compile_features(test_kh3_native_frame_wait_polyhook PRIVATE cxx_std_20)
        target_compile_options(test_kh3_native_frame_wait_polyhook PRIVATE /UNDEBUG)
        target_compile_definitions(test_kh3_native_frame_wait_polyhook PRIVATE
            KH3AP_TEST_POLYHOOK ZYDIS_STATIC_BUILD ZYCORE_STATIC_BUILD)
        target_include_directories(test_kh3_native_frame_wait_polyhook PRIVATE
            "${KH3AP_POLYHOOK_BUILD}/_deps/polyhook2-src"
            "${KH3AP_POLYHOOK_BUILD}/_deps/polyhook2-src/asmjit/src"
            "${KH3AP_POLYHOOK_BUILD}/_deps/zydis-src/include"
            "${KH3AP_POLYHOOK_BUILD}/_deps/zydis-src/dependencies/zycore/include")
        target_link_directories(test_kh3_native_frame_wait_polyhook PRIVATE
            "${KH3AP_POLYHOOK_BUILD}/Output/Game__Shipping__Win64/bin")
        target_link_libraries(test_kh3_native_frame_wait_polyhook PRIVATE
            PolyHook_2 Zydis Zycore asmjit asmtk)
        add_test(NAME test_kh3_native_frame_wait_polyhook COMMAND test_kh3_native_frame_wait_polyhook)
        add_executable(test_kh3_airstep_gate_polyhook test_airstep_gate_stub.cpp)
        target_compile_features(test_kh3_airstep_gate_polyhook PRIVATE cxx_std_20)
        target_compile_options(test_kh3_airstep_gate_polyhook PRIVATE /UNDEBUG)
        target_compile_definitions(test_kh3_airstep_gate_polyhook PRIVATE
            KH3AP_TEST_POLYHOOK ZYDIS_STATIC_BUILD ZYCORE_STATIC_BUILD)
        target_include_directories(test_kh3_airstep_gate_polyhook PRIVATE
            "${KH3AP_POLYHOOK_BUILD}/_deps/polyhook2-src"
            "${KH3AP_POLYHOOK_BUILD}/_deps/polyhook2-src/asmjit/src"
            "${KH3AP_POLYHOOK_BUILD}/_deps/zydis-src/include"
            "${KH3AP_POLYHOOK_BUILD}/_deps/zydis-src/dependencies/zycore/include")
        target_link_directories(test_kh3_airstep_gate_polyhook PRIVATE
            "${KH3AP_POLYHOOK_BUILD}/Output/Game__Shipping__Win64/bin")
        target_link_libraries(test_kh3_airstep_gate_polyhook PRIVATE PolyHook_2 Zydis Zycore asmjit asmtk)
        add_test(NAME test_kh3_airstep_gate_polyhook COMMAND test_kh3_airstep_gate_polyhook)
    endif()
endif()

add_executable(test_kh3_minigame_rewards test_minigame_rewards.cpp)
target_compile_features(test_kh3_minigame_rewards PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(test_kh3_minigame_rewards PRIVATE /UNDEBUG)
else()
    target_compile_options(test_kh3_minigame_rewards PRIVATE -UNDEBUG)
endif()
add_test(NAME test_kh3_minigame_rewards COMMAND test_kh3_minigame_rewards)

add_executable(test_kh3_exit_confirmation test_exit_confirmation.cpp)
target_compile_features(test_kh3_exit_confirmation PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(test_kh3_exit_confirmation PRIVATE /UNDEBUG)
else()
    target_compile_options(test_kh3_exit_confirmation PRIVATE -UNDEBUG)
endif()
add_test(NAME test_kh3_exit_confirmation COMMAND test_kh3_exit_confirmation)

add_executable(test_kh3_ingredient_tracker test_ingredient_tracker.cpp)
target_compile_features(test_kh3_ingredient_tracker PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(test_kh3_ingredient_tracker PRIVATE /UNDEBUG)
else()
    target_compile_options(test_kh3_ingredient_tracker PRIVATE -UNDEBUG)
endif()
add_test(NAME test_kh3_ingredient_tracker COMMAND test_kh3_ingredient_tracker)

if(WIN32)
    add_executable(test_kh3_ingredient_checkpoint_writer test_ingredient_checkpoint_writer.cpp
        "${KH3AP_PROBE_ROOT}/kh3ap/features/ingredients/ingredient_checkpoint_writer.cpp"
        "${KH3AP_PROBE_ROOT}/kh3ap/bridge/transport/checkpoint_file.cpp")
    target_compile_features(test_kh3_ingredient_checkpoint_writer PRIVATE cxx_std_20)
    target_include_directories(test_kh3_ingredient_checkpoint_writer PRIVATE "${KH3AP_PROBE_ROOT}")
    if(MSVC)
        target_compile_options(test_kh3_ingredient_checkpoint_writer PRIVATE /UNDEBUG)
    endif()
    add_test(NAME test_kh3_ingredient_checkpoint_writer COMMAND test_kh3_ingredient_checkpoint_writer)
    set_tests_properties(test_kh3_ingredient_checkpoint_writer PROPERTIES TIMEOUT 10)
endif()

add_executable(test_kh3_rapunzel_water_bypass test_rapunzel_water_bypass.cpp)
target_compile_features(test_kh3_rapunzel_water_bypass PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(test_kh3_rapunzel_water_bypass PRIVATE /UNDEBUG)
else()
    target_compile_options(test_kh3_rapunzel_water_bypass PRIVATE -UNDEBUG)
endif()
add_test(NAME test_kh3_rapunzel_water_bypass COMMAND test_kh3_rapunzel_water_bypass)

add_executable(test_kh3_rapunzel_swing_frame_rate test_rapunzel_swing_frame_rate.cpp)
target_compile_features(test_kh3_rapunzel_swing_frame_rate PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(test_kh3_rapunzel_swing_frame_rate PRIVATE /UNDEBUG)
else()
    target_compile_options(test_kh3_rapunzel_swing_frame_rate PRIVATE -UNDEBUG)
endif()
add_test(NAME test_kh3_rapunzel_swing_frame_rate COMMAND test_kh3_rapunzel_swing_frame_rate)

add_executable(test_kh3_rapunzel_hair_approach test_rapunzel_hair_approach.cpp)
target_compile_features(test_kh3_rapunzel_hair_approach PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(test_kh3_rapunzel_hair_approach PRIVATE /UNDEBUG)
else()
    target_compile_options(test_kh3_rapunzel_hair_approach PRIVATE -UNDEBUG)
endif()
add_test(NAME test_kh3_rapunzel_hair_approach COMMAND test_kh3_rapunzel_hair_approach)

add_executable(test_kh3_runtime_command_guard
    test_command_guard.cpp
    ${KH3AP_PROBE_ROOT}/kh3ap/bridge/serialization/json.cpp
    ${KH3AP_PROBE_ROOT}/kh3ap/bridge/serialization/json_fields.cpp)
target_compile_features(test_kh3_runtime_command_guard PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(test_kh3_runtime_command_guard PRIVATE /UNDEBUG)
else()
    target_compile_options(test_kh3_runtime_command_guard PRIVATE -UNDEBUG)
endif()
add_test(NAME test_kh3_runtime_command_guard COMMAND test_kh3_runtime_command_guard)

add_executable(test_kh3_boss_hp_limit_policy test_boss_hp_limit_policy.cpp)
target_compile_features(test_kh3_boss_hp_limit_policy PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(test_kh3_boss_hp_limit_policy PRIVATE /UNDEBUG)
else()
    target_compile_options(test_kh3_boss_hp_limit_policy PRIVATE -UNDEBUG)
endif()
add_test(NAME test_kh3_boss_hp_limit_policy COMMAND test_kh3_boss_hp_limit_policy)

add_executable(test_kh3_boss_remap_policy test_boss_remap_policy.cpp)
target_compile_features(test_kh3_boss_remap_policy PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(test_kh3_boss_remap_policy PRIVATE /UNDEBUG)
else()
    target_compile_options(test_kh3_boss_remap_policy PRIVATE -UNDEBUG)
endif()
add_test(NAME test_kh3_boss_remap_policy COMMAND test_kh3_boss_remap_policy)

foreach(_mode IN ITEMS player diagnostic)
    add_executable(test_kh3_performance_build_${_mode} test_performance_build.cpp)
    target_compile_features(test_kh3_performance_build_${_mode} PRIVATE cxx_std_17)
    if(_mode STREQUAL "diagnostic")
        target_compile_definitions(test_kh3_performance_build_${_mode} PRIVATE
            KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS=1 TEST_EXPECT_DIAGNOSTICS=1)
    else()
        target_compile_definitions(test_kh3_performance_build_${_mode} PRIVATE TEST_EXPECT_DIAGNOSTICS=0)
    endif()
    add_test(NAME test_kh3_performance_build_${_mode} COMMAND test_kh3_performance_build_${_mode})
endforeach()

add_executable(test_kh3_engine_hitch_state test_engine_hitch_state.cpp)
target_compile_features(test_kh3_engine_hitch_state PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(test_kh3_engine_hitch_state PRIVATE /UNDEBUG)
else()
    target_compile_options(test_kh3_engine_hitch_state PRIVATE -UNDEBUG)
endif()
add_test(NAME test_kh3_engine_hitch_state COMMAND test_kh3_engine_hitch_state)

add_executable(test_kh3_party_map_policy test_party_map_policy.cpp)
add_executable(test_kh3_music_pause test_music_pause.cpp)
target_compile_features(test_kh3_music_pause PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(test_kh3_music_pause PRIVATE /UNDEBUG)
else()
    target_compile_options(test_kh3_music_pause PRIVATE -UNDEBUG)
endif()
add_test(NAME test_kh3_music_pause COMMAND test_kh3_music_pause)

add_executable(test_kh3_now_playing_gfx_lifecycle
    ../../tests/now_playing_gfx_lifecycle_test.cpp)
target_compile_features(test_kh3_now_playing_gfx_lifecycle PRIVATE cxx_std_20)
target_include_directories(test_kh3_now_playing_gfx_lifecycle PRIVATE ../../KH3APProbeCpp)
if(MSVC)
    target_compile_options(test_kh3_now_playing_gfx_lifecycle PRIVATE /UNDEBUG)
else()
    target_compile_options(test_kh3_now_playing_gfx_lifecycle PRIVATE -UNDEBUG)
endif()
add_test(NAME test_kh3_now_playing_gfx_lifecycle COMMAND test_kh3_now_playing_gfx_lifecycle)

add_executable(test_kh3_boss_hp_setup_policy
    ../../tests/boss_hp_setup_policy_test.cpp)
target_compile_features(test_kh3_boss_hp_setup_policy PRIVATE cxx_std_20)
target_include_directories(test_kh3_boss_hp_setup_policy PRIVATE ../../KH3APProbeCpp)
if(MSVC)
    target_compile_options(test_kh3_boss_hp_setup_policy PRIVATE /UNDEBUG)
else()
    target_compile_options(test_kh3_boss_hp_setup_policy PRIVATE -UNDEBUG)
endif()
add_test(NAME test_kh3_boss_hp_setup_policy COMMAND test_kh3_boss_hp_setup_policy)

add_executable(test_kh3_mom_audio test_mom_audio.cpp)
target_compile_features(test_kh3_mom_audio PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(test_kh3_mom_audio PRIVATE /UNDEBUG)
else()
    target_compile_options(test_kh3_mom_audio PRIVATE -UNDEBUG)
endif()
add_test(NAME test_kh3_mom_audio COMMAND test_kh3_mom_audio)

if(WIN32)
    add_executable(test_kh3_mom_audio_windows test_mom_audio_windows.cpp
        ${KH3AP_PROBE_ROOT}/kh3ap/features/treasure/music_chest/mom_audio.cpp)
    target_compile_features(test_kh3_mom_audio_windows PRIVATE cxx_std_17)
    target_link_libraries(test_kh3_mom_audio_windows PRIVATE ole32)
    if(MSVC)
        target_compile_options(test_kh3_mom_audio_windows PRIVATE /UNDEBUG)
    else()
        target_compile_options(test_kh3_mom_audio_windows PRIVATE -UNDEBUG)
    endif()
    add_test(NAME test_kh3_mom_audio_windows COMMAND test_kh3_mom_audio_windows)
    set_tests_properties(test_kh3_mom_audio_windows PROPERTIES SKIP_RETURN_CODE 77)
endif()

add_executable(test_kh3_music_chest_status test_music_chest_status.cpp)
target_compile_features(test_kh3_music_chest_status PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(test_kh3_music_chest_status PRIVATE /UNDEBUG)
else()
    target_compile_options(test_kh3_music_chest_status PRIVATE -UNDEBUG)
endif()
add_test(NAME test_kh3_music_chest_status COMMAND test_kh3_music_chest_status)

add_executable(test_kh3_music_chest_initialization test_music_chest_initialization.cpp)
target_compile_features(test_kh3_music_chest_initialization PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(test_kh3_music_chest_initialization PRIVATE /UNDEBUG)
else()
    target_compile_options(test_kh3_music_chest_initialization PRIVATE -UNDEBUG)
endif()
add_test(NAME test_kh3_music_chest_initialization COMMAND test_kh3_music_chest_initialization)

add_executable(test_kh3_music_chest_opening_work test_music_chest_opening_work.cpp)
target_compile_features(test_kh3_music_chest_opening_work PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(test_kh3_music_chest_opening_work PRIVATE /UNDEBUG)
else()
    target_compile_options(test_kh3_music_chest_opening_work PRIVATE -UNDEBUG)
endif()
add_test(NAME test_kh3_music_chest_opening_work COMMAND test_kh3_music_chest_opening_work)

add_executable(test_kh3_chest_discovery_queue test_chest_discovery_queue.cpp)
target_compile_features(test_kh3_chest_discovery_queue PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(test_kh3_chest_discovery_queue PRIVATE /UNDEBUG)
else()
    target_compile_options(test_kh3_chest_discovery_queue PRIVATE -UNDEBUG)
endif()
add_test(NAME test_kh3_chest_discovery_queue COMMAND test_kh3_chest_discovery_queue)
target_compile_features(test_kh3_party_map_policy PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(test_kh3_party_map_policy PRIVATE /UNDEBUG)
else()
    target_compile_options(test_kh3_party_map_policy PRIVATE -UNDEBUG)
endif()
add_test(NAME test_kh3_party_map_policy COMMAND test_kh3_party_map_policy)

add_executable(test_kh3_party_catalog test_party_catalog.cpp)
target_compile_features(test_kh3_party_catalog PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(test_kh3_party_catalog PRIVATE /UNDEBUG)
else()
    target_compile_options(test_kh3_party_catalog PRIVATE -UNDEBUG)
endif()
add_test(NAME test_kh3_party_catalog COMMAND test_kh3_party_catalog)

add_executable(test_kh3_party_pair_policy test_party_pair_policy.cpp)
target_compile_features(test_kh3_party_pair_policy PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(test_kh3_party_pair_policy PRIVATE /UNDEBUG)
else()
    target_compile_options(test_kh3_party_pair_policy PRIVATE -UNDEBUG)
endif()
add_test(NAME test_kh3_party_pair_policy COMMAND test_kh3_party_pair_policy)

add_executable(test_kh3_map_ap_grant_catalog test_map_ap_grant_catalog.cpp)
target_compile_features(test_kh3_map_ap_grant_catalog PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(test_kh3_map_ap_grant_catalog PRIVATE /UNDEBUG)
else()
    target_compile_options(test_kh3_map_ap_grant_catalog PRIVATE -UNDEBUG)
endif()
add_test(NAME test_kh3_map_ap_grant_catalog COMMAND test_kh3_map_ap_grant_catalog)

add_executable(test_kh3_retry_point_probe_state
        test_retry_point_probe_state.cpp
)

target_compile_features(test_kh3_retry_point_probe_state PRIVATE cxx_std_17)

target_include_directories(test_kh3_retry_point_probe_state
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../KH3APProbeCpp
)

add_test(
        NAME test_kh3_retry_point_probe_state
        COMMAND test_kh3_retry_point_probe_state
)

add_executable(test_kh3_retry_blueprint_probe_state
        test_retry_blueprint_probe_state.cpp
)

target_compile_features(test_kh3_retry_blueprint_probe_state PRIVATE cxx_std_17)

target_include_directories(test_kh3_retry_blueprint_probe_state
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../KH3APProbeCpp
)

add_test(
        NAME test_kh3_retry_blueprint_probe_state
        COMMAND test_kh3_retry_blueprint_probe_state
)

add_executable(test_kh3_save_point_auto_spawn_state
        test_save_point_auto_spawn_state.cpp
        ../../KH3APProbeCpp/kh3ap/features/grants/grant_pawn_state.cpp
)

target_compile_features(test_kh3_save_point_auto_spawn_state PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(test_kh3_save_point_auto_spawn_state PRIVATE /UNDEBUG)
else()
    target_compile_options(test_kh3_save_point_auto_spawn_state PRIVATE -UNDEBUG)
endif()

target_include_directories(test_kh3_save_point_auto_spawn_state
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../KH3APProbeCpp
)

add_test(
        NAME test_kh3_save_point_auto_spawn_state
        COMMAND test_kh3_save_point_auto_spawn_state
)

add_executable(test_kh3_portal_auto_spawn_context
        test_portal_auto_spawn_context.cpp
)

target_compile_features(test_kh3_portal_auto_spawn_context PRIVATE cxx_std_17)

target_include_directories(test_kh3_portal_auto_spawn_context
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../KH3APProbeCpp
)

add_test(
        NAME test_kh3_portal_auto_spawn_context
        COMMAND test_kh3_portal_auto_spawn_context
)

add_executable(test_kh3_runtime_battle_level_progress
        test_battle_level_progress.cpp
)

target_compile_features(test_kh3_runtime_battle_level_progress PRIVATE cxx_std_17)

target_include_directories(test_kh3_runtime_battle_level_progress
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../KH3APProbeCpp
)

add_test(
        NAME test_kh3_runtime_battle_level_progress
        COMMAND test_kh3_runtime_battle_level_progress
)

add_executable(test_kh3_runtime_experience_multiplier_math
        test_experience_multiplier_math.cpp
)

target_compile_features(test_kh3_runtime_experience_multiplier_math PRIVATE cxx_std_17)

target_include_directories(test_kh3_runtime_experience_multiplier_math
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../KH3APProbeCpp
)

add_test(
        NAME test_kh3_runtime_experience_multiplier_math
        COMMAND test_kh3_runtime_experience_multiplier_math
)

add_executable(test_kh3_treasure_mapping_resolver
        test_treasure_mapping_resolver.cpp
        ${KH3AP_PROBE_ROOT}/kh3ap/features/treasure/treasure_mapping_resolver.cpp
        ${KH3AP_PROBE_ROOT}/kh3ap/core/text_format.cpp
)

target_compile_features(test_kh3_treasure_mapping_resolver PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(test_kh3_treasure_mapping_resolver PRIVATE /UNDEBUG)
else()
    target_compile_options(test_kh3_treasure_mapping_resolver PRIVATE -UNDEBUG)
endif()

target_include_directories(test_kh3_treasure_mapping_resolver
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../KH3APProbeCpp
)

add_test(
        NAME test_kh3_treasure_mapping_resolver
        COMMAND test_kh3_treasure_mapping_resolver
)

add_executable(test_kh3_runtime_sora_state_revalidation
        test_sora_state_revalidation.cpp
)

target_compile_features(test_kh3_runtime_sora_state_revalidation PRIVATE cxx_std_20)

target_include_directories(test_kh3_runtime_sora_state_revalidation
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../KH3APProbeCpp
)

add_test(
        NAME test_kh3_runtime_sora_state_revalidation
        COMMAND test_kh3_runtime_sora_state_revalidation
)

add_executable(test_kh3_runtime_sora_state_catalog
        test_sora_state_catalog.cpp
        ../../KH3APProbeCpp/kh3ap/features/player/state/sora_state_catalog.cpp
)

target_compile_features(test_kh3_runtime_sora_state_catalog PRIVATE cxx_std_17)

target_include_directories(test_kh3_runtime_sora_state_catalog
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../KH3APProbeCpp
)

add_test(
        NAME test_kh3_runtime_sora_state_catalog
        COMMAND test_kh3_runtime_sora_state_catalog
)

add_executable(test_kh3_runtime_caribbean_chart_unlock_state
        test_caribbean_chart_unlock_state.cpp
)

target_compile_features(test_kh3_runtime_caribbean_chart_unlock_state PRIVATE cxx_std_17)

target_include_directories(test_kh3_runtime_caribbean_chart_unlock_state
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../KH3APProbeCpp
)

add_test(
        NAME test_kh3_runtime_caribbean_chart_unlock_state
        COMMAND test_kh3_runtime_caribbean_chart_unlock_state
)

add_executable(test_kh3_runtime_keyblade_enhancement_native_profile
        test_keyblade_enhancement_profile.cpp
        ../../KH3APProbeCpp/kh3ap/features/grants/keyblades/keyblade_enhancement_profile.cpp
)

target_compile_features(test_kh3_runtime_keyblade_enhancement_native_profile PRIVATE cxx_std_17)

target_include_directories(test_kh3_runtime_keyblade_enhancement_native_profile
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../KH3APProbeCpp
)

add_test(
        NAME test_kh3_runtime_keyblade_enhancement_native_profile
        COMMAND test_kh3_runtime_keyblade_enhancement_native_profile
)

add_executable(test_kh3_runtime_steam_identity
        test_steam_identity.cpp
        ../../KH3APProbeCpp/kh3ap/core/platform/steam_identity.cpp
)

target_compile_features(test_kh3_runtime_steam_identity PRIVATE cxx_std_17)

target_include_directories(test_kh3_runtime_steam_identity
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../KH3APProbeCpp
)

if(WIN32)
    target_link_libraries(test_kh3_runtime_steam_identity PRIVATE Advapi32)
endif()

add_test(
        NAME test_kh3_runtime_steam_identity
        COMMAND test_kh3_runtime_steam_identity
)

add_executable(test_kh3_runtime_steam_language
        test_steam_language.cpp
        ../../KH3APProbeCpp/kh3ap/core/platform/steam_language.cpp
)

target_compile_features(test_kh3_runtime_steam_language PRIVATE cxx_std_17)

target_include_directories(test_kh3_runtime_steam_language
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../KH3APProbeCpp
)

add_test(
        NAME test_kh3_runtime_steam_language
        COMMAND test_kh3_runtime_steam_language
)

add_executable(test_kh3_runtime_steam_query_port_recovery
        test_steam_query_port_recovery.cpp
)

target_compile_features(
        test_kh3_runtime_steam_query_port_recovery PRIVATE cxx_std_17)

target_include_directories(test_kh3_runtime_steam_query_port_recovery
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../KH3APProbeCpp
)

add_test(
        NAME test_kh3_runtime_steam_query_port_recovery
        COMMAND test_kh3_runtime_steam_query_port_recovery
)

add_executable(test_kh3_runtime_olympus_tutorial_bypass
        test_olympus_tutorial_bypass.cpp
        ../../KH3APProbeCpp/kh3ap/features/tutorials/olympus_tutorial_bypass.cpp
)

target_compile_features(test_kh3_runtime_olympus_tutorial_bypass PRIVATE cxx_std_17)

target_include_directories(test_kh3_runtime_olympus_tutorial_bypass
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../KH3APProbeCpp
)

add_test(
        NAME test_kh3_runtime_olympus_tutorial_bypass
        COMMAND test_kh3_runtime_olympus_tutorial_bypass
)

add_executable(test_kh3_runtime_final_world_guidance_bypass
        test_final_world_guidance_bypass.cpp
        ../../KH3APProbeCpp/kh3ap/features/story/world_fixes/final_world_guidance_bypass.cpp
)

target_compile_features(test_kh3_runtime_final_world_guidance_bypass PRIVATE cxx_std_17)

target_include_directories(test_kh3_runtime_final_world_guidance_bypass
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../KH3APProbeCpp
)

add_test(
        NAME test_kh3_runtime_final_world_guidance_bypass
        COMMAND test_kh3_runtime_final_world_guidance_bypass
)

add_executable(test_kh3_runtime_tutorial_popup_bypass
        test_tutorial_popup_bypass.cpp
        ../../KH3APProbeCpp/kh3ap/features/tutorials/tutorial_popup_bypass.cpp
)

target_compile_features(test_kh3_runtime_tutorial_popup_bypass PRIVATE cxx_std_17)

target_include_directories(test_kh3_runtime_tutorial_popup_bypass
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../KH3APProbeCpp
)

add_test(
        NAME test_kh3_runtime_tutorial_popup_bypass
        COMMAND test_kh3_runtime_tutorial_popup_bypass
)

add_executable(test_kh3_runtime_final_world_tutorial_pawn_capture
        test_final_world_tutorial_pawn_capture.cpp
)

target_compile_features(test_kh3_runtime_final_world_tutorial_pawn_capture PRIVATE cxx_std_17)

target_include_directories(test_kh3_runtime_final_world_tutorial_pawn_capture
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../KH3APProbeCpp
)

add_test(
        NAME test_kh3_runtime_final_world_tutorial_pawn_capture
        COMMAND test_kh3_runtime_final_world_tutorial_pawn_capture
)

add_executable(test_kh3_runtime_festival_dance_story_patch
        test_festival_dance_story_patch.cpp
)

target_compile_features(test_kh3_runtime_festival_dance_story_patch PRIVATE cxx_std_17)

target_include_directories(test_kh3_runtime_festival_dance_story_patch
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../KH3APProbeCpp
)

add_test(
        NAME test_kh3_runtime_festival_dance_story_patch
        COMMAND test_kh3_runtime_festival_dance_story_patch
)

add_executable(test_kh3_runtime_beat_of_lead_completion
        test_beat_of_lead_completion.cpp
)

target_compile_features(test_kh3_runtime_beat_of_lead_completion PRIVATE cxx_std_17)

target_include_directories(test_kh3_runtime_beat_of_lead_completion
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../KH3APProbeCpp
)

add_test(
        NAME test_kh3_runtime_beat_of_lead_completion
        COMMAND test_kh3_runtime_beat_of_lead_completion
)

add_executable(test_kh3_lucky_mark_native_retirement
        test_lucky_mark_retirement.cpp
)

target_compile_features(test_kh3_lucky_mark_native_retirement PRIVATE cxx_std_17)

target_include_directories(test_kh3_lucky_mark_native_retirement
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../KH3APProbeCpp
)

add_test(
        NAME test_kh3_lucky_mark_native_retirement
        COMMAND test_kh3_lucky_mark_native_retirement
)

add_executable(test_kh3_runtime_union_cross_completion
        test_union_cross_completion.cpp
)

target_compile_features(test_kh3_runtime_union_cross_completion PRIVATE cxx_std_17)

target_include_directories(test_kh3_runtime_union_cross_completion
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../KH3APProbeCpp
)

add_test(
        NAME test_kh3_runtime_union_cross_completion
        COMMAND test_kh3_runtime_union_cross_completion
)

add_executable(test_kh3_runtime_guardians_of_light_skip
        test_guardians_of_light_skip.cpp
)

target_compile_features(test_kh3_runtime_guardians_of_light_skip PRIVATE cxx_std_17)

target_include_directories(test_kh3_runtime_guardians_of_light_skip
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../KH3APProbeCpp
)

add_test(
        NAME test_kh3_runtime_guardians_of_light_skip
        COMMAND test_kh3_runtime_guardians_of_light_skip
)

add_executable(test_kh3_runtime_quality_of_life
        test_quality_of_life.cpp
)

target_compile_features(test_kh3_runtime_quality_of_life PRIVATE cxx_std_17)

target_include_directories(test_kh3_runtime_quality_of_life
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../KH3APProbeCpp
)

add_test(
        NAME test_kh3_runtime_quality_of_life
        COMMAND test_kh3_runtime_quality_of_life
)

add_executable(test_kh3_runtime_world_bootstrap
        test_world_bootstrap.cpp
)

target_compile_features(test_kh3_runtime_world_bootstrap PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(test_kh3_runtime_world_bootstrap PRIVATE /UNDEBUG)
else()
    target_compile_options(test_kh3_runtime_world_bootstrap PRIVATE -UNDEBUG)
endif()

target_include_directories(test_kh3_runtime_world_bootstrap
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../KH3APProbeCpp
)

add_test(
        NAME test_kh3_runtime_world_bootstrap
        COMMAND test_kh3_runtime_world_bootstrap
)

add_executable(test_kh3_runtime_shoot_flow_restore_state
        test_shoot_flow_restore_state.cpp
)

target_compile_features(test_kh3_runtime_shoot_flow_restore_state PRIVATE cxx_std_17)

target_include_directories(test_kh3_runtime_shoot_flow_restore_state
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../KH3APProbeCpp
)

add_test(
        NAME test_kh3_runtime_shoot_flow_restore_state
        COMMAND test_kh3_runtime_shoot_flow_restore_state
)

add_executable(test_kh3_runtime_moogle_shop_scan_state
        test_moogle_shop_scan_state.cpp
        ../../KH3APProbeCpp/kh3ap/bridge/serialization/json.cpp
        ../../KH3APProbeCpp/kh3ap/bridge/serialization/json_fields.cpp
)

target_compile_features(test_kh3_runtime_moogle_shop_scan_state PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(test_kh3_runtime_moogle_shop_scan_state PRIVATE /UNDEBUG)
else()
    target_compile_options(test_kh3_runtime_moogle_shop_scan_state PRIVATE -UNDEBUG)
endif()

target_include_directories(test_kh3_runtime_moogle_shop_scan_state
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../KH3APProbeCpp
)

add_test(
        NAME test_kh3_runtime_moogle_shop_scan_state
        COMMAND test_kh3_runtime_moogle_shop_scan_state
)

add_executable(test_kh3_runtime_blocking_ui_state
        test_blocking_ui_state.cpp
        ../../KH3APProbeCpp/kh3ap/hooks/blocking_ui/blocking_ui_state.cpp
)

target_compile_features(test_kh3_runtime_blocking_ui_state PRIVATE cxx_std_20)

target_include_directories(test_kh3_runtime_blocking_ui_state
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../KH3APProbeCpp
)

add_test(
        NAME test_kh3_runtime_blocking_ui_state
        COMMAND test_kh3_runtime_blocking_ui_state
)

add_executable(test_kh3_runtime_player_reacquire_state
        test_player_reacquire_state.cpp
        ../../KH3APProbeCpp/kh3ap/features/player/context/player_reacquire_state.cpp
        ../../KH3APProbeCpp/kh3ap/bridge/catalog/update_catalog_state.cpp
)

target_compile_features(test_kh3_runtime_player_reacquire_state PRIVATE cxx_std_20)

target_include_directories(test_kh3_runtime_player_reacquire_state
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../KH3APProbeCpp
)

add_test(
        NAME test_kh3_runtime_player_reacquire_state
        COMMAND test_kh3_runtime_player_reacquire_state
)

add_executable(test_kh3_runtime_object_observation_state
        test_object_observation_state.cpp
        ../../KH3APProbeCpp/kh3ap/features/player/objects/object_observation_state.cpp
)

target_compile_features(test_kh3_runtime_object_observation_state PRIVATE cxx_std_20)

target_include_directories(test_kh3_runtime_object_observation_state
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../KH3APProbeCpp
)

add_test(
        NAME test_kh3_runtime_object_observation_state
        COMMAND test_kh3_runtime_object_observation_state
)

add_executable(test_kh3_runtime_game_thread_driver_state
        test_game_thread_driver_state.cpp
        ../../KH3APProbeCpp/kh3ap/app/game_thread_driver_state.cpp
        ../../KH3APProbeCpp/kh3ap/bridge/catalog/update_catalog_state.cpp
)

target_compile_features(test_kh3_runtime_game_thread_driver_state PRIVATE cxx_std_20)

target_include_directories(test_kh3_runtime_game_thread_driver_state
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../KH3APProbeCpp
)

add_test(
        NAME test_kh3_runtime_game_thread_driver_state
        COMMAND test_kh3_runtime_game_thread_driver_state
)

add_executable(test_kh3_runtime_kingdom_of_corona_qol
        test_kingdom_of_corona_qol.cpp
)

target_compile_features(test_kh3_runtime_kingdom_of_corona_qol PRIVATE cxx_std_17)

target_include_directories(test_kh3_runtime_kingdom_of_corona_qol
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../KH3APProbeCpp
)

add_test(
        NAME test_kh3_runtime_kingdom_of_corona_qol
        COMMAND test_kh3_runtime_kingdom_of_corona_qol
)

add_executable(test_kh3_runtime_olympus_coliseum_completion
        test_olympus_coliseum_completion.cpp
)

target_compile_features(
        test_kh3_runtime_olympus_coliseum_completion PRIVATE cxx_std_20)

target_include_directories(test_kh3_runtime_olympus_coliseum_completion
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../KH3APProbeCpp
)

add_test(
        NAME test_kh3_runtime_olympus_coliseum_completion
        COMMAND test_kh3_runtime_olympus_coliseum_completion
)

add_executable(test_kh02_tide_balance kh02/test_kh02_tide_balance.cpp)
target_compile_features(test_kh02_tide_balance PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(test_kh02_tide_balance PRIVATE /UNDEBUG)
else()
    target_compile_options(test_kh02_tide_balance PRIVATE -UNDEBUG)
endif()
add_test(NAME test_kh02_tide_balance COMMAND test_kh02_tide_balance)

add_executable(test_kh3_durable_ability_bank_policy test_durable_ability_bank_policy.cpp)
target_compile_features(test_kh3_durable_ability_bank_policy PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(test_kh3_durable_ability_bank_policy PRIVATE /UNDEBUG)
else()
    target_compile_options(test_kh3_durable_ability_bank_policy PRIVATE -UNDEBUG)
endif()
add_test(NAME test_kh3_durable_ability_bank_policy COMMAND test_kh3_durable_ability_bank_policy)

add_executable(test_kh3_command_refresh_plan test_command_refresh_plan.cpp)
target_compile_features(test_kh3_command_refresh_plan PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(test_kh3_command_refresh_plan PRIVATE /UNDEBUG)
else()
    target_compile_options(test_kh3_command_refresh_plan PRIVATE -UNDEBUG)
endif()
add_test(NAME test_kh3_command_refresh_plan COMMAND test_kh3_command_refresh_plan)

add_executable(test_kh3_translation_transform_layout test_translation_transform_layout.cpp)
target_compile_features(test_kh3_translation_transform_layout PRIVATE cxx_std_20)
target_include_directories(test_kh3_translation_transform_layout PRIVATE "${KH3AP_PROBE_ROOT}")
if(MSVC)
    target_compile_options(test_kh3_translation_transform_layout PRIVATE /UNDEBUG)
else()
    target_compile_options(test_kh3_translation_transform_layout PRIVATE -UNDEBUG)
endif()
add_test(NAME test_kh3_translation_transform_layout COMMAND test_kh3_translation_transform_layout)

add_executable(test_kh3_runtime_sora_ability_snapshot
        test_sora_ability_snapshot.cpp
)
target_compile_features(test_kh3_runtime_sora_ability_snapshot PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(test_kh3_runtime_sora_ability_snapshot PRIVATE /UNDEBUG)
else()
    target_compile_options(test_kh3_runtime_sora_ability_snapshot PRIVATE -UNDEBUG)
endif()
add_test(NAME test_kh3_runtime_sora_ability_snapshot
        COMMAND test_kh3_runtime_sora_ability_snapshot)

if(WIN32)
    add_executable(test_kh3_checked_process_read test_checked_process_read.cpp)
    target_compile_features(test_kh3_checked_process_read PRIVATE cxx_std_20)
    target_include_directories(test_kh3_checked_process_read PRIVATE "${KH3AP_PROBE_ROOT}")
    if(MSVC)
        target_compile_options(test_kh3_checked_process_read PRIVATE /UNDEBUG)
    else()
        target_compile_options(test_kh3_checked_process_read PRIVATE -UNDEBUG)
    endif()
    add_test(NAME test_kh3_checked_process_read COMMAND test_kh3_checked_process_read)
endif()

add_executable(test_kh3_runtime_controller_ability_refresh_state
        test_controller_ability_refresh_state.cpp)
target_compile_features(test_kh3_runtime_controller_ability_refresh_state PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(test_kh3_runtime_controller_ability_refresh_state PRIVATE /UNDEBUG)
else()
    target_compile_options(test_kh3_runtime_controller_ability_refresh_state PRIVATE -UNDEBUG)
endif()
add_test(NAME test_kh3_runtime_controller_ability_refresh_state
        COMMAND test_kh3_runtime_controller_ability_refresh_state)

add_executable(test_kh3_runtime_controller_focus
        ../../tests/controller_focus_test.cpp)
target_compile_features(test_kh3_runtime_controller_focus PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(test_kh3_runtime_controller_focus PRIVATE /UNDEBUG)
else()
    target_compile_options(test_kh3_runtime_controller_focus PRIVATE -UNDEBUG)
endif()
add_test(NAME test_kh3_runtime_controller_focus
        COMMAND test_kh3_runtime_controller_focus)

add_executable(test_kh3_runtime_controller_preferences
        ../../tests/controller_preferences_test.cpp)
target_compile_features(test_kh3_runtime_controller_preferences PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(test_kh3_runtime_controller_preferences PRIVATE /UNDEBUG)
else()
    target_compile_options(test_kh3_runtime_controller_preferences PRIVATE -UNDEBUG)
endif()
add_test(NAME test_kh3_runtime_controller_preferences
        COMMAND test_kh3_runtime_controller_preferences)

add_executable(test_kh3_runtime_mom_focus_handoff
        ../../tests/mom_focus_handoff_test.cpp)
target_compile_features(test_kh3_runtime_mom_focus_handoff PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(test_kh3_runtime_mom_focus_handoff PRIVATE /UNDEBUG)
else()
    target_compile_options(test_kh3_runtime_mom_focus_handoff PRIVATE -UNDEBUG)
endif()
add_test(NAME test_kh3_runtime_mom_focus_handoff COMMAND test_kh3_runtime_mom_focus_handoff)

if(WIN32)
    add_executable(test_kh3_runtime_mom_socket_observer
            ../../tests/mom_socket_observer_test.cpp)
    target_compile_features(test_kh3_runtime_mom_socket_observer PRIVATE cxx_std_20)
    target_compile_options(test_kh3_runtime_mom_socket_observer PRIVATE /UNDEBUG)
    target_link_libraries(test_kh3_runtime_mom_socket_observer PRIVATE ws2_32)
    add_test(NAME test_kh3_runtime_mom_socket_observer COMMAND test_kh3_runtime_mom_socket_observer)
    add_executable(test_kh3_runtime_mom_window_observer
            ../../tests/mom_window_observer_test.cpp)
    target_compile_features(test_kh3_runtime_mom_window_observer PRIVATE cxx_std_20)
    target_link_libraries(test_kh3_runtime_mom_window_observer PRIVATE user32)
    add_test(NAME test_kh3_runtime_mom_window_observer COMMAND test_kh3_runtime_mom_window_observer)
endif()

add_executable(test_kh3_runtime_toy_box_cactuar_bypass test_toy_box_cactuar_bypass.cpp)
target_compile_features(test_kh3_runtime_toy_box_cactuar_bypass PRIVATE cxx_std_17)
target_include_directories(test_kh3_runtime_toy_box_cactuar_bypass PRIVATE ../../KH3APProbeCpp)
add_test(NAME test_kh3_runtime_toy_box_cactuar_bypass COMMAND test_kh3_runtime_toy_box_cactuar_bypass)

target_link_libraries(test_kh3_runtime_command_guard PRIVATE kh3ap_json_headers)
target_link_libraries(test_kh3_runtime_moogle_shop_scan_state PRIVATE kh3ap_json_headers)
target_link_libraries(test_kh3_runtime_item_delivery PRIVATE kh3ap_json_headers)

add_executable(test_kh3_kh02_progress kh02/test_kh02_progress.cpp)
target_compile_features(test_kh3_kh02_progress PRIVATE cxx_std_20)
target_link_libraries(test_kh3_kh02_progress PRIVATE kh3ap_json_headers)
if(MSVC)
    target_compile_options(test_kh3_kh02_progress PRIVATE /UNDEBUG)
else()
    target_compile_options(test_kh3_kh02_progress PRIVATE -UNDEBUG)
endif()
add_test(NAME test_kh3_kh02_progress COMMAND test_kh3_kh02_progress)

add_executable(test_kh3_kh02_native_movie_state kh02/test_kh02_movie_state.cpp)
target_compile_features(test_kh3_kh02_native_movie_state PRIVATE cxx_std_20)
target_link_libraries(test_kh3_kh02_native_movie_state PRIVATE kh3ap_json_headers)
if(MSVC)
    target_compile_options(test_kh3_kh02_native_movie_state PRIVATE /UNDEBUG)
else()
    target_compile_options(test_kh3_kh02_native_movie_state PRIVATE -UNDEBUG)
endif()
add_test(NAME test_kh3_kh02_native_movie_state COMMAND test_kh3_kh02_native_movie_state)

add_executable(test_kh3_kh02_castle_path_geometry kh02/test_kh02_castle_path_geometry.cpp)
target_compile_features(test_kh3_kh02_castle_path_geometry PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(test_kh3_kh02_castle_path_geometry PRIVATE /UNDEBUG)
else()
    target_compile_options(test_kh3_kh02_castle_path_geometry PRIVATE -UNDEBUG)
endif()
add_test(NAME test_kh3_kh02_castle_path_geometry COMMAND test_kh3_kh02_castle_path_geometry)

add_executable(test_kh3_kh02_native_doors kh02/test_kh02_doors.cpp)
target_compile_features(test_kh3_kh02_native_doors PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(test_kh3_kh02_native_doors PRIVATE /UNDEBUG)
else()
    target_compile_options(test_kh3_kh02_native_doors PRIVATE -UNDEBUG)
endif()
add_test(NAME test_kh3_kh02_native_doors COMMAND test_kh3_kh02_native_doors)

foreach(policy IN ITEMS castle_scenery_plan challenge_reconciliation)
    add_executable(test_kh3_kh02_${policy} kh02/test_kh02_${policy}.cpp)
    target_compile_features(test_kh3_kh02_${policy} PRIVATE cxx_std_20)
    target_link_libraries(test_kh3_kh02_${policy} PRIVATE kh3ap_json_headers)
    if(MSVC)
        target_compile_options(test_kh3_kh02_${policy} PRIVATE /UNDEBUG)
    else()
        target_compile_options(test_kh3_kh02_${policy} PRIVATE -UNDEBUG)
    endif()
    add_test(NAME test_kh3_kh02_${policy} COMMAND test_kh3_kh02_${policy})
endforeach()

add_executable(test_kh3_kh02_challenge_navigation kh02/test_kh02_challenge_navigation.cpp)
target_compile_features(test_kh3_kh02_challenge_navigation PRIVATE cxx_std_20)
target_include_directories(test_kh3_kh02_challenge_navigation PRIVATE ../../KH3APProbeCpp)
if(MSVC)
    target_compile_options(test_kh3_kh02_challenge_navigation PRIVATE /UNDEBUG)
else()
    target_compile_options(test_kh3_kh02_challenge_navigation PRIVATE -UNDEBUG)
endif()
add_test(NAME test_kh3_kh02_challenge_navigation COMMAND test_kh3_kh02_challenge_navigation)

add_executable(test_kh3_kh02_challenge_tracking kh02/test_kh02_challenge_tracking.cpp)
target_compile_features(test_kh3_kh02_challenge_tracking PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(test_kh3_kh02_challenge_tracking PRIVATE /UNDEBUG)
else()
    target_compile_options(test_kh3_kh02_challenge_tracking PRIVATE -UNDEBUG)
endif()
add_test(NAME test_kh3_kh02_challenge_tracking COMMAND test_kh3_kh02_challenge_tracking)

add_executable(test_kh3_kh02_loading_policy kh02/test_kh02_loading_policy.cpp)
target_compile_features(test_kh3_kh02_loading_policy PRIVATE cxx_std_20)
target_include_directories(test_kh3_kh02_loading_policy PRIVATE ../../KH3APProbeCpp)
if(MSVC)
    target_compile_options(test_kh3_kh02_loading_policy PRIVATE /UNDEBUG)
else()
    target_compile_options(test_kh3_kh02_loading_policy PRIVATE -UNDEBUG)
endif()
add_test(NAME test_kh3_kh02_loading_policy COMMAND test_kh3_kh02_loading_policy)

add_executable(test_kh3_kh02_call_parameters kh02/test_kh02_call_parameters.cpp)
target_compile_features(test_kh3_kh02_call_parameters PRIVATE cxx_std_20)
target_include_directories(test_kh3_kh02_call_parameters PRIVATE ../../KH3APProbeCpp)
if(MSVC)
    target_compile_options(test_kh3_kh02_call_parameters PRIVATE /UNDEBUG)
    # This allocation test models the shipping STL; Debug iterator proxies
    # allocate even when a vector is empty and are absent from the runtime DLL.
    target_compile_definitions(test_kh3_kh02_call_parameters PRIVATE _ITERATOR_DEBUG_LEVEL=0)
else()
    target_compile_options(test_kh3_kh02_call_parameters PRIVATE -UNDEBUG)
endif()
add_test(NAME test_kh3_kh02_call_parameters COMMAND test_kh3_kh02_call_parameters)

add_executable(test_kh3_kh02_header_cache kh02/test_kh02_header_cache.cpp)
target_compile_features(test_kh3_kh02_header_cache PRIVATE cxx_std_20)
target_include_directories(test_kh3_kh02_header_cache PRIVATE ../../KH3APProbeCpp)
if(MSVC)
    target_compile_options(test_kh3_kh02_header_cache PRIVATE /UNDEBUG)
else()
    target_compile_options(test_kh3_kh02_header_cache PRIVATE -UNDEBUG)
endif()
add_test(NAME test_kh3_kh02_header_cache COMMAND test_kh3_kh02_header_cache)

add_executable(test_kh3_kh02_asset_path kh02/test_kh02_asset_path.cpp)
target_compile_features(test_kh3_kh02_asset_path PRIVATE cxx_std_20)
target_include_directories(test_kh3_kh02_asset_path PRIVATE ../../KH3APProbeCpp)
if(MSVC)
    target_compile_options(test_kh3_kh02_asset_path PRIVATE /UNDEBUG)
else()
    target_compile_options(test_kh3_kh02_asset_path PRIVATE -UNDEBUG)
endif()
add_test(NAME test_kh3_kh02_asset_path COMMAND test_kh3_kh02_asset_path)

if(WIN32)
    add_executable(test_kh3_kh02_movie_audio kh02/test_kh02_movie_audio.cpp
        ../../shared/kh02/kh02_movie_shared.cpp)
    target_compile_features(test_kh3_kh02_movie_audio PRIVATE cxx_std_20)
    target_link_libraries(test_kh3_kh02_movie_audio PRIVATE ole32 advapi32)
    if(MSVC)
        target_compile_options(test_kh3_kh02_movie_audio PRIVATE /UNDEBUG)
    endif()
    add_test(NAME test_kh3_kh02_movie_audio COMMAND test_kh3_kh02_movie_audio)
    set_tests_properties(test_kh3_kh02_movie_audio PROPERTIES TIMEOUT 10)

    add_executable(test_kh3_kh02_movie_audio_discard kh02/test_kh02_movie_audio_discard.cpp
        ../../shared/kh02/kh02_movie_shared.cpp)
    target_compile_features(test_kh3_kh02_movie_audio_discard PRIVATE cxx_std_20)
    target_link_libraries(test_kh3_kh02_movie_audio_discard PRIVATE ole32 advapi32)
    if(MSVC)
        target_compile_options(test_kh3_kh02_movie_audio_discard PRIVATE /UNDEBUG)
    endif()
    add_test(NAME test_kh3_kh02_movie_audio_discard COMMAND test_kh3_kh02_movie_audio_discard)
    set_tests_properties(test_kh3_kh02_movie_audio_discard PROPERTIES TIMEOUT 10)
endif()

# The adapter needs the same old UE headers used by the diagnostic DLL. Keep
# this local ABI test optional on machines without that licensed source tree.
set(KH02_ENGINE_CORE "" CACHE PATH "Optional licensed KH0.2 UE4.17 Core/Public headers for the ABI test")
if(WIN32 AND MSVC AND EXISTS "${KH02_ENGINE_CORE}/Templates/Function.h")
    add_executable(test_kh3_kh02_async_abi kh02/test_kh02_async_abi.cpp)
    target_compile_features(test_kh3_kh02_async_abi PRIVATE cxx_std_20)
    target_include_directories(test_kh3_kh02_async_abi PRIVATE
        "${KH02_ENGINE_CORE}" "${KH02_ENGINE_CORE}/Windows"
        "${KH02_ENGINE_CORE}/Containers" "${KH02_ENGINE_CORE}/Templates")
    target_compile_options(test_kh3_kh02_async_abi PRIVATE /UNDEBUG /permissive /Zc:preprocessor-)
    target_compile_definitions(test_kh3_kh02_async_abi PRIVATE
        UNICODE _UNICODE UE_BUILD_SHIPPING=1 PLATFORM_WINDOWS=1 CORE_API= IS_MONOLITHIC=1 IS_PROGRAM=0 UE_BUILD_MINIMAL=1
        WITH_PLUGIN_SUPPORT=0 WITH_SERVER_CODE=1 WITH_ENGINE=0 WITH_EDITOR=0
        WITH_EDITORONLY_DATA=0 STATS=0 USE_LOGGING_IN_SHIPPING=0 WITH_UNREAL_DEVELOPER_TOOLS=0)
    add_test(NAME test_kh3_kh02_async_abi COMMAND test_kh3_kh02_async_abi)
endif()

# KH0.2 lifecycle and companion protocol contracts are independent of Unreal.
foreach(_kh02_test IN ITEMS map_session_state session_retirement entry_session lifecycle save_destination world_identity chest_journal control_protocol cache_concurrency rpc_metrics compatibility combo_master_visit combo_master_refresh swing_hit_history attack_input_buffer)
    add_executable(test_kh02_${_kh02_test} kh02/test_kh02_${_kh02_test}.cpp)
    target_compile_features(test_kh02_${_kh02_test} PRIVATE cxx_std_20)
    target_include_directories(test_kh02_${_kh02_test} PRIVATE ../../KH3APProbeCpp)
    if(MSVC)
        target_compile_options(test_kh02_${_kh02_test} PRIVATE /UNDEBUG)
    else()
        target_compile_options(test_kh02_${_kh02_test} PRIVATE -UNDEBUG)
    endif()
    add_test(NAME test_kh02_${_kh02_test} COMMAND test_kh02_${_kh02_test})
endforeach()
if(WIN32)
    add_executable(test_kh02_asset_readiness kh02/test_kh02_asset_readiness.cpp)
    target_compile_features(test_kh02_asset_readiness PRIVATE cxx_std_20)
    target_include_directories(test_kh02_asset_readiness PRIVATE ../..)
    if(MSVC)
        target_compile_options(test_kh02_asset_readiness PRIVATE /UNDEBUG)
    endif()
    add_test(NAME test_kh02_asset_readiness COMMAND test_kh02_asset_readiness)
    set_tests_properties(test_kh02_asset_readiness PROPERTIES TIMEOUT 10)
    add_executable(test_kh02_control_pipe kh02/test_kh02_control_pipe.cpp
        ../../shared/kh02/kh02_control_pipe.cpp)
    target_compile_features(test_kh02_control_pipe PRIVATE cxx_std_20)
    target_link_libraries(test_kh02_control_pipe PRIVATE advapi32)
    if(MSVC)
        target_compile_options(test_kh02_control_pipe PRIVATE /UNDEBUG)
    endif()
    add_test(NAME test_kh02_control_pipe COMMAND test_kh02_control_pipe)
    set_tests_properties(test_kh02_control_pipe PROPERTIES TIMEOUT 15)
endif()

set_tests_properties(test_kh02_cache_concurrency PROPERTIES TIMEOUT 10)

foreach(_kh02_presentation_test IN ITEMS aqua_level_policy presentation_policy runtime_config entry_adapters resource_policies hud_combat_policy remote_read_service menu_owners movie_playback_state combat_event_journal mirror_travel postboss_handoff damage_event_metadata diagnostic_actions movie_frame_presenter checkpoint_adapters source_script_registry battle_level_policy darkside_reflection_policy darkside_pillar_origin darkside_pillar_size challenge_rule_domains preview_schedule shared_unlocks chest_interaction_policy)
    add_executable(test_kh02_${_kh02_presentation_test} kh02/test_kh02_${_kh02_presentation_test}.cpp)
    target_compile_features(test_kh02_${_kh02_presentation_test} PRIVATE cxx_std_20)
    target_link_libraries(test_kh02_${_kh02_presentation_test} PRIVATE kh3ap_json_headers)
    target_include_directories(test_kh02_${_kh02_presentation_test} PRIVATE ../../KH3APProbeCpp)
    if(MSVC)
        target_compile_options(test_kh02_${_kh02_presentation_test} PRIVATE /UNDEBUG)
    else()
        target_compile_options(test_kh02_${_kh02_presentation_test} PRIVATE -UNDEBUG)
    endif()
    add_test(NAME test_kh02_${_kh02_presentation_test} COMMAND test_kh02_${_kh02_presentation_test})
endforeach()

add_executable(test_kh02_chest_persistence kh02/test_kh02_chest_persistence.cpp)
target_compile_features(test_kh02_chest_persistence PRIVATE cxx_std_20)
target_include_directories(test_kh02_chest_persistence PRIVATE ../../KH3APProbeCpp)
if(MSVC)
    target_compile_options(test_kh02_chest_persistence PRIVATE /UNDEBUG)
else()
    target_compile_options(test_kh02_chest_persistence PRIVATE -UNDEBUG)
endif()
add_test(NAME test_kh02_chest_persistence COMMAND test_kh02_chest_persistence)

foreach(_kh02_state_test IN ITEMS bootstrap recovery)
    add_executable(test_kh02_${_kh02_state_test}_state
        ../../tests/kh02/kh02_${_kh02_state_test}_state_tests.cpp)
    target_compile_features(test_kh02_${_kh02_state_test}_state PRIVATE cxx_std_20)
    target_include_directories(test_kh02_${_kh02_state_test}_state PRIVATE ../../KH3APProbeCpp)
    if(MSVC)
        target_compile_options(test_kh02_${_kh02_state_test}_state PRIVATE /UNDEBUG)
    else()
        target_compile_options(test_kh02_${_kh02_state_test}_state PRIVATE -UNDEBUG)
    endif()
    add_test(NAME test_kh02_${_kh02_state_test}_state COMMAND test_kh02_${_kh02_state_test}_state)
endforeach()

set_tests_properties(test_kh02_remote_read_service PROPERTIES TIMEOUT 10)

add_executable(test_kh02_profile kh02/test_kh02_profile.cpp)
target_compile_features(test_kh02_profile PRIVATE cxx_std_20)
target_include_directories(test_kh02_profile PRIVATE ../../KH3APProbeCpp)
if(MSVC)
    target_compile_options(test_kh02_profile PRIVATE /UNDEBUG)
else()
    target_compile_options(test_kh02_profile PRIVATE -UNDEBUG)
endif()
add_test(NAME test_kh02_profile COMMAND test_kh02_profile)

add_executable(big_hero_rescue_damage_test big_hero_rescue_damage_test.cpp)
target_compile_features(big_hero_rescue_damage_test PRIVATE cxx_std_20)
target_include_directories(big_hero_rescue_damage_test PRIVATE ../../KH3APProbeCpp)
if(MSVC)
    target_compile_options(big_hero_rescue_damage_test PRIVATE /UNDEBUG)
else()
    target_compile_options(big_hero_rescue_damage_test PRIVATE -UNDEBUG)
endif()
add_test(NAME big_hero_rescue_damage_test COMMAND big_hero_rescue_damage_test)

add_executable(test_kh3_moogle_shop_catalog_unlock
    test_moogle_shop_catalog_unlock.cpp
    "${KH3AP_PROBE_ROOT}/kh3ap/features/moogle_shop/moogle_shop_catalog.cpp"
    "${KH3AP_PROBE_ROOT}/kh3ap/features/moogle_shop/ultima_weapon_shop_unlock.cpp"
    "${KH3AP_PROBE_ROOT}/kh3ap/bridge/serialization/json.cpp"
    "${KH3AP_PROBE_ROOT}/kh3ap/bridge/serialization/json_fields.cpp"
    "${KH3AP_PROBE_ROOT}/kh3ap/bridge/catalog/slot_data_lookup_parser.cpp")
target_compile_features(test_kh3_moogle_shop_catalog_unlock PRIVATE cxx_std_20)
target_include_directories(test_kh3_moogle_shop_catalog_unlock PRIVATE
    "${KH3AP_PROBE_ROOT}/../tests/hook_stubs")
target_link_libraries(test_kh3_moogle_shop_catalog_unlock PRIVATE kh3ap_json_headers)
if(MSVC)
    target_compile_options(test_kh3_moogle_shop_catalog_unlock PRIVATE /UNDEBUG)
else()
    target_compile_options(test_kh3_moogle_shop_catalog_unlock PRIVATE -UNDEBUG)
endif()
add_test(NAME test_kh3_moogle_shop_catalog_unlock COMMAND test_kh3_moogle_shop_catalog_unlock)

# One canonical project include root and its owned metadata headers.
get_filename_component(KH3AP_NATIVE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
get_property(_native_policy_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(_target IN LISTS _native_policy_targets)
    get_target_property(_type ${_target} TYPE)
    if(_type STREQUAL "EXECUTABLE" OR _type STREQUAL "STATIC_LIBRARY")
        target_include_directories(${_target} PRIVATE "${KH3AP_NATIVE_ROOT}" "${KH3AP_NATIVE_ROOT}/include")
    endif()
endforeach()

# Line-oriented driver for APWorld/native generator and pak-reader parity tests.
# Python supplies fixtures; this target has no APWorld build dependency.
if(WIN32)
    add_executable(randomization test_randomization.cpp
        "${KH3AP_PROBE_ROOT}/kh3ap/features/enemies/randomization.cpp"
        "${KH3AP_PROBE_ROOT}/kh3ap/core/settings/pak_seed_settings.cpp"
        "${KH3AP_PROBE_ROOT}/kh3ap/bridge/serialization/json.cpp")
    target_compile_features(randomization PRIVATE cxx_std_20)
    target_link_libraries(randomization PRIVATE kh3ap_json_headers bcrypt)
    if(MSVC)
        target_compile_options(randomization PRIVATE /utf-8 /UNDEBUG)
    endif()
endif()

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/kh3-test-assertions.cmake")
