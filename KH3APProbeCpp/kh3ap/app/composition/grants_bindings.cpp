#include "kh3ap/app/composition/detour.h"
#include "kh3ap/app/composition/bindings.h"
#include "kh3ap/app/composition/settings.h"
#include "kh3ap/bridge/transport/bridge_paths.h"
#include "kh3ap/hooks/kh3_runtime_object_observer.h"
#include "kh3ap/features/grants/abilities/ability_state_probe_helpers.h"
#include "kh3ap/diagnostics/output/diagnostic_probe_output_helpers.h"
#include "kh3ap/features/grants/grant_pawn_state.h"
#include "kh3ap/features/grants/delivery/item_grant_apply_helpers.h"
#include "kh3ap/features/grants/delivery/item_grant_poll_helpers.h"
#include "kh3ap/features/player/objects/map_actor_lifecycle_service.h"
#include "kh3ap/features/story/remind/remind_completion_service.h"
#include "kh3ap/diagnostics/retry/retry_blueprint_probe_state.h"
#include "kh3ap/diagnostics/retry/retry_point_probe_state.h"
#include "kh3ap/features/player/state/sora_level_monitor_state.h"
#include "kh3ap/app/periodic_bridge_service.h"
#include "kh3ap/features/player/context/player_reacquire_service.h"
#include "kh3ap/features/tutorials/tutorial_skip_service.h"
#include <atomic>
#include <string>
#include <string_view>

namespace kh3ap::composition_detail
{
    using namespace RC;
    using namespace RC::Unreal;
    using namespace settings;
    // UE4SS's global ProcessEvent callbacks live for the process lifetime.
    // Keep their cached target and counters independent of the C++ mod object's
    // lifetime so the observer callbacks do not capture or dereference `this`.
    kh3ap::RuntimeRetryPointProbeState s_retry_point_probe_state{};
    std::atomic<UFunction*> s_create_retry_point_process_event_target{nullptr};
    kh3ap::RuntimeRetryBlueprintProbeState s_retry_blueprint_probe_state{};
    std::atomic<UFunction*> s_retry_anim_notify_process_target{nullptr};
    std::atomic<UFunction*> s_retry_logic_begin_play_process_target{nullptr};
    static_assert(std::atomic<UFunction*>::is_always_lock_free);

    auto append_ability_state_probe_jsonl(std::wstring_view kind, std::wstring_view reason,
                                          std::wstring_view object_name,
                                          std::wstring_view class_name,
                                          std::wstring_view property_name,
                                          std::wstring_view property_class, int offset,
                                          int element_size, std::wstring_view value) -> void
    {
        const std::wstring context_summary = composition_context_summary();
        kh3ap::append_ability_state_probe_jsonl(
            kh3ap::make_runtime_ability_state_probe_deps(try_get_bridge_dir(), s_build_stamp,
                                                         context_summary, s_runtime_state),
            kind, reason, object_name, class_name, property_name, property_class, offset,
            element_size, value);
    }

    auto log_ability_state_snapshot(std::wstring_view reason, UObject* preferred_object) -> void
    {
        const std::wstring context_summary = composition_context_summary();
        kh3ap::log_ability_state_snapshot(
            kh3ap::make_runtime_ability_state_probe_deps(try_get_bridge_dir(), s_build_stamp,
                                                         context_summary, s_runtime_state),
            reason, preferred_object);
    }

    auto log_ability_array_needle_probe(std::wstring_view reason, UObject* preferred_object) -> void
    {
        const std::wstring context_summary = composition_context_summary();
        kh3ap::log_ability_array_needle_probe_if_enabled(
            kh3ap::make_runtime_ability_diagnostic_probe_output_deps(
                try_get_bridge_dir(), s_build_stamp, context_summary, s_runtime_state),
            s_runtime_state, s_enable_ability_array_needle_probe, reason, preferred_object);
    }

    auto log_live_ability_owner_probe(std::wstring_view reason, UObject* preferred_object) -> void
    {
        const std::wstring context_summary = composition_context_summary();
        kh3ap::log_live_ability_owner_probe_if_enabled(
            kh3ap::make_runtime_ability_diagnostic_probe_output_deps(
                try_get_bridge_dir(), s_build_stamp, context_summary, s_runtime_state),
            s_runtime_state, s_enable_live_ability_owner_probe, reason, preferred_object);
    }

    auto log_targeted_sora_state_probe(std::wstring_view reason, UObject* preferred_object) -> void
    {
        const std::wstring context_summary = composition_context_summary();
        kh3ap::log_targeted_sora_state_probe_data(
            kh3ap::make_runtime_ability_diagnostic_probe_output_deps(
                try_get_bridge_dir(), s_build_stamp, context_summary, s_runtime_state),
            s_runtime_state, reason, preferred_object);
    }

    auto runtime_item_grant_apply_deps(std::wstring_view context_summary)
        -> kh3ap::RuntimeItemGrantApplyDeps
    {
        return kh3ap::make_runtime_item_grant_apply_deps(
            s_runtime_state, s_bp_get_item_path, s_bp_add_munny_path, s_set_game_flag_boolean_path,
            s_bp_tres_start_game_over_path, s_tres_game_blueprint_library_default_object_path,
            s_tresui_open_information_path, s_tres_item_manager_path, context_summary,
            s_debug_logging,
            kh3ap::RuntimeControllerUnlockGrantDeps{s_runtime_controller_abilities});
    }

    auto runtime_sora_current_state_cache_view(const kh3ap::RuntimeSoraLevelMonitorState& monitor)
        -> kh3ap::RuntimeSoraCurrentStateCacheView
    {
        return kh3ap::RuntimeSoraCurrentStateCacheView{
            .available = true,
            .state_comp = monitor.cached_runtime_sora_state_comp,
            .current_state = monitor.cached_runtime_sora_current_state,
            .state_comp_valid = monitor.cached_runtime_sora_state_comp_valid,
            .current_state_valid = monitor.cached_runtime_sora_current_state_valid,
            .current_state_id = monitor.cached_runtime_sora_current_state_id,
            .state_class = monitor.cached_runtime_sora_state_class,
            .state_source = monitor.cached_runtime_sora_state_source,
            .pawn_address = monitor.cached_runtime_sora_pawn_address,
        };
    }

    auto runtime_item_grant_post_apply_observer_deps(std::wstring_view context_summary)
        -> kh3ap::RuntimeItemGrantPostApplyObserverDeps
    {
        return kh3ap::make_runtime_item_grant_post_apply_observer_deps(
            s_runtime_state, s_enable_runtime_ability_state_probe,
            s_enable_targeted_runtime_ability_query_probe, context_summary,
            {{
                {STR("level"), s_bp_ability_get_level_path},
                {STR("equip_num"), s_bp_ability_get_equip_num_path},
                {STR("enabled"), s_bp_is_ability_enable_path},
            }},
            log_ability_state_snapshot, append_ability_state_probe_jsonl,
            log_live_ability_owner_probe);
    }
} // namespace kh3ap::composition_detail
