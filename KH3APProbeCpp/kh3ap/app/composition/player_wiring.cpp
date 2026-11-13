#include "kh3ap/app/composition/service_graph.h"
#include "kh3ap/app/composition/bindings.h"
#include "kh3ap/app/composition/settings.h"
#include "kh3ap/features/music/events/music_update_events.h"
#include "kh3ap/features/kh02/story/kh02_story.h"
#include "kh3ap/bridge/transport/bridge_paths.h"
#include "kh3ap/hooks/memory/kh3_memory_hooks.h"
#include "kh3ap/features/grants/battle_level/battle_level_sync_service.h"
#include "kh3ap/features/grants/caribbean_chart_unlock_state.h"
#include "kh3ap/features/player/controller_abilities/controller_ability_service.h"
#include "kh3ap/features/grants/grant_pawn_state.h"
#include "kh3ap/features/grants/delivery/item_grant_service.h"
#include "kh3ap/features/player/keyblades/keyblade_options_helpers.h"
#include "kh3ap/features/player/objects/map_actor_lifecycle_service.h"
#include "kh3ap/diagnostics/profiling/reflection_profile.h"
#include "kh3ap/features/player/context/player_context_service.h"
#include "kh3ap/features/player/context/player_reacquire_service.h"
#include "kh3ap/features/player/state/sora_state_hook_service.h"
#include "kh3ap/features/tutorials/tutorial_skip_service.h"
#include "kh3ap/features/story/bootstrap/world_bootstrap_service.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <optional>
#include <string>
#include <string_view>

namespace kh3ap
{
    using namespace composition_detail;
    using namespace composition_detail::settings;
    using namespace RC;
    using namespace RC::Unreal;
    auto RuntimeComposition::Impl::make_runtime_item_grants() -> RuntimeItemGrantService
    {
        return RuntimeItemGrantService{RuntimeItemGrantServiceConfig{
            .runtime_state = s_runtime_state,
            .sora_current_state_cache =
                [this]()
            {
                return runtime_sora_current_state_cache_view(m_sora_level_monitor);
            },
            .bridge_dir_resolver =
                []()
            {
                return try_get_bridge_dir();
            },
            .runtime_context_object_not_real =
                []()
            {
                return composition_context_object_is_not_real();
            },
            .base_apply_deps =
                [this](std::wstring_view context_summary)
            {
                auto deps = runtime_item_grant_apply_deps(context_summary);
                deps.auto_equip_essential_abilities = m_goa_settings.auto_equip_essential_abilities();
                deps.party_member_unlock =
                    [this](std::wstring_view token, UObject* sora, std::wstring* failure)
                {
                    return m_party_members.apply_unlock_item(token, sora, failure);
                };
                return deps;
            },
            .post_apply_observer_deps =
                [](std::wstring_view context_summary)
            {
                return runtime_item_grant_post_apply_observer_deps(context_summary);
            },
            .progressive_keyblade_level_applied =
                [this]()
            {
                if (m_service_graph_ready)
                {
                    m_moogle_shop.observe_progressive_keyblade_level_applied();
                    m_keyblade_level_menu.observe_progressive_keyblade_level_applied();
                }
            },
        }};
    }

    auto RuntimeComposition::Impl::make_memory_hooks() -> Kh3MemoryHooks
    {
        return Kh3MemoryHooks{m_sora_level_monitor,
                              Kh3MemoryHookConfig{s_build_stamp, []()
                                                  {
                                                      return try_get_bridge_dir();
                                                  }}};
    }

