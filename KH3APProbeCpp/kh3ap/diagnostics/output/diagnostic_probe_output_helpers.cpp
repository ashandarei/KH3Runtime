#include "kh3ap/bridge/transport/bridge_channels.h"
#include "kh3ap/diagnostics/output/diagnostic_probe_output_helpers.h"

#include "kh3ap/diagnostics/ability_probe/ability_probe_diagnostic_state.h"
#include "kh3ap/hooks/blocking_ui/blocking_ui_state.h"
#include "kh3ap/features/world/space/space_service.h"
#include "kh3ap/features/grants/grant_safety.h"
#include "kh3ap/hooks/hook_filters.h"
#include "kh3ap/hooks/hook_phase.h"
#include "kh3ap/diagnostics/ability_probe/ability_reflection_event_data.h"
#include "kh3ap/diagnostics/ability_probe/ability_reflection_probe_helpers.h"
#include "kh3ap/diagnostics/ability_probe/chrinit_reflection_helpers.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/diagnostics/reflection/reflection_diagnostics.h"
#include "kh3ap/features/player/state/sora_state_catalog.h"
#include "kh3ap/app/state.h"
#include "kh3ap/core/types.h"
#include "kh3ap/diagnostics/ability_probe/stable_ability_trace_dedupe.h"
#include "kh3ap/bridge/catalog/update_catalog_state.h"
#include "kh3ap/bridge/utilities.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <UnrealDef.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UObject.hpp>

#include <chrono>
#include <cstdint>
#include <format>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

namespace kh3ap
{
    using namespace RC;

    auto make_runtime_diagnostic_probe_output_deps(std::optional<std::filesystem::path> bridge_dir,
                                                   std::wstring_view build_stamp,
                                                   std::wstring_view runtime_context_summary,
                                                   AbilityProbeDiagnosticState* diagnostic_state)
        -> RuntimeDiagnosticProbeOutputDeps
    {
        return {
            std::move(bridge_dir),
            build_stamp,
            runtime_context_summary,
            diagnostic_state,
        };
    }

    auto make_runtime_ability_diagnostic_probe_output_deps(
        std::optional<std::filesystem::path> bridge_dir, std::wstring_view build_stamp,
        std::wstring_view runtime_context_summary, RuntimeState& runtime_state)
        -> RuntimeDiagnosticProbeOutputDeps
    {
        return make_runtime_diagnostic_probe_output_deps(
            std::move(bridge_dir), build_stamp, runtime_context_summary,
            &runtime_state.ability_diagnostic_state().probe_diagnostic_state());
    }

    auto log_runtime_update_heartbeat_if_needed(RuntimeState& runtime_state,
                                                std::chrono::steady_clock::time_point now,
                                                std::chrono::steady_clock::duration check_interval,
                                                std::chrono::steady_clock::duration log_interval)
        -> void
    {
        const uint64_t on_update_call_count =
            runtime_state.ui_diagnostic_state().heartbeat_state().increment_calls();
        if (!runtime_state.ui_diagnostic_state().heartbeat_state().should_check(now,
                                                                                check_interval))
        {
            return;
        }

        const size_t pending_resample_count =
            runtime_state.event_state().deferred_resample_queue().size();
        const size_t pending_treasure_state_count =
            runtime_state.treasure_state().treasure_state_retry_queue().size();
        const size_t pending_treasure_logic_count =
            runtime_state.treasure_state().treasure_logic_actor_retry_queue().size();
        const bool has_pending_work = pending_resample_count > 0 ||
                                      pending_treasure_state_count > 0 ||
                                      pending_treasure_logic_count > 0;
        if (!has_pending_work ||
            !runtime_state.ui_diagnostic_state().heartbeat_state().should_log(now, log_interval))
        {
            return;
        }

        Output::send<LogLevel::Verbose>(
            STR("[KH3APProbeCpp] on_update heartbeat calls={} pending_resamples={} "
                "pending_treasure_states={} pending_treasure_logic={}\n"),
            on_update_call_count, static_cast<uint64_t>(pending_resample_count),
            static_cast<uint64_t>(pending_treasure_state_count),
            static_cast<uint64_t>(pending_treasure_logic_count));
    }

