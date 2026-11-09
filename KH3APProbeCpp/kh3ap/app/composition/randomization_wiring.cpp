#include "kh3ap/app/composition/service_graph.h"
#include "kh3ap/app/composition/bindings.h"
#include "kh3ap/app/composition/settings.h"
#include "kh3ap/bridge/transport/bridge_paths.h"
#include "kh3ap/features/enemies/enemy_remap_service.h"
#include "kh3ap/features/enemies/fun_heartless_service.h"
#include "kh3ap/features/enemies/experience_multiplier_service.h"
#include "kh3ap/core/settings/goa_settings_service.h"
#include "kh3ap/core/input/controller_preferences_service.h"
#include "kh3ap/features/treasure/music_chest/music_chest_service.h"
#include "kh3ap/features/music/shuffle/bgm_shuffle_service.h"
#include "kh3ap/features/music/now_playing/now_playing_overlay.h"
#include "kh3ap/features/grants/delivery/item_call_helpers.h"
#include "kh3ap/core/platform/steam_platform_recovery_service.h"
#include "kh3ap/features/colors/color_service.h"
#include "kh3ap/features/party/party_member_service.h"
#include "kh3ap/features/player/state/sora_state_catalog.h"
#include "kh3ap/features/player/characters/character_swap.h"
#include "kh3ap/features/tutorials/tutorial_skip_service.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <string>

namespace kh3ap
{
    using namespace composition_detail;
    using namespace composition_detail::settings;
    using namespace RC;
    using namespace RC::Unreal;
    auto RuntimeComposition::Impl::make_steam_platform_recovery()
        -> RuntimeSteamPlatformRecoveryService
    {
        return RuntimeSteamPlatformRecoveryService{s_bypass_kh3_steam_game_server_init};
    }

    auto RuntimeComposition::Impl::make_controller_preferences()
        -> RuntimeControllerPreferencesService
    {
        return RuntimeControllerPreferencesService{make_controller_preferences_config()};
    }

    auto RuntimeComposition::Impl::make_goa_settings() -> RuntimeGoaSettingsService
    {
        return RuntimeGoaSettingsService{RuntimeGoaSettingsConfig{
            .bridge_dir_resolver =
                []()
            {
                return try_get_bridge_dir();
            },
        }};
    }

    auto RuntimeComposition::Impl::make_experience_multiplier()
        -> RuntimeExperienceMultiplierService
    {
        return RuntimeExperienceMultiplierService{RuntimeExperienceMultiplierConfig{
            .goa_settings = m_goa_settings,
            .build_stamp = s_build_stamp,
        }};
    }

    auto RuntimeComposition::Impl::make_enemy_remaps() -> RuntimeEnemyRemapService
    {
        return RuntimeEnemyRemapService{RuntimeEnemyRemapConfig{
            .enabled = true,
            .goa_settings = m_goa_settings,
            .bridge_dir_resolver =
                []()
            {
                return try_get_bridge_dir();
            },
        }};
    }

    auto RuntimeComposition::Impl::make_fun_heartless() -> RuntimeFunHeartlessService
    {
        return RuntimeFunHeartlessService{RuntimeFunHeartlessConfig{
            .bridge_dir =
                []()
            {
                return try_get_bridge_dir();
            },
            .sora =
                []()
            {
                return s_runtime_state.grant_state().pawn_state().object();
            },
            .battle_active =
                []()
            {
                int32_t flag{};
                return try_call_get_game_flag(
                           STR("gameflow_KG"), flag, s_runtime_state, s_get_game_flag_path,
                           s_tres_game_blueprint_library_default_object_path, false) &&
                       flag == 100;
            },
        }};
    }

    auto RuntimeComposition::Impl::make_party_members() -> RuntimePartyMemberService
    {
        return RuntimePartyMemberService{RuntimePartyMemberConfig{
            .runtime_state = s_runtime_state,
            .goa_settings = m_goa_settings,
            .bridge_dir_resolver =
                []()
            {
                return try_get_bridge_dir();
            },
            .on_pawn_possessed =
                [this](UObject* pawn)
            {
                m_colors.on_pawn_possessed(pawn);
            },
        }};
    }

