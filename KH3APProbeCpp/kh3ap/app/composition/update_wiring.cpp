#include "kh3ap/features/player/characters/character_swap.h"
#include "kh3ap/app/composition/service_graph.h"
#include "kh3ap/app/composition/bindings.h"
#include "kh3ap/app/composition/settings.h"
#include "kh3ap/app/composition/update_trace.h"
#include "kh3ap/features/tutorials/tutorial_suppression.h"
#include "kh3ap/bridge/transport/bridge_channels.h"
#include "kh3ap/features/kh02/story/kh02_story.h"
#include "kh3ap/bridge/transport/bridge_paths.h"
#include "kh3ap/bridge/transport/pipe_transport.h"
#include "kh3ap/hooks/memory/kh3_memory_hooks.h"
#include "kh3ap/features/grants/caribbean_chart_unlock_state.h"
#include "kh3ap/features/player/context/context_store.h"
#include "kh3ap/features/player/controller_abilities/controller_ability_service.h"
#include "kh3ap/app/enums.h"
#include "kh3ap/features/enemies/experience_multiplier_service.h"
#include "kh3ap/features/music/shuffle/bgm_shuffle_service.h"
#include "kh3ap/features/music/now_playing/now_playing_overlay.h"
#include "kh3ap/features/ui/options_menu_service.h"
#include "kh3ap/features/ui/map_overlay/map_overlay_service.h"
#include "kh3ap/features/story/minigames/beat_of_lead_completion.h"
#include "kh3ap/features/story/minigames/beat_of_lead_service.h"
#include "kh3ap/app/game_thread_driver_service.h"
#include "kh3ap/features/grants/grant_pawn_state.h"
#include "kh3ap/features/ui/keyblade_level/keyblade_level_menu_service.h"
#include "kh3ap/features/player/emblems/lucky_emblem_tracker_service.h"
#include "kh3ap/features/moogle_shop/shop_movie_service.h"
#include "kh3ap/features/journal/ingredient_journal_service.h"
#include "kh3ap/features/colors/color_service.h"
#include "kh3ap/features/world/startup/fast_new_game_service.h"
#include "kh3ap/features/kh02/runtime/kh02_runtime.h"
#include "kh3ap/features/party/party_member_service.h"
#include "kh3ap/features/story/cutscenes/shoot_flow_restore_helpers.h"
#include "kh3ap/features/tutorials/tutorial_skip_service.h"
#include "kh3ap/features/story/minigames/union_cross_command_service.h"
#include "kh3ap/features/story/minigames/union_cross_completion.h"
#include "kh3ap/features/story/quality_of_life_service.h"
#include "kh3ap/app/update_coordinator.h"
#include "kh3ap/features/story/bootstrap/world_bootstrap_service.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <chrono>
#include <format>
#include <string>
#include <string_view>

namespace kh3ap
{
    using namespace composition_detail;
    using namespace composition_detail::settings;
    using namespace RC;
    using namespace RC::Unreal;
    namespace
    {
        constexpr auto minimum_update_interval = std::chrono::milliseconds(5);
    }
    auto RuntimeComposition::Impl::make_runtime_update_coordinator() -> RuntimeUpdateCoordinator
    {
        return RuntimeUpdateCoordinator{RuntimeUpdateCoordinatorConfig{
            s_runtime_state,
            m_he02_shoot_flow_restore,
            m_sora_level_monitor,
            m_battle_level_sync,
            m_memory_hooks,
            m_story_progress,
            m_quality_of_life,
            m_world_bootstrap,
            m_autosave_probe,
            m_sora_state_hooks,
            m_periodic_bridge,
            [this](bool discard)
            {
                m_runtime_object_observations.drain(discard);
                if (discard)
                {
                    m_union_cross_commands.reset();
                    m_beat_of_lead.reset();
                }
            },
            [this](std::chrono::steady_clock::time_point now)
            {
                m_lucky_emblem_tracker.tick(now);
            },
            [this]()
            {
                m_retry_probes.refresh_blueprint_process_observer_targets(false);
            },
            [this]()
            {
                m_retry_probes.flush_deferred_blueprint_events();
            },
            [this]()
            {
                m_retry_probes.flush_deferred_retry_point_events();
            },
            [this]()
            {
                m_goa_autosave.process_pending();
            },
            [this](std::chrono::steady_clock::time_point now)
            {
                m_player_reacquire.process(now);
            },
            [this]()
            {
                m_union_cross_commands.tick();
            },
            [this]()
            {
                m_beat_of_lead.tick();
            },
        }};
    }

