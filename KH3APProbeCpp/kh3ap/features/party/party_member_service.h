#pragma once

#include "kh3ap/core/settings/goa_settings_service.h"
#include "kh3ap/features/party/party_catalog.h"
#include "kh3ap/features/party/party_pair_policy.h"
#include "kh3ap/core/events/event_work.h"
#include "kh3ap/core/events/wake_hook.h"

#include <Unreal/FWeakObjectPtr.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    class RuntimeNativeRevisionHooks;
    class RuntimeGoaSettingsService;
    class RuntimeState;
    enum class RuntimePartyMapPolicy;

    struct RuntimePartyMemberStatus
    {
        bool available{};
        bool randomization_enabled{};
        bool reroll_every_room{};
        std::size_t unlocked_count{};
        std::size_t friend_1_choice{};
        std::size_t friend_2_choice{};
        std::size_t active_friend_1_choice{};
        std::size_t active_friend_2_choice{};
        bool hook_installed{};
    };

    struct RuntimePartyMemberConfig
    {
        RuntimeState& runtime_state;
        RuntimeGoaSettingsService& goa_settings;
        std::function<std::optional<std::filesystem::path>()> bridge_dir_resolver{};
        // Fired for every pawn possessed by any controller, not just party
        // members - callers decide for themselves which pawn classes they
        // care about. Used by the native color-randomizer trigger.
        std::function<void(RC::Unreal::UObject*)> on_pawn_possessed{};
    };

    struct RuntimePartyTestSnapshot
    {
        bool available{};
        bool test_active{};
        bool pending{};
        bool slots_readable{};
        bool slots_match{};
        bool randomized{};
        bool every_room{};
        std::array<std::size_t, runtime_party_member_slot_count> selected{};
        std::array<std::size_t, runtime_party_member_slot_count> active{};
        std::array<std::wstring, runtime_party_member_slot_count> pawns{};
        std::array<std::wstring, runtime_party_member_slot_count> classes{};
        std::array<std::wstring, runtime_party_member_slot_count> vanilla_npc_ids{};
        std::array<std::wstring, runtime_party_member_slot_count> active_npc_ids{};
        std::array<std::wstring, runtime_party_member_slot_count> navigation{};
        std::wstring manager{};
        std::wstring level{};
        std::wstring map_policy{};
        uint64_t requests{};
        uint64_t reconciles{};
        uint64_t failures{};
        uint64_t deletes{};
        uint64_t spawns{};
        double last_reconcile_ms{};
        double last_native_work_ms{};
    };

    // Replaces the retired GoA animation-notify actor with an event-driven
    // TresFriendComponent service. Choice 0 is None; choices 1..25 correspond
    // to the supported TresPartyData rows; choice 26 is Vanilla and choice 27
    // is Marshmallow. The nonexistent PartyMember_021 row is omitted.
    class RuntimePartyMemberService
    {
    public:
        explicit RuntimePartyMemberService(RuntimePartyMemberConfig config);
        ~RuntimePartyMemberService();

        auto install_once() -> void;
        auto tick() -> void;
        auto observe_goa_map_ready() -> void;
        auto observe_sora_context_ready() -> void;
        auto observe_sora_state(RC::Unreal::UObject* sora_pawn) -> void;

        [[nodiscard]] auto status() const -> RuntimePartyMemberStatus;
        [[nodiscard]] auto choice_count() const -> std::size_t;
        [[nodiscard]] auto choice_label(std::size_t choice) const -> std::wstring_view;
        [[nodiscard]] auto choice_available(int32_t friend_slot, std::size_t choice) const -> bool;

        auto set_randomization_enabled(bool enabled) -> bool;
        auto set_reroll_every_room(bool enabled) -> bool;
        auto set_friend_choice(int32_t friend_slot, std::size_t choice) -> bool;
        // Game-thread-only test controls. Overrides and unlocks are process-local;
        // ordinary reconciliation, random selection and native spawn paths are shared.
        auto test_begin(uint64_t seed, bool all_choices) -> bool;
        auto test_configure(bool randomized, bool every_room, std::size_t first, std::size_t second)
            -> bool;
        auto test_reroll(bool room_event) -> bool;
        auto test_end() -> void;
        [[nodiscard]] auto test_snapshot() -> RuntimePartyTestSnapshot;
        [[nodiscard]] auto choice_definition(std::size_t choice) const
            -> RuntimePartyMemberDefinition;
        [[nodiscard]] auto choice_unlocked(std::size_t choice) const -> bool;
        [[nodiscard]] auto test_active() const -> bool
        {
            return test_settings_.has_value();
        }
        auto apply_unlock_item(std::wstring_view runtime_token, RC::Unreal::UObject* sora_pawn,
                               std::wstring* failure_reason = nullptr) -> bool;
        // Current friend-slot pawns (nullptr per slot when empty/unresolved).
        // Snapshot for explicit diagnostic requests. Color discovery uses
        // UObject lifecycle notifications, including default Donald/Goofy.
        [[nodiscard]] auto active_party_pawns()
            -> std::array<RC::Unreal::UObject*, runtime_party_member_slot_count>;

    private:
        [[nodiscard]] auto effective_settings() const -> RuntimeGoaSettingsStatus;
        [[nodiscard]] auto effective_map_policy(RC::Unreal::UObject* level) const
            -> RuntimePartyMapPolicy;
        struct ActionViableStatePatch
        {
            std::wstring class_path{};
            RC::Unreal::FWeakObjectPtr default_object{};
            // Cooked CDOs can have serial zero in the pinned UE4SS build.
            // This fallback is protected by a process-lifetime GC root.
            RC::Unreal::UObject* rooted_default_object{};
            std::vector<std::byte> original_values{};
            int32_t element_size{};
            int32_t element_alignment{};
            bool snapshot_valid{};
            bool applied{};
        };

        auto request_reconcile(std::wstring_view reason, bool reroll_random_pair) -> void;
        auto observe_set_friend_all(RC::Unreal::UObject* friend_manager) -> void;
        auto observe_controller_possess(RC::Unreal::UObject* controller) -> void;
        auto refresh_ship_control() -> void;
        auto observe_can_battle(RC::Unreal::UObject* pawn, bool can_battle) -> void;
        auto refresh_unlocks_from_bridge() -> bool;
        auto unlock_choice(std::size_t choice) -> bool;
        [[nodiscard]] auto resolve_friend_manager() -> RC::Unreal::UObject*;
        [[nodiscard]] auto resolve_party_player() const -> RC::Unreal::UObject*;
        [[nodiscard]] auto desired_choices(RC::Unreal::UObject* level)
            -> std::array<std::size_t, runtime_party_member_slot_count>;
        [[nodiscard]] auto choose_random_pair(std::wstring_view level_name)
            -> std::array<std::size_t, runtime_party_member_slot_count>;
        auto capture_vanilla_party(RC::Unreal::UObject* friend_manager) -> bool;
        auto begin_reconcile(RC::Unreal::UObject* friend_manager) -> bool;
        auto yield_to_vanilla_party(RC::Unreal::UObject* level) -> bool;
        auto finish_reconcile() -> bool;
        auto delete_current_party() -> bool;
        auto party_assets_ready() -> bool;
        auto apply_custom_party_ai_for_friend_choice(std::size_t choice) -> void;
        auto tick_maintenance(std::chrono::steady_clock::time_point now) -> void;
        auto maintain_party_navigation() -> bool;
        auto initialize_party_knockout() -> void;
        auto maintain_party_placement() -> bool;
        auto apply_custom_party_ai_for_possession(RC::Unreal::UObject* controller,
                                                  RC::Unreal::UObject* pawn) -> bool;
        auto reconcile_action_viable_state_patches(RC::Unreal::UObject* sora_level) -> void;
        auto restore_action_viable_state_patches() -> void;

        RuntimePartyMemberConfig config_;
        std::optional<std::filesystem::path> bridge_dir_{};
        std::array<std::atomic_bool, party_catalog::party_choices.size() - 1> unlocked_{};
        RC::Unreal::FWeakObjectPtr current_friend_manager_{};
        RC::Unreal::FWeakObjectPtr vanilla_friend_manager_{};
        std::array<std::wstring, runtime_party_member_slot_count> vanilla_npc_ids_{};
        std::array<std::wstring, runtime_party_member_slot_count> pending_npc_ids_{};
        std::array<std::wstring, runtime_party_member_slot_count> active_npc_ids_{};
        bool reuse_native_pair_{};
        RC::Unreal::FWeakObjectPtr reconcile_friend_manager_{};
        RC::Unreal::FWeakObjectPtr goa_entry_pawn_{};
        RC::Unreal::FWeakObjectPtr context_entry_pawn_{};
        RC::Unreal::FWeakObjectPtr ship_control_sora_{};
        bool ship_control_active_{};
        bool awaiting_entry_control_{};
        std::array<RC::Unreal::FWeakObjectPtr, party_catalog::custom_party_ai_definitions.size()>
            custom_party_assigned_pawns_{};
        std::array<RC::Unreal::FWeakObjectPtr, party_catalog::custom_party_ai_definitions.size()>
            custom_party_trees_{};
        std::array<bool, party_catalog::custom_party_ai_definitions.size()>
            custom_party_tree_resolution_attempted_{};
        std::array<ActionViableStatePatch, party_catalog::action_viable_state_definitions.size()>
            action_viable_state_patches_{};
        std::array<std::optional<bool>, runtime_party_member_slot_count>
            party_presentation_modes_{};
        std::array<std::size_t, runtime_party_member_slot_count> pending_choices_{};
        std::array<std::wstring, runtime_party_member_slot_count> pending_pawn_class_paths_{};
        std::chrono::steady_clock::time_point party_asset_deadline_{};
        std::chrono::steady_clock::time_point next_party_asset_request_{};
        uint32_t party_asset_request_sequence_{};
        std::array<std::size_t, runtime_party_member_slot_count> random_choices_{};
        std::array<std::size_t, runtime_party_member_slot_count> active_choices_{};
        std::mt19937_64 random_{};
        uint64_t unlock_input_generation_{};
        uint64_t unlock_input_revision_{};
        RuntimeEventWork maintenance_;
        RuntimeNativeWakeHook teleport_wake_;
        std::unique_ptr<RuntimeNativeRevisionHooks> navigation_hooks_;
        uint64_t navigation_revision_{};
        RC::Unreal::FWeakObjectPtr placement_sora_{};
        std::array<float, 3> placement_sora_position_{};
        bool placement_pending_{};
        std::chrono::steady_clock::time_point next_reconcile_attempt_{};
        std::wstring reconcile_reason_{};
        std::pair<int, int> set_friend_all_hook_ids_{};
        std::pair<int, int> controller_possess_hook_ids_{};
        std::pair<int, int> can_battle_hook_ids_{};
        uint32_t settle_ticks_{};
        bool installed_{};
        bool active_{};
        bool hook_installed_{};
        bool reconcile_pending_{};
        bool preload_phase_complete_{};
        bool delete_phase_complete_{};
        bool random_choices_valid_{};
        bool force_random_pair_{};
        bool party_presentation_event_active_{};
        std::optional<RuntimeGoaSettingsStatus> test_settings_{};
        bool test_all_choices_{};
        std::mt19937_64 saved_random_{};
        std::array<std::size_t, runtime_party_member_slot_count> saved_random_choices_{};
        bool saved_random_choices_valid_{};
        bool saved_force_random_pair_{};
        uint64_t test_requests_{};
        uint64_t test_reconciles_{};
        uint64_t test_failures_{};
        uint64_t test_deletes_{};
        uint64_t test_spawns_{};
        std::chrono::steady_clock::time_point test_reconcile_started_{};
        double test_last_reconcile_ms_{};
        double test_native_work_ms_{};
    };
} // namespace kh3ap