    auto append_targeted_sora_state_probe_entry(const RuntimeDiagnosticProbeOutputDeps& deps,
                                                std::wstring_view reason,
                                                const PropertyProbeEntry& entry) -> void;
    auto append_targeted_reflected_state_properties(const RuntimeDiagnosticProbeOutputDeps& deps,
                                                    std::wstring_view reason,
                                                    RC::Unreal::UObject* object,
                                                    std::wstring_view object_role,
                                                    int max_properties) -> void;
    auto append_targeted_equipment_manual_offsets(const RuntimeDiagnosticProbeOutputDeps& deps,
                                                  std::wstring_view reason,
                                                  RC::Unreal::UObject* equipment_object) -> void;
    auto append_live_ability_owner_probe_entry(const RuntimeDiagnosticProbeOutputDeps& deps,
                                               std::wstring_view reason,
                                               const PropertyProbeEntry& entry) -> void;
    auto append_live_ability_owner_probe_row(
        const RuntimeDiagnosticProbeOutputDeps& deps, std::wstring_view kind,
        std::wstring_view reason, std::wstring_view object_name, std::wstring_view class_name,
        std::wstring_view property_name = {}, std::wstring_view property_class = {},
        int offset = -1, int element_size = -1, std::wstring_view value = {}) -> void;
    auto append_tres_equipment_component_offset_probe(const RuntimeDiagnosticProbeOutputDeps& deps,
                                                      std::wstring_view reason,
                                                      RC::Unreal::UObject* object,
                                                      bool include_deep_targets) -> void;

    namespace
    {
        constexpr auto max_targeted_pawn_properties = 80;
        constexpr auto max_targeted_pawn_samples = 2;
        constexpr auto diagnostic_suppression_log_interval = 100;
        auto append_probe_jsonl(const RuntimeDiagnosticProbeOutputDeps& deps,
                                const std::filesystem::path& filename, std::string_view json)
            -> void
        {
            if (!deps.bridge_dir.has_value())
            {
                return;
            }

            if (deps.diagnostic_state)
            {
                auto lock = deps.diagnostic_state->acquire_log_lock();
                append_text_file(*deps.bridge_dir / filename, json);
                return;
            }

            append_text_file(*deps.bridge_dir / filename, json);
        }

    } // namespace

    auto append_property_probe_jsonl(const RuntimeDiagnosticProbeOutputDeps& deps,
                                     const std::filesystem::path& filename,
                                     const PropertyProbeEntry& entry) -> void
    {
        const std::string json =
            serialize_property_probe_entry(deps.build_stamp, deps.runtime_context_summary, entry);
        append_probe_jsonl(deps, filename, json);
    }

    auto append_targeted_sora_state_probe_entry(const RuntimeDiagnosticProbeOutputDeps& deps,
                                                std::wstring_view reason,
                                                const PropertyProbeEntry& entry) -> void
    {
        PropertyProbeEntry output_entry = entry;
        output_entry.reason = std::wstring{reason};
        append_property_probe_jsonl(deps, "targeted_sora_state_probe.jsonl", output_entry);
    }

    auto append_live_ability_owner_probe_entry(const RuntimeDiagnosticProbeOutputDeps& deps,
                                               std::wstring_view reason,
                                               const PropertyProbeEntry& entry) -> void
    {
        PropertyProbeEntry output_entry = entry;
        output_entry.reason = std::wstring{reason};
        append_property_probe_jsonl(deps, "live_ability_owner_probe.jsonl", output_entry);
    }

