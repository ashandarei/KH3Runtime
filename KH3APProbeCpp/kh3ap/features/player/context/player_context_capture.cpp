#include "kh3ap/app/config.h"
#include "kh3ap/features/player/context/player_context_internal.h"
#include "kh3ap/bridge/transport/bridge_channels.h"
#include "kh3ap/app/state.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/bridge/utilities.h"
#include "kh3ap/features/tutorials/final_world_tutorial_pawn_capture.h"
#include "kh3ap/features/grants/grant_internal_call_depth.h"
#include "kh3ap/features/grants/delivery/item_call_helpers.h"
#include "kh3ap/features/player/objects/object_resolver.h"
#include "kh3ap/features/grants/battle_level/battle_level_sync_service.h"
#include "kh3ap/features/tutorials/tutorial_skip_service.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <UnrealDef.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <algorithm>
#include <cstddef>
#include <vector>

namespace kh3ap
{
    using namespace RC;
    using namespace RC::Unreal;
    using namespace player_context_detail;
    namespace
    {
        using ::kh3ap::settings::s_final_world_story_ew_flag;
        using ::kh3ap::settings::s_get_game_flag_path;
        using ::kh3ap::settings::s_get_local_tres_player_pawn_base_path;
        using ::kh3ap::settings::s_tres_game_blueprint_library_default_object_path;
    }

    auto RuntimePlayerContextService::Impl::try_cache_final_world_tutorial_pawn_candidate(
        UObject* pawn, std::wstring_view source, std::wstring_view caller) -> bool
    {
        if (!final_world_capture_armed ||
            !config.tutorial_skips.final_world_ui_bypasses_applied() ||
            final_world_capture_closed || !pawn || !kh3ap::is_uobject_valid_fast(pawn))
        {
            return false;
        }

        UClass* const pawn_class = pawn->GetClassPrivate();
        const std::wstring class_name = pawn_class ? pawn_class->GetName() : std::wstring{};
        const std::wstring pawn_name = format_object_debug_name(pawn);
        if (!is_validated_sora_pawn_name(pawn_name, class_name) ||
            !is_runtime_final_world_tutorial_pawn_identity(pawn_name, class_name))
        {
            Output::send<LogLevel::Verbose>(
                STR("[KH3APProbeCpp] FinalWorldTutorialPawnCapture rejected source={} "
                    "reason=not_p_ew001 caller={} pawn={} class={}\n"),
                source, caller, pawn_name, class_name);
            return false;
        }

        int32_t gameflow_ew = 0;
        std::wstring flag_failure{};
        const bool gameflow_available = try_call_get_game_flag(
            s_final_world_story_ew_flag, gameflow_ew, config.runtime_state, s_get_game_flag_path,
            s_tres_game_blueprint_library_default_object_path, false, &flag_failure);
        if (!gameflow_available)
        {
            if (!final_world_capture_flag_failure_logged)
            {
                final_world_capture_flag_failure_logged = true;
                Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] FinalWorldTutorialPawnCapture deferred event=ew_01 "
                        "source={} reason=gameflow_EW_unavailable details={}\n"),
                    source, flag_failure.empty() ? STR("<none>") : flag_failure);
            }
            return false;
        }

        if (!should_capture_runtime_final_world_tutorial_pawn_scope(
                final_world_capture_armed, config.tutorial_skips.final_world_ui_bypasses_applied(),
                true, gameflow_ew))
        {
            if (gameflow_ew >= runtime_final_world_tutorial_pawn_capture_gameflow_exclusive_max)
            {
                final_world_capture_armed = false;
                final_world_capture_closed = true;
                Output::send<LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] FinalWorldTutorialPawnCapture closed event=ew_01 "
                        "source={} gameflow_EW={} required_lt={} caller={}\n"),
                    source, gameflow_ew,
                    runtime_final_world_tutorial_pawn_capture_gameflow_exclusive_max, caller);
            }
            return false;
        }

        if (!cache_validated_sora_pawn_candidate(pawn, pawn_name, source, true))
        {
            return false;
        }
        final_world_capture_armed = false;
        final_world_capture_closed = true;
        config.battle_level_sync.sync_after_controller_reacquire(
            STR("special_battle_pawn_captured_final_world_tutorial"));
        Output::send<LogLevel::Warning>(
            STR("[KH3APProbeCpp] FinalWorldTutorialPawnCapture cached event=ew_01 source={} "
                "gameflow_EW={} required_lt={} caller={} pawn={}\n"),
            source, gameflow_ew, runtime_final_world_tutorial_pawn_capture_gameflow_exclusive_max,
            caller, pawn_name);
        return true;
    }

    auto RuntimePlayerContextService::Impl::call_get_local_tres_player_pawn_base_once(
        UObject* world_context_object, std::wstring_view source) -> UObject*
    {
        if (!world_context_object || !kh3ap::is_uobject_valid_fast(world_context_object))
        {
            return nullptr;
        }
        UFunction* const function_object = UObjectGlobals::StaticFindObject<UFunction*>(
            nullptr, nullptr, s_get_local_tres_player_pawn_base_path);
        UObject* self_object =
            RuntimeObjectResolver::try_get_tres_game_blueprint_library_default_object(
                config.runtime_state.object_cache(),
                s_tres_game_blueprint_library_default_object_path);
        if (!self_object)
        {
            self_object = try_get_function_default_object(function_object);
        }
        if (!function_object || !self_object)
        {
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] FinalWorldTutorialPawnCapture one_shot_unavailable source={} "
                    "function={} self={}\n"),
                source,
                function_object ? function_object->GetFullName() : std::wstring{STR("<null>")},
                format_object_debug_name(self_object));
            return nullptr;
        }

        std::vector<std::byte> params(static_cast<size_t>(function_object->GetParmsSize()));
        std::fill(params.begin(), params.end(), std::byte{0});
        FProperty* const world_property =
            find_function_param_by_name(function_object, STR("WorldContextObject"));
        FProperty* const return_property =
            find_function_param_by_name(function_object, STR("ReturnValue"));
        UObject** const world_value =
            world_property ? world_property->ContainerPtrToValuePtr<UObject*>(params.data())
                           : nullptr;
        UObject** const return_value =
            return_property ? return_property->ContainerPtrToValuePtr<UObject*>(params.data())
                            : nullptr;
        if (!world_value || !return_value)
        {
            return nullptr;
        }
        *world_value = world_context_object;
        {
            const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
            self_object->ProcessEvent(function_object, params.data());
        }
        UObject* const pawn = *return_value;
        Output::send<LogLevel::Verbose>(STR("[KH3APProbeCpp] FinalWorldTutorialPawnCapture "
                                            "one_shot source={} context={} pawn={}\n"),
                                        source, format_object_debug_name(world_context_object),
                                        format_object_debug_name(pawn));
        return pawn && kh3ap::is_uobject_valid_fast(pawn) ? pawn : nullptr;
    }

    auto RuntimePlayerContextService::Impl::try_capture_final_world_tutorial_pawn_once(
        UObject* world_context_object, std::wstring_view source, std::wstring_view caller) -> bool
    {
        if (!final_world_capture_armed ||
            !config.tutorial_skips.final_world_ui_bypasses_applied() || final_world_capture_closed)
        {
            return false;
        }
        return try_cache_final_world_tutorial_pawn_candidate(
            call_get_local_tres_player_pawn_base_once(world_context_object, source), source,
            caller);
    }
} // namespace kh3ap
