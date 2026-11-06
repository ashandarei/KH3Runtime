#include "kh3ap/bridge/transport/bridge_channels.h"
#include "kh3ap/diagnostics/reflection/reflection_diagnostics.h"
#include "kh3ap/core/reflection/reflection_core.h"

#include "kh3ap/diagnostics/output/diagnostic_once_set.h"
#include "kh3ap/diagnostics/output/diagnostic_probe_serializer.h"
#include "kh3ap/features/grants/messages/ap_message_param_storage.h"
#include "kh3ap/hooks/blocking_ui/blocking_ui_state.h"
#include "kh3ap/hooks/hook_filters.h"
#include "kh3ap/app/enums.h"
#include "kh3ap/features/player/objects/object_resolver.h"
#include "kh3ap/features/player/state/sora_pawn_catalog.h"
#include "kh3ap/app/state.h"
#include "kh3ap/bridge/utilities.h"

#include <UnrealDef.hpp>
#include <Unreal/FFrame.hpp>
#include <Unreal/FString.hpp>
#include <Unreal/FText.hpp>
#include <Unreal/NameTypes.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <Unreal/Property/FMapProperty.hpp>
#include <Unreal/Property/FStructProperty.hpp>
#include <Unreal/TMap.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UScriptStruct.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <cwctype>
#include <format>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#define NOMINMAX
#include <windows.h>

namespace kh3ap
{
    namespace
    {
        constexpr auto max_reflection_actor_samples = 8;
    }

    auto dump_function_signature_once(std::wstring_view function_path, std::wstring_view label,
                                      bool enabled, DiagnosticOnceSet& seen_paths) -> void
    {
        if (!enabled)
        {
            return;
        }

        const std::wstring path_key{function_path};
        RC::Unreal::UFunction* const function_object =
            RC::Unreal::UObjectGlobals::StaticFindObject<RC::Unreal::UFunction*>(nullptr, nullptr,
                                                                                 function_path);
        if (!function_object)
        {
            return;
        }

        if (!seen_paths.check_and_remember(path_key))
        {
            return;
        }

        RC::Output::send<RC::LogLevel::Warning>(
            STR("[KH3APProbeCpp] Runtime grant signature label={} path={} parms_size={} "
                "full_name={}\n"),
            label, function_path, function_object->GetParmsSize(), function_object->GetFullName());

        for (RC::Unreal::FProperty* param = function_object->GetFirstProperty(); param;
             param = param->HasNext() ? param->GetNextFieldAsProperty() : nullptr)
        {
            if (!param->HasAnyPropertyFlags(RC::Unreal::CPF_Parm))
            {
                continue;
            }

            RC::Output::send<RC::LogLevel::Warning>(
                STR("[KH3APProbeCpp] Runtime grant signature label={} param={} class={} offset={} "
                    "elem_size={} flags={}\n"),
                label, param->GetName(), param->GetClass().GetName(), param->GetOffset_Internal(),
                param->GetElementSize(), summarize_param_flags(param));
        }
    }

    auto dump_function_signature_for_debug(RC::Unreal::UFunction* function_object,
                                           std::wstring_view label) -> void
    {
        if (!function_object)
        {
            RC::Output::send<RC::LogLevel::Warning>(
                STR("[KH3APProbeCpp] Debug signature label={} function=<null>\n"), label);
            return;
        }

        RC::Output::send<RC::LogLevel::Warning>(
            STR("[KH3APProbeCpp] Debug signature label={} parms_size={} full_name={}\n"), label,
            function_object->GetParmsSize(), function_object->GetFullName());

        for (RC::Unreal::FProperty* param = function_object->GetFirstProperty(); param;
             param = param->HasNext() ? param->GetNextFieldAsProperty() : nullptr)
        {
            if (!param || !param->HasAnyPropertyFlags(RC::Unreal::CPF_Parm))
            {
                continue;
            }

            RC::Output::send<RC::LogLevel::Warning>(
                STR("[KH3APProbeCpp] Debug signature label={} param={} class={} offset={} "
                    "elem_size={} flags={} detail={}\n"),
                label, param->GetName(), param->GetClass().GetName(), param->GetOffset_Internal(),
                param->GetElementSize(), summarize_param_flags(param),
                describe_property_detail(param));
        }
    }

    auto serialize_property_probe_entry(std::wstring_view build_stamp,
                                        std::wstring_view runtime_context,
                                        const PropertyProbeEntry& entry) -> std::string
    {
        return DiagnosticProbeSerializer::serialize_property_probe_event(
            build_stamp, entry.kind, entry.reason, runtime_context, entry.object_name,
            entry.class_name, entry.property_name, entry.property_class, entry.offset,
            entry.element_size, entry.value);
    }

