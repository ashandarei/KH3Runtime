#pragma once

#include "kh3ap/features/story/remind/remind_portal_route.h"

namespace kh3ap
{
    inline constexpr std::wstring_view yozora_return_caller =
            L"Function /Game/Levels/ss_DLC/ss_01/umap/ss_01_gameflow."
            L"ss_01_gameflow_C:ExecuteUbergraph_ss_01_gameflow";
    inline constexpr auto yozora_clear_map = L"/Game/Maps/UI/EndSave";
    inline constexpr auto yozora_clear_tag = L"#FalsusRexClear";
    inline constexpr auto yozora_return_map = L"/Game/Maps/GardenOfAssemblage/umap/GoA_01";
    inline constexpr auto yozora_return_tag = L"NormalSpawn";

    inline auto redirect_yozora_return_bytecode(
            std::wstring_view caller,
            uint8_t* script, size_t size, size_t argument_offset,
            uintptr_t map_jump_function,
            const PortalScriptName& original_map, const PortalScriptName& original_tag,
            const PortalScriptName& garden_map, const PortalScriptName& garden_tag)
            -> RemindPortalRouteResult
    {
        // The victory cutscene and its skip converge on this EndSave call.
        // Match the clear tag too: the loss ending uses the same map.
        return redirect_map_jump_bytecode(
                caller, yozora_return_caller, script, size, argument_offset,
                map_jump_function, original_map, original_tag, garden_map, garden_tag, 0);
    }

    auto try_redirect_yozora_return(
            RC::Unreal::FFrame& stack, RC::Unreal::UFunction* map_jump_function,
            std::wstring_view caller) -> void;
}
