#pragma once

#include "kh3ap/features/story/remind/remind_portal_route.h"

namespace kh3ap
{
    inline constexpr std::wstring_view remind_return_caller =
        L"Function /Game/Levels/ss_DLC/ss_51/umap/ss_51_gameflow."
        L"ss_51_gameflow_C:ExecuteUbergraph_ss_51_gameflow";
    inline constexpr auto remind_clear_map = L"/Game/Maps/UI/EndSave";
    inline constexpr auto remind_clear_tag = L"#ReMindClear";
    inline constexpr auto remind_return_map = L"/Game/Maps/GardenOfAssemblage/umap/GoA_01";
    inline constexpr auto remind_return_tag = L"NormalSpawn";

    inline auto redirect_remind_return_bytecode(
        std::wstring_view caller, uint8_t* script, size_t size, size_t argument_offset,
        uintptr_t map_jump_function, const PortalScriptName& original_map,
        const PortalScriptName& original_tag, const PortalScriptName& garden_map,
        const PortalScriptName& garden_tag) -> RemindPortalRouteResult
    {
        // Both authored ending/skip branches use this exact clear-data exit.
        // Preserve the ending and its rewards; replace only the save-menu travel.
        return redirect_map_jump_bytecode(caller, remind_return_caller, script, size,
            argument_offset, map_jump_function, original_map, original_tag,
            garden_map, garden_tag, 0);
    }

    auto try_redirect_remind_return(RC::Unreal::FFrame& stack,
        RC::Unreal::UFunction* map_jump_function, std::wstring_view caller) -> void;
}