    auto RuntimeComposition::Impl::make_battle_level_sync() -> RuntimeBattleLevelSyncService
    {
        return RuntimeBattleLevelSyncService{RuntimeBattleLevelSyncConfig{
            s_runtime_state,
            m_sora_level_monitor,
            m_goa_settings,
            [this](UObject* controller, std::wstring_view source, std::wstring_view controller_name,
                   bool force)
            {
                if (!m_service_graph_ready)
                {
                    return false;
                }
                return m_player_context.cache_pawn_from_player_controller(controller, source,
                                                                          controller_name, force);
            },
            [](std::wstring_view scope_name, uint64_t elapsed_us)
            {
                if constexpr (s_enable_runtime_sora_level_profiling)
                {
                    record_runtime_profile_sample(scope_name, elapsed_us);
                }
            },
        }};
    }

    auto RuntimeComposition::Impl::make_sora_state_hooks() -> RuntimeSoraStateHookService
    {
        return RuntimeSoraStateHookService{RuntimeSoraStateHookConfig{
            s_runtime_state,
            m_sora_level_monitor,
            m_memory_hooks,
            m_battle_level_sync,
            []()
            {
                return try_get_bridge_dir();
            },
            []()
            {
                return composition_context_summary();
            },
            [this](std::wstring_view pawn_name, std::wstring_view class_name)
            {
                if (!m_service_graph_ready)
                {
                    return false;
                }
                return m_player_context.is_validated_sora_pawn_name(pawn_name, class_name);
            },
            [this](UObject* pawn, std::wstring_view pawn_name, std::wstring_view source)
            {
                if (!m_service_graph_ready)
                {
                    return false;
                }
                return m_player_context.cache_validated_sora_pawn_candidate(pawn, pawn_name,
                                                                            source);
            },
            [this](UObject* level_owner)
            {
                if (!m_service_graph_ready)
                {
                    return false;
                }
                return m_player_context.maybe_clear_special_battle_from_sora_level_owner(
                    level_owner);
            },
            [this](std::wstring_view state_class, std::optional<uint8_t> state_id)
            {
                if (!m_service_graph_ready)
                {
                    return;
                }
                m_player_context.activate_blocking_ui_from_sora_game_over_state(state_class,
                                                                                state_id);
            },
            [this](bool active)
            {
                if (m_service_graph_ready)
                {
                    m_moogle_shop.observe_shop_session(active);
                    m_workshop.request_refresh();
                }
            },
            [this]()
            {
                if (m_service_graph_ready)
                {
                    request_music_update(MusicUpdate::Form);
                    mark_runtime_keyblade_weapon_changed(s_runtime_state);
                }
            },
            [this](bool entering_cinematic)
            {
                if (m_service_graph_ready)
                {
                    schedule_runtime_keyblade_ascension_check(s_runtime_state, entering_cinematic);
                }
            },
            [this](UObject* pawn)
            {
                if (!m_service_graph_ready)
                {
                    return;
                }
                m_party_members.observe_sora_state(pawn);
                m_colors.on_sora_state(pawn);
                if (m_player_context.refresh_controller_from_sora_event(pawn))
                {
                    mark_runtime_keyblade_weapon_changed(s_runtime_state);
                    s_runtime_controller_abilities.request_refresh(
                        RuntimeControllerAbilityEvent::ControllerChanged);
                }
            },
        }};
    }

