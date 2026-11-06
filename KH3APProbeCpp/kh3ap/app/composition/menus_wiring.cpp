#include "kh3ap/core/settings/pak_seed_settings.h"
#include "kh3ap/core/settings/seed_slot_options.h"
#include "kh3ap/features/player/characters/character_swap.h"
#include "kh3ap/app/composition/service_graph.h"
#include "kh3ap/app/composition/bindings.h"
#include "kh3ap/app/composition/settings.h"
#include "kh3ap/bridge/transport/bridge_channels.h"
#include "kh3ap/features/kh02/story/kh02_story.h"
#include "kh3ap/bridge/transport/bridge_paths.h"
#include "kh3ap/bridge/serialization/json_fields.h"
#include "kh3ap/features/ui/goa_options_menu.h"
#include "kh3ap/features/ui/options_menu_service.h"
#include "kh3ap/features/ui/map_overlay/map_overlay_service.h"
#include "kh3ap/features/ui/map_overlay/minimap_context_service.h"
#include "kh3ap/features/world/garden_return/garden_return_service.h"
#include "kh3ap/features/story/save_points/goa_autosave_service.h"
#include "kh3ap/features/grants/delivery/item_call_helpers.h"
#include "kh3ap/features/ui/keyblade_level/keyblade_level_menu_service.h"
#include "kh3ap/features/moogle_shop/moogle_shop_service.h"
#include "kh3ap/features/moogle_shop/shop_movie_service.h"
#include "kh3ap/features/journal/ingredient_journal_service.h"
#include "kh3ap/app/periodic_bridge_service.h"
#include "kh3ap/features/player/state/sora_state_catalog.h"
#include "kh3ap/features/tutorials/tutorial_skip_service.h"
#include "kh3ap/core/types.h"
#include "kh3ap/bridge/utilities.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <chrono>
#include <optional>
#include <string_view>
#include <vector>

namespace kh3ap
{
    using namespace composition_detail;
    using namespace composition_detail::settings;
    using namespace RC;
    using namespace RC::Unreal;
    auto RuntimeComposition::Impl::make_goa_autosave() -> RuntimeGoaAutosaveService
    {
        return RuntimeGoaAutosaveService{RuntimeGoaAutosaveConfig{s_runtime_state}};
    }

    auto RuntimeComposition::Impl::make_minimap_context() -> RuntimeMinimapContextService
    {
        return RuntimeMinimapContextService{RuntimeMinimapContextConfig{
            [this]() -> UObject*
            {
                if (auto* hero = Kh02Story::map_overlay_hero())
                {
                    return hero;
                }
                if (auto* hero = character_swap::reward_context())
                {
                    return hero;
                }
                return m_player_context.trusted_current_playable_cached_sora_pawn();
            },
            [this]()
            {
                return !m_native_hook_service.title_or_ui_controller_active() &&
                       !m_player_reacquire.ignored_map_active() &&
                       !s_runtime_state.ui_diagnostic_state().blocking_ui_state().snapshot().active;
            },
            []()
            {
                return try_get_bridge_dir();
            },
        }};
    }