    auto serialize_runtime_blocking_ui_state_event(bool active, std::wstring_view reason,
                                                   std::wstring_view ui_context,
                                                   std::wstring_view event_name,
                                                   int64_t callback_id) -> std::string
    {
        return DiagnosticProbeSerializer::serialize_runtime_blocking_ui_state(
            active, reason, ui_context, event_name, callback_id);
    }

    auto serialize_ability_array_needle_probe_entry(std::wstring_view build_stamp,
                                                    std::wstring_view runtime_context,
                                                    std::wstring_view reason,
                                                    const AbilityArrayNeedleProbeEntry& entry)
        -> std::string
    {
        return DiagnosticProbeSerializer::serialize_ability_array_needle_probe_event(
            build_stamp, entry.kind, reason, runtime_context, entry.object_name, entry.class_name,
            entry.property_name, entry.property_class, entry.inner_class, entry.offset,
            entry.element_size, entry.inner_size, entry.array_num, entry.array_max, entry.matches,
            entry.value);
    }

    auto should_log_rewardish_property(std::wstring_view property_name,
                                       std::wstring_view property_class) -> bool
    {
        if (property_class != STR("NameProperty") && property_class != STR("StrProperty") &&
            property_class != STR("ObjectProperty") && property_class != STR("ArrayProperty") &&
            property_class != STR("StructProperty") && property_class != STR("MapProperty") &&
            property_class != STR("BoolProperty"))
        {
            return false;
        }

        return property_name.find(STR("Bonus")) != std::wstring_view::npos ||
               property_name.find(STR("Ability")) != std::wstring_view::npos ||
               property_name.find(STR("Reward")) != std::wstring_view::npos ||
               property_name.find(STR("Flag")) != std::wstring_view::npos ||
               property_name.find(STR("Level")) != std::wstring_view::npos ||
               property_name.find(STR("Name")) != std::wstring_view::npos ||
               property_name.find(STR("Item")) != std::wstring_view::npos ||
               property_name.find(STR("Map")) != std::wstring_view::npos ||
               property_name.find(STR("Track")) != std::wstring_view::npos ||
               property_name.find(STR("Obtain")) != std::wstring_view::npos ||
               property_name.find(STR("Event")) != std::wstring_view::npos ||
               property_name.find(STR("Pair")) != std::wstring_view::npos ||
               property_name.find(STR("Pass")) != std::wstring_view::npos ||
               property_name.find(STR("Text")) != std::wstring_view::npos ||
               property_name.find(STR("Icon")) != std::wstring_view::npos;
    }

    auto should_track_event_array_diffs(std::wstring_view property_name) -> bool
    {
        return property_name == STR("localObtainedEvents") ||
               property_name == STR("localTrackedEvents") ||
               property_name == STR("obtainedEventData") ||
               property_name == STR("trackedEventData") ||
               property_name == STR("notObtainedVBonusData");
    }

    auto append_world_context_candidate(std::vector<RC::Unreal::UObject*>& candidates,
                                        RC::Unreal::UObject* object) -> void
    {
        if (!object || !kh3ap::is_uobject_valid_fast(object))
        {
            return;
        }

        if (std::find(candidates.begin(), candidates.end(), object) == candidates.end())
        {
            candidates.push_back(object);
        }
    }

    auto collect_vbonus_level_actor_snapshot() -> std::vector<VBonusLevelActorSnapshotEntry>
    {
        std::vector<RC::Unreal::UObject*> actors{};
        RC::Unreal::UObjectGlobals::FindAllOf(STR("VBonusLevelActor_C"), actors);
        std::vector<VBonusLevelActorSnapshotEntry> snapshot{};
        snapshot.reserve(std::min<size_t>(actors.size(), max_reflection_actor_samples));
        for (RC::Unreal::UObject* actor : actors)
        {
            if (!actor)
            {
                continue;
            }

            snapshot.emplace_back(VBonusLevelActorSnapshotEntry{
                .full_name = actor->GetFullName(),
                .level_name = try_read_object_string_property(actor, STR("LevelName"))
                                  .value_or(STR("<none>")),
                .vbonus_name =
                    try_read_object_name_property(actor, STR("vbonusName")).value_or(STR("<none>")),
            });

            if (snapshot.size() >= max_reflection_actor_samples)
            {
                break;
            }
        }

        return snapshot;
    }

    auto dump_runtime_grant_target_signatures_once(
        bool startup_dump_enabled, bool runtime_grant_signature_dump_enabled,
        DiagnosticOnceSet& runtime_grant_signature_paths,
        std::initializer_list<std::pair<std::wstring_view, std::wstring_view>> targets) -> void
    {
        if (!startup_dump_enabled)
        {
            return;
        }

        for (const auto& [function_path, label] : targets)
        {
            dump_function_signature_once(function_path, label, runtime_grant_signature_dump_enabled,
                                         runtime_grant_signature_paths);
        }
    }

