#pragma once
#include "kh3ap/app/composition/detour.h"
#include "kh3ap/app/composition.h"
#include "kh3ap/core/performance_build.h"
#include "kh3ap/core/types.h"
#include <chrono>
#include "kh3ap/core/platform/steam_platform_recovery_service.h"
#include "kh3ap/core/input/controller_preferences_service.h"
#include "kh3ap/core/settings/goa_settings_service.h"
#include "kh3ap/features/enemies/experience_multiplier_service.h"
#include "kh3ap/features/enemies/enemy_remap_service.h"
#include "kh3ap/features/enemies/fun_heartless_service.h"
#include "kh3ap/features/story/cutscenes/shoot_flow_restore_helpers.h"
#include "kh3ap/features/story/progression/story_progress_service.h"
#include "kh3ap/features/story/quality_of_life_service.h"
#include "kh3ap/features/story/world_fixes/fz02_player_start_service.h"
#include "kh3ap/features/tutorials/tutorial_skip_service.h"
#include "kh3ap/features/story/cutscenes/cutscene_skip_service.h"
#include "kh3ap/features/story/remind/remind_completion_service.h"
#include "kh3ap/features/player/emblems/lucky_emblem_tracker_service.h"
#include "kh3ap/features/player/objects/object_observation_service.h"
#include "kh3ap/features/party/party_member_service.h"
#include "kh3ap/features/colors/color_service.h"
#include "kh3ap/features/story/world_fixes/festival_dance_story_service.h"
#include "kh3ap/features/story/minigames/beat_of_lead_service.h"
#include "kh3ap/features/story/minigames/union_cross_command_service.h"
#include "kh3ap/diagnostics/retry/retry_probe_service.h"
#include "kh3ap/features/story/save_points/goa_autosave_service.h"
#include "kh3ap/diagnostics/kh3_autosave_probe.h"
#include "kh3ap/features/player/state/sora_level_monitor_state.h"
#include "kh3ap/features/grants/delivery/item_grant_service.h"
#include "kh3ap/hooks/memory/kh3_memory_hooks.h"
#include "kh3ap/features/grants/battle_level/battle_level_sync_service.h"
#include "kh3ap/features/ui/map_overlay/minimap_context_service.h"
#include "kh3ap/features/ui/map_overlay/map_overlay_service.h"
#include "kh3ap/features/ui/options_menu_service.h"
#include "kh3ap/features/ui/keyblade_level/keyblade_level_menu_service.h"
#include "kh3ap/features/player/state/sora_state_hook_service.h"
#include "kh3ap/features/player/context/player_context_service.h"
#include "kh3ap/features/story/bootstrap/world_bootstrap_service.h"
#include "kh3ap/features/player/context/player_reacquire_service.h"
#include "kh3ap/features/moogle_shop/shop_movie_service.h"
#include "kh3ap/features/journal/ingredient_journal_service.h"
#include "kh3ap/features/moogle_shop/moogle_shop_service.h"
#include "kh3ap/features/player/objects/map_actor_lifecycle_service.h"
#include "kh3ap/features/grants/messages/ap_message_gate.h"
#include "kh3ap/hooks/blocking_ui/blocking_ui_hook_service.h"
#include "kh3ap/app/periodic_bridge_service.h"
#include "kh3ap/hooks/hook_service.h"
#include "kh3ap/app/update_coordinator.h"
#include "kh3ap/features/kh02/runtime/kh02_runtime.h"
#include "kh3ap/features/enemies/enemy_clear_service.h"
#include "kh3ap/features/party/party_test_service.h"
#include "kh3ap/features/world/startup/new_game_service.h"
#include "kh3ap/features/world/ui_control/ui_control_service.h"
#include "kh3ap/features/world/startup/autoload_service.h"
#include "kh3ap/features/treasure/music_chest/music_chest_service.h"
#include "kh3ap/features/music/now_playing/now_playing_overlay.h"
#include "kh3ap/features/music/shuffle/bgm_shuffle_service.h"
#include "kh3ap/diagnostics/randomizer_test_service.h"
#include "kh3ap/app/game_thread_driver_service.h"
#include "kh3ap/features/world/startup/fast_new_game_service.h"
#include "kh3ap/hooks/patches/kh3_expanded_save_slots_patch.h"
#include "kh3ap/hooks/patches/kh3_signed_munny_patch.h"
#include "kh3ap/hooks/patches/kh3_sqex_translucent_slice_view_guard_patch.h"
#include "kh3ap/hooks/hook_lifecycle_service.h"
#include "kh3ap/app/startup_service.h"
#include "kh3ap/features/world/portals/portal_service.h"
#include "kh3ap/features/world/space/space_service.h"
#include "kh3ap/features/world/garden_return/garden_return_service.h"
#include "kh3ap/features/world/space/save_space_service.h"
#include "kh3ap/features/story/minigames/minigame_reward_service.h"
#include "kh3ap/features/moogle_shop/workshop_service.h"
#include "kh3ap/features/ingredients/ingredient_service.h"
#include "kh3ap/features/journal/adversary_service.h"
#include "kh3ap/features/story/minigames/classic_kingdom_completion_service.h"
#include "kh3ap/features/story/minigames/olympus_coliseum_completion_service.h"
#include "kh3ap/features/story/world_fixes/kingdom_of_corona_qol_service.h"
#include "kh3ap/features/story/world_fixes/monstropolis_mapset_service.h"
#include "kh3ap/features/story/progression/battlegate_activation_service.h"