    auto RuntimeComposition::Impl::make_colors() -> RuntimeColorService
    {
        return RuntimeColorService{RuntimeColorServiceConfig{
            .runtime_state = s_runtime_state,
            .seed_identity_resolver = [this]() -> std::wstring
            {
                return m_goa_settings.seed_identity();
            },
            // The live setting, not the yaml value: the yaml only
            // seeds it, and the in-game AP Options row overrides
            // and persists from there.
            .harmony_mode_resolver = [this]() -> kh3ap::RuntimeColorHarmonyMode
            {
                return m_goa_settings.status().color_randomization_mode == 2
                           ? kh3ap::RuntimeColorHarmonyMode::Wild
                           : kh3ap::RuntimeColorHarmonyMode::Coordinated;
            },
            .enabled_resolver = [this]() -> bool
            {
                return m_goa_settings.status().color_randomization_mode != 0;
            },
        }};
    }

    auto RuntimeComposition::Impl::make_music_chests() -> RuntimeMusicChestService
    {
        return RuntimeMusicChestService{RuntimeMusicChestConfig{
            []()
            {
                return try_get_bridge_dir();
            },
            [this]()
            {
                // Use the verified possessed alternate, never a cached outgoing Sora.
                if (character_swap::handoff_pending())
                {
                    return static_cast<UObject*>(nullptr);
                }
                if (auto* hero = character_swap::current_hero())
                {
                    // Melody owns a temporary game pause. Keep the possessed
                    // pawn available so its pause can be released on return.
                    return character_swap::owns_swap_pawn(hero) ? hero : nullptr;
                }
                return m_player_context.trusted_current_playable_cached_sora_pawn();
            },
            [this]()
            {
                return !m_native_hook_service.title_or_ui_controller_active() &&
                       !m_player_reacquire.ignored_map_active() &&
                       !s_runtime_state.ui_diagnostic_state().blocking_ui_state().snapshot().active;
            },
            [this](const FWeakObjectPtr& chest)
            {
                m_map_overlay.notify_chest_changed(chest);
            },
            [this]()
            {
                m_map_overlay.reset_map_lifecycle();
            },
        }};
    }

    auto RuntimeComposition::Impl::make_now_playing() -> NowPlayingOverlay
    {
        return NowPlayingOverlay{NowPlayingOverlayConfig{
            {s_gfx_object_get_member_value_path, s_gfx_object_invoke_path,
             s_gfx_object_set_member_value_path},
            [this]() -> const std::wstring&
            {
                return m_bgm_shuffle.now_playing_title();
            },
            [this]()
            {
                return m_goa_settings.song_display() &&
                       !m_native_hook_service.title_or_ui_controller_active() &&
                       !m_player_reacquire.ignored_map_active() &&
                       !s_runtime_state.ui_diagnostic_state()
                            .blocking_ui_state()
                            .snapshot()
                            .active &&
                       !runtime_sora_state_matches_kind(
                           RuntimeSoraStateKind::Cinematic,
                           m_sora_level_monitor.cached_runtime_sora_current_state_id,
                           m_sora_level_monitor.cached_runtime_sora_state_class);
            },
        }};
    }

    auto RuntimeComposition::Impl::make_bgm_shuffle() -> RuntimeBgmShuffleService
    {
        return RuntimeBgmShuffleService{RuntimeBgmShuffleConfig{
            [this]()
            {
                return m_goa_settings.music_shuffle_mode();
            },
            [this]()
            {
                return m_goa_settings.seed_identity();
            },
            [this]()
            {
                return !m_native_hook_service.title_or_ui_controller_active() &&
                       !m_player_reacquire.ignored_map_active();
            },
            [this]()
            {
                return m_goa_settings.form_themes_enabled();
            },
            [this]()
            {
                return m_player_context.trusted_current_playable_cached_sora_pawn();
            },
            []()
            {
                return s_runtime_state.ui_diagnostic_state().blocking_ui_state().snapshot().active;
            },
            [this]()
            {
                return m_goa_settings.include_custom_music();
            },
        }};
    }
} // namespace kh3ap
