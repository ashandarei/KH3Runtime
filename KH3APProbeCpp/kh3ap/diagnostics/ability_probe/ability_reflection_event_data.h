#pragma once

#include "kh3ap/diagnostics/ability_probe/ability_reflection_models.h"

#include <cstdint>
#include <cstddef>
#include <chrono>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace RC::Unreal
{
    struct FFrame;
    struct FName;
    class FProperty;
    class FStructProperty;
    class UClass;
    class UObject;
    class UFunction;
    class UStruct;
    class UScriptStruct;
}

namespace kh3ap
{
    auto prepare_stable_ability_trace_data(
            std::wstring_view phase_name,
            std::wstring_view hook_name,
            std::wstring_view context_name,
            RC::Unreal::FFrame& frame,
            RC::Unreal::UFunction* target_function,
            void* params,
            bool include_returns) -> std::optional<StableAbilityTraceData>;

}