    auto RuntimeComposition::Impl::make_kh02_runtime() -> Kh02Runtime
    {
        return Kh02Runtime{Kh02RuntimeConfig{
            [this]()
            {
                return m_player_context.trusted_current_playable_cached_sora_pawn();
            },
            [this]()
            {
                return !m_native_hook_service.title_or_ui_controller_active() &&
                       !m_player_reacquire.ignored_map_active() &&
                       !s_runtime_state.ui_diagnostic_state().blocking_ui_state().snapshot().active;
            },
            [this]()
            {
                return runtime_sora_game_over(
                    m_player_context.trusted_current_playable_cached_sora_pawn());
            },
            [](const std::string& message)
            {
                Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] KH02 runtime error={}\n"),
                                                std::wstring(message.begin(), message.end()));
            },
            []()
            {
                s_runtime_controller_abilities.request_refresh(
                    RuntimeControllerAbilityEvent::GameAbilityChanged);
            },
            [this]()
            {
                const auto settings = m_goa_settings.status();
                return Kh02BattleLevelSettings{settings.battle_level_sync_enabled,
                                               settings.battle_level_offset};
            }}};
    }

    auto RuntimeComposition::Impl::tick(bool unreal_initialized,
                                        std::chrono::steady_clock::time_point unreal_init_time)
        -> void
    {
        OnUpdatePhaseTrace phase_trace{};
        const RuntimeProfileScope<s_enable_runtime_hot_path_profiling> profile{STR("on_update")};
        // Idle updates perform one atomic flag read. The
        // deadline and gameplay guard are consulted only
        // while a UI focus release is pending; no Unreal
        // object or game-state polling occurs here.
        const auto now = std::chrono::steady_clock::now();
        if (unreal_initialized)
        {
            m_shop_movie.tick();
            phase_trace.mark(STR("shop_movie"));
            m_ingredients.tick();
            m_ingredient_journal.tick();
            phase_trace.mark(STR("ingredient_journal"));
            m_adversaries.tick(now);
            phase_trace.mark(STR("adversaries"));
            m_space.tick(now);
            phase_trace.mark(STR("space"));
            // Finish the combat handoff before retiring experimental ownership,
            // including a settings change that returns to Sora and selects Off.
            m_garden_return.tick(now);
            if (character_swap::tick(now))
            {
                s_runtime_controller_abilities.request_refresh(RuntimeControllerAbilityEvent::GameAbilityChanged);
            }
            phase_trace.mark(STR("garden_return"));
            m_save_space.tick(now);
            phase_trace.mark(STR("save_space"));
            m_minigame_rewards.tick();
            phase_trace.mark(STR("pooh_bonus"));
            tick_runtime_tutorial_suppression(now);
            phase_trace.mark(STR("tutorial_suppression"));
        }
        m_blocking_ui_hooks.process_pending_focus_release(now);
        phase_trace.mark(STR("focus_release"));
        {
            const RuntimeProfileScope<s_enable_runtime_hot_path_profiling> music_profile{
                STR("on_update:music_chests")};
            m_music_chests.tick();
        }
        phase_trace.mark(STR("music_chests"));
        m_bgm_shuffle.tick();
        m_now_playing.tick();
        phase_trace.mark(STR("bgm_shuffle"));
        m_experience_multiplier.tick();
        phase_trace.mark(STR("experience_multiplier"));
        m_enemy_remaps.process_pending_class_prewarm();
        m_fun_heartless.tick();
        phase_trace.mark(STR("enemy_prewarm"));
        m_party_members.tick();
        phase_trace.mark(STR("party_members"));
        {
            const RuntimeProfileScope<s_enable_runtime_hot_path_profiling> portal_profile{
                STR("on_update:portals")};
            m_portals.tick(now);
        }
        phase_trace.mark(STR("portals"));
        m_colors.tick(now);
        phase_trace.mark(STR("colors"));
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        m_party_test.tick();
        phase_trace.mark(STR("party_test"));
        m_enemy_clear.tick();
        phase_trace.mark(STR("enemy_clear"));
#endif
        m_goa_settings_menu.tick();
        phase_trace.mark(STR("goa_settings_menu"));
        m_minimap_context.flush_pending();
        phase_trace.mark(STR("minimap_context"));
        m_map_overlay.tick();
        phase_trace.mark(STR("map_overlay"));
        m_keyblade_level_menu.tick();
        m_workshop.tick();
        phase_trace.mark(STR("keyblade_menu"));
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        m_autoload.tick(unreal_initialized, m_native_hook_service.title_or_ui_controller_active(),
                        m_player_reacquire.ignored_map_active());
        phase_trace.mark(STR("autoload"));
        m_new_game.tick(unreal_initialized);
        phase_trace.mark(STR("new_game"));
#endif
        m_kh02_runtime.tick(unreal_initialized);
        phase_trace.mark(STR("kh02_runtime"));
        if (unreal_initialized)
        {
            m_cutscene_skips.tick();
        }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        // Publish a preparation failure in this frame: the
        // native error UI can suspend subsequent game ticks.
        m_autoload.flush_native_load_failure();
        m_ui_control.tick(unreal_initialized);
        phase_trace.mark(STR("ui_control"));
        m_randomizer_test.tick(unreal_initialized);
        phase_trace.mark(STR("randomizer_test"));
#endif
        m_fast_new_game.tick(unreal_initialized,
                             m_native_hook_service.title_or_ui_controller_active());
        phase_trace.mark(STR("fast_new_game"));
        m_runtime_update_coordinator.tick(unreal_initialized,
                                          m_native_hook_service.title_or_ui_controller_active(),
                                          m_player_reacquire.ignored_map_active(), unreal_init_time,
                                          [&phase_trace](std::wstring_view phase)
                                          {
                                              phase_trace.mark(phase);
                                          });
    }

    auto RuntimeComposition::Impl::make_runtime_game_thread_driver()
        -> RuntimeGameThreadDriverService
    {
        return RuntimeGameThreadDriverService{RuntimeGameThreadDriverConfig{
            s_enable_game_engine_tick_detour,
            s_enable_runtime_update_loop,
            s_build_stamp,
            minimum_update_interval,
            [this](bool unreal_initialized, std::chrono::steady_clock::time_point unreal_init_time)
            {
                tick(unreal_initialized, unreal_init_time);
            },
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            [this]()
            {
                const auto ui =
                    s_runtime_state.ui_diagnostic_state().blocking_ui_state().snapshot();
                const auto age_ms = ui.updated_at.time_since_epoch().count() == 0
                                        ? -1
                                        : std::chrono::duration_cast<std::chrono::milliseconds>(
                                              std::chrono::steady_clock::now() - ui.updated_at)
                                              .count();
                // Cached strings and state only: this diagnostic must
                // not resolve, scan or dereference Unreal objects.
                return std::format(STR("map_ready_context={} blocking_ui={} ui_reason={} "
                                       "ui_event={} ui_age_ms={} title_or_ui={} ignored_map={}"),
                                   m_player_context.map_ready_context_key(), ui.active, ui.reason,
                                   ui.event, age_ms,
                                   m_native_hook_service.title_or_ui_controller_active(),
                                   m_player_reacquire.ignored_map_active());
            },
#endif
        }};
    }

    auto RuntimeComposition::Impl::make_fast_new_game() -> RuntimeFastNewGameService
    {
        return RuntimeFastNewGameService{[]()
                                         {
                                             return try_get_bridge_dir();
                                         }};
    }
} // namespace kh3ap
