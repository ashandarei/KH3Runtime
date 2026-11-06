#include "kh3ap/features/player/objects/object_observation_service.h"

#include "kh3ap/features/player/objects/object_observation_state.h"
#include "kh3ap/features/treasure/treasure_class_cache.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/Hooks.hpp>
#include <Unreal/UObject.hpp>
#include <UnrealDef.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <utility>

using namespace RC;
using namespace RC::Unreal;

namespace kh3ap
{
    namespace
    {
        // UE4SS retains StaticConstructObject callbacks for the process lifetime.
        // Keep both the callback and its target independent of the mod instance so
        // the registered closure never captures a destructible `this` pointer.
        Kh3RuntimeObjectObserver s_runtime_object_observer{};
        RuntimeObjectObservationState s_runtime_object_observation_state{};

        auto ensure_static_construct_callback_registered(bool callback_enabled,
                                                         bool observer_installed) -> void
        {
            if (!s_runtime_object_observation_state.should_register_static_construct_callback(
                    callback_enabled, observer_installed))
            {
                return;
            }

            Output::send<LogLevel::Verbose>(STR(
                "[KH3APProbeCpp] Hook install stage=register_static_construct_callback begin\n"));
            Hook::RegisterStaticConstructObjectPostCallback(
                [](const FStaticConstructObjectParameters&, UObject* constructed_object) -> UObject*
                {
                    // StaticConstructObject's existing UE4SS detour has completed the
                    // constructor at this point. Keep this shared path bounded: raw
                    // FName comparisons and, only on a match, a fixed-queue push.
                    s_runtime_object_observer.observe_constructed_object(constructed_object);
                    return constructed_object;
                });
            s_runtime_object_observation_state.mark_static_construct_callback_registered();
            Output::send<LogLevel::Verbose>(
                STR("[KH3APProbeCpp] Hook install stage=register_static_construct_callback end\n"));
        }
    } // namespace

    struct RuntimeObjectObservationService::Impl
    {
        explicit Impl(RuntimeObjectObservationConfig value) : config(std::move(value))
        {
        }

        ~Impl()
        {
            uninstall();
        }

        auto install() -> bool
        {
            refresh_open_treasure_state_class_cache(
                config.treasure_class_cache, config.open_treasure_player_state_class_path,
                config.open_treasure_char_state_class_path, config.treasure_logic_actor_class_path);

            const bool observer_installed =
                config.observer_enabled &&
                s_runtime_object_observer.install(Kh3RuntimeObjectObservationOptions{
                    .observe_open_treasure_player_state = config.observe_open_treasure_player_state,
                    .observe_open_treasure_char_state = config.observe_open_treasure_char_state,
                    .observe_sora_pawn = config.observe_sora_pawn,
                    .observe_festival_dance_story = config.observe_festival_dance_story,
                    .observe_beat_of_lead_story = config.observe_beat_of_lead_story,
                    .observe_union_cross_commands = config.observe_union_cross_commands,
                    .observe_final_world_gameflow_patches =
                        config.observe_final_world_gameflow_patches,
                    .observe_olympus_completion_gameflow_patch =
                        config.observe_olympus_completion_gameflow_patch,
                    .observe_olympus_early_gameflow_patch =
                        config.observe_olympus_early_gameflow_patch,
                    .observe_twilight_town_gameflow_patch =
                        config.observe_twilight_town_gameflow_patch,
                    .observe_olympus_save_points_gameflow_patch =
                        config.observe_olympus_save_points_gameflow_patch,
                    .observe_caribbean_sea_spectacle_patch =
                        config.observe_caribbean_sea_spectacle_patch,
                    .observe_toy_box_gameflow_patches = config.observe_toy_box_gameflow_patches,
                    .observe_link_tutorial_patches = config.observe_link_tutorial_patches,
                    .observe_informational_help_patches = config.observe_informational_help_patches,
                    .observe_lucky_mark_components = config.observe_lucky_mark_components,
                    .observe_runtime_portal_classes = config.observe_runtime_portal_classes,
                    .observe_runtime_world_bootstrap_classes =
                        config.observe_runtime_world_bootstrap_classes});
            ensure_static_construct_callback_registered(config.static_construct_callback_enabled,
                                                        observer_installed);
            return observer_installed;
        }

