#include "kh3ap/features/grants/abilities/targeted_ability_query_helpers.h"

#include "kh3ap/features/grants/abilities/ability_query_helpers.h"
#include "kh3ap/app/enums.h"
#include "kh3ap/features/grants/grant_internal_call_depth.h"
#include "kh3ap/features/grants/grant_safety.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/app/state.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <UnrealDef.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>

#include <algorithm>
#include <cstddef>
#include <format>
#include <string>
#include <vector>

namespace kh3ap
{
    using namespace RC;
    using namespace RC::Unreal;

    auto log_targeted_runtime_ability_query_snapshot(
            std::wstring_view reason,
            std::wstring_view runtime_token,
            UObject* preferred_pawn_object,
            RuntimeState& runtime_state,
            bool enabled,
            std::wstring_view runtime_context_summary,
            const std::array<std::pair<std::wstring_view, std::wstring_view>, 3>& query_functions,
            AbilityProbeJsonlEmitter emit_probe_jsonl) -> void
    {
        if (!enabled)
        {
            return;
        }

        if (!runtime_token.starts_with(STR("ETresAbilityKind::")))
        {
            return;
        }

        const auto ability_value = try_resolve_runtime_enum_value(runtime_token);
        if (!ability_value.has_value())
        {
            Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] TargetedAbilityProbe reason={} token={} status=unresolved_enum context={}\n"),
                    reason,
                    runtime_token,
                    runtime_context_summary);
            return;
        }

        const auto pawn_candidates = collect_sora_ability_pawn_candidates(runtime_state, preferred_pawn_object);
        Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] TargetedAbilityProbe reason={} token={} value={} candidates={} context={}\n"),
                reason,
                runtime_token,
                *ability_value,
                static_cast<uint64_t>(pawn_candidates.size()),
                runtime_context_summary);
        if (emit_probe_jsonl)
        {
            emit_probe_jsonl(
                    STR("targeted_summary"),
                    reason,
                    format_object_debug_name(preferred_pawn_object),
                    std::format(
                            STR("token={} value={} candidates={}"),
                            std::wstring{runtime_token},
                            *ability_value,
                            static_cast<uint64_t>(pawn_candidates.size())),
                    {},
                    {},
                    -1,
                    -1,
                    {});
        }

        size_t logged_pawns = 0;
        for (UObject* pawn_object : pawn_candidates)
        {
            if (!pawn_object || !kh3ap::is_uobject_valid_fast(pawn_object))
            {
                continue;
            }
            if (logged_pawns++ >= 3)
            {
                break;
            }

            for (const auto& [label, path] : query_functions)
            {
                UFunction* const function_object = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, path);
                if (!function_object)
                {
                    Output::send<LogLevel::Warning>(
                            STR("[KH3APProbeCpp] TargetedAbilityProbe reason={} query={} token={} pawn={} status=missing_function\n"),
                            reason,
                            label,
                            runtime_token,
                            format_object_debug_name(pawn_object));
                    continue;
                }

                std::vector<std::byte> params(static_cast<size_t>(function_object->GetParmsSize()));
                std::fill(params.begin(), params.end(), std::byte{0});
                const bool wrote_ability = write_ability_kind_query_param(function_object, params.data(), *ability_value);
                if (!wrote_ability)
                {
                    Output::send<LogLevel::Warning>(
                            STR("[KH3APProbeCpp] TargetedAbilityProbe reason={} query={} token={} pawn={} status=missing_ability_param function={}\n"),
                            reason,
                            label,
                            runtime_token,
                            format_object_debug_name(pawn_object),
                            function_object->GetFullName());
                    continue;
                }

                {
                    const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
                    pawn_object->ProcessEvent(function_object, params.data());
                }

                const std::wstring return_value = format_function_return_value(function_object, params.data());
                Output::send<LogLevel::Warning>(
                        STR("[KH3APProbeCpp] TargetedAbilityProbe reason={} query={} token={} pawn={} return={}\n"),
                        reason,
                        label,
                        runtime_token,
                        format_object_debug_name(pawn_object),
                        return_value);
                if (emit_probe_jsonl)
                {
                    emit_probe_jsonl(
                            STR("targeted_query"),
                            reason,
                            pawn_object->GetFullName(),
                            pawn_object->GetClassPrivate() ? pawn_object->GetClassPrivate()->GetName() : std::wstring{STR("<null>")},
                            label,
                            STR("TargetedQuery"),
                            -1,
                            -1,
                            std::format(
                                    STR("token={} return={}"),
                                    std::wstring{runtime_token},
                                    return_value));
                }
            }
        }
    }
}