namespace kh3ap
{
    class RuntimeComposition::Impl
    {
    public:
        Impl();
        auto initialize() -> void;
        auto shutdown() -> void;
        auto reroll_colors() -> void;

    private:
        auto reset_runtime_map_actor_spawn_lifecycle_state() -> void;
        auto reset_runtime_map_lifecycle_state() -> void;

        // Factories return prvalues so even non-movable services are constructed
        // directly in their final member storage through guaranteed copy elision.
        auto make_steam_platform_recovery() -> RuntimeSteamPlatformRecoveryService;
        auto make_controller_preferences() -> RuntimeControllerPreferencesService;
        auto make_goa_settings() -> RuntimeGoaSettingsService;
        auto make_experience_multiplier() -> RuntimeExperienceMultiplierService;
        auto make_enemy_remaps() -> RuntimeEnemyRemapService;
        auto make_fun_heartless() -> RuntimeFunHeartlessService;
        auto make_he02_shoot_flow_restore() -> RuntimeShootFlowRestoreService;
        auto make_story_progress() -> RuntimeStoryProgressService;
        auto make_quality_of_life() -> RuntimeQualityOfLifeService;
        auto make_fz02_player_start() -> RuntimeFz02PlayerStartService;
        auto make_tutorial_skips() -> RuntimeTutorialSkipService;
        auto make_cutscene_skips() -> RuntimeCutsceneSkipService;
        auto make_remind_completion() -> RuntimeRemindCompletionService;
        auto make_lucky_emblem_tracker() -> RuntimeLuckyEmblemTrackerService;
        auto make_runtime_object_observations() -> RuntimeObjectObservationService;
        auto make_party_members() -> RuntimePartyMemberService;
        auto make_colors() -> RuntimeColorService;
        auto make_festival_dance_story() -> RuntimeFestivalDanceStoryService;
        auto make_beat_of_lead() -> RuntimeBeatOfLeadService;
        auto make_union_cross_commands() -> RuntimeUnionCrossCommandService;
        auto make_retry_probes() -> RuntimeRetryProbeService;
        auto make_goa_autosave() -> RuntimeGoaAutosaveService;
        auto make_runtime_item_grants() -> RuntimeItemGrantService;
        auto make_memory_hooks() -> Kh3MemoryHooks;
        auto make_battle_level_sync() -> RuntimeBattleLevelSyncService;
        auto make_minimap_context() -> RuntimeMinimapContextService;
        auto make_map_overlay() -> RuntimeMapOverlayService;
        auto make_goa_settings_menu() -> RuntimeOptionsMenuService;
        auto make_keyblade_level_menu() -> RuntimeKeybladeLevelMenuService;
        auto make_sora_state_hooks() -> RuntimeSoraStateHookService;
        auto make_player_context() -> RuntimePlayerContextService;
        auto make_world_bootstrap() -> RuntimeWorldBootstrapService;
        auto make_player_reacquire() -> RuntimePlayerReacquireService;
        auto make_shop_movie() -> RuntimeShopMovieService;
        auto make_ingredient_journal() -> RuntimeIngredientJournalService;
        auto make_moogle_shop() -> RuntimeMoogleShopService;
        auto make_map_actor_lifecycle() -> RuntimeMapActorLifecycleService;
        auto make_blocking_ui_hooks() -> RuntimeBlockingUiHookService;
        auto make_periodic_bridge() -> RuntimePeriodicBridgeService;
        auto make_native_hook_service() -> RuntimeNativeHookService;
        auto make_runtime_update_coordinator() -> RuntimeUpdateCoordinator;
        auto make_kh02_runtime() -> Kh02Runtime;
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        auto make_enemy_clear() -> RuntimeEnemyClearService;
#endif
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        auto make_party_test() -> RuntimePartyTestService;
#endif
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        auto make_new_game() -> RuntimeNewGameService;
#endif
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        auto make_ui_control() -> RuntimeUiControlService;
#endif
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        auto make_autoload() -> RuntimeAutoloadService;
#endif
        auto make_music_chests() -> RuntimeMusicChestService;
        auto make_now_playing() -> NowPlayingOverlay;
        auto make_bgm_shuffle() -> RuntimeBgmShuffleService;
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        auto make_randomizer_test() -> RuntimeRandomizerTestService;
#endif
        auto make_runtime_game_thread_driver() -> RuntimeGameThreadDriverService;
        auto tick(bool unreal_initialized, std::chrono::steady_clock::time_point unreal_init_time)
            -> void;
        auto make_fast_new_game() -> RuntimeFastNewGameService;
        auto make_native_hook_lifecycle() -> RuntimeNativeHookLifecycleService;
        auto make_runtime_startup() -> RuntimeStartupService;
        auto make_portals() -> RuntimePortalService;
        auto make_space() -> RuntimeSpaceService;
        auto make_garden_return() -> RuntimeGardenReturnService;
        auto make_save_space() -> RuntimeSaveSpaceService;
        auto make_minigame_rewards() -> RuntimeMinigameRewardService;
        auto make_workshop() -> RuntimeWorkshopService;
        auto make_ingredients() -> RuntimeIngredientService;
        auto make_adversaries() -> RuntimeAdversaryService;
        auto make_classic_kingdom_completion() -> RuntimeClassicKingdomCompletionService;
        auto make_olympus_coliseum_completion() -> RuntimeOlympusColiseumCompletionService;
        auto make_kingdom_of_corona_qol() -> RuntimeKingdomOfCoronaQolService;
        auto make_monstropolis_mapset() -> RuntimeMonstropolisMapSetService;
        auto make_battlegate_activation() -> RuntimeBattlegateActivationService;