    auto RuntimeComposition::Impl::make_player_context() -> RuntimePlayerContextService
    {
        return RuntimePlayerContextService{RuntimePlayerContextConfig{
            s_runtime_state,
            m_sora_level_monitor,
            m_battle_level_sync,
            m_tutorial_skips,
            m_sora_state_hooks,
            []()
            {
                return try_get_bridge_dir();
            },
            []()
            {
                return composition_context_summary();
            },
            [this](std::wstring_view observed_context, std::wstring_view source)
            {
                if (!m_service_graph_ready)
                {
                    return;
                }
                m_player_reacquire.enter_ignored_map(observed_context, source);
            },
            [this](std::wstring_view observed_context, std::wstring_view source)
            {
                if (!m_service_graph_ready)
                {
                    return;
                }
                m_player_reacquire.leave_ignored_map_from_validated_pawn(observed_context, source);
            },
            [this](std::wstring_view source)
            {
                if (m_service_graph_ready)
                {
                    m_player_reacquire.disarm(source);
                }
            },
            [](auto now, std::wstring_view source)
            {
                maybe_publish_cached_pawn_runtime_context(now, source);
            },
            [](std::wstring_view)
            {
                s_runtime_state.grant_state()
                    .item_grant_poll_state()
                    .delivery_wake_schedule()
                    .request();
            },
            [this]()
            {
                if (m_service_graph_ready)
                {
                    m_periodic_bridge.request_keyblade_style_refresh();
                    m_quality_of_life.request_refresh();
                    m_story_progress.notify_context_ready();
                    m_lucky_emblem_tracker.notify_context_ready();
                    m_workshop.request_refresh();
                    // Retry can recreate Sora without the map-name ready
                    // callback. His command gates must be restored too.
                    s_runtime_controller_abilities.request_refresh(
                        RuntimeControllerAbilityEvent::PawnChanged);
                }
            }, // Reapply pawn-owned effects after replacement, including Retry.
            [this]()
            {
                s_runtime_controller_abilities.request_refresh(
                    RuntimeControllerAbilityEvent::MapReady);
                if (m_service_graph_ready)
                {
                    // Fallback if a special return supplied Sora
                    // without HandleStartingNewPlayer. This is an
                    // ownership-gated no-op after normal possession.
                    m_world_bootstrap.world_ready();
                    m_lucky_emblem_tracker.notify_context_ready();
                }
            },
        }};
    }

    auto RuntimeComposition::Impl::make_world_bootstrap() -> RuntimeWorldBootstrapService
    {
        return RuntimeWorldBootstrapService{RuntimeWorldBootstrapConfig{
            .player_context = m_player_context,
            .bridge_dir_resolver =
                []
            {
                return try_get_bridge_dir();
            },
            .enabled = s_enable_runtime_world_bootstrap,
            .debug_logging = s_debug_logging,
            .retry_interval = s_runtime_world_bootstrap_retry_interval,
            .asset_load_retry_interval = s_runtime_world_bootstrap_asset_load_retry_interval,
        }};
    }

    auto RuntimeComposition::Impl::make_player_reacquire() -> RuntimePlayerReacquireService
    {
        return RuntimePlayerReacquireService{RuntimePlayerReacquireConfig{
            .runtime_state = s_runtime_state,
            .is_player_controller =
                [this](UObject* controller)
            {
                return m_player_context.is_player_controller_object(controller);
            },
            .cache_pawn_from_controller =
                [this](UObject* controller, std::wstring_view source,
                       std::wstring_view controller_name, bool force)
            {
                return m_player_context.cache_pawn_from_player_controller(controller, source,
                                                                          controller_name, force);
            },
            .reset_battle_level_lifecycle =
                [this]()
            {
                m_battle_level_sync.reset_for_map_lifecycle();
            },
            .sync_battle_level_after_reacquire =
                [this](std::wstring_view reason)
            {
                m_battle_level_sync.sync_after_controller_reacquire(reason);
            },
            .clear_special_battle_pending =
                [this]()
            {
                if (m_service_graph_ready)
                {
                    m_native_hook_service.clear_special_battle_pending();
                }
            },
            .reset_map_lifecycle =
                [this]()
            {
                if (m_service_graph_ready)
                {
                    reset_runtime_map_lifecycle_state();
                }
            },
            .runtime_context_resolver =
                []()
            {
                return composition_context_summary();
            },
            .reacquire_interval = s_sora_controller_reacquire_interval,
        }};
    }

    auto RuntimeComposition::Impl::make_map_actor_lifecycle() -> RuntimeMapActorLifecycleService
    {
        return RuntimeMapActorLifecycleService{RuntimeMapActorLifecycleConfig{
            s_runtime_state,
            m_player_context,
            s_runtime_controller_abilities,
        }};
    }
} // namespace kh3ap
