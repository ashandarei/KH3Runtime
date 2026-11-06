#pragma once
#include "kh02_diagnostic_actions.h"
#include "kh02_runtime_inspection.h"
#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#include "kh3ap/features/kh02/cutscenes/kh02_cutscene_reflection.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/core/objects/actor_discovery.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/bridge/utilities.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/AActor.hpp>
#include <Unreal/World.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#include <Unreal/Property/FStructProperty.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <array>
#include <cmath>
#include <cstring>
#include <numbers>
#include <chrono>
#include <stdexcept>

namespace kh3ap::kh02diagnostic_detail
{
    using namespace RC::Unreal;
    using Json = nlohmann::json;
    using Action = Kh02DiagnosticAction;
    using kh02cutscene_detail::flag;
    using kh02cutscene_detail::real;
    using kh02cutscene_detail::set_flag;
    constexpr std::size_t native_vector_bytes = 3 * sizeof(float);
    constexpr std::size_t reflected_parameter_limit_bytes = 2'048;
    constexpr int actor_scan_limit = 16'384;
    constexpr int sora_player_kind = 0;
    constexpr int aqua_player_kind = 10;

    struct ReflectedCall : Kh02ReflectedCall
    {
        ReflectedCall(UObject* owner, const wchar_t* method)
            : Kh02ReflectedCall(owner, method, "kh02_story", reflected_parameter_limit_bytes)
        {
        }
    };

    inline auto path(UObject* object) -> std::string
    {
        return is_uobject_valid_fast(object) ? to_utf8(object->GetPathName()) : "";
    }
} // namespace kh3ap::kh02diagnostic_detail
