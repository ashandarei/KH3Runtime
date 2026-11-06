#include "kh3ap/features/story/remind/remind_portal_route.h"
#include "kh3ap/features/story/remind/yozora_return_route.h"
#include "kh3ap/core/objects/uobject_validity.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/FFrame.hpp>
#include <Unreal/NameTypes.hpp>
#include <Unreal/UFunction.hpp>
#include <UnrealDef.hpp>

namespace kh3ap
{
    static auto try_redirect_story_map_jump(
            RC::Unreal::FFrame& stack, RC::Unreal::UFunction* map_jump_function,
            std::wstring_view caller, std::wstring_view expected_caller,
            const wchar_t* original_map, const wchar_t* original_tag,
            const wchar_t* destination_map, const wchar_t* destination_tag,
            uint8_t fade_kind, std::wstring_view route_name) -> void
    {
        using namespace RC::Unreal;
        if (caller != expected_caller) return;
        auto* const frame = reinterpret_cast<FFrame_50_AndBelow*>(&stack);
        if (!is_uobject_valid_fast(frame->Node) ||
            !is_uobject_valid_fast(map_jump_function)) return;

        auto& script = frame->Node->GetScript();
        const auto begin = reinterpret_cast<uintptr_t>(script.GetData());
        const auto code = reinterpret_cast<uintptr_t>(frame->Code);
        if (!begin || script.Num() <= 0 || code < begin ||
            code - begin >= static_cast<size_t>(script.Num())) return;

        const auto script_name = [](const wchar_t* text) -> PortalScriptName {
            const FName name(text, FNAME_Add);
            return {name.GetComparisonIndex(), name.GetDisplayIndex(), name.GetNumber()};
        };
        // Native Blueprint calls still have their arguments in bytecode here;
        // frame.Locals belongs to the caller, not TresMapJump's parameter block.
        const auto result = redirect_map_jump_bytecode(
                caller, expected_caller, script.GetData(), static_cast<size_t>(script.Num()), code - begin,
                reinterpret_cast<uintptr_t>(map_jump_function),
                script_name(original_map), script_name(original_tag),
                script_name(destination_map), script_name(destination_tag), fade_kind);
        if (result == RemindPortalRouteResult::applied ||
            result == RemindPortalRouteResult::rejected)
            RC::Output::send<RC::LogLevel::Warning>(
                    STR("[KH3APProbeCpp] {} status={} map={} tag={} caller={}\n"), route_name,
                    result == RemindPortalRouteResult::applied ? STR("redirected") : STR("layout_rejected"),
                    destination_map, destination_tag, caller);
    }

    auto try_redirect_remind_scala_portal(
            RC::Unreal::FFrame& stack, RC::Unreal::UFunction* map_jump_function,
            std::wstring_view caller) -> void
    {
        try_redirect_story_map_jump(stack, map_jump_function, caller, remind_portal_caller,
                vanilla_scala_map, vanilla_scala_tag, remind_portal_map, remind_portal_tag,
                1, L"ReMindScalaPortal");
    }

    auto try_redirect_yozora_return(
            RC::Unreal::FFrame& stack, RC::Unreal::UFunction* map_jump_function,
            std::wstring_view caller) -> void
    {
        try_redirect_story_map_jump(stack, map_jump_function, caller, yozora_return_caller,
                yozora_clear_map, yozora_clear_tag, yozora_return_map, yozora_return_tag,
                0, L"YozoraVictoryReturn");
    }
}