    auto append_live_ability_owner_probe_row(const RuntimeDiagnosticProbeOutputDeps& deps,
                                             std::wstring_view kind, std::wstring_view reason,
                                             std::wstring_view object_name,
                                             std::wstring_view class_name,
                                             std::wstring_view property_name,
                                             std::wstring_view property_class, int offset,
                                             int element_size, std::wstring_view value) -> void
    {
        append_live_ability_owner_probe_entry(deps, reason,
                                              PropertyProbeEntry{
                                                  .kind = std::wstring{kind},
                                                  .reason = std::wstring{reason},
                                                  .object_name = std::wstring{object_name},
                                                  .class_name = std::wstring{class_name},
                                                  .property_name = std::wstring{property_name},
                                                  .property_class = std::wstring{property_class},
                                                  .offset = offset,
                                                  .element_size = element_size,
                                                  .value = std::wstring{value},
                                              });
    }

    auto append_deep_target_object_probe(const RuntimeDiagnosticProbeOutputDeps& deps,
                                         std::wstring_view reason, RC::Unreal::UObject* object,
                                         std::wstring_view source_property) -> void
    {
        for (const PropertyProbeEntry& entry :
             prepare_deep_target_object_probe_entries(object, source_property))
        {
            append_live_ability_owner_probe_entry(deps, reason, entry);
        }
    }

    auto append_tres_equipment_component_offset_probe(const RuntimeDiagnosticProbeOutputDeps& deps,
                                                      std::wstring_view reason,
                                                      RC::Unreal::UObject* object,
                                                      bool include_deep_targets) -> void
    {
        const EquipmentComponentOffsetProbeData probe_data =
            prepare_tres_equipment_component_offset_probe_data(object, include_deep_targets);
        for (const PropertyProbeEntry& entry : probe_data.entries)
        {
            append_live_ability_owner_probe_entry(deps, reason, entry);
        }
        for (const EquipmentComponentDeepTarget& deep_target : probe_data.deep_targets)
        {
            append_deep_target_object_probe(deps, reason, deep_target.object,
                                            deep_target.source_property);
        }
    }

    auto append_targeted_reflected_state_properties(const RuntimeDiagnosticProbeOutputDeps& deps,
                                                    std::wstring_view reason,
                                                    RC::Unreal::UObject* object,
                                                    std::wstring_view object_role,
                                                    int max_properties) -> void
    {
        for (const PropertyProbeEntry& entry :
             prepare_targeted_reflected_state_property_entries(object, object_role, max_properties))
        {
            append_targeted_sora_state_probe_entry(deps, reason, entry);
        }
    }

    auto append_targeted_equipment_reached_asset_map(const RuntimeDiagnosticProbeOutputDeps& deps,
                                                     std::wstring_view reason,
                                                     RC::Unreal::UObject* object,
                                                     std::wstring_view source_property) -> void
    {
        for (const PropertyProbeEntry& entry :
             prepare_targeted_equipment_reached_asset_map_entries(object, source_property))
        {
            append_targeted_sora_state_probe_entry(deps, reason, entry);
        }
    }

    auto append_targeted_equipment_manual_offsets(const RuntimeDiagnosticProbeOutputDeps& deps,
                                                  std::wstring_view reason,
                                                  RC::Unreal::UObject* equipment_object) -> void
    {
        const EquipmentComponentOffsetProbeData probe_data =
            prepare_targeted_equipment_manual_offset_probe_data(equipment_object);
        for (const PropertyProbeEntry& entry : probe_data.entries)
        {
            append_targeted_sora_state_probe_entry(deps, reason, entry);
        }
        for (const EquipmentComponentDeepTarget& deep_target : probe_data.deep_targets)
        {
            append_targeted_equipment_reached_asset_map(deps, reason, deep_target.object,
                                                        deep_target.source_property);
        }
    }

