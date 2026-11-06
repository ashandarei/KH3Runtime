#include "kh3ap/app/composition/service_graph.h"
#include "kh3ap/app/composition/bindings.h"
#include "kh3ap/app/composition/settings.h"
#include "kh3ap/bridge/transport/bridge_paths.h"
#include "kh3ap/features/story/minigames/classic_kingdom_completion_service.h"
#include "kh3ap/features/world/space/space_service.h"
#include "kh3ap/features/world/portals/portal_service.h"
#include "kh3ap/features/world/garden_return/garden_return_service.h"
#include "kh3ap/features/world/space/save_space_service.h"
#include "kh3ap/features/ingredients/ingredient_service.h"
#include "kh3ap/features/player/characters/character_swap.h"
#include "kh3ap/features/journal/adversary_service.h"
#include "kh3ap/features/story/minigames/minigame_reward_service.h"
#include "kh3ap/features/story/minigames/olympus_coliseum_completion_service.h"
#include "kh3ap/features/story/world_fixes/kingdom_of_corona_qol_service.h"
#include "kh3ap/features/story/world_fixes/monstropolis_mapset_service.h"
#include "kh3ap/features/story/progression/battlegate_activation_service.h"
#include "kh3ap/features/grants/delivery/item_call_helpers.h"
#include "kh3ap/features/moogle_shop/workshop_service.h"
#include "kh3ap/features/tutorials/tutorial_skip_service.h"
#include "kh3ap/app/state.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <chrono>
#include <string_view>

namespace kh3ap
{
    using namespace composition_detail;
    using namespace composition_detail::settings;
    using namespace RC;
    using namespace RC::Unreal;
    auto RuntimeComposition::Impl::make_portals() -> RuntimePortalService
    {
        return RuntimePortalService{
            []()
            {
                return try_get_bridge_dir();
            },
            [this]()
            {
                return m_service_graph_ready
                           ? m_player_context.trusted_current_playable_cached_sora_pawn()
                           : nullptr;
            }};
    }

    auto RuntimeComposition::Impl::make_space() -> RuntimeSpaceService
    {
        return RuntimeSpaceService{
            []()
            {
                return try_get_bridge_dir();
            },
            [this]()
            {
                return m_service_graph_ready
                           ? m_player_context.trusted_current_playable_cached_sora_pawn()
                           : nullptr;
            }};
    }

    auto RuntimeComposition::Impl::make_garden_return() -> RuntimeGardenReturnService
    {
        return RuntimeGardenReturnService{[]()
                                          {
                                              return try_get_bridge_dir();
                                          }};
    }

    auto RuntimeComposition::Impl::make_save_space() -> RuntimeSaveSpaceService
    {
        return RuntimeSaveSpaceService{[]()
                                       {
                                           return try_get_bridge_dir();
                                       }};
    }

    auto RuntimeComposition::Impl::make_minigame_rewards() -> RuntimeMinigameRewardService
    {
        return RuntimeMinigameRewardService{
            []()
            {
                return try_get_bridge_dir();
            },
            [this]()
            {
                return m_service_graph_ready &&
                               !m_native_hook_service.title_or_ui_controller_active() &&
                               !m_player_reacquire.ignored_map_active() &&
                               !m_player_context.map_ready_context_key().empty() &&
                               !s_runtime_state.ui_diagnostic_state()
                                    .blocking_ui_state()
                                    .snapshot()
                                    .active
                           ? m_player_context.trusted_current_playable_cached_sora_pawn()
                           : nullptr;
            }};
    }

    auto RuntimeComposition::Impl::make_workshop() -> RuntimeWorkshopService
    {
        return RuntimeWorkshopService{{
            []()
            {
                return try_get_bridge_dir();
            },
            [this]()
            {
                return !m_native_hook_service.title_or_ui_controller_active() &&
                       !m_player_reacquire.ignored_map_active() &&
                       m_player_context.trusted_current_playable_cached_sora_pawn() != nullptr;
            },
            [this](std::wstring_view token)
            {
                auto* sora = m_player_context.trusted_current_playable_cached_sora_pawn();
                bool owned = false;
                return sora &&
                       try_call_bp_tres_have_item(
                           token, sora, owned, s_runtime_state, s_bp_have_item_path,
                           s_tres_game_blueprint_library_default_object_path, false, nullptr) &&
                       owned;
            },
        }};
    }