        auto uninstall() -> void
        {
            s_runtime_object_observer.uninstall();
        }

        auto drain(bool discard) -> void
        {
            std::array<Kh3RuntimeObjectObservation,
                       Kh3RuntimeObjectObserver::max_pending_observations>
                observations{};
            const size_t observation_count = s_runtime_object_observer.drain(observations);
            const uint64_t dropped = s_runtime_object_observer.take_dropped_count();
            // Coalesce existing treasure events, including overflow recovery.
            // This only invalidates overlay data; never reads actors in hooks.
            bool treasure_changed = dropped != 0;
            for (size_t index = 0; index < observation_count; ++index)
            {
                switch (observations[index].kind)
                {
                case Kh3RuntimeObjectObservationKind::OpenTreasurePlayerState:
                case Kh3RuntimeObjectObservationKind::OpenTreasureCharState:
                case Kh3RuntimeObjectObservationKind::TreasureLogicActorConstructed:
                case Kh3RuntimeObjectObservationKind::TreasureBoxReady:
                case Kh3RuntimeObjectObservationKind::NativeItemGrant:
                    treasure_changed = true;
                    break;
                default:
                    break;
                }
            }
            if (treasure_changed && config.notify_treasure_changed)
            {
                config.notify_treasure_changed();
            }
            const auto plan =
                make_runtime_object_observation_drain_plan(discard, observation_count, dropped);
            if (plan.report_overflow)
            {
                Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] Runtime object observation queue overflow dropped={}\n"),
                    dropped);
            }
            if (!plan.dispatch_observations)
            {
                // Playable-map objects can finish constructing while the
                // transition guard is still active. Preserve only weakly held
                // Lucky Emblem components across that guard; all mutation and
                // reflected polling remains deferred until normal gameplay.
                if (discard && config.process_lucky_mark_component_object)
                {
                    for (size_t index = 0; index < observation_count; ++index)
                    {
                        const auto& observation = observations[index];
                        if (observation.kind !=
                            Kh3RuntimeObjectObservationKind::LuckyMarkComponentConstructed)
                        {
                            continue;
                        }
                        if (UObject* const object =
                                s_runtime_object_observer.resolve_observation_object(observation))
                        {
                            config.process_lucky_mark_component_object(object);
                        }
                    }
                }
                return;
            }