    auto append_ability_array_needle_probe_jsonl(const RuntimeDiagnosticProbeOutputDeps& deps,
                                                 const AbilityArrayNeedleProbeEntry& entry,
                                                 std::wstring_view reason) -> void
    {
        const std::string json = serialize_ability_array_needle_probe_entry(
            deps.build_stamp, deps.runtime_context_summary, reason, entry);
        append_probe_jsonl(deps, "ability_array_needle_probe.jsonl", json);
    }

    auto append_ability_array_needle_probe_data(const RuntimeDiagnosticProbeOutputDeps& deps,
                                                std::wstring_view reason,
                                                RC::Unreal::UObject* preferred_object,
                                                RC::Unreal::UObject* last_world_context_object)
        -> void
    {
        for (const AbilityArrayNeedleProbeEntry& entry : prepare_ability_array_needle_probe_entries(
                 preferred_object, last_world_context_object))
        {
            append_ability_array_needle_probe_jsonl(deps, entry, reason);
        }
    }

    auto log_ability_array_needle_probe_if_enabled(const RuntimeDiagnosticProbeOutputDeps& deps,
                                                   AbilityProbeDiagnosticState& diagnostic_state,
                                                   bool probe_enabled, std::wstring_view reason,
                                                   RC::Unreal::UObject* preferred_object,
                                                   RC::Unreal::UObject* last_world_context_object)
        -> void
    {
        if (!probe_enabled && !diagnostic_state.ability_array_needle_force_enabled())
        {
            return;
        }

        const std::wstring probe_key =
            std::wstring{reason} + STR("|") + std::wstring{deps.runtime_context_summary};
        if (!diagnostic_state.mark_ability_array_needle_probe_key_if_new(probe_key))
        {
            return;
        }

        append_ability_array_needle_probe_data(deps, reason, preferred_object,
                                               last_world_context_object);
    }

    auto log_ability_array_needle_probe_if_enabled(const RuntimeDiagnosticProbeOutputDeps& deps,
                                                   RuntimeState& runtime_state, bool probe_enabled,
                                                   std::wstring_view reason,
                                                   RC::Unreal::UObject* preferred_object) -> void
    {
        log_ability_array_needle_probe_if_enabled(
            deps, runtime_state.ability_diagnostic_state().probe_diagnostic_state(), probe_enabled,
            reason, preferred_object, runtime_state.object_cache().last_world_context_object());
    }

    auto append_targeted_sora_state_probe_data(const RuntimeDiagnosticProbeOutputDeps& deps,
                                               RuntimeState& runtime_state,
                                               std::wstring_view reason,
                                               RC::Unreal::UObject* preferred_object) -> void
    {
        const auto pawn_candidates =
            collect_sora_ability_pawn_candidates(runtime_state, preferred_object);
        append_targeted_sora_state_probe_entry(
            deps, reason,
            PropertyProbeEntry{
                .kind = std::wstring{STR("summary")},
                .reason = std::wstring{reason},
                .object_name = format_object_debug_name(preferred_object),
                .class_name = std::format(STR("sora_candidates={}"),
                                          static_cast<uint64_t>(pawn_candidates.size())),
            });

        int pawn_count = 0;
        for (RC::Unreal::UObject* pawn_object : pawn_candidates)
        {
            if (!pawn_object || !kh3ap::is_uobject_valid_fast(pawn_object))
            {
                continue;
            }

            append_targeted_reflected_state_properties(deps, reason, pawn_object, STR("sora_pawn"),
                                                       max_targeted_pawn_properties);

            const TargetedSoraEquipmentLinkProbeData equipment_link =
                prepare_targeted_sora_equipment_link_probe_data(pawn_object);
            append_targeted_sora_state_probe_entry(deps, reason, equipment_link.entry);
            if (equipment_link.equipment_object)
            {
                append_targeted_equipment_manual_offsets(deps, reason,
                                                         equipment_link.equipment_object);
            }

            ++pawn_count;
            if (pawn_count >= max_targeted_pawn_samples)
            {
                break;
            }
        }
    }