    auto RuntimeComposition::Impl::make_ingredients() -> RuntimeIngredientService
    {
        return RuntimeIngredientService{RuntimeIngredientConfig{
            .bridge_dir =
                []()
            {
                return try_get_bridge_dir();
            },
            .inventory_context =
                []()
            {
                // Ingredient event snapshots only read shared inventory. Keep
                // their owned alternate context separate from Sora's AP grants.
                if (auto* hero = character_swap::current_hero();
                    hero && character_swap::owns_swap_pawn(hero))
                {
                    return hero;
                }
                return s_runtime_state.grant_state().pawn_state().object();
            },
        }};
    }

    auto RuntimeComposition::Impl::make_adversaries() -> RuntimeAdversaryService
    {
        return RuntimeAdversaryService{
            []()
            {
                return try_get_bridge_dir();
            },
            [this]()
            {
                return m_player_context.trusted_current_playable_cached_sora_pawn();
            },
        };
    }

    auto RuntimeComposition::Impl::make_classic_kingdom_completion()
        -> RuntimeClassicKingdomCompletionService
    {
        return RuntimeClassicKingdomCompletionService{RuntimeClassicKingdomCompletionConfig{
            .enabled = s_enable_classic_kingdom_completion_hooks,
            .bridge_dir_resolver =
                []()
            {
                return try_get_bridge_dir();
            },
            .runtime_grant_transition_started =
                [](std::wstring_view game_id, std::wstring_view source)
            {
                const auto now = std::chrono::steady_clock::now();
                auto& grant_state = s_runtime_state.grant_state();
                const bool newly_armed =
                    grant_state.mark_classic_kingdom_transition_waiting(source, game_id, now);
                Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] ClassicKingdom runtime grant gate armed source={} "
                        "game_id={} newly_armed={} grants_safe=false mode=event_driven\n"),
                    source, game_id, newly_armed);
            },
        }};
    }

    auto RuntimeComposition::Impl::make_olympus_coliseum_completion()
        -> RuntimeOlympusColiseumCompletionService
    {
        return RuntimeOlympusColiseumCompletionService{RuntimeOlympusColiseumCompletionConfig{
            .enabled = s_enable_olympus_coliseum_completion_hooks,
            .bridge_dir_resolver =
                []()
            {
                return try_get_bridge_dir();
            },
            .runtime_grant_transition_started =
                [](std::wstring_view cup_id, std::wstring_view source)
            {
                const auto now = std::chrono::steady_clock::now();
                auto& grant_state = s_runtime_state.grant_state();
                const bool newly_armed =
                    grant_state.mark_olympus_coliseum_transition_waiting(source, cup_id, now);
                Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] OlympusColiseum runtime grant gate armed source={} "
                        "cup_id={} newly_armed={} grants_safe=false mode=event_driven\n"),
                    source, cup_id, newly_armed);
            },
        }};
    }

    auto RuntimeComposition::Impl::make_kingdom_of_corona_qol() -> RuntimeKingdomOfCoronaQolService
    {
        return RuntimeKingdomOfCoronaQolService{RuntimeKingdomOfCoronaQolConfig{
            .runtime_state = s_runtime_state,
            .set_game_flag_path = s_set_game_flag_path,
            .set_game_flag_boolean_path = s_set_game_flag_boolean_path,
            .set_flag_label_path = s_set_flag_label_path,
            .greater_equal_flag_label_path = s_greater_equal_flag_label_path,
            .get_game_flag_path = s_get_game_flag_path,
            .get_game_flag_boolean_path = s_get_game_flag_boolean_path,
            .map_ready_event_path = s_uip_map_name_on_callback_path,
            .blueprint_library_default_object_path =
                s_tres_game_blueprint_library_default_object_path,
            .enabled = s_enable_kingdom_of_corona_qol_hooks,
            .debug_logging = s_debug_logging,
        }};
    }

    auto RuntimeComposition::Impl::make_monstropolis_mapset() -> RuntimeMonstropolisMapSetService
    {
        return RuntimeMonstropolisMapSetService{RuntimeMonstropolisMapSetConfig{
            .runtime_state = s_runtime_state,
            .get_game_flag_path = s_get_game_flag_path,
            .blueprint_library_default_object_path =
                s_tres_game_blueprint_library_default_object_path,
        }};
    }

    auto RuntimeComposition::Impl::make_battlegate_activation()
        -> RuntimeBattlegateActivationService
    {
        return RuntimeBattlegateActivationService{RuntimeBattlegateActivationConfig{
            .runtime_state = s_runtime_state,
            .get_game_flag_path = s_get_game_flag_path,
            .blueprint_library_default_object_path =
                s_tres_game_blueprint_library_default_object_path,
        }};
    }
} // namespace kh3ap
