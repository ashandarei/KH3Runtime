#include "kh3ap/hooks/hook_lifecycle_service.h"

#include "kh3ap/diagnostics/kh3_autosave_probe.h"
#include "kh3ap/hooks/inventory/inventory_snapshot.h"
#include "kh3ap/hooks/frame_wait/kh3_frame_wait.h"
#include "kh3ap/core/performance_build.h"
#include "kh3ap/hooks/patches/kh3_expanded_save_slots_patch.h"
#include "kh3ap/hooks/patches/kh3_signed_munny_patch.h"
#include "kh3ap/hooks/patches/kh3_sqex_translucent_slice_view_guard_patch.h"
#include "kh3ap/hooks/blocking_ui/blocking_ui_hook_service.h"
#include "kh3ap/features/player/controller_abilities/controller_ability_service.h"
#include "kh3ap/hooks/patches/code_menu_first_bypass.h"
#include "kh3ap/hooks/patches/ez_codes_tutorial_bypass.h"
#include "kh3ap/hooks/patches/menu_notice_bypass.h"
#include "kh3ap/hooks/patches/cuisine_availability.h"
#include "kh3ap/features/story/world_fixes/big_hero_rescue_damage.h"
#include "kh3ap/app/game_thread_driver_service.h"
#include "kh3ap/hooks/hook_service.h"
#include "kh3ap/features/ui/map_overlay/minimap_context_service.h"
#include "kh3ap/features/player/objects/object_observation_service.h"
#include "kh3ap/hooks/production_hook_registry.h"
#include "kh3ap/diagnostics/retry/retry_probe_service.h"
#include "kh3ap/features/player/state/sora_state_hook_service.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <UnrealDef.hpp>

#include <utility>

using namespace RC;

namespace kh3ap
{
    struct RuntimeNativeHookLifecycleService::Impl
    {
        explicit Impl(RuntimeNativeHookLifecycleConfig config)
            : config_(std::move(config)),
              production_hooks_(make_production_hook_config())
        {
        }

        ~Impl()
        {
            shutdown(STR("destructor"));
        }

        auto make_production_hook_config()
                -> RuntimeProductionHookRegistryConfig
        {
            return RuntimeProductionHookRegistryConfig{
                    .apply_map_name = [this](auto& context, auto& hook, auto phase) {
                        config_.native_hooks.handle_apply_map_name(
                                context,
                                hook,
                                phase);
                    },
                    .uip_map_name_callback = [this](
                                                       auto& context,
                                                       auto& hook,
                                                       auto phase) {
                        config_.native_hooks.handle_uip_map_name_callback(
                                context,
                                hook,
                                phase);
                    },
                    .tres_map_jump = [this](auto& context, auto& hook, auto phase) {
                        config_.native_hooks.handle_tres_map_jump(
                                context,
                                hook,
                                phase);
                    },
                    .handle_starting_new_player = [this](
                                                            auto& context,
                                                            auto& hook,
                                                            auto phase) {
                        config_.native_hooks.handle_starting_new_player(
                                context,
                                hook,
                                phase);
                    },
                    .find_player_start = [this](auto& context, auto& hook, auto phase) {
                        config_.native_hooks.handle_find_player_start(
                                context,
                                hook,
                                phase);
                    },
                    .set_pause_menu_type = [this](auto& context, auto& hook, auto phase) {
                        config_.native_hooks.handle_set_pause_menu_type(
                                context,
                                hook,
                                phase);
                    },
                    .set_special_battle_mode = [this](
                                                         auto& context,
                                                         auto& hook,
                                                         auto phase) {
                        config_.native_hooks.handle_set_special_battle_mode(
                                context,
                                hook,
                                phase);
                    },
                    .note_game_thread = [this](std::wstring_view source) {
                        config_.game_thread_driver.note_game_thread(source);
                    },
            };
        }

        auto install_native_dispatch() -> void
        {
            if (production_hooks_.installed())
            {
                return;
            }

            Output::send<LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] Hook install stage=native_runtime_dispatch begin\n"));
            const bool runtime_object_observer_installed =
                    config_.object_observations.install();
            const bool game_engine_tick_hook_installed =
                    config_.game_thread_driver.install_tick_hook();
            config_.retry_probes.refresh_blueprint_process_observer_targets(
                    true);
            production_hooks_.install();
            config_.minimap_context.install();
            code_menu_first_bypass_.install();
            ez_codes_tutorial_bypass_.install();
            menu_notice_bypass_.install();
            cuisine_availability_.install();
            big_hero_rescue_damage_.install();
            // Add the narrow UI-focus and engine-pause observers during Unreal
            // initialization. Their pre/post callbacks reject unrelated
            // dispatches with two exact FName comparisons apiece.
            config_.blocking_ui_hooks.install_once();
            config_.controller_abilities.install_event_hooks();

