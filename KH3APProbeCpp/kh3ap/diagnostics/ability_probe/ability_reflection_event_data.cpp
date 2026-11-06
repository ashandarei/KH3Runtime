#include "kh3ap/bridge/transport/bridge_channels.h"
#include "kh3ap/diagnostics/ability_probe/ability_reflection_event_data.h"
#include "kh3ap/diagnostics/ability_probe/ability_reflection_probe_helpers.h"
#include "kh3ap/diagnostics/ability_probe/chrinit_reflection_helpers.h"
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
    auto prepare_stable_ability_trace_data(
            std::wstring_view phase_name,
            std::wstring_view hook_name,
            std::wstring_view context_name,
            RC::Unreal::FFrame& frame,
            RC::Unreal::UFunction* target_function,
            void* params,
            bool include_returns) -> std::optional<StableAbilityTraceData>
    {
        if (!target_function || !params)
        {
            return std::nullopt;
        }

        RC::Unreal::UFunction* const frame_node = get_frame_node(frame);
        const std::wstring frame_name = frame_node ? frame_node->GetFullName() : std::wstring{STR("<null>")};
        const std::wstring params_summary = summarize_stable_function_params(target_function, params, include_returns);
        return StableAbilityTraceData{
                .phase_name = std::wstring{phase_name},
                .hook_name = std::wstring{hook_name},
                .context_name = std::wstring{context_name},
                .frame_name = frame_name,
                .params_summary = params_summary,
                .dedupe_key = std::format(
                        STR("{}|{}|{}|{}|{}"),
                        phase_name,
                        hook_name,
                        context_name,
                        frame_name,
                        params_summary),
        };
    }

}