    auto log_targeted_sora_state_probe_data(const RuntimeDiagnosticProbeOutputDeps& deps,
                                            RuntimeState& runtime_state, std::wstring_view reason,
                                            RC::Unreal::UObject* preferred_object) -> void
    {
        append_targeted_sora_state_probe_data(deps, runtime_state, reason, preferred_object);
    }

    auto append_live_ability_owner_probe_data(const RuntimeDiagnosticProbeOutputDeps& deps,
                                              std::wstring_view reason,
                                              RC::Unreal::UObject* preferred_object,
                                              RC::Unreal::UObject* last_world_context_object)
        -> void
    {
        const bool skip_heavy_probe = should_skip_heavy_live_owner_probe_for_reason(reason);
        if (skip_heavy_probe)
        {
            append_live_ability_owner_probe_row(deps, STR("summary"), reason,
                                                format_object_debug_name(preferred_object),
                                                STR("post_grant_owner_scan_skipped"));
            return;
        }

        const LiveAbilityOwnerProbeCandidateData candidate_data =
            prepare_live_ability_owner_probe_candidate_data(preferred_object,
                                                            last_world_context_object);
        Output::send<LogLevel::Warning>(
            STR("[KH3APProbeCpp] LiveAbilityOwnerProbe reason={} candidates={} preferred={} "
                "context={}\n"),
            reason, static_cast<uint64_t>(candidate_data.candidate_count),
            format_object_debug_name(preferred_object), deps.runtime_context_summary);
        append_live_ability_owner_probe_row(
            deps, STR("summary"), reason, format_object_debug_name(preferred_object),
            std::format(STR("candidates={}"),
                        static_cast<uint64_t>(candidate_data.candidate_count)));

        for (const LiveAbilityOwnerProbeObjectEntry& object_entry : candidate_data.objects)
        {
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] LiveAbilityOwnerProbeObject reason={} object={} class={}\n"),
                reason, object_entry.object_name, object_entry.class_name);
            append_live_ability_owner_probe_row(deps, STR("object"), reason,
                                                object_entry.object_name, object_entry.class_name);