            Output::send<LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] Hook install stage=native_runtime_dispatch end "
                        "engine_tick={} runtime_object_observer={}\n"),
                    config_.game_engine_tick_hook_enabled
                            ? (game_engine_tick_hook_installed
                                       ? STR("installed")
                                       : STR("failed"))
                            : STR("disabled"),
                    config_.runtime_object_observer_enabled
                            ? (runtime_object_observer_installed
                                       ? STR("installed")
                                       : STR("failed"))
                            : STR("disabled"));
        }

        auto install() -> void
        {
            if (!config_.hook_install_enabled)
            {
                Output::send<LogLevel::Verbose>(
                        STR("[KH3APProbeCpp] Native hook install disabled by build config\n"));
                return;
            }

            shutdown_ = false;
            auto install_dispatch = [this]() { install_native_dispatch(); };
            if (config_.profile_native_dispatch_install)
            {
                config_.profile_native_dispatch_install(
                        std::move(install_dispatch));
            }
            else
            {
                install_dispatch();
            }

            if (config_.signed_munny_patch_enabled)
            {
                config_.signed_munny_patch.install(config_.build_stamp);
            }
            if (config_.expanded_save_slots_patch_enabled)
            {
                config_.expanded_save_slots_patch.install(config_.build_stamp);
            }
            if (config_.sqex_translucent_slice_view_guard_patch_enabled)
            {
                config_.sqex_translucent_slice_view_guard_patch.install(
                        config_.build_stamp);
            }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            install_native_frame_wait(config_.build_stamp);
#endif

            Kh3AutosaveProbeConfig autosave_config{};
            autosave_config.enabled = config_.autosave_probe_enabled;
            autosave_config.steam_save_startup_guard_enabled =
                    config_.steam_save_startup_guard_enabled;
            autosave_config.save_game_to_slot_counter_enabled = false;
            // Completion tracking patches only the KH3 executable's import
            // slot. It must never detour CloseHandle process-wide.
            autosave_config.manual_save_close_tracking_enabled = true;
            autosave_config.build_stamp = std::wstring{config_.build_stamp};
            autosave_config.protocol_name = std::wstring{config_.protocol_name};
            autosave_config.protocol_version = config_.protocol_version;
            autosave_config.bridge_dir_resolver =
                    config_.bridge_dir_resolver;
            autosave_config.runtime_context_resolver =
                    config_.runtime_context_resolver;
            autosave_config.manual_save_completed_handler =
                    config_.manual_save_completed_handler;
            autosave_config.olympus_coliseum_save_completed_handler =
                    config_.olympus_coliseum_save_completed_handler;
            config_.autosave_probe.install(std::move(autosave_config));
            inventory_snapshot::install();

            // The focus hooks are still only armed after gameplay controller
            // discovery; this call does not attach them on the title screen.
            config_.sora_state_hooks.install_hooks(STR("on_unreal_init"));
        }

        auto shutdown(std::wstring_view reason) -> void
        {
            if (shutdown_)
            {
                return;
            }

            // Stop admission and drain production callbacks before releasing
            // any of the services those callbacks may access.
            production_hooks_.uninstall();
            config_.game_thread_driver.mark_unreal_uninitialized();
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            uninstall_native_frame_wait();
#endif
            config_.minimap_context.uninstall();
            config_.object_observations.uninstall();
            config_.game_thread_driver.uninstall_tick_hook();
            config_.autosave_probe.uninstall(reason);
            inventory_snapshot::uninstall();
            config_.sqex_translucent_slice_view_guard_patch.uninstall(reason);
            config_.expanded_save_slots_patch.uninstall(reason);
            config_.signed_munny_patch.uninstall(reason);
            config_.controller_abilities.uninstall_event_hooks();
            code_menu_first_bypass_.uninstall();
            ez_codes_tutorial_bypass_.uninstall();
            menu_notice_bypass_.uninstall();
            big_hero_rescue_damage_.uninstall();
            cuisine_availability_.uninstall();
            config_.blocking_ui_hooks.uninstall(reason);
            config_.sora_state_hooks.uninstall_hooks();

            shutdown_ = true;
        }

        RuntimeNativeHookLifecycleConfig config_;
        RuntimeProductionHookRegistry production_hooks_;
        RuntimeCodeMenuFirstBypass code_menu_first_bypass_;
        RuntimeEzCodesTutorialBypass ez_codes_tutorial_bypass_;
        RuntimeMenuNoticeBypass menu_notice_bypass_;
        RuntimeCuisineAvailability cuisine_availability_;
        BigHeroRescueDamage big_hero_rescue_damage_;
        bool shutdown_{false};
    };

    RuntimeNativeHookLifecycleService::RuntimeNativeHookLifecycleService(
            RuntimeNativeHookLifecycleConfig config)
        : impl_(std::make_unique<Impl>(std::move(config)))
    {
    }

    RuntimeNativeHookLifecycleService::~RuntimeNativeHookLifecycleService() =
            default;
    RuntimeNativeHookLifecycleService::RuntimeNativeHookLifecycleService(
            RuntimeNativeHookLifecycleService&&) noexcept = default;
    auto RuntimeNativeHookLifecycleService::operator=(
            RuntimeNativeHookLifecycleService&&) noexcept
            -> RuntimeNativeHookLifecycleService& = default;

    auto RuntimeNativeHookLifecycleService::install() -> void
    {
        impl_->install();
    }

    auto RuntimeNativeHookLifecycleService::shutdown(
            std::wstring_view reason) -> void
    {
        impl_->shutdown(reason);
    }
}
