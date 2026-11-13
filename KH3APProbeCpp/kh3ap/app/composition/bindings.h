#pragma once
#include "kh3ap/core/types.h"
#include "kh3ap/bridge/transport/bridge_paths.h"
#include "kh3ap/bridge/rewards/vbonus_reward_bundle.h"
#include "kh3ap/app/state.h"
#include "kh3ap/features/grants/delivery/item_grant_apply_helpers.h"
#include "kh3ap/features/grants/delivery/item_grant_poll_helpers.h"
#include "kh3ap/features/player/state/sora_current_state_cache_view.h"
#include "kh3ap/hooks/kh3_runtime_object_observer.h"
#include "kh3ap/features/player/controller_abilities/controller_ability_service.h"
#include "kh3ap/features/treasure/treasure_runtime_helpers.h"
#include "kh3ap/features/grants/events/event_snapshot_helpers.h"
#include "kh3ap/hooks/hook_phase.h"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace kh3ap
{
    class RuntimeRetryPointProbeState;
    class RuntimeRetryBlueprintProbeState;
    struct RuntimeSoraLevelMonitorState;
} // namespace kh3ap

namespace kh3ap::composition_detail
{
    using RC::Unreal::UFunction;
    using RC::Unreal::UObject;
    // Process-lifetime state is shared with native observer callbacks, whose
    // registration lifetime extends beyond RuntimeComposition's C++ owner.
    extern RuntimeState s_runtime_state;
    extern RuntimeControllerAbilityService s_runtime_controller_abilities;
    extern RuntimeRetryPointProbeState s_retry_point_probe_state;
    extern RuntimeRetryBlueprintProbeState s_retry_blueprint_probe_state;
    extern std::atomic<UFunction*> s_create_retry_point_process_event_target;
    extern std::atomic<UFunction*> s_retry_anim_notify_process_target;
    extern std::atomic<UFunction*> s_retry_logic_begin_play_process_target;
    auto record_treasure_route_observation(Kh3RuntimeObjectObservationKind kind) noexcept -> void;

    auto bridge_path_resolver() -> kh3ap::BridgePathResolver&;

    auto get_output_paths() -> const std::vector<std::filesystem::path>&;

    auto try_get_bridge_dir() -> std::optional<std::filesystem::path>;

    auto auto_skip_cutscenes_enabled_from_seed() -> bool;

    auto style_change_mode_from_seed() -> kh3ap::RuntimeStyleChangeMode;

    auto runtime_toggle_enabled_from_seed(std::string_view option_name) -> bool;

    auto guardians_and_mickey_walk_skip_enabled_from_seed() -> bool;

    auto fast_dance_minigame_enabled_from_seed() -> bool;

    auto fast_beat_of_lead_enabled_from_seed() -> bool;

    auto map_overlay_keybinds_option_from_seed() -> bool;




    auto fast_union_cross_enabled_from_seed() -> bool;

    auto append_ability_state_probe_jsonl(std::wstring_view kind, std::wstring_view reason,
                                          std::wstring_view object_name,
                                          std::wstring_view class_name,
                                          std::wstring_view property_name = {},
                                          std::wstring_view property_class = {}, int offset = -1,
                                          int element_size = -1, std::wstring_view value = {})
        -> void;

    auto log_ability_state_snapshot(std::wstring_view reason, UObject* preferred_object) -> void;

    auto log_ability_array_needle_probe(std::wstring_view reason, UObject* preferred_object)
        -> void;

    auto log_live_ability_owner_probe(std::wstring_view reason, UObject* preferred_object) -> void;

    auto log_targeted_sora_state_probe(std::wstring_view reason, UObject* preferred_object) -> void;

    auto runtime_item_grant_apply_deps(std::wstring_view context_summary)
        -> kh3ap::RuntimeItemGrantApplyDeps;

    auto runtime_sora_current_state_cache_view(const kh3ap::RuntimeSoraLevelMonitorState& monitor)
        -> kh3ap::RuntimeSoraCurrentStateCacheView;

    auto runtime_item_grant_post_apply_observer_deps(std::wstring_view context_summary)
        -> kh3ap::RuntimeItemGrantPostApplyObserverDeps;

    auto read_runtime_context_snapshot() -> std::optional<RuntimeContextSnapshot>;

    auto get_current_frame_context_snapshot() -> std::optional<RuntimeContextSnapshot>;

    auto composition_context_summary() -> std::wstring;

    auto composition_context_object_is_not_real() -> bool;

    auto is_vbonus_probe_hook_name(std::wstring_view hook_name) -> bool;

    auto should_skip_vbonus_probe_for_runtime_context() -> bool;

    auto maybe_publish_cached_pawn_runtime_context(std::chrono::steady_clock::time_point now,
                                                   std::wstring_view pawn_source = {},
                                                   bool force_publish = false) -> void;

    auto publish_goa_runtime_context(UObject* context, std::wstring_view hook_name) -> void;

    auto maybe_rearm_runtime_grant_for_ready_context(const RuntimeContextSnapshot& snapshot,
                                                     bool auto_equip_essential)
        -> void;

    auto update_composition_frame_context(UFunction* frame_node, std::wstring_view hook_name,
                                           bool auto_equip_essential)
        -> std::optional<RuntimeContextSnapshot>;

    auto build_unique_event_bridge_key_lookup() -> std::unordered_map<std::wstring, std::wstring>;

    auto build_vbonus_bridge_key_lookup() -> std::unordered_map<std::wstring, std::wstring>;

    auto cached_treasure_bridge_key_lookup() -> const kh3ap::TreasureBridgeKeyLookup&;

    auto treasure_runtime_deps() -> kh3ap::TreasureRuntimeDeps;

    auto build_vbonus_reward_bundle_lookup()
        -> std::unordered_map<std::wstring, VBonusRewardBundleEntry>;

    auto build_unique_vbonus_runtime_reward_lookup()
        -> std::unordered_map<std::wstring, std::wstring>;

    auto maybe_suppress_twilight_town_bistro_reward_popup(const HookTargetInfo& hook_info,
                                                          HookPhase phase,
                                                          UFunction* target_function, void* params,
                                                          std::wstring_view token_value,
                                                          std::wstring_view context_name) -> void;

    auto find_vbonus_bridge_key_for_source_id(std::wstring_view source_id)
        -> std::optional<std::wstring>;

    auto find_vbonus_reward_bundle_entry(std::wstring_view source_id)
        -> std::optional<VBonusRewardBundleEntry>;

    auto log_vbonus_reward_bundle_summary(std::wstring_view source_id,
                                           std::wstring_view resolution_source) -> void;

    auto find_vbonus_source_id_for_runtime_reward_token(std::wstring_view runtime_reward_token)
        -> std::optional<std::wstring>;

    auto emit_runtime_payload_once(const RuntimeLootPayload& payload) -> bool;

    auto should_emit_runtime_payload(const RuntimeLootPayload& payload) -> bool;

    auto try_emit_vbonus_payload_from_event_array_removed(std::wstring_view source,
                                                          std::wstring_view snapshot_key,
                                                          std::wstring_view entry) -> void;

    auto try_build_runtime_payload(std::wstring_view hook_name, std::wstring_view token_field_name,
                                   std::wstring_view token_value, std::wstring_view actor_name)
        -> std::optional<RuntimeLootPayload>;

    auto runtime_event_snapshot_deps() -> kh3ap::RuntimeEventSnapshotDeps;
} // namespace kh3ap::composition_detail