            for (const LiveAbilityOwnerPropertyProbeEntry& property_entry :
                 prepare_live_ability_owner_property_probe_entries(object_entry.object,
                                                                   object_entry.property_limit))
            {
                Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] LiveAbilityOwnerProbeProperty reason={} object={} "
                        "property={} prop_class={} offset={} elem_size={} raw={} value={}\n"),
                    reason, property_entry.entry.object_name, property_entry.entry.property_name,
                    property_entry.entry.property_class, property_entry.entry.offset,
                    property_entry.entry.element_size, property_entry.raw_value,
                    property_entry.entry.value);
                append_live_ability_owner_probe_entry(deps, reason, property_entry.entry);

                if (property_entry.followed_object)
                {
                    RC::Unreal::UClass* followed_class =
                        property_entry.followed_object->GetClassPrivate();
                    append_live_ability_owner_probe_row(
                        deps, STR("follow_object"), reason,
                        property_entry.followed_object->GetFullName(),
                        followed_class ? followed_class->GetName() : std::wstring{STR("<null>")});
                    append_tres_equipment_component_offset_probe(
                        deps, reason, property_entry.followed_object, true);

                    if (followed_class)
                    {
                        for (const PropertyProbeEntry& entry :
                             prepare_live_equipmentish_follow_property_entries(
                                 property_entry.followed_object))
                        {
                            append_live_ability_owner_probe_entry(deps, reason, entry);
                        }
                    }
                }
            }
        }
    }

    auto log_live_ability_owner_probe_if_enabled(const RuntimeDiagnosticProbeOutputDeps& deps,
                                                 AbilityProbeDiagnosticState& diagnostic_state,
                                                 bool probe_enabled, std::wstring_view reason,
                                                 RC::Unreal::UObject* preferred_object,
                                                 RC::Unreal::UObject* last_world_context_object)
        -> void
    {
        if (!probe_enabled && !diagnostic_state.live_owner_force_enabled())
        {
            return;
        }

        const std::wstring probe_key =
            std::wstring{reason} + STR("|") + std::wstring{deps.runtime_context_summary};
        if (!diagnostic_state.mark_live_owner_probe_key_if_new(probe_key))
        {
            return;
        }

        append_live_ability_owner_probe_data(deps, reason, preferred_object,
                                             last_world_context_object);
    }

    auto log_live_ability_owner_probe_if_enabled(const RuntimeDiagnosticProbeOutputDeps& deps,
                                                 RuntimeState& runtime_state, bool probe_enabled,
                                                 std::wstring_view reason,
                                                 RC::Unreal::UObject* preferred_object) -> void
    {
        log_live_ability_owner_probe_if_enabled(
            deps, runtime_state.ability_diagnostic_state().probe_diagnostic_state(), probe_enabled,
            reason, preferred_object, runtime_state.object_cache().last_world_context_object());
    }

    auto log_targeted_chrinit_probe_data(bool probe_enabled, bool thunder_patch_enabled,
                                         std::wstring_view reason,
                                         std::wstring_view runtime_context) -> void
    {
        log_targeted_chrinit_probe(probe_enabled, thunder_patch_enabled, reason, runtime_context);
    }

    auto log_ability_discovery_snapshot_data(std::wstring_view reason,
                                             RC::Unreal::UObject* preferred_object,
                                             RC::Unreal::UObject* last_world_context_object,
                                             std::wstring_view runtime_context) -> void
    {
        const auto candidates =
            collect_ability_discovery_candidates(preferred_object, last_world_context_object);
        Output::send<LogLevel::Warning>(
            STR("[KH3APProbeCpp] AbilityDiscovery reason={} candidates={} context={}\n"), reason,
            static_cast<uint64_t>(candidates.size()), runtime_context);

        for (const AbilityDiscoveryObjectProbeEntry& object_entry :
             prepare_ability_discovery_snapshot_entries(candidates))
        {
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] AbilityDiscovery reason={} object={} class={}\n"), reason,
                object_entry.object_name, object_entry.class_name);

            for (const AbilityDiscoveryPropertyProbeEntry& property_entry : object_entry.properties)
            {
                Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] AbilityDiscovery reason={} object={} property={} "
                        "prop_class={} raw={} value={}\n"),
                    reason, property_entry.entry.object_name, property_entry.entry.property_name,
                    property_entry.entry.property_class, property_entry.raw_value,
                    property_entry.entry.value);
            }
        }
    }

    auto log_ability_discovery_snapshot_data(const RuntimeDiagnosticProbeOutputDeps& deps,
                                             std::wstring_view reason,
                                             RC::Unreal::UObject* preferred_object,
                                             RC::Unreal::UObject* last_world_context_object) -> void
    {
        log_ability_discovery_snapshot_data(reason, preferred_object, last_world_context_object,
                                            deps.runtime_context_summary);
    }

    auto log_ability_discovery_snapshot_data(const RuntimeDiagnosticProbeOutputDeps& deps,
                                             RuntimeState& runtime_state, std::wstring_view reason,
                                             RC::Unreal::UObject* preferred_object) -> void
    {
        log_ability_discovery_snapshot_data(
            deps, reason, preferred_object,
            runtime_state.object_cache().last_world_context_object());
    }

    auto log_stable_ability_trace_data(StableAbilityTraceDedupe& dedupe,
                                       const StableAbilityTraceData& trace_data) -> void
    {
        const auto dedupe_result = dedupe.check_and_remember(
            trace_data.dedupe_key, StableAbilityTraceDedupe::max_unique_entries);
        if (dedupe_result.decision == StableAbilityTraceDecision::Duplicate)
        {
            return;
        }
        if (dedupe_result.decision == StableAbilityTraceDecision::UniqueLimitReached)
        {
            if (dedupe_result.suppressed_count % diagnostic_suppression_log_interval == 1)
            {
                Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] AbilityTrace suppressed={} reason=unique-log-limit\n"),
                    dedupe_result.suppressed_count);
            }
            return;
        }

        Output::send<LogLevel::Warning>(
            STR("[KH3APProbeCpp] AbilityTrace phase={} hook={} context={} frame={} params={}\n"),
            trace_data.phase_name, trace_data.hook_name, trace_data.context_name,
            trace_data.frame_name, trace_data.params_summary);
    }

    auto log_stable_ability_trace_if_enabled(StableAbilityTraceDedupe& dedupe,
                                             const HookTargetInfo& hook_info, HookPhase phase,
                                             RC::Unreal::FFrame& frame,
                                             RC::Unreal::UFunction* target_function, void* params,
                                             std::wstring_view context_name) -> void
    {
        if (!target_function || !params ||
            !is_stable_ability_trace_hook_name(hook_info.function_name))
        {
            return;
        }

        const std::wstring phase_name =
            phase == HookPhase::Pre ? std::wstring{STR("pre")} : std::wstring{STR("post")};
        const auto trace_data = prepare_stable_ability_trace_data(
            phase_name, hook_info.function_name, context_name, frame, target_function, params,
            phase == HookPhase::Post);
        if (!trace_data.has_value())
        {
            return;
        }

        log_stable_ability_trace_data(dedupe, *trace_data);
    }

    auto log_stable_ability_trace_if_enabled(RuntimeState& runtime_state,
                                             const HookTargetInfo& hook_info, HookPhase phase,
                                             RC::Unreal::FFrame& frame,
                                             RC::Unreal::UFunction* target_function, void* params,
                                             std::wstring_view context_name) -> void
    {
        log_stable_ability_trace_if_enabled(
            runtime_state.ability_diagnostic_state().stable_trace_dedupe(), hook_info, phase, frame,
            target_function, params, context_name);
    }

    auto write_runtime_blocking_ui_state_file(const RuntimeDiagnosticProbeOutputDeps& deps,
                                              bool active, std::wstring_view reason,
                                              std::wstring_view ui_context,
                                              std::wstring_view event_name, int64_t callback_id)
        -> void
    {
        RuntimeSpaceService::notify_context_changed();
        if (!deps.bridge_dir.has_value())
        {
            return;
        }

        const std::string json = serialize_runtime_blocking_ui_state_event(
            active, reason, ui_context, event_name, callback_id);
        write_text_file(*deps.bridge_dir / channels::runtime_blocking_ui_state, json);
    }

    auto update_runtime_blocking_ui_state_from_probe_data(
        const RuntimeDiagnosticProbeOutputDeps& deps, RuntimeBlockingUiState& blocking_ui_state,
        const RuntimeBlockingUiProbeUpdateData& update_data) -> void
    {
        const bool should_write = blocking_ui_state.update(
            update_data.next_active, update_data.reason, update_data.context_name,
            update_data.event_name, update_data.callback_id);

        if (!should_write)
        {
            return;
        }
        const auto snapshot = blocking_ui_state.snapshot();

        write_runtime_blocking_ui_state_file(deps, snapshot.active, snapshot.reason,
                                             snapshot.context, snapshot.event,
                                             snapshot.callback_id);
        Output::send<LogLevel::Verbose>(STR("[KH3APProbeCpp] Runtime blocking UI {} reason={} "
                                            "context={} event={} callback_id={}\n"),
                                        snapshot.active ? STR("active") : STR("cleared"),
                                        snapshot.reason, snapshot.context, snapshot.event,
                                        snapshot.callback_id);
    }
} // namespace kh3ap