    auto RuntimeComposition::Impl::make_map_overlay() -> RuntimeMapOverlayService
    {
        return RuntimeMapOverlayService{RuntimeMapOverlayConfig{
            {s_gfx_object_get_member_value_path, s_gfx_object_invoke_path,
             s_gfx_object_set_member_value_path},
            [this]() -> UObject*
            {
                if (auto* hero = Kh02Story::map_overlay_hero())
                {
                    return hero;
                }
                if (auto* hero = character_swap::reward_context())
                {
                    return hero;
                }
                return m_player_context.trusted_current_playable_cached_sora_pawn();
            },
            [this]()
            {
                if (m_native_hook_service.title_or_ui_controller_active() ||
                    s_runtime_state.ui_diagnostic_state().blocking_ui_state().snapshot().active)
                {
                    return false;
                }
                if (auto* hero = Kh02Story::map_overlay_hero())
                {
                    return Kh02Story::map_overlay_ready(hero);
                }
                if (character_swap::current_hero())
                {
                    return character_swap::reward_context() != nullptr;
                }
                return !m_player_reacquire.ignored_map_active() &&
                       !runtime_sora_state_matches_kind(
                           RuntimeSoraStateKind::Cinematic,
                           m_sora_level_monitor.cached_runtime_sora_current_state_id,
                           m_sora_level_monitor.cached_runtime_sora_state_class);
            },
            [this]()
            {
                return m_goa_settings.map_overlay_enabled();
            },
            [this](UObject* pawn, bool rediscover, std::vector<RuntimeLuckyEmblemMarker>& markers)
            {
                m_lucky_emblem_tracker.unrecorded_markers(pawn, rediscover, markers);
            },
            [this](bool rediscover, std::vector<RuntimeIngredientMarker>& markers)
            {
                m_ingredients.uncollected_markers(rediscover, markers);
            },
            [this]()
            {
                return m_goa_settings.map_icon_options();
            },
            [this](MapIconOption option, bool enabled)
            {
                (void)m_goa_settings.set_map_icon_option(option, enabled);
            },
            [this](bool visible)
            {
                return m_world_bootstrap.set_battle_level_map_visible(visible);
            },
            [](UObject* pawn, std::wstring_view token) -> std::optional<bool>
            {
                bool owned{};
                if (!try_call_bp_tres_have_item(
                        token, pawn, owned, s_runtime_state, s_bp_have_item_path,
                        s_tres_game_blueprint_library_default_object_path, false, nullptr))
                    return {};
                return owned;
            },
            []()
            {
                return map_overlay_keybinds_option_from_seed();
            },
        }};
    }

    auto RuntimeComposition::Impl::make_goa_settings_menu() -> RuntimeOptionsMenuService
    {
        return RuntimeOptionsMenuService{RuntimeOptionsMenuConfig{
            .menus =
                {
                    make_goa_runtime_options_menu(m_experience_multiplier, m_battle_level_sync,
                                                  m_controller_preferences, m_goa_settings,
                                                  []()
                                                  {
                                                      return try_get_bridge_dir();
                                                  }),
                    make_randomization_runtime_options_menu(m_enemy_remaps, m_party_members,
                                                            m_goa_settings),
                    make_music_runtime_options_menu(m_goa_settings),
                    make_mom_runtime_options_menu(
                        []()
                        {
                            return try_get_bridge_dir();
                        }),
                    make_goa_credits_menu(),
                },
            .gfx_paths =
                GfxProbeFunctionPaths{
                    s_gfx_object_get_member_value_path,
                    s_gfx_object_invoke_path,
                    s_gfx_object_set_member_value_path,
                },
            .enabled = true,
            .background_controller_input_allowed =
                [this]()
            {
                return m_controller_preferences.background_input_allowed();
            },
            .gameplay_key =
                [this](int32_t controller, std::wstring_view key, int32_t event, bool gamepad)
            {
                if (Kh02Story::route_challenges(controller, key, event, gamepad))
                {
                    return true;
                }
                // The open map owns the triggers for zoom before character swapping.
                if (m_map_overlay.route(controller, key, event, gamepad))
                {
                    return true;
                }
                if (character_swap::route(controller, key, event, gamepad))
                {
                    return true;
                }
                if (RuntimeGardenReturnService::route_kh02_character(controller, key, event,
                                                                     gamepad))
                {
                    return true;
                }
                if (m_ingredient_journal.route(controller, key, event, gamepad))
                {
                    return true;
                }
                return false;
            },
            .pointer_motion =
                [this]()
            {
                m_ingredient_journal.pointer_motion();
            },
        }};
    }