            std::optional<TreasureRuntimeDeps> cached_treasure_deps{};
            const auto treasure_deps = [&]() -> const TreasureRuntimeDeps&
            {
                if (!cached_treasure_deps.has_value())
                {
                    cached_treasure_deps.emplace(config.treasure_runtime_deps());
                }
                return *cached_treasure_deps;
            };
            for (size_t index = 0; index < observation_count; ++index)
            {
                config.record_treasure_route_observation(observations[index].kind);
            }
            for (size_t index = 0; index < observation_count; ++index)
            {
                const auto& observation = observations[index];
                UObject* const object =
                    s_runtime_object_observer.resolve_observation_object(observation);
                if (!object)
                {
                    continue;
                }

                switch (observation.kind)
                {
                case Kh3RuntimeObjectObservationKind::OpenTreasurePlayerState:
                    handle_open_treasure_state_constructed(
                        object, STR("UObjectCreate TresPlayerState_OpenTreasureBox"),
                        treasure_deps());
                    break;
                case Kh3RuntimeObjectObservationKind::OpenTreasureCharState:
                    handle_open_treasure_state_constructed(
                        object, STR("UObjectCreate TresCharState_OpenTreasureBox"),
                        treasure_deps());
                    break;
                case Kh3RuntimeObjectObservationKind::TreasureLogicActorConstructed:
                    handle_treasure_logic_actor_constructed(
                        object, STR("UObjectCreate TreasureLogicActor_C"), treasure_deps(), true);
                    break;
                case Kh3RuntimeObjectObservationKind::FestivalDanceStoryObjectConstructed:
                    config.process_festival_dance_story_object(object);
                    break;
                case Kh3RuntimeObjectObservationKind::BeatOfLeadManagerConstructed:
                    if (config.process_beat_of_lead_manager_object)
                    {
                        config.process_beat_of_lead_manager_object(object);
                    }
                    break;
                case Kh3RuntimeObjectObservationKind::UnionCrossCommandObjectConstructed:
                    if (config.process_union_cross_command_object)
                    {
                        config.process_union_cross_command_object(object);
                    }
                    break;
                case Kh3RuntimeObjectObservationKind::SoraPawnConstructed:
                    if (config.process_sora_pawn_object)
                    {
                        config.process_sora_pawn_object(object);
                    }
                    break;
                case Kh3RuntimeObjectObservationKind::LuckyMarkComponentConstructed:
                    if (config.process_lucky_mark_component_object)
                    {
                        config.process_lucky_mark_component_object(object);
                    }
                    break;
                case Kh3RuntimeObjectObservationKind::BxPortalClassConstructed:
                    if (config.process_bx_portal_class_object)
                    {
                        config.process_bx_portal_class_object(object);
                    }
                    break;
                case Kh3RuntimeObjectObservationKind::DeathMirrorClassConstructed:
                    if (config.process_death_mirror_class_object)
                    {
                        config.process_death_mirror_class_object(object);
                    }
                    break;
                case Kh3RuntimeObjectObservationKind::WorldBootstrapClassConstructed:
                    if (config.process_runtime_world_bootstrap_class_object)
                    {
                        config.process_runtime_world_bootstrap_class_object(object);
                    }
                    break;
                case Kh3RuntimeObjectObservationKind::TreasureBoxReady:
                    try_emit_treasure_payload_from_box(
                        object, STR("TreasureLogicActor K2_DestroyActor beacon"), treasure_deps());
                    break;
                case Kh3RuntimeObjectObservationKind::NativeItemGrant:
                {
                    std::wstring item_key = observation.item_key.ToString();
                    if (item_key.empty())
                    {
                        item_key = STR("<none>");
                    }
                    handle_treasure_logic_actor_constructed(
                        object,
                        std::format(STR("BP_TresGetItem post TreasureLogicActor_C item={}"),
                                    item_key),
                        treasure_deps(), true);
                    break;
                }
                }
            }
        }

        RuntimeObjectObservationConfig config;
    };

    RuntimeObjectObservationService::RuntimeObjectObservationService(
        RuntimeObjectObservationConfig config)
        : impl_(std::make_unique<Impl>(std::move(config)))
    {
    }

    RuntimeObjectObservationService::~RuntimeObjectObservationService() = default;
    RuntimeObjectObservationService::RuntimeObjectObservationService(
        RuntimeObjectObservationService&&) noexcept = default;
    auto RuntimeObjectObservationService::operator=(RuntimeObjectObservationService&&) noexcept
        -> RuntimeObjectObservationService& = default;

    auto RuntimeObjectObservationService::install() -> bool
    {
        return impl_->install();
    }

    auto RuntimeObjectObservationService::uninstall() -> void
    {
        impl_->uninstall();
    }

    auto RuntimeObjectObservationService::disable_festival_dance_story_observation() -> void
    {
        s_runtime_object_observer.disable_festival_dance_story_observation();
    }

    auto RuntimeObjectObservationService::drain(bool discard) -> void
    {
        impl_->drain(discard);
    }
} // namespace kh3ap
