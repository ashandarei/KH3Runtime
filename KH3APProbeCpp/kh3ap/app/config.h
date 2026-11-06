#pragma once

#include "kh3ap/bridge/transport/bridge_channels.h"
#include "kh3ap/core/performance_build.h"
#include <chrono>
#include <cstdint>
#include <string_view>

namespace kh3ap::settings
{
    inline constexpr bool s_debug_logging = false;
    inline constexpr bool s_enable_native_hook_install = true;
    // Keep AddMunny's upper cap and lifetime-earned accounting intact, but
    // allow the current balance to remain below zero when AP restores shop
    // costs after loading an older save.
    inline constexpr bool s_enable_signed_munny_patch = true;
    // Expand the EGS and Steam manual save lists from slots 1-9 to slots 1-99.
    // Autosave remains a separate entry, matching KH3's existing menu model.
    inline constexpr bool s_enable_expanded_save_slots_patch = true;
    // Square's translucent-slice shader extension accepts FSceneView but reads
    // a subclass-only FViewInfo field. Guard the read on EGS so base views use
    // the function's existing dummy-texture fallback.
    inline constexpr bool s_enable_sqex_translucent_slice_view_guard_patch = true;
    inline constexpr bool s_enable_runtime_update_loop = true;
    // Hook the verified UGameEngine::Tick implementation directly. The hook
    // fails closed unless the exact executable image layout, function RVA,
    // unwind entry, and prologue signature all match.
    inline constexpr bool s_enable_game_engine_tick_detour = true;
    // Keep reward diagnostics available in code, but off in live gameplay.
    // They walk reflected params, snapshot randomizer state, and add noticeable
    // hitching around rewards without contributing to the bridge path anymore.
    inline constexpr bool s_enable_runtime_reward_diagnostics = false;
    inline constexpr bool s_enable_runtime_uobject_create_observer = true;
    inline constexpr bool s_enable_sora_pawn_creation_cache = true;
    // Publish the exact native Lucky Emblem ID when the camera records it.
    inline constexpr bool s_enable_lucky_emblem_tracker = true;
    // Live soak telemetry showed TreasureLogicActor_C winning 9/9 chest
    // reports across multiple maps and both chest sizes. Keep the two state
    // observations available as compile-time rollback switches, but remove
    // them from the active object-create hot path.
    inline constexpr bool s_enable_open_treasure_player_state_fallback_observer = false;
    inline constexpr bool s_enable_open_treasure_char_state_fallback_observer = false;
    // Probe map-transition/GoA callbacks are useful while investigating runtime
    // reapply, but this hook group can crash during UE4SS startup. Keep the
    // code available, but leave it off for the stable AP bridge build.
    inline constexpr bool s_enable_post_load_probe_hooks = false;
    inline constexpr bool s_enable_playable_context_ready_dispatch_hook = true;
    inline constexpr bool s_enable_stable_ability_trace_hooks = false;
    inline constexpr bool s_enable_runtime_ability_state_probe = false;
    inline constexpr bool s_enable_targeted_runtime_ability_query_probe = false;
    inline constexpr bool s_enable_live_ability_owner_probe = false;
    inline constexpr bool s_enable_ability_array_needle_probe = false;
    // Manual-file request only. Use targeted_only=true for the narrow Sora and
    // MyEquipment reflection walk without the heavier global ability scan.
    inline constexpr bool s_enable_manual_ability_state_probe = false;
    inline constexpr bool s_enable_manual_equipment_probe = false;
    // Diagnostic builds only. Inert unless an exact manual_keyblade_enhancement_probe.json request is
    // placed in the active seed bridge directory. Mutating requests must also
    // carry confirm_mutation=true.
    inline constexpr bool s_enable_manual_keyblade_enhancement_probe = KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS != 0;
    inline constexpr bool s_enable_bonus_trace_logging = false;
    inline constexpr bool s_enable_startup_signature_dump = false;
    inline constexpr bool s_enable_vanilla_level_write_polyhook = true;
    inline constexpr bool s_enable_cutscene_auto_skip_hooks = true;
    inline constexpr bool s_enable_cutscene_ui_probe_hooks = false;
    inline constexpr bool s_enable_cutscene_state_probe_logging = false;
    inline constexpr bool s_enable_cutscene_end_skip_probe_logging = false;
    // he_01 opens a standalone Team Attacks help panel, then Shotlock and
    // Airstepping tutorial choices immediately after
    // BP_SetAbilityShootFlow(true) and BP_SetAbilityAthleticFlow(true). Jump
    // from each setter entry to the prompt's exact declined continuation so AP
    // remains authoritative for both abilities. The same gameflow's completion
    // sequence has a hardcoded Hero's Origin grant, two game-help panels, and a
    // forced camp-menu visit; bypass that sequence and its later vanilla
    // boolean ability-set cluster from existing map-load events. Do not poll
    // for the Blueprint or install a process-wide detour.
    inline constexpr bool s_enable_olympus_completion_tutorial_bypass = true;
    // EW's Darkside flow opens the guidance choice and later the Combat Basics
    // game-help panel. Retarget their exact validated bytecode entries to the
    // same continuations reached after dismissing those windows.
    inline constexpr bool s_enable_final_world_ui_bypasses = true;
    // he_02 opens Casting Magic and Free Running from exact latent calls and
    // later opens standalone Flowmotion and Attractions help panels. Route
    // each validated call past its tutorial UI, including Attractions'
    // misleading unlock notice. Suppress every validated tutorial-owned
    // BP_SetAbility*(true/false) call, including both the setup restrictions
    // and later restore cluster. Apply from map lifecycle events; do not poll
    // or install a process-wide game-help hook.
    inline constexpr bool s_enable_olympus_early_tutorial_bypasses = true;
    // he_03 owns the Flowmotion and Formchange tutorial choices, Team Attacks
    // and Grand Magic help, Attractions tutorial, and later the adjacent Game
    // Help id 0 (Save Points) and id 1 (The Moogle Shop) calls. Skip their
    // exact declined/post-dismiss paths, including Formchange's follow-up
    // situation-command help, suppress its validated true/false ability-set
    // calls, and leave cinematic cleanup, autosave, and story flags intact.
    // AP's controller-unlock grants remain authoritative for Wall Run, Magic,
    // Style Change, Attraction Flow, Shoot Flow, and Athletic Flow.
    inline constexpr bool s_enable_olympus_save_points_tutorial_bypass = true;
    // ca_02_island01_treasure01 uses a latent Game Help id 43 call for Links:
    // Sea Spectacle. Skip only that call through its stock post-dismiss
    // TresEndCinematicMode block. Preserve the delay and balanced cinematic
    // wrapper so input/camera cleanup still runs; CA_ISLAND_EVENT3 is separate.
    inline constexpr bool s_enable_caribbean_sea_spectacle_tutorial_bypass = true;
    // Toy Box opens standalone Game Help panels for Aerial Recovery, Gigas,
    // Mag Assemble, and 8-Bit Blast from four separate level Blueprints.
    // Retarget those exact latent calls to their original post-dismiss paths.
    // Keep cinematic/gimmick cleanup and story flags. The Link patch also
    // bypasses the direct vanilla Pixel Heartbinder grant; AP delivers it.
    inline constexpr bool s_enable_link_tutorial_bypasses = true;
    inline constexpr bool s_enable_informational_help_bypasses = true;
    inline constexpr bool s_enable_toy_box_tutorial_bypasses = true;
    // tt_01's Taking Photos, Lucky Emblems, Links, and Meow Wow Balloon Game
    // Help calls do not grant abilities/items or alter Sora/controller state.
    // Skip those exact calls while preserving their normal post-dismiss flow.
    // Taking Photos still ends cinematic mode and writes the persistent
    // "Using the Gummiphone Camera" Game Help flag before continuing.
    inline constexpr bool s_enable_twilight_town_tutorial_bypasses = true;
    // Shorten only the mandatory Corona story dance. The runtime patch is
    // scoped to the ra_02_mission_Dance level and its live festival manager;
    // it leaves lv_ra_FestivalDance scores and the repeatable minigame untouched.
    inline constexpr bool s_enable_festival_dance_story_fast_completion = true;
    // End the mandatory Beat of Lead encounter through its stock ClearGame
    // route after the first scored mecha kill. The manager's Minigame flag
    // keeps the repeatable post-story score challenge unchanged.
    inline constexpr bool s_enable_beat_of_lead_story_fast_completion = true;
    // Report Classic Kingdom clears from narrow score-update UFunction hooks.
    // No UObject enumeration, validity sweep, or gameplay-state poll is used.
    inline constexpr bool s_enable_classic_kingdom_completion_hooks = true;
    // Parse the external Olympus Coliseum mod's own progress save after the
    // existing KH3-scoped CloseHandle observer reports a completed write. No
    // Blueprint/UObject hook is installed in the arena.
    inline constexpr bool s_enable_olympus_coliseum_completion_hooks = true;
    // Replace the three Kingdom of Corona Lua memory patches with named flag
    // setter hooks. The AreaC rabbit passage enters its stock Level Blueprint
    // opening branch through an exact callsite-scoped predicate override. Never
    // write the rabbit flag or directly mutate passage actors. Reconcile old
    // saves once from the map-name completion event.
    inline constexpr bool s_enable_kingdom_of_corona_qol_hooks = true;
    // Finish the Union Cross ride through its stock final-success route as soon
    // as the ride reports Started. No reaction-command count is required, and
    // no vanilla asset is modified or distributed.
    inline constexpr bool s_enable_union_cross_immediate_completion = true;
    // Advance the Re:Mind Keyblade Graveyard route once when Sora enters
    // bt_DLC/bt_07 by using KH3's reflected SetGameFlag API. Read the value
    // back once for verification; do not continuously poll the story flag.
    inline constexpr bool s_enable_guardians_of_light_slow_mickey_skip = true;
    // Replace the two requested QualityOfLifeActor flag rules with native
    // UE4SS reconciliation. Their enablement comes from canonical seed
    // options in the installed seed pak; no runtime DataTable or actor is involved.
    inline constexpr bool s_enable_quality_of_life_actor_replacement = true;
    // Own native VBonus/Event tracking plus the two useful
    // effects formerly supplied by A_GoA_SavePoint_Logic. None of these paths
    // depend on Sora animation notifies anymore.
    inline constexpr bool s_enable_runtime_world_bootstrap = true;
    // The Armored Xehanort AP location is the authoritative Re:Mind completion
    // signal. Inspect game_state.json only when its file signature changes,
    // then raise (never lower) gameflow_BT_DLC to its final-event threshold.
    inline constexpr bool s_enable_remind_completion_check_reconcile = true;
    // Seed-owned one-copy item rows appear in the normal Moogle Buy menu.
    // Load the seed shop catalog when ShopMenu first opens, then inspect
    // inventory markers when it closes to report purchases. Restore markers
    // for checks already acknowledged by the AP server once the shop UI is
    // safe to leave. Seeds with moogle_shop_checks=0 skip the catalog,
    // game-state reconciliation, reserved-item enum patch, and inventory lane.
    inline constexpr bool s_enable_moogle_shop_checks = true;
    // TresItemData MATERIAL rows 61-80 are unused but remain inside the
    // compiled ETresItemDefMaterial _MAX=100 range. Register explicit enum
    // entries for those reserved rows before the Buy menu builds its catalog.
    inline constexpr bool s_enable_moogle_shop_item_enum_augmentation = true;
    inline constexpr int s_moogle_shop_material_item_index_min = 61;
    inline constexpr int s_moogle_shop_material_item_index_max = 80;
    inline constexpr auto s_moogle_shop_inventory_query_retry_interval = std::chrono::seconds(5);
    inline constexpr auto s_moogle_shop_report_retry_interval = std::chrono::seconds(5);
    inline constexpr bool s_enable_cutscene_skip_candidate_catalog = false;
    inline constexpr bool s_enable_runtime_blocking_ui_probe_hooks = true;
    // Required by save reconciliation: 0x18362C0 captures the automatic-save
    // pre-serialization boundary and CreateFileW confirms the later slot write.
    inline constexpr bool s_enable_runtime_save_tracking = true;
    // KH3 does not use its local Steam game server for single-player runtime
    // behavior. Keep the valid Steam client path and leave the server disabled.
    inline constexpr bool s_bypass_kh3_steam_game_server_init = true;
    // Steam-only save handling. Vanilla Steam remains the sole identity
    // authority. Every recognized Steam SaveGames path is forced to the
    // shared 32-zero account; EGS and unrelated paths are never rewritten.
    inline constexpr bool s_enable_kh3_steam_save_startup_guard = true;
    // Do not patch CreateRetryPoint directly. The direct UE4SS UFunction hook
    // crashed at special-battle entry even with an atomic-counter-only callback.
    // Global ProcessEvent/ProcessInternal detours are disabled in the stable
    // runtime. Keep these diagnostic observers off with them.
    inline constexpr bool s_enable_kh3_retry_point_direct_probe_hooks = false;
    inline constexpr bool s_enable_kh3_retry_point_process_event_observer = false;
    inline constexpr bool s_enable_kh3_retry_blueprint_process_observer = false;
    inline constexpr bool s_enable_kh3_create_retry_point_probe_hook = true;
    inline constexpr bool s_enable_kh3_discard_retry_point_probe_hook = false;
    inline constexpr bool s_enable_runtime_event_flow_probe_hooks = true;
    // KH3's UE4.17 async loader is not compatible with UE4SS's global
    // StaticConstructObject detour. UObjectArray's create listener remains the
    // event-driven observation path; never register the redundant post hook.
    inline constexpr bool s_enable_static_construct_callback = false;
    inline constexpr bool s_enable_runtime_file_polling = true;
    inline constexpr bool s_enable_runtime_hook_integrity_diagnostic = true;
    // Runtime keyblade style unlocks are owned by AP and reconciled against
    // the active Sora/controller from the periodic game-thread update.
    inline constexpr bool s_enable_runtime_keyblade_style_sanity = true;
    inline constexpr bool s_enable_runtime_item_grant_polling = true;
    inline constexpr bool s_enable_runtime_item_grant_candidate_recovery = true;
    inline constexpr bool s_enable_runtime_item_grant_playable_safety = true;
    inline constexpr bool s_enable_runtime_item_grant_apply = true;
    inline constexpr bool s_enable_runtime_grant_signature_dump = false;
    inline constexpr bool s_enable_manual_runtime_item_grant = true;
    inline constexpr bool s_enable_runtime_grant_cpp_profile_events = false;
    // Keep broad runtime profiling off in performance builds. The profiler
    // touches hot update/hook paths and emits frequent UE4SS log lines.
    inline constexpr bool s_enable_runtime_profiling = false;
    // Narrow profiler for the remaining lag suspects. This intentionally
    // avoids profiling every hook while still timing the update-loop chunks and
    // the expensive player-start transition path.
    inline constexpr bool s_enable_runtime_hot_path_profiling = KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS != 0;
    // Low-overhead hitch attribution: phase timestamps stay in memory and only
    // one aggregate UE4SS log line is emitted per interval when a slow update
    // or adaptive frame gap was actually observed.
    inline constexpr bool s_enable_runtime_hitch_diagnostics = KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS != 0;
    inline constexpr auto s_runtime_hitch_work_threshold = std::chrono::microseconds(1500);
    // A 120 FPS baseline is about 8.3 ms, but KH3 commonly presents individual
    // 16.7 ms frames without a perceptible hitch. Keep the adaptive threshold
    // above that normal 120/60 cadence while retaining sub-50 ms hitch coverage.
    inline constexpr auto s_runtime_hitch_min_frame_gap = std::chrono::microseconds(25000);
    inline constexpr uint64_t s_runtime_hitch_baseline_multiplier_numerator = 5;
    inline constexpr uint64_t s_runtime_hitch_baseline_multiplier_denominator = 2;
    inline constexpr auto s_runtime_hitch_log_interval = std::chrono::seconds(2);
    inline constexpr uint64_t s_runtime_hitch_warmup_samples = 30;
    // Narrow native-hook profiler used while optimizing on_native_function.
    // Keep the broad runtime profiler off; this only times selected shared
    // setup, parameter scan, and active non-grant branch blocks.
    inline constexpr bool s_enable_native_function_hot_path_profiling = false;
    // Targeted level-up profiler. This stays narrow enough for normal play and
    // separates owner classification, battle-level ProcessEvent work, and
    // level-event sidecar I/O without enabling the broad per-frame profiler.
    inline constexpr bool s_enable_runtime_sora_level_profiling = KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS != 0;
    inline constexpr bool s_enable_hold_camera_runtime_grant_gate_probe_hooks = false;
    inline constexpr bool s_enable_shootlock_runtime_grant_gate = true;
    inline constexpr bool s_enable_current_state_write_polyhook = true;
    // Live state events drive native grant-safety and GameOver -> Retry handling
    // even when KH3 never opens a save slot. The client keeps their diagnostic
    // history in bounded memory; event publication must not depend on disk logs.
    inline constexpr bool s_enable_current_state_write_polyhook_event_log = true;
    inline constexpr bool s_enable_runtime_sora_state_sidecar = true;
    inline constexpr bool s_enable_runtime_save_point_spawn_request = true;
    inline constexpr bool s_enable_runtime_save_menu_command = true;
    inline constexpr bool s_enable_runtime_map_jump_command = true;
    inline constexpr bool s_enable_runtime_portal_spawn_request = false;
    // Keep HE_SAVE07 as a menu destination; Mt. Olympus does not need a spawned
    // save-point actor or the associated GoA save-point slot sync on arrival.
    inline constexpr bool s_enable_runtime_he02_save_point_auto_spawn = false;
    inline constexpr bool s_enable_runtime_fz02_save_point_auto_spawn = true;
    inline constexpr bool s_enable_runtime_bx02_portal_auto_spawn = true;
    inline constexpr bool s_enable_runtime_he02_death_mirror_auto_spawn = true;
    // GoA already writes these flags before sending the player to the Caribbean,
    // but CA's first-visit initialization can run afterward and leave its chart
    // point cache empty until the next zone load. Arm the authored GoA set at
    // player start, then reassert it only after the map-name callback and a full
    // tick delay. Calling SetGameFlagBoolean from HandleStartingNewPlayer can
    // make CA's async-loading flush wait forever.
    inline constexpr bool s_enable_caribbean_chart_visit_unlock_on_player_start = true;
    inline constexpr auto s_caribbean_chart_visit_unlock_post_map_ready_delay = std::chrono::milliseconds(1000);
    inline constexpr auto s_caribbean_chart_visit_unlock_retry_delay = std::chrono::milliseconds(2000);
    inline constexpr int s_caribbean_chart_visit_unlock_max_attempts = 3;
    inline constexpr bool s_enable_runtime_save_point_lifetime_deep_samples = false;
    inline constexpr bool s_enable_runtime_nearby_actor_scan_request = true;
    inline constexpr bool s_enable_runtime_sora_wallrun_idle_clamp = false;
    inline constexpr bool s_enable_runtime_sora_state_event_log = false;
    // Feature path: convert generated Lucky Mark AP reward tokens into native
    // item ids, then replace the KH3 popup text from the generated item row.
    inline constexpr bool s_enable_lucky_mark_native_ap_resolver = true;
    inline constexpr bool s_enable_lucky_mark_native_ap_datatable_text = true;
    inline constexpr bool s_enable_lucky_mark_native_ap_shell_item_text_initializer = true;
    inline constexpr bool s_enable_lucky_mark_native_ap_fstring_assignment = true;
    // Hook installation byte dumps are diagnostic-build evidence only. Keep
    // synchronous per-call JSONL and the one-time row dump off by default.
    inline constexpr bool s_enable_lucky_mark_native_ap_resolver_install_diagnostics = KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS != 0;
    inline constexpr bool s_enable_lucky_mark_native_ap_resolver_event_diagnostics = false;
    inline constexpr bool s_enable_lucky_mark_native_ap_datatable_row_dump_diagnostic = false;
    inline constexpr bool s_enable_lucky_mark_native_ftext_data_diagnostic = false;
    inline constexpr int s_lucky_mark_native_text_shell_item_id = 517;
    // These native functions live at different RVAs in the Steam and Epic
    // executables. The Epic layout below is the 1.0.0.0 / CL-1065258 image
    // (SHA256 E4EDB6FFDF8BD70AECF838ECF6A72663B13453E99E19559FDF2AA7C689C4B407).
    // Installation still requires the platform-specific byte guards in
    // find_lucky_mark_native_ap_resolver_target to match before hooking.
    inline constexpr uint64_t s_lucky_mark_reward_token_to_item_id_steam_offset = 0x112F510;
    inline constexpr uint64_t s_lucky_mark_item_id_to_reward_text_steam_offset = 0x157ED30;
    inline constexpr uint64_t s_lucky_mark_reward_token_to_item_id_egs_offset = 0x13ED0B0;
    inline constexpr uint64_t s_lucky_mark_item_id_to_reward_text_egs_offset = 0x18D91E0;
    inline constexpr int s_lucky_mark_native_ap_resolver_max_diagnostic_events = 1400;
    inline constexpr int s_lucky_mark_native_ap_resolver_converter_sample_events = 64;
    inline constexpr std::wstring_view s_build_stamp = L"KH3AP 2.0.0 2026-11-07";
    inline constexpr bool s_enable_starting_player_pawn_cache_hook = true;
    // The he_02 tutorial bytecode bypass now removes its Shoot Flow false call
    // directly. Keep the old recovery service dormant so it cannot grant Shoot
    // Flow independently of Archipelago ownership.
    inline constexpr bool s_enable_he02_shoot_flow_restore = false;
    inline constexpr auto s_he02_shoot_flow_restore_settle_delay = std::chrono::milliseconds(250);
    // Prewarm reflection metadata only. Bad-status UObject instances remain
    // lazy so no map-owned state is rooted across vanilla save-point travel.
    inline constexpr bool s_enable_starting_player_bad_status_cache_prewarm = true;
    inline constexpr bool s_enable_twilight_town_bistro_reward_popup_suppression = false;
    // Requests are published by atomic rename and are safe to apply on the
    // first dedicated grant poll once the existing context gates pass.
    inline constexpr auto s_runtime_item_grant_delay = std::chrono::milliseconds(0);
    inline constexpr auto s_runtime_item_grant_special_battle_exit_delay = std::chrono::seconds(5);
    inline constexpr auto s_runtime_item_grant_ready_context_debounce = std::chrono::milliseconds(1000);
    // Grant safety still polls every runtime-file interval. This only controls
    // the repeated diagnostic heartbeat while an unchanged gate remains active.
    inline constexpr auto s_runtime_item_grant_slow_wait_log_delay = std::chrono::seconds(10);
    inline constexpr auto s_runtime_ap_message_quiet_window = std::chrono::milliseconds(3000);
    inline constexpr auto s_runtime_blocking_ui_focus_handoff_grace =
            std::chrono::milliseconds(250);
    inline constexpr auto s_runtime_file_poll_startup_delay = std::chrono::seconds(10);
    inline constexpr auto s_runtime_file_poll_interval = std::chrono::milliseconds(500);
    inline constexpr auto s_runtime_profile_log_interval = std::chrono::seconds(5);
    inline constexpr auto s_runtime_profile_slow_log_threshold = std::chrono::milliseconds(25);
    inline constexpr auto s_runtime_profile_slow_log_interval = std::chrono::seconds(1);
    inline constexpr auto s_background_drain_interval = std::chrono::milliseconds(500);
    inline constexpr auto s_sora_controller_reacquire_interval = std::chrono::seconds(5);
    inline constexpr auto s_runtime_sora_state_sidecar_dirty_interval = std::chrono::seconds(1);
    inline constexpr auto s_runtime_sora_state_sidecar_fallback_interval = std::chrono::seconds(5);
    inline constexpr auto s_lucky_emblem_tracker_retry_interval =
            std::chrono::milliseconds(500);
    inline constexpr auto s_runtime_quality_of_life_retry_interval =
            std::chrono::seconds(1);
    inline constexpr auto s_runtime_world_bootstrap_retry_interval =
            std::chrono::milliseconds(250);
    inline constexpr auto s_runtime_world_bootstrap_asset_load_retry_interval =
            std::chrono::seconds(2);
    inline constexpr auto s_runtime_he02_save_point_auto_spawn_stable_delay = std::chrono::seconds(5);
    inline constexpr auto s_runtime_he02_save_point_auto_spawn_retry_interval = std::chrono::seconds(5);
    inline constexpr auto s_runtime_fz02_save_point_auto_spawn_stable_delay = std::chrono::seconds(5);
    inline constexpr auto s_runtime_fz02_save_point_auto_spawn_retry_interval = std::chrono::seconds(5);
    inline constexpr auto s_runtime_bx02_portal_auto_spawn_stable_delay = std::chrono::seconds(3);
    inline constexpr auto s_runtime_bx02_portal_auto_spawn_retry_interval = std::chrono::seconds(3);
    inline constexpr auto s_runtime_bx02_portal_auto_spawn_verification_delay = std::chrono::seconds(20);
    inline constexpr auto s_runtime_he02_death_mirror_auto_spawn_stable_delay = std::chrono::seconds(3);
    inline constexpr auto s_runtime_he02_death_mirror_auto_spawn_retry_interval = std::chrono::seconds(3);
    inline constexpr auto s_runtime_he02_death_mirror_auto_spawn_verification_delay = std::chrono::seconds(20);
    inline constexpr auto s_bridge_dir_cache_ttl = std::chrono::seconds(1);
    inline constexpr std::wstring_view s_bridge_protocol_name = L"kh3_ap_file_bridge";
    inline constexpr int s_bridge_protocol_version = 1;
    inline constexpr std::wstring_view s_bp_get_item_path = L"/Script/TresGame.TresGameBlueprintLibrary:BP_TresGetItem";
    inline constexpr std::wstring_view s_bp_have_item_path = L"/Script/TresGame.TresGameBlueprintLibrary:BP_TresHaveItem";
    inline constexpr std::wstring_view s_bp_add_munny_path = L"/Script/TresGame.TresGameBlueprintLibrary:BP_AddMunny";
    inline constexpr std::wstring_view s_actor_get_location_path = L"/Script/Engine.Actor:K2_GetActorLocation";
    inline constexpr std::wstring_view s_save_point_class_path = L"/Game/Blueprints/Gimmick/ex/g_ex_SavePoint.g_ex_SavePoint_C";
    inline constexpr std::wstring_view s_save_point_blueprint_path = L"/Game/Blueprints/Gimmick/ex/g_ex_SavePoint.g_ex_SavePoint";
    inline constexpr std::wstring_view s_goa_save_point_data_class_path =
            L"/Game/Blueprints/GoA/SaveData/GoASavePointData.GoASavePointData_C";
    inline constexpr std::wstring_view s_goa_save_point_data_blueprint_path =
            L"/Game/Blueprints/GoA/SaveData/GoASavePointData.GoASavePointData";
    inline constexpr std::wstring_view s_runtime_save_point_default_player_start_tag = L"he_02_enemy_aes";
    inline constexpr std::wstring_view s_runtime_save_point_default_my_reactor_path = L"TresReactorComponent'/Game/Blueprints/Gimmick/ex/g_ex_SavePoint.Default__g_ex_SavePoint_C:TresReactor0'";
    inline constexpr std::wstring_view s_runtime_save_point_default_my_recover_path = L"TresSavePointRecoverComponent'/Game/Blueprints/Gimmick/ex/g_ex_SavePoint.Default__g_ex_SavePoint_C:TresRecover0'";
    inline constexpr std::wstring_view s_runtime_save_point_default_my_map_marker_path = L"TresMapMarkerComponent'/Game/Blueprints/Gimmick/ex/g_ex_SavePoint.Default__g_ex_SavePoint_C:TresMapMarker0'";
    inline constexpr std::wstring_view s_runtime_he02_save_point_auto_spawn_map_path = L"/Game/Levels/he/he_02/he_02";
    inline constexpr std::wstring_view s_runtime_he02_save_point_auto_spawn_player_start_tag = L"he_02_AutoSave_02";
    inline constexpr std::wstring_view s_runtime_he02_save_point_auto_spawn_disp_map_name = L"he_02_An_02";
    inline constexpr std::wstring_view s_runtime_he02_save_point_auto_spawn_slot_name = L"SavePointHE";
    inline constexpr std::wstring_view s_runtime_he02_save_point_auto_spawn_level_path_property = L"HELevelPath";
    inline constexpr std::wstring_view s_runtime_he02_save_point_auto_spawn_player_start_property = L"HEPlayerStart";
    inline constexpr std::wstring_view s_final_world_story_ew_flag = L"gameflow_EW";
    inline constexpr double s_runtime_he02_save_point_auto_spawn_x = 2785.772;
    inline constexpr double s_runtime_he02_save_point_auto_spawn_y = 15449.859;
    inline constexpr double s_runtime_he02_save_point_auto_spawn_z = -6220.677;
    inline constexpr std::wstring_view s_runtime_fz02_save_point_auto_spawn_map_path = L"/Game/Levels/fz/fz_02/fz_02";
    inline constexpr std::wstring_view s_runtime_fz02_save_point_auto_spawn_player_start_tag = L"fz_02_Lv_AP_Save_01";
    inline constexpr std::wstring_view s_runtime_fz02_save_point_auto_spawn_disp_map_name = L"fz_02_An_02";
    inline constexpr std::wstring_view s_runtime_fz02_save_point_auto_spawn_slot_name = L"SavePointFZ";
    inline constexpr std::wstring_view s_runtime_fz02_save_point_auto_spawn_level_path_property = L"FZLevelPath";
    inline constexpr std::wstring_view s_runtime_fz02_save_point_auto_spawn_player_start_property = L"FZPlayerStart";
    inline constexpr double s_runtime_fz02_save_point_auto_spawn_x = 4130.931;
    inline constexpr double s_runtime_fz02_save_point_auto_spawn_y = 8915.027;
    inline constexpr double s_runtime_fz02_save_point_auto_spawn_z = 11893.331;
    inline constexpr double s_runtime_fz02_player_start_z = 12002.750;
    inline constexpr double s_runtime_fz02_player_start_yaw = 165.0;
    inline constexpr std::wstring_view s_bx_portal_class_path =
            L"/Game/Maps/GardenOfAssemblage/uasset/Textures/Portals/BX_Portal.BX_Portal_C";
    inline constexpr std::wstring_view s_bx_portal_blueprint_path =
            L"/Game/Maps/GardenOfAssemblage/uasset/Textures/Portals/BX_Portal.BX_Portal";
    inline constexpr std::wstring_view s_runtime_bx02_portal_auto_spawn_map_path = L"/Game/Levels/bx/bx_02/bx_02";
    inline constexpr double s_runtime_bx02_portal_auto_spawn_x = -14412.544;
    inline constexpr double s_runtime_bx02_portal_auto_spawn_y = -37426.406;
    inline constexpr double s_runtime_bx02_portal_auto_spawn_z = 0.150;
    inline constexpr std::wstring_view s_death_mirror_class_path =
            L"/Game/Blueprints/GoA/Actors/DeathMirror/A_GoA_DeathMirror.A_GoA_DeathMirror_C";
    inline constexpr std::wstring_view s_death_mirror_blueprint_path =
            L"/Game/Blueprints/GoA/Actors/DeathMirror/A_GoA_DeathMirror.A_GoA_DeathMirror";
    inline constexpr std::wstring_view s_runtime_he02_death_mirror_auto_spawn_map_path =
            L"/Game/Levels/he/he_02/he_02";
    // The Blueprint's visible mirror plane is offset from its actor origin by
    // approximately (1.844, -1136.221, -0.600). These origin coordinates put
    // that plane at the calibrated HE02 target, 450 units below Sora's sample,
    // while preserving the actor's existing rotation.
    inline constexpr double s_runtime_he02_death_mirror_auto_spawn_x = 828.061;
    inline constexpr double s_runtime_he02_death_mirror_auto_spawn_y = -2744.299;
    inline constexpr double s_runtime_he02_death_mirror_auto_spawn_z = -539.405;
    inline constexpr std::wstring_view s_gfx_object_get_member_value_path = L"/Script/ScaleformUI.GFxObject:GetMemberValue";
    inline constexpr std::wstring_view s_gfx_object_set_member_value_path = L"/Script/ScaleformUI.GFxObject:SetMemberValue";
    inline constexpr std::wstring_view s_gfx_object_invoke_path = L"/Script/ScaleformUI.GFxObject:Invoke";
    inline constexpr std::wstring_view s_tres_game_blueprint_library_default_object_path = L"/Script/TresGame.Default__TresGameBlueprintLibrary";
    inline constexpr bool s_enable_bp_tres_set_battle_level = true;
    inline constexpr std::wstring_view s_bp_get_vbonus_path = L"/Script/TresGame.TresGameBlueprintLibrary:BP_TresGetVictoryBonus";
    inline constexpr std::wstring_view s_tres_get_vbonus_path = L"/Script/TresGame.TresGameBlueprintLibrary:TresGetVictoryBonus";
    inline constexpr std::wstring_view s_wait_bp_get_multiitem_path = L"/Script/TresGame.TresGameBlueprintLibrary:WaitBPTresGetMultiItem";
    inline constexpr std::wstring_view s_bp_ability_add_path = L"/Script/TresGame.TresCharPawnBase:BP_AbilityAdd";
    inline constexpr std::wstring_view s_bp_ability_remove_path = L"/Script/TresGame.TresCharPawnBase:BP_AbilityRemove";
    inline constexpr std::wstring_view s_bp_ability_get_level_path = L"/Script/TresGame.TresCharPawnBase:BP_AbilityGetLevel";
    inline constexpr uint64_t s_vanilla_level_write_offset = 0x576EA2;
    inline constexpr std::wstring_view s_bp_ability_get_equip_num_path = L"/Script/TresGame.TresCharPawnBase:BP_AbilityGetEquipNum";
    inline constexpr std::wstring_view s_bp_is_ability_enable_path = L"/Script/TresGame.TresCharPawnBase:BP_IsAbilityEnable";
    inline constexpr std::wstring_view s_save_game_to_slot_path = L"/Script/Engine.GameplayStatics:SaveGameToSlot";
    inline constexpr std::wstring_view s_load_game_from_slot_path = L"/Script/Engine.GameplayStatics:LoadGameFromSlot";
    inline constexpr std::wstring_view s_create_save_game_object_path = L"/Script/Engine.GameplayStatics:CreateSaveGameObject";
    inline constexpr std::wstring_view s_gameplay_statics_default_object_path = L"/Script/Engine.Default__GameplayStatics";
    inline constexpr std::wstring_view s_retry_anim_notify_received_notify_path = L"/Game/Mods/KH3Randomizer/RetryAnimNotify.RetryAnimNotify_C:Received_Notify";
    inline constexpr std::wstring_view s_retry_logic_actor_receive_begin_play_path = L"/Game/Mods/KH3Randomizer/RetryLogicActor.RetryLogicActor_C:ReceiveBeginPlay";
    inline constexpr std::wstring_view s_set_game_flag_path = L"/Script/TresGame.TresGameBlueprintLibrary:SetGameFlag";
    inline constexpr std::wstring_view s_world_map_progress_flag = L"gameflow_WM";
    inline constexpr std::wstring_view s_set_game_flag_boolean_path = L"/Script/TresGame.TresGameBlueprintLibrary:SetGameFlagBoolean";
    inline constexpr std::wstring_view s_get_game_flag_path = L"/Script/TresGame.TresGameBlueprintLibrary:GetGameFlag";
    inline constexpr std::wstring_view s_get_game_flag_boolean_path = L"/Script/TresGame.TresGameBlueprintLibrary:GetGameFlagBoolean";
    inline constexpr std::wstring_view s_set_flag_label_path = L"/Script/TresGame.TresGameBlueprintLibrary:SetFlagLabel";
    inline constexpr std::wstring_view s_greater_equal_flag_label_path = L"/Script/TresGame.TresGameBlueprintLibrary:GreaterEqual_FlagLabel";
    inline constexpr std::wstring_view s_starlight_runtime_token = L"WEP_KEYBLADE_SO_018";
    inline constexpr std::wstring_view s_starlight_story_flag = L"gameflow_KG_got_Starlight";
    inline constexpr std::wstring_view s_caribbean_level_path_token = L"/game/levels/ca/";
    inline constexpr std::wstring_view s_caribbean_chart_visit_flags[] = {
            L"gameflow_CA_chart_visit01_island01_Pirates",
            L"gameflow_CA_chart_visit02_island02_TwoWay",
            L"gameflow_CA_chart_visit03_island03_Sunkenship",
            L"gameflow_CA_chart_visit04_island04_Monkey",
            L"gameflow_CA_chart_visit05_island05_Lagoon",
            L"gameflow_CA_chart_visit06_island06_Forest",
            L"gameflow_CA_chart_visit08_island08_Rock",
            L"gameflow_CA_chart_visit09_island09_Desert",
            L"gameflow_CA_chart_visit10_island10_Center",
            L"gameflow_CA_chart_visit11_island12_West",
            L"gameflow_CA_chart_visit12_island14_FrontPortroyal",
    };
    inline constexpr std::wstring_view s_bp_tres_start_game_over_path = L"/Script/TresGame.TresGameBlueprintLibrary:BP_TresStartGameOver";
    inline constexpr std::wstring_view s_tres_set_special_battle_mode_path = L"/Script/TresGame.TresGameBlueprintLibrary:TresSetSpecialBattleMode";
    inline constexpr std::wstring_view s_tres_set_pause_menu_type_path = L"/Script/TresGame.TresLevelScriptActor:TresSetPauseMenuType";
    inline constexpr std::wstring_view s_tres_level_script_actor_on_event_skip_path = L"/Script/TresGame.TresLevelScriptActor:OnEventSkip";
    inline constexpr std::wstring_view s_kismet_print_string_path = L"/Script/Engine.KismetSystemLibrary:PrintString";
    inline constexpr std::wstring_view s_kismet_system_library_default_object_path = L"/Script/Engine.Default__KismetSystemLibrary";
    inline constexpr std::wstring_view s_tresui_open_information_path = L"/Script/TresGame.TresGameBlueprintLibrary:TresUI_OpenInformation";
    inline constexpr std::wstring_view s_tresui_clear_information_text_cache_path = L"/Script/TresGame.TresGameBlueprintLibrary:TresUI_ClearInformationTextCache";
    inline constexpr std::wstring_view s_tresui_open_center_information_path = L"/Script/TresGame.TresGameBlueprintLibrary:TresUI_OpenCenterInformation";
    inline constexpr std::wstring_view s_tresui_open_message_path = L"/Script/TresGame.TresGameBlueprintLibrary:TresUI_OpenMessage";
    inline constexpr std::wstring_view s_tresui_map_in_notice_message_path = L"/Script/TresGame.TresGameBlueprintLibrary:TresUI_MapInNoticeMessage";
    inline constexpr std::wstring_view s_apply_map_name_path = L"/Script/TresGame.TresAreaNameStatics:ApplyMapName";
    inline constexpr std::wstring_view s_tres_map_jump_path = L"/Script/TresGame.TresGameBlueprintLibrary:TresMapJump";
    inline constexpr std::wstring_view s_handle_starting_new_player_path = L"/Script/Engine.GameModeBase:HandleStartingNewPlayer";
    inline constexpr std::wstring_view s_get_local_tres_player_pawn_base_path = L"/Script/TresGame.TresGameBlueprintLibrary:GetLocalTresPlayerPawnBase";
    inline constexpr std::wstring_view s_find_player_start_path = L"/Script/Engine.GameModeBase:FindPlayerStart";
    inline constexpr std::wstring_view s_hidden_mickey_is_lucky_mark_recorded_function_name = L"IsLuckyMarkRecorded";
    inline constexpr std::wstring_view s_lucky_emblem_mark_id_property_name = L"m_MarkID";
    inline constexpr std::wstring_view s_tres_reactor_do_command_delegate_signature_path =
        L"/Script/TresGame.TresReactorDoCommandSignature__DelegateSignature";
    inline constexpr std::wstring_view s_tres_player_controller_base_bp_notify_set_action_command1_path = L"/Script/TresGame.TresPlayerControllerBase:BP_NotifySetActionCommand1";
    inline constexpr std::wstring_view s_tres_player_controller_base_simulate_keypress_path = L"/Script/TresGame.TresPlayerControllerBase:SimulateKeypress";
    inline constexpr std::wstring_view s_tres_player_controller_base_bp_set_ability_wallrun_path = L"/Script/TresGame.TresPlayerControllerBase:BP_SetAbilityWallRun";
    inline constexpr std::wstring_view s_bp_get_equip_weapon_id_path = L"/Script/TresGame.TresCharPawnBase:BP_GetEquipWeaponID";
    inline constexpr std::wstring_view s_tres_player_controller_base_bp_set_ability_style_change_path = L"/Script/TresGame.TresPlayerControllerBase:BP_SetAbilityStyleChange";
    inline constexpr std::wstring_view s_tres_player_controller_base_bp_set_restrict_style_path = L"/Script/TresGame.TresPlayerControllerBase:BP_SetRestrictStyle";
    inline constexpr std::wstring_view s_tres_player_controller_base_bp_notify_start_special_anti_form_path = L"/Script/TresGame.TresPlayerControllerBase:BP_NotifyStartSpecialAntiForm";
    inline constexpr std::wstring_view s_tres_player_controller_base_bp_set_ability_magic_path = L"/Script/TresGame.TresPlayerControllerBase:BP_SetAbilityMagic";
    inline constexpr std::wstring_view s_tres_player_controller_base_bp_set_ability_attraction_flow_path = L"/Script/TresGame.TresPlayerControllerBase:BP_SetAbilityAttractionFlow";
    inline constexpr std::wstring_view s_tres_player_controller_base_bp_set_ability_athletic_flow_path = L"/Script/TresGame.TresPlayerControllerBase:BP_SetAbilityAthleticFlow";
    inline constexpr std::wstring_view s_tres_enemy_pawn_e_ex_301_class_path = L"/Script/TresGame.TresEnemyPawnBase";
    inline constexpr std::wstring_view s_tres_enemy_pawn_e_ex_301_is_ascension_mode_path = L"/Script/TresGame.TresEnemyPawnEEx301:IsAscensionMode";
    inline constexpr std::wstring_view s_tres_player_controller_base_notify_start_action_command_path = L"/Script/TresGame.TresPlayerControllerBase:NotifyStartActionCommand";
    inline constexpr std::wstring_view s_he01_level_path_prefix = L"/Game/Levels/he/he_01/";
    inline constexpr std::wstring_view s_he01_gameflow_execute_ubergraph_path = L"/Game/Levels/he/he_01/umap/he_01_gameflow.he_01_gameflow_C:ExecuteUbergraph_he_01_gameflow";
    inline constexpr std::wstring_view s_he02_level_path_prefix = L"/Game/Levels/he/he_02/";
    inline constexpr std::wstring_view s_he02_gameflow_execute_ubergraph_path = L"/Game/Levels/he/he_02/umap/he_02_gameflow.he_02_gameflow_C:ExecuteUbergraph_he_02_gameflow";
    inline constexpr std::wstring_view s_he03_level_path_prefix = L"/Game/Levels/he/he_03/";
    inline constexpr std::wstring_view s_he03_gameflow_execute_ubergraph_path = L"/Game/Levels/he/he_03/umap/he_03_gameflow.he_03_gameflow_C:ExecuteUbergraph_he_03_gameflow";
    inline constexpr std::wstring_view s_ca02_level_path_prefix = L"/Game/Levels/ca/ca_02/";
    inline constexpr std::wstring_view s_ca02_island01_treasure01_execute_ubergraph_path = L"/Game/Levels/ca/ca_02/umap/ca_02_island01_treasure01.ca_02_island01_treasure01_C:ExecuteUbergraph_ca_02_island01_treasure01";
    inline constexpr std::wstring_view s_ts01_level_path_prefix = L"/Game/Levels/ts/ts_01/";
    inline constexpr std::wstring_view s_ts01_gameflow_execute_ubergraph_path = L"/Game/Levels/ts/ts_01/umap/ts_01_gameflow.ts_01_gameflow_C:ExecuteUbergraph_ts_01_gameflow";
    inline constexpr std::wstring_view s_ts02_level_path_prefix = L"/Game/Levels/ts/ts_02/";
    inline constexpr std::wstring_view s_ts02_camera_ms030e_execute_ubergraph_path = L"/Game/Levels/ts/ts_02/umap/ts_02_camera_ms030e.ts_02_camera_ms030e_C:ExecuteUbergraph_ts_02_camera_ms030e";
    inline constexpr std::wstring_view s_ts02_denjigimmickfront_execute_ubergraph_path = L"/Game/Levels/ts/ts_02/umap/ts_02_Denjigimmickfront.ts_02_Denjigimmickfront_C:ExecuteUbergraph_ts_02_Denjigimmickfront";
    inline constexpr std::wstring_view s_ts03_level_path_prefix = L"/Game/Levels/ts/ts_03/";
    inline constexpr std::wstring_view s_ts03_gameflow_execute_ubergraph_path = L"/Game/Levels/ts/ts_03/umap/ts_03_gameflow.ts_03_gameflow_C:ExecuteUbergraph_ts_03_gameflow";
    inline constexpr std::wstring_view s_tt01_level_path_prefix = L"/Game/Levels/tt/tt_01/";
    inline constexpr std::wstring_view s_tt01_gameflow_execute_ubergraph_path = L"/Game/Levels/tt/tt_01/umap/tt_01_gameflow.tt_01_gameflow_C:ExecuteUbergraph_tt_01_gameflow";
    inline constexpr std::wstring_view s_ew01_gameflow_execute_ubergraph_path = L"/Game/Levels/ew/ew_01/umap/ew_01_gameflow.ew_01_gameflow_C:ExecuteUbergraph_ew_01_gameflow";
    inline constexpr std::wstring_view s_ew04_gameflow_execute_ubergraph_path = L"/Game/Levels/ew_DLC/ew_04/umap/ew_04_gameflow.ew_04_gameflow_C:ExecuteUbergraph_ew_04_gameflow";
    inline constexpr std::wstring_view s_remy_final_milestone_reward_path = L"/Script/TresGame.TresGameBlueprintLibrary:RemyGetFinalMilestoneRewardAndShowMsg";
    inline constexpr std::wstring_view s_tresui_show_get_treasure_prize_snow_curling_path = L"/Script/TresGame.TresGameBlueprintLibrary:TresUI_ShowGetTreasurePrizeSnowCurling";
    inline constexpr std::wstring_view s_tresui_show_get_bonus_point_snow_curling_path = L"/Script/TresGame.TresGameBlueprintLibrary:TresUI_ShowGetBonusPointSnowCurling";
    inline constexpr std::wstring_view s_uip_map_name_on_callback_path = L"/Script/TresGame.TresUIP_MapName:OnCallback";
    inline constexpr std::wstring_view s_uip_multi_item_get_on_callback_path = L"/Script/TresGame.TresUIP_MultiItemGet:OnCallback";
    inline constexpr std::wstring_view s_bp_get_item_name = L"BP_TresGetItem";
    inline constexpr std::wstring_view s_bp_get_vbonus_name = L"BP_TresGetVictoryBonus";
    inline constexpr std::wstring_view s_tres_get_vbonus_name = L"TresGetVictoryBonus";
    inline constexpr std::wstring_view s_wait_bp_get_vbonus_name = L"WaitBPTresGetVictoryBonus";
    inline constexpr std::wstring_view s_wait_bp_get_keyitem_name = L"WaitBPTresGetKeyitem";
    inline constexpr std::wstring_view s_wait_bp_get_multiitem_name = L"WaitBPTresGetMultiItem";
    inline constexpr std::wstring_view s_bp_ability_add_name = L"BP_AbilityAdd";
    inline constexpr std::wstring_view s_bp_ability_remove_name = L"BP_AbilityRemove";
    inline constexpr std::wstring_view s_bp_ability_get_level_name = L"BP_AbilityGetLevel";
    inline constexpr std::wstring_view s_bp_ability_get_equip_num_name = L"BP_AbilityGetEquipNum";
    inline constexpr std::wstring_view s_bp_is_ability_enable_name = L"BP_IsAbilityEnable";
    inline constexpr std::wstring_view s_bp_get_prize_name = L"BP_TresGetPrize";
    inline constexpr std::wstring_view s_bpev_get_item_name = L"BPEV_GetItem";
    inline constexpr std::wstring_view s_tres_notify_pre_load_map_name = L"TresNotifyOnPreLoadMap";
    inline constexpr std::wstring_view s_tres_notify_post_load_map_name = L"TresNotifyOnPostLoadMap";
    inline constexpr std::wstring_view s_bp_notify_pre_load_map_name = L"BPNotifyOnPreLoadMap";
    inline constexpr std::wstring_view s_bp_notify_post_load_map_name = L"BPNotifyOnPostLoadMap";
    inline constexpr std::wstring_view s_create_auto_save_data_name = L"CreateAutoSaveData";
    inline constexpr std::wstring_view s_create_auto_save_data_ex_name = L"CreateAutoSaveDataEx";
    inline constexpr std::wstring_view s_create_retry_point_name = L"CreateRetryPoint";
    inline constexpr std::wstring_view s_discard_retry_point_name = L"DiscardRetryPoint";
    inline constexpr std::wstring_view s_tres_set_special_battle_mode_name = L"TresSetSpecialBattleMode";
    inline constexpr std::wstring_view s_tres_set_pause_menu_type_name = L"TresSetPauseMenuType";
    inline constexpr std::wstring_view s_apply_map_name_name = L"ApplyMapName";
    inline constexpr std::wstring_view s_tres_show_map_name_name = L"TresShowMapName";
    inline constexpr std::wstring_view s_tres_map_jump_name = L"TresMapJump";
    inline constexpr std::wstring_view s_handle_starting_new_player_name = L"HandleStartingNewPlayer";
    inline constexpr std::wstring_view s_find_player_start_name = L"FindPlayerStart";
    inline constexpr std::wstring_view s_tres_player_controller_base_bp_set_ability_shoot_flow_name = L"BP_SetAbilityShootFlow";
    inline constexpr std::wstring_view s_bp_unlock_battle_portal_reward_action_name = L"BP_TresUnlockBattlePortalRewardAction";
    inline constexpr std::wstring_view s_remy_final_milestone_reward_name = L"RemyGetFinalMilestoneRewardAndShowMsg";
    inline constexpr std::wstring_view s_tresui_show_get_treasure_prize_snow_curling_name = L"TresUI_ShowGetTreasurePrizeSnowCurling";
    inline constexpr std::wstring_view s_tresui_show_get_bonus_point_snow_curling_name = L"TresUI_ShowGetBonusPointSnowCurling";
    inline constexpr std::wstring_view s_uip_map_name_on_callback_name = L"OnCallback";
    inline constexpr std::wstring_view s_uip_multi_item_get_on_callback_name = L"OnCallback";
    inline constexpr std::wstring_view s_vehicle_get_troops_reward_data_name = L"BP_GetTroopsRewardData";
    inline constexpr std::wstring_view s_treasure_box_anim_notify_name = L"TreasureBoxAnimNotify";
    inline constexpr std::wstring_view s_ap_treasure_looted_name = L"AP_TreasureLooted";
    inline constexpr std::wstring_view s_treasure_logic_actor_get_treasure_name_name = L"TreasureLogicActorGetTreasureName";
    inline constexpr std::wstring_view s_treasure_logic_actor_get_smallest_vector_name = L"TreasureLogicActorGetSmallestVector";
    inline constexpr std::wstring_view s_treasure_logic_actor_receive_begin_play_name = L"TreasureLogicActorReceiveBeginPlay";
    inline constexpr std::wstring_view s_treasure_logic_actor_execute_ubergraph_name = L"TreasureLogicActorExecuteUbergraph";
    inline constexpr std::wstring_view s_tres_treasure_box_open_optional_name = L"TresTreasureBoxOpenOptionalOpenedBox";
    inline constexpr std::wstring_view s_tres_gimmick_actor_receive_do_command_name = L"TresGimmickActorReceiveGimmickDoCommand";
    inline constexpr std::wstring_view s_convert_accessory_name = L"BP_TresConvertItemIDtoKeyNameAccessory";
    inline constexpr std::wstring_view s_convert_protector_name = L"BP_TresConvertItemIDtoKeyNameProtector";
    inline constexpr std::wstring_view s_convert_weapon_name = L"BP_TresConvertItemIDtoKeyNameWeapon";
    inline constexpr std::wstring_view s_convert_report_name = L"BP_TresConvertItemIDtoKeyNameReport";
    inline constexpr std::wstring_view s_convert_keyitem_name = L"BP_TresConvertItemIDtoKeyNameKeyitem";
    inline constexpr std::wstring_view s_convert_battle_name = L"BP_TresConvertBattleItemIDtoKeyName";
    inline constexpr std::wstring_view s_default_output_path = L"Mods\\KH3APProbeCpp\\runtime_loot_event.json";
    inline constexpr std::wstring_view s_runtime_item_grant_filename = kh3ap::channels::runtime_item_grant_file;
    inline constexpr std::wstring_view s_runtime_item_grant_priority_filename = kh3ap::channels::runtime_item_grant_priority_file;
    inline constexpr std::wstring_view s_runtime_item_apply_event_filename = kh3ap::channels::runtime_item_apply_event_file;
    inline constexpr std::wstring_view s_runtime_ap_message_filename = kh3ap::channels::runtime_ap_message_file;
    inline constexpr std::wstring_view s_runtime_ap_message_result_filename = kh3ap::channels::runtime_ap_message_result_file;
    inline constexpr std::wstring_view s_runtime_hook_integrity_diagnostic_filename = L"runtime_hook_integrity_diagnostic.json";
    inline constexpr std::wstring_view s_runtime_hook_integrity_diagnostic_result_filename = L"runtime_hook_integrity_diagnostic_result.json";
    inline constexpr std::wstring_view s_runtime_hook_integrity_diagnostic_events_filename = L"runtime_hook_integrity_diagnostic_events.jsonl";
    inline constexpr std::wstring_view s_runtime_save_point_spawn_request_filename = L"runtime_save_point_spawn_request.json";
    inline constexpr std::wstring_view s_runtime_save_point_spawn_result_filename = L"runtime_save_point_spawn_result.json";
    inline constexpr std::wstring_view s_runtime_save_point_spawn_events_filename = L"runtime_save_point_spawn_events.jsonl";
    inline constexpr std::wstring_view s_runtime_save_menu_request_filename = L"runtime_save_menu_request.json";
    inline constexpr std::wstring_view s_runtime_save_menu_result_filename = L"runtime_save_menu_result.json";
    inline constexpr std::wstring_view s_runtime_save_menu_events_filename = L"runtime_save_menu_events.jsonl";
    inline constexpr std::wstring_view s_runtime_map_jump_request_filename = L"runtime_map_jump_request.json";
    inline constexpr std::wstring_view s_runtime_map_jump_result_filename = L"runtime_map_jump_result.json";
    inline constexpr std::wstring_view s_runtime_map_jump_events_filename = L"runtime_map_jump_events.jsonl";
    inline constexpr std::wstring_view s_runtime_portal_spawn_request_filename = L"runtime_portal_spawn_request.json";
    inline constexpr std::wstring_view s_runtime_portal_spawn_result_filename = L"runtime_portal_spawn_result.json";
    inline constexpr std::wstring_view s_runtime_portal_spawn_events_filename = L"runtime_portal_spawn_events.jsonl";
    inline constexpr std::wstring_view s_runtime_nearby_actor_scan_request_filename = L"runtime_nearby_actor_scan_request.json";
    inline constexpr std::wstring_view s_runtime_nearby_actor_scan_result_filename = L"runtime_nearby_actor_scan_result.json";
    inline constexpr std::wstring_view s_runtime_nearby_actor_scan_events_filename = L"runtime_nearby_actor_scan_events.jsonl";
    inline constexpr std::wstring_view s_runtime_he02_shoot_flow_restore_event_filename = L"runtime_he02_shoot_flow_restore_event.json";
    inline constexpr std::wstring_view s_runtime_he02_shoot_flow_restore_events_filename = L"runtime_he02_shoot_flow_restore_events.jsonl";
    inline constexpr std::wstring_view s_runtime_kh3_retry_point_event_filename = kh3ap::channels::runtime_kh3_retry_point_event_file;
    inline constexpr std::wstring_view s_runtime_kh3_retry_point_events_filename = L"runtime_kh3_retry_point_events.jsonl";
    inline constexpr std::wstring_view s_runtime_kh3_retry_blueprint_event_filename = kh3ap::channels::runtime_kh3_retry_blueprint_event_file;
    inline constexpr std::wstring_view s_runtime_kh3_retry_blueprint_events_filename = L"runtime_kh3_retry_blueprint_events.jsonl";
    inline constexpr std::wstring_view s_runtime_lucky_emblem_state_filename = kh3ap::channels::runtime_lucky_emblem_state_file;
    inline constexpr std::wstring_view s_manual_runtime_item_grant_filename = L"runtime_item_grant.manual.json";
    inline constexpr std::wstring_view s_manual_runtime_item_grant_result_filename = L"runtime_item_grant.manual_result.json";
    inline constexpr std::wstring_view s_manual_ability_state_probe_filename = L"manual_ability_state_probe.json";
    inline constexpr std::wstring_view s_manual_ability_state_probe_result_filename = L"manual_ability_state_probe_result.json";
    inline constexpr std::wstring_view s_manual_equipment_probe_filename = L"manual_equipment_probe.json";
    inline constexpr std::wstring_view s_manual_equipment_probe_result_filename = L"manual_equipment_probe_result.json";
    inline constexpr std::wstring_view s_manual_keyblade_enhancement_probe_filename = L"manual_keyblade_enhancement_probe.json";
    inline constexpr std::wstring_view s_manual_keyblade_enhancement_probe_result_filename = L"manual_keyblade_enhancement_probe_result.json";
    inline constexpr std::wstring_view s_manual_keyblade_enhancement_probe_events_filename = L"manual_keyblade_enhancement_probe_events.jsonl";
    inline constexpr std::wstring_view s_tres_item_weapon_enhance_data_path = L"/Game/Load/Tres/ItemSynthesis/TresItemWeaponEnhanceData.TresItemWeaponEnhanceData";
    inline constexpr std::wstring_view s_tres_item_manager_path = L"/Engine/Transient.TresItemMan_0";
    inline constexpr std::wstring_view s_tres_item_synthesis_checker_default_object_path = L"/Script/TresGame.Default__TresItemSynthesisChecker";
    inline constexpr std::wstring_view s_tres_item_weapon_enhance_test_path = L"/Script/TresGame.TresItemSynthesisChecker:ExcuteItemWeaponEnhanceTest";
    inline constexpr std::wstring_view s_shared_tres_game_enums_relative_path = L"..\\..\\shared\\types\\TresGame_enums.lua";
    inline constexpr std::wstring_view s_probe_config_path = L"Mods\\KH3APProbeCpp\\config.lua";
    inline constexpr std::wstring_view s_open_treasure_player_state_class_path = L"/Script/TresGame.TresPlayerState_OpenTreasureBox";
    inline constexpr std::wstring_view s_open_treasure_char_state_class_path = L"/Script/TresGame.TresCharState_OpenTreasureBox";
    inline constexpr std::wstring_view s_treasure_logic_actor_class_path = L"/Game/Mods/KH3Randomizer/TreasureLogicActor.TreasureLogicActor_C";
} // namespace kh3ap::settings