        // Declaration order is construction order. Forward callbacks stay
        // guarded until the constructor marks the complete graph ready.
        bool m_service_graph_ready{false};

        RuntimeSteamPlatformRecoveryService m_steam_platform_recovery{
            make_steam_platform_recovery()};

        RuntimeControllerPreferencesService m_controller_preferences{make_controller_preferences()};

        RuntimeGoaSettingsService m_goa_settings{make_goa_settings()};
        RuntimeExperienceMultiplierService m_experience_multiplier{make_experience_multiplier()};
        RuntimeEnemyRemapService m_enemy_remaps{make_enemy_remaps()};

        RuntimeFunHeartlessService m_fun_heartless{make_fun_heartless()};
        RuntimeShootFlowRestoreService m_he02_shoot_flow_restore{make_he02_shoot_flow_restore()};
        RuntimeStoryProgressService m_story_progress{make_story_progress()};
        RuntimeQualityOfLifeService m_quality_of_life{make_quality_of_life()};
        RuntimeFz02PlayerStartService m_fz02_player_start{make_fz02_player_start()};
        RuntimeTutorialSkipService m_tutorial_skips{make_tutorial_skips()};
        RuntimeCutsceneSkipService m_cutscene_skips{make_cutscene_skips()};
        RuntimeRemindCompletionService m_remind_completion{make_remind_completion()};
        RuntimeLuckyEmblemTrackerService m_lucky_emblem_tracker{make_lucky_emblem_tracker()};
        RuntimeObjectObservationService m_runtime_object_observations{
            make_runtime_object_observations()};
        RuntimePartyMemberService m_party_members{make_party_members()};
        RuntimeColorService m_colors{make_colors()};
        RuntimeFestivalDanceStoryService m_festival_dance_story{make_festival_dance_story()};
        RuntimeBeatOfLeadService m_beat_of_lead{make_beat_of_lead()};
        RuntimeUnionCrossCommandService m_union_cross_commands{make_union_cross_commands()};
        RuntimeRetryProbeService m_retry_probes{make_retry_probes()};
        RuntimeGoaAutosaveService m_goa_autosave{make_goa_autosave()};
        Kh3AutosaveProbe m_autosave_probe{};
        RuntimeSoraLevelMonitorState m_sora_level_monitor{};
        RuntimeItemGrantService m_runtime_item_grants{make_runtime_item_grants()};
        Kh3MemoryHooks m_memory_hooks{make_memory_hooks()};
        RuntimeBattleLevelSyncService m_battle_level_sync{make_battle_level_sync()};
        RuntimeMinimapContextService m_minimap_context{make_minimap_context()};
        RuntimeMapOverlayService m_map_overlay{make_map_overlay()};
        RuntimeOptionsMenuService m_goa_settings_menu{make_goa_settings_menu()};
        RuntimeKeybladeLevelMenuService m_keyblade_level_menu{make_keyblade_level_menu()};
        RuntimeSoraStateHookService m_sora_state_hooks{make_sora_state_hooks()};
        RuntimePlayerContextService m_player_context{make_player_context()};
        RuntimeWorldBootstrapService m_world_bootstrap{make_world_bootstrap()};