    auto dump_ap_message_candidate_signatures_once(
        bool startup_dump_enabled, DiagnosticOnceSet& ap_message_signature_candidates,
        std::initializer_list<std::pair<std::wstring_view, std::wstring_view>> candidates) -> void
    {
        if (!startup_dump_enabled)
        {
            return;
        }

        if (!ap_message_signature_candidates.check_and_remember(
                std::wstring{STR("ap_message_candidate_signatures")}))
        {
            return;
        }

        RC::Output::send<RC::LogLevel::Warning>(
            STR("[KH3APProbeCpp] AP message candidate signature dump begin\n"));
        for (const auto& [function_path, label] : candidates)
        {
            dump_function_signature_for_debug(
                RC::Unreal::UObjectGlobals::StaticFindObject<RC::Unreal::UFunction*>(
                    nullptr, nullptr, std::wstring{function_path}.c_str()),
                label);
        }
        RC::Output::send<RC::LogLevel::Warning>(
            STR("[KH3APProbeCpp] AP message candidate signature dump end\n"));
    }

    auto initialize_runtime_startup_diagnostics(
        RuntimeState& runtime_state, bool startup_dump_enabled,
        bool runtime_grant_signature_dump_enabled, std::wstring_view tres_get_vbonus_path,
        std::wstring_view bp_get_vbonus_path, std::wstring_view bp_ability_add_path,
        std::wstring_view bp_ability_get_level_path, std::wstring_view bp_is_ability_enable_path,
        std::wstring_view kismet_print_string_path, std::wstring_view tresui_open_information_path,
        std::wstring_view tresui_open_center_information_path,
        std::wstring_view tresui_open_message_path,
        std::wstring_view tresui_map_in_notice_message_path, std::wstring_view bp_get_item_path,
        std::wstring_view wait_bp_get_multiitem_path,
        std::wstring_view remy_final_milestone_reward_path,
        std::wstring_view tresui_show_get_treasure_prize_snow_curling_path,
        std::wstring_view tresui_show_get_bonus_point_snow_curling_path,
        std::wstring_view uip_map_name_on_callback_path,
        std::wstring_view uip_multi_item_get_on_callback_path) -> void
    {
        dump_runtime_grant_target_signatures_once(
            startup_dump_enabled, runtime_grant_signature_dump_enabled,
            runtime_state.diagnostic_state().runtime_grant_signature_paths(),
            {
                {tres_get_vbonus_path, STR("TresGetVictoryBonus")},
                {bp_get_vbonus_path, STR("BP_TresGetVictoryBonus")},
                {bp_ability_add_path, STR("BP_AbilityAdd")},
                {bp_ability_get_level_path, STR("BP_AbilityGetLevel")},
                {bp_is_ability_enable_path, STR("BP_IsAbilityEnable")},
            });
        dump_ap_message_candidate_signatures_once(
            startup_dump_enabled,
            runtime_state.diagnostic_state().ap_message_signature_candidates(),
            {
                {kismet_print_string_path, STR("APMessageCandidate:Kismet.PrintString")},
                {tresui_open_information_path, STR("APMessageCandidate:TresUI_OpenInformation")},
                {tresui_open_center_information_path,
                 STR("APMessageCandidate:TresUI_OpenCenterInformation")},
                {tresui_open_message_path, STR("APMessageCandidate:TresUI_OpenMessage")},
                {tresui_map_in_notice_message_path,
                 STR("APMessageCandidate:TresUI_MapInNoticeMessage")},
                {bp_get_item_path, STR("APMessageCandidate:BP_TresGetItem")},
                {wait_bp_get_multiitem_path, STR("APMessageCandidate:WaitBPTresGetMultiItem")},
                {remy_final_milestone_reward_path,
                 STR("APMessageCandidate:RemyGetFinalMilestoneRewardAndShowMsg")},
                {tresui_show_get_treasure_prize_snow_curling_path,
                 STR("APMessageCandidate:TresUI_ShowGetTreasurePrizeSnowCurling")},
                {tresui_show_get_bonus_point_snow_curling_path,
                 STR("APMessageCandidate:TresUI_ShowGetBonusPointSnowCurling")},
                {uip_map_name_on_callback_path,
                 STR("APMessageCandidate:TresUIP_MapName.OnCallback")},
                {uip_multi_item_get_on_callback_path,
                 STR("APMessageCandidate:TresUIP_MultiItemGet.OnCallback")},
            });
    }
} // namespace kh3ap
