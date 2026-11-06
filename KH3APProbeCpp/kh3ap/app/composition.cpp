#include "kh3ap/features/player/characters/character_controller_input_policy.h"
#include "kh3ap/app/composition/service_graph.h"
#include "kh3ap/core/settings/pak_seed_settings.h"
#include "kh3ap/app/composition/bindings.h"
#include "kh3ap/app/composition/settings.h"
#include "kh3ap/bridge/transport/pipe_transport.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/UObjectGlobals.hpp>

namespace kh3ap
{
    using namespace composition_detail;
    using namespace composition_detail::settings;
    using namespace RC;
    using namespace RC::Unreal;

    RuntimeComposition::Impl::Impl()
    {
        configure_runtime_controller_input(character_controller_input_policy());
        m_steam_platform_recovery.capture_early_platform_state();
        m_autosave_probe.install_exit_confirmation_bypass();

        if constexpr (s_enable_kh3_steam_save_startup_guard)
        {
            // KH3 resolves its Steam SaveGames root before on_unreal_init.
            // Observe the completed vanilla identity refresh early enough to
            // redirect only that root; never rewrite the identity itself.
            m_autosave_probe.install_early_steam_save_startup_guard();
        }

        // All value dependencies are now constructed. Forward callbacks
        // below gate on this flag, so a service constructor cannot reach
        // a later, partially constructed service.
        m_service_graph_ready = true;

        if (s_debug_logging)
        {
            Output::send<LogLevel::Verbose>(STR("[KH3APProbeCpp] constructed build_stamp={}\n"),
                                            s_build_stamp);
        }
    }

    auto RuntimeComposition::Impl::initialize() -> void
    {
        if (!m_service_graph_ready)
        {
            return;
        }
        if (const auto root = try_get_bridge_dir())
        {
            kh3ap::pipe::start(*root);
        }
        m_steam_platform_recovery.observe_unreal_startup_state();
        m_runtime_startup.initialize();
        m_goa_settings.install_once();
        m_controller_preferences.install_once(
            m_goa_settings.status().legacy_controller_focus_enabled);
        m_experience_multiplier.install_once();
        m_enemy_remaps.install_once();
        m_party_members.install_once();
        m_colors.install_once();
        m_goa_settings_menu.install_once();
        m_keyblade_level_menu.install_once();
        m_classic_kingdom_completion.install_once();
        m_ingredients.install_once();
        m_ingredient_journal.install_once();
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        m_garden_return.set_gummiphone_diagnostic(
            [this](int navigation)
            {
                return m_ingredient_journal.diagnostic_json(navigation);
            });
#endif
        m_minigame_rewards.install_once();
        m_space.install_once();
        m_garden_return.install_once();
        m_save_space.install_once();
        m_kingdom_of_corona_qol.install_once();
        m_monstropolis_mapset.install_once();
        m_battlegate_activation.install_once();
        m_periodic_bridge.install_event_hooks();
        m_moogle_shop.initialize_runtime();
    }

    auto RuntimeComposition::Impl::reroll_colors() -> void
    {
        m_colors.reroll_colors();
    }

    auto RuntimeComposition::Impl::shutdown() -> void
    {
        m_ingredients.shutdown();
        kh3ap::pipe::stop();
    }

    auto RuntimeComposition::Impl::reset_runtime_map_actor_spawn_lifecycle_state() -> void
    {
        m_map_actor_lifecycle.reset();
        m_world_bootstrap.reset();
        m_player_context.reset_playable_context();
        if (m_service_graph_ready)
        {
            m_music_chests.reset_map_lifecycle();
            m_map_overlay.reset_map_lifecycle();
        }
    }

    auto RuntimeComposition::Impl::reset_runtime_map_lifecycle_state() -> void
    {
        reset_runtime_map_actor_spawn_lifecycle_state();
        m_lucky_emblem_tracker.reset_map_lifecycle();
        m_union_cross_commands.reset();
        m_beat_of_lead.reset();
        m_fz02_player_start.reset_map_lifecycle();
        m_story_progress.reset_map_lifecycle();
        m_remind_completion.reset();
        m_moogle_shop.request_reconciliation(STR("map_lifecycle_reset"));
    }

    RuntimeComposition::RuntimeComposition()
        : impl_((bind_installed_pak_seed_source(), std::make_unique<Impl>()))
    {
    }

    RuntimeComposition::~RuntimeComposition()
    {
        impl_->shutdown();
    }

    auto RuntimeComposition::initialize() -> void
    {
        impl_->initialize();
    }

    auto RuntimeComposition::reroll_colors() -> void
    {
        impl_->reroll_colors();
    }

} // namespace kh3ap
