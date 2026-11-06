#include "kh3ap/app/composition/service_graph.h"
#include "kh3ap/diagnostics/profiling/event_read_benchmark.h"
#include "kh3ap/app/composition/bindings.h"
#include "kh3ap/app/composition/settings.h"
#include "kh3ap/bridge/transport/bridge_paths.h"
#include "kh3ap/features/player/controller_abilities/controller_ability_service.h"
#include "kh3ap/features/grants/grant_safety.h"
#include "kh3ap/features/grants/delivery/item_call_helpers.h"
#include "kh3ap/diagnostics/randomizer_test_service.h"
#include "kh3ap/features/world/startup/autoload_service.h"
#include "kh3ap/features/world/space/space_policy.h"
#include "kh3ap/features/world/startup/new_game_service.h"
#include "kh3ap/features/party/party_test_service.h"
#include "kh3ap/features/enemies/enemy_clear_service.h"
#include "kh3ap/features/world/ui_control/ui_control_service.h"
#include "kh3ap/features/player/state/sora_state_catalog.h"
#include "kh3ap/features/tutorials/tutorial_skip_service.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <string>
#include <string_view>

namespace kh3ap
{
    using namespace composition_detail;
    using namespace composition_detail::settings;
    using namespace RC;
    using namespace RC::Unreal;
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    auto RuntimeComposition::Impl::make_enemy_clear() -> RuntimeEnemyClearService
    {
        return RuntimeEnemyClearService{RuntimeEnemyClearConfig{
            []()
            {
                return try_get_bridge_dir();
            },
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
            &m_enemy_remaps,
            []()
            {
                return s_runtime_state.grant_state().special_battle_mode_active();
            },
        }};
    }
#endif

#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    auto RuntimeComposition::Impl::make_party_test() -> RuntimePartyTestService
    {
        return RuntimePartyTestService{RuntimePartyTestConfig{
            m_party_members,
            []()
            {
                return try_get_bridge_dir();
            },
            [this]()
            {
                return m_player_context.trusted_current_playable_cached_sora_pawn();
            },
            [this]()
            {
                return !m_native_hook_service.title_or_ui_controller_active() &&
                       !m_player_reacquire.ignored_map_active() &&
                       !m_player_context.map_ready_context_key().empty() &&
                       !s_runtime_state.ui_diagnostic_state().blocking_ui_state().snapshot().active;
            },
            [this]()
            {
                return !m_native_hook_service.title_or_ui_controller_active() &&
                       !m_player_reacquire.ignored_map_active() &&
                       !s_runtime_state.ui_diagnostic_state()
                            .blocking_ui_state()
                            .snapshot()
                            .active &&
                       runtime_sora_control_ready(
                           m_player_context.trusted_current_playable_cached_sora_pawn());
            },
        }};
    }
#endif

#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    auto RuntimeComposition::Impl::make_new_game() -> RuntimeNewGameService
    {
        return RuntimeNewGameService{RuntimeNewGameConfig{
            []()
            {
                return try_get_bridge_dir();
            },
            [this]()
            {
                return m_player_context.trusted_current_playable_cached_sora_pawn();
            },
            [this](std::wstring_view row)
            {
                for (size_t choice = 1; choice < m_party_members.choice_count(); ++choice)
                {
                    if (m_party_members.choice_definition(choice).row_id == row)
                    {
                        return m_party_members.choice_unlocked(choice) ? 1 : 0;
                    }
                }
                return -1;
            },
        }};
    }
#endif

#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    auto RuntimeComposition::Impl::make_ui_control() -> RuntimeUiControlService
    {
        return RuntimeUiControlService{RuntimeUiControlConfig{
            m_kh02_runtime,
            []()
            {
                return try_get_bridge_dir();
            },
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
            [](RC::Unreal::UObject* sora)
            {
                return runtime_damage_sora_for_checkpoint(sora);
            },
            [](RC::Unreal::UObject* sora)
            {
                for (auto token : {L"MAT_ITEM45", L"MAT_ITEM09", L"MAT_ITEM21"})
                {
                    for (int i = 0; i < 6; ++i)
                    {
                        if (!try_call_bp_tres_get_item(
                                token, sora, false, s_runtime_state, s_bp_get_item_path,
                                s_tres_game_blueprint_library_default_object_path, false, nullptr))
                        {
                            return false;
                        }
                    }
                }
                return true;
            },
            []()
            {
                s_runtime_controller_abilities.request_refresh(
                    RuntimeControllerAbilityEvent::MapReady);
            },
            [this]()
            {
                return m_cutscene_skips.scene_identity();
            },
            [this](bool remove)
            {
                return m_world_bootstrap.diagnostic_snapshot(remove);
            },
            [this](UObject* pawn, bool toggle)
            {
                return m_map_overlay.diagnostic_probe(pawn, toggle);
            },
            [](UObject* pawn)
            {
                return benchmark_event_reads(s_runtime_state, pawn);
            },
        }};
    }
#endif

#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    auto RuntimeComposition::Impl::make_autoload() -> RuntimeAutoloadService
    {
        return RuntimeAutoloadService{RuntimeAutoloadConfig{
            []()
            {
                return try_get_bridge_dir();
            },
            [this]()
            {
                return m_player_context.trusted_current_playable_cached_sora_pawn();
            },
            [this]()
            {
                return m_player_context.map_ready_context_key();
            },
            []()
            {
                return s_runtime_state.ui_diagnostic_state().blocking_ui_state().snapshot().active;
            },
            []()
            {
                return runtime_cached_sora_state_matches_kind(s_runtime_state,
                                                              RuntimeSoraStateKind::Idle);
            },
            []()
            {
                const auto ui =
                    s_runtime_state.ui_diagnostic_state().blocking_ui_state().snapshot();
                return !space::inventory_ui_allowed(ui.active, ui.reason, ui.context);
            },
        }};
    }
#endif

#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    auto RuntimeComposition::Impl::make_randomizer_test() -> RuntimeRandomizerTestService
    {
        return RuntimeRandomizerTestService{RuntimeRandomizerTestConfig{
            m_goa_settings,
            m_bgm_shuffle,
            m_colors,
            []()
            {
                return try_get_bridge_dir();
            },
            [this]()
            {
                return m_player_context.trusted_current_playable_cached_sora_pawn();
            },
            [this]()
            {
                return !m_native_hook_service.title_or_ui_controller_active() &&
                       !m_player_reacquire.ignored_map_active();
            },
            [this]()
            {
                return m_now_playing.test_snapshot();
            },
        }};
    }
#endif

} // namespace kh3ap