        RuntimePlayerReacquireService m_player_reacquire{make_player_reacquire()};
        RuntimeShopMovieService m_shop_movie{make_shop_movie()};
        RuntimeIngredientJournalService m_ingredient_journal{make_ingredient_journal()};
        RuntimeMoogleShopService m_moogle_shop{make_moogle_shop()};
        RuntimeMapActorLifecycleService m_map_actor_lifecycle{make_map_actor_lifecycle()};
        RuntimeApMessageGateState m_runtime_ap_message_gate_state{};
        RuntimeBlockingUiHookService m_blocking_ui_hooks{make_blocking_ui_hooks()};
        RuntimePeriodicBridgeService m_periodic_bridge{make_periodic_bridge()};
        RuntimeNativeHookService m_native_hook_service{make_native_hook_service()};

        // Update scheduling and lifecycle owners are constructed last because
        // they consume the complete feature graph above.
        RuntimeUpdateCoordinator m_runtime_update_coordinator{make_runtime_update_coordinator()};
        Kh02Runtime m_kh02_runtime{make_kh02_runtime()};
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        RuntimeEnemyClearService m_enemy_clear{make_enemy_clear()};
        RuntimePartyTestService m_party_test{make_party_test()};
        RuntimeNewGameService m_new_game{make_new_game()};
        RuntimeUiControlService m_ui_control{make_ui_control()};
        RuntimeAutoloadService m_autoload{make_autoload()};
#endif
        RuntimeMusicChestService m_music_chests{make_music_chests()};
        NowPlayingOverlay m_now_playing{make_now_playing()};
        RuntimeBgmShuffleService m_bgm_shuffle{make_bgm_shuffle()};
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        RuntimeRandomizerTestService m_randomizer_test{make_randomizer_test()};
#endif
        RuntimeGameThreadDriverService m_runtime_game_thread_driver{
            make_runtime_game_thread_driver()};
        RuntimeFastNewGameService m_fast_new_game{make_fast_new_game()};
        Kh3ExpandedSaveSlotsPatch m_expanded_save_slots_patch{};
        Kh3SignedMunnyPatch m_signed_munny_patch{};
        Kh3SqexTranslucentSliceViewGuardPatch m_sqex_translucent_slice_view_guard_patch{};
        RuntimeNativeHookLifecycleService m_native_hook_lifecycle{make_native_hook_lifecycle()};
        RuntimeStartupService m_runtime_startup{make_runtime_startup()};
        RuntimePortalService m_portals{make_portals()};
        RuntimeSpaceService m_space{make_space()};
        RuntimeGardenReturnService m_garden_return{make_garden_return()};
        RuntimeSaveSpaceService m_save_space{make_save_space()};
        RuntimeMinigameRewardService m_minigame_rewards{make_minigame_rewards()};
        RuntimeWorkshopService m_workshop{make_workshop()};
        RuntimeIngredientService m_ingredients{make_ingredients()};
        RuntimeAdversaryService m_adversaries{make_adversaries()};
        RuntimeClassicKingdomCompletionService m_classic_kingdom_completion{
            make_classic_kingdom_completion()};
        RuntimeOlympusColiseumCompletionService m_olympus_coliseum_completion{
            make_olympus_coliseum_completion()};
        RuntimeKingdomOfCoronaQolService m_kingdom_of_corona_qol{make_kingdom_of_corona_qol()};

        RuntimeMonstropolisMapSetService m_monstropolis_mapset{make_monstropolis_mapset()};

        RuntimeBattlegateActivationService m_battlegate_activation{make_battlegate_activation()};
    };
} // namespace kh3ap