    auto RuntimeComposition::Impl::make_keyblade_level_menu() -> RuntimeKeybladeLevelMenuService
    {
        return RuntimeKeybladeLevelMenuService{RuntimeKeybladeLevelMenuConfig{
            .gfx_paths =
                GfxProbeFunctionPaths{
                    s_gfx_object_get_member_value_path,
                    s_gfx_object_invoke_path,
                    s_gfx_object_set_member_value_path,
                },
            .levels_start_at_max =
                []()
            {
                return seed_keyblade_levels_start_at_max(installed_seed_slot_data());
            },
            // Selection updates are driven by the stock Scaleform
            // OnListIndexChange callback. The service performs no
            // periodic menu-tree polling.
            .enabled = true,
        }};
    }

    auto RuntimeComposition::Impl::make_shop_movie() -> RuntimeShopMovieService
    {
        return RuntimeShopMovieService{
            [this]()
            {
                return m_player_context.trusted_current_playable_cached_sora_pawn();
            }};
    }

    auto RuntimeComposition::Impl::make_ingredient_journal() -> RuntimeIngredientJournalService
    {
        return RuntimeIngredientJournalService{RuntimeIngredientJournalConfig{
            {s_gfx_object_get_member_value_path, s_gfx_object_invoke_path,
             s_gfx_object_set_member_value_path},
            [this]()
            {
                return m_player_context.trusted_current_playable_cached_sora_pawn();
            },
            [this]()
            {
                return m_ingredients.journal_collected();
            },
            [this]()
            {
                return m_controller_preferences.background_input_allowed();
            },
            [this]()
            {
                return m_ingredients.journal_revision();
            },
        }};
    }

    auto RuntimeComposition::Impl::make_moogle_shop() -> RuntimeMoogleShopService
    {
        return RuntimeMoogleShopService{RuntimeMoogleShopConfig{
            .runtime_state = s_runtime_state,
            .payload_emitter =
                [](const RuntimeLootPayload& payload)
            {
                return emit_runtime_payload_once(payload);
            },
            .runtime_context_resolver =
                []()
            {
                return composition_context_summary();
            },
            .have_item_path = s_bp_have_item_path,
            .get_item_path = s_bp_get_item_path,
            .add_munny_path = s_bp_add_munny_path,
            .blueprint_library_default_object_path =
                s_tres_game_blueprint_library_default_object_path,
            .report_retry_interval = s_moogle_shop_report_retry_interval,
            .inventory_query_retry_interval = s_moogle_shop_inventory_query_retry_interval,
            .max_catalog_locations = 20,
            .enabled = s_enable_moogle_shop_checks,
            .debug_logging = s_debug_logging,
            .bridge_dir_resolver =
                []()
            {
                return try_get_bridge_dir();
            },
            .unreal_init_time_resolver =
                [this]()
            {
                if (!m_service_graph_ready)
                {
                    return std::chrono::steady_clock::time_point{};
                }
                return m_runtime_game_thread_driver.unreal_init_time();
            },
            .build_stamp = s_build_stamp,
            .material_item_index_min = s_moogle_shop_material_item_index_min,
            .material_item_index_max = s_moogle_shop_material_item_index_max,
            .material_enum_augmentation_enabled = s_enable_moogle_shop_item_enum_augmentation,
        }};
    }

    auto RuntimeComposition::Impl::make_periodic_bridge() -> RuntimePeriodicBridgeService
    {
        return RuntimePeriodicBridgeService{RuntimePeriodicBridgeConfig{
            s_runtime_state,
            m_remind_completion,
            m_moogle_shop,
            m_map_actor_lifecycle,
            m_runtime_ap_message_gate_state,
            m_runtime_item_grants,
            s_runtime_controller_abilities,
            []()
            {
                return try_get_bridge_dir();
            },
            []()
            {
                return composition_context_summary();
            },
            log_ability_array_needle_probe,
            log_targeted_sora_state_probe,
            log_live_ability_owner_probe,
            []()
            {
                return treasure_runtime_deps();
            },
            []()
            {
                return runtime_event_snapshot_deps();
            },
            [this]()
            {
                m_map_overlay.notify_treasure_changed();
            },
        }};
    }
} // namespace kh3ap
