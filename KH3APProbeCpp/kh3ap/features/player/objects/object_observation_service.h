#pragma once

#include "kh3ap/hooks/kh3_runtime_object_observer.h"
#include "kh3ap/features/treasure/treasure_runtime_helpers.h"

#include <functional>
#include <memory>
#include <string_view>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    class TreasureClassCache;

    struct RuntimeObjectObservationConfig
    {
        bool observer_enabled{};
        bool static_construct_callback_enabled{};
        bool observe_open_treasure_player_state{};
        bool observe_open_treasure_char_state{};
        bool observe_sora_pawn{};
        bool observe_festival_dance_story{};
        bool observe_beat_of_lead_story{};
        bool observe_union_cross_commands{};
        bool observe_final_world_gameflow_patches{};
        bool observe_olympus_completion_gameflow_patch{};
        bool observe_olympus_early_gameflow_patch{};
        bool observe_twilight_town_gameflow_patch{};
        bool observe_olympus_save_points_gameflow_patch{};
        bool observe_caribbean_sea_spectacle_patch{};
        bool observe_toy_box_gameflow_patches{};
        bool observe_link_tutorial_patches{};
        bool observe_informational_help_patches{};
        bool observe_lucky_mark_components{};
        bool observe_runtime_portal_classes{};
        bool observe_runtime_world_bootstrap_classes{};
        TreasureClassCache& treasure_class_cache;
        std::wstring_view open_treasure_player_state_class_path{};
        std::wstring_view open_treasure_char_state_class_path{};
        std::wstring_view treasure_logic_actor_class_path{};
        std::function<TreasureRuntimeDeps()> treasure_runtime_deps{};
        std::function<void(Kh3RuntimeObjectObservationKind)>
                record_treasure_route_observation{};
        std::function<void(RC::Unreal::UObject*)>
                process_festival_dance_story_object{};
        std::function<void(RC::Unreal::UObject*)>
                process_beat_of_lead_manager_object{};
        std::function<void(RC::Unreal::UObject*)>
                process_union_cross_command_object{};
        std::function<void(RC::Unreal::UObject*)>
                process_sora_pawn_object{};
        std::function<void(RC::Unreal::UObject*)>
                process_lucky_mark_component_object{};
        std::function<void(RC::Unreal::UObject*)>
                process_bx_portal_class_object{};
        std::function<void(RC::Unreal::UObject*)>
                process_death_mirror_class_object{};
        std::function<void(RC::Unreal::UObject*)>
                process_runtime_world_bootstrap_class_object{};
        std::function<void()> notify_treasure_changed{};
    };

    // Owns the process-wide UObject-create observer, the append-only UE4SS
    // StaticConstructObject callback registration, and game-thread queue dispatch.
    class RuntimeObjectObservationService
    {
    public:
        explicit RuntimeObjectObservationService(
                RuntimeObjectObservationConfig config);
        ~RuntimeObjectObservationService();

        RuntimeObjectObservationService(
                const RuntimeObjectObservationService&) = delete;
        auto operator=(const RuntimeObjectObservationService&)
                -> RuntimeObjectObservationService& = delete;
        RuntimeObjectObservationService(
                RuntimeObjectObservationService&&) noexcept;
        auto operator=(RuntimeObjectObservationService&&) noexcept
                -> RuntimeObjectObservationService&;

        auto install() -> bool;
        auto uninstall() -> void;
        auto disable_festival_dance_story_observation() -> void;
        auto drain(bool discard) -> void;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
