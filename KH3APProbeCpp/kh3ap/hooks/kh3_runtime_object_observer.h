#pragma once

#include "kh3ap/features/story/bootstrap/world_bootstrap.h"
#include "kh3ap/features/tutorials/link_tutorial_bypass.h"
#include "kh3ap/features/tutorials/world_keyblade_bypass.h"

#include <Unreal/NameTypes.hpp>
#include <Unreal/UObjectArray.hpp>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    enum class Kh3RuntimeObjectObservationKind : uint8_t
    {
        OpenTreasurePlayerState,
        OpenTreasureCharState,
        TreasureLogicActorConstructed,
        FestivalDanceStoryObjectConstructed,
        BeatOfLeadManagerConstructed,
        UnionCrossCommandObjectConstructed,
        SoraPawnConstructed,
        LuckyMarkComponentConstructed,
        BxPortalClassConstructed,
        DeathMirrorClassConstructed,
        WorldBootstrapClassConstructed,
        TreasureBoxReady,
        NativeItemGrant,
    };

    struct Kh3RuntimeObjectObservation
    {
        Kh3RuntimeObjectObservationKind kind{};
        RC::Unreal::UObject* object{};
        RC::Unreal::FName item_key{};
        RC::Unreal::FUObjectItem* object_item{};
    };

    struct Kh3RuntimeObjectObservationOptions
    {
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
    };

    // Multiplexes the one process-wide UObject-create listener used by the
    // runtime. It classifies objects and queues narrow observations.
    // Timing-critical pre-BeginPlay story matches are routed synchronously to
    // their owning patch components; all patch mechanics live outside this
    // observer.
    class Kh3RuntimeObjectObserver final : public RC::Unreal::FUObjectCreateListener
    {
    public:
        // The Lucky Emblem gallery can construct all 90 native components in
        // one frame. Keep enough bounded capacity for that map plus the other
        // narrow runtime observations created alongside it.
        static constexpr size_t max_pending_observations = 128;

        Kh3RuntimeObjectObserver() = default;
        ~Kh3RuntimeObjectObserver() override;

        Kh3RuntimeObjectObserver(const Kh3RuntimeObjectObserver&) = delete;
        auto operator=(const Kh3RuntimeObjectObserver&) -> Kh3RuntimeObjectObserver& = delete;
        Kh3RuntimeObjectObserver(Kh3RuntimeObjectObserver&&) = delete;
        auto operator=(Kh3RuntimeObjectObserver&&) -> Kh3RuntimeObjectObserver& = delete;

        auto install(const Kh3RuntimeObjectObservationOptions& options) -> bool;
        auto uninstall() -> void;
        [[nodiscard]] auto installed() const -> bool;
        auto disable_festival_dance_story_observation() -> void;

        [[nodiscard]] auto is_treasure_logic_actor(const RC::Unreal::UObject* object) const -> bool;
        auto observe_constructed_object(const RC::Unreal::UObjectBase* object) -> bool;
        auto enqueue_native_item_grant(RC::Unreal::UObject* treasure_logic_actor,
                                       const RC::Unreal::FName& item_key) -> void;
        auto enqueue_ready_treasure_box(RC::Unreal::UObject* treasure_box) -> void;
        auto drain(std::array<Kh3RuntimeObjectObservation, max_pending_observations>& output)
            -> size_t;
        [[nodiscard]] auto
        resolve_observation_object(const Kh3RuntimeObjectObservation& observation) const
            -> RC::Unreal::UObject*;
        auto take_dropped_count() -> uint64_t;

        auto NotifyUObjectCreated(const RC::Unreal::UObjectBase* object, RC::Unreal::int32 index)
            -> void override;
        auto OnUObjectArrayShutdown() -> void override;

    private:
        std::optional<bool> observe_created_class(const RC::Unreal::UObjectBase* object);
        std::optional<bool> apply_story_patches(const RC::Unreal::UObjectBase* object,
                                                const RC::Unreal::FName* class_name);
        std::optional<bool> apply_tutorial_patches(const RC::Unreal::UObjectBase* object,
                                                   const RC::Unreal::FName* class_name);

        bool initialize_class_names(const Kh3RuntimeObjectObservationOptions& options);
        void configure_observations(const Kh3RuntimeObjectObservationOptions& options);

        [[nodiscard]] auto object_name(const RC::Unreal::UObjectBase* object) const
            -> const RC::Unreal::FName*;
        auto enqueue(Kh3RuntimeObjectObservation observation) -> void;

        size_t object_name_offset_{};
        RC::Unreal::FName open_treasure_player_state_name_{};
        RC::Unreal::FName open_treasure_char_state_name_{};
        RC::Unreal::FName treasure_logic_actor_name_{};
        RC::Unreal::FName sora_pawn_class_name_{};
        RC::Unreal::FName lucky_mark_component_class_name_{};
        RC::Unreal::FName festival_dance_story_class_name_{};
        RC::Unreal::FName rapunzel_water_class_name_{};
        RC::Unreal::FName rapunzel_swing_camera_class_name_{};
        RC::Unreal::FName rapunzel_hair_approach_class_name_{};
        std::array<RC::Unreal::FName, 3> graveyard_backtracking_class_names_{};
        RC::Unreal::FName goa_level_script_class_name_{};
        RC::Unreal::FName festival_dance_story_manager_class_name_{};
        RC::Unreal::FName beat_of_lead_manager_class_name_{};
        RC::Unreal::FName union_cross_command_class_name_{};
        RC::Unreal::FName bx_portal_class_object_name_{};
        RC::Unreal::FName death_mirror_class_object_name_{};
        // Every slot is validated at install; unused slots would disable the
        // entire observer, including Lucky Emblem discovery.
        std::array<RC::Unreal::FName, runtime_world_bootstrap_class_object_names.size()>
            runtime_world_bootstrap_class_object_names_{};
        RC::Unreal::FName final_world_guidance_ew01_gameflow_class_name_{};
        RC::Unreal::FName final_world_guidance_ew04_gameflow_class_name_{};
        RC::Unreal::FName olympus_completion_gameflow_class_name_{};
        std::array<RC::Unreal::FName, runtime_world_keyblade_profiles.size()>
            world_keyblade_class_names_{};
        RC::Unreal::FName olympus_early_gameflow_class_name_{};
        RC::Unreal::FName twilight_town_gameflow_class_name_{};
        RC::Unreal::FName olympus_save_points_gameflow_class_name_{};
        RC::Unreal::FName caribbean_sea_spectacle_class_name_{};
        std::array<RC::Unreal::FName, 4> toy_box_gameflow_class_names_{};
        std::array<RC::Unreal::FName, runtime_link_tutorial_count> link_tutorial_class_names_{};
        std::array<RC::Unreal::FName, 6> informational_help_class_names_{};
        std::atomic<bool> observe_link_tutorial_patches_{false};
        std::atomic<bool> observe_informational_help_patches_{false};
        bool observe_open_treasure_player_state_{};
        bool observe_open_treasure_char_state_{};
        bool observe_any_open_treasure_state_{};
        bool observe_sora_pawn_{};
        bool observe_lucky_mark_components_{};
        bool observe_runtime_portal_classes_{};
        bool observe_runtime_world_bootstrap_classes_{};
        std::atomic<bool> observe_festival_dance_story_{false};
        std::atomic<bool> observe_beat_of_lead_story_{false};
        std::atomic<bool> observe_union_cross_commands_{false};
        std::atomic<bool> observe_final_world_gameflow_patches_{false};
        std::atomic<bool> observe_olympus_completion_gameflow_patch_{false};
        std::atomic<bool> observe_olympus_early_gameflow_patch_{false};
        std::atomic<bool> observe_twilight_town_gameflow_patch_{false};
        std::atomic<bool> observe_olympus_save_points_gameflow_patch_{false};
        std::atomic<bool> observe_caribbean_sea_spectacle_patch_{false};
        std::atomic<uint8_t> toy_box_enabled_patch_mask_{0};
        std::array<Kh3RuntimeObjectObservation, max_pending_observations> pending_{};
        size_t pending_count_{};
        std::mutex pending_mutex_{};
        std::atomic<size_t> published_pending_count_{0};
        std::atomic<uint64_t> dropped_count_{0};
        std::atomic<bool> installed_{false};
    };
} // namespace kh3ap
