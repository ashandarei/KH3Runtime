#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace RC::Unreal { struct FFrame; class UFunction; }

namespace kh3ap
{
    inline constexpr std::wstring_view remind_portal_caller =
            L"Function /Game/Levels/kg_DLC/kg_05/umap/kg_05_gimmick."
            L"kg_05_gimmick_C:ExecuteUbergraph_kg_05_gimmick";
    inline constexpr auto remind_portal_map = L"/Game/Levels/bt_DLC/bt_08/bt_08";
    inline constexpr auto remind_portal_tag = L"bt_08_Lv_Start_01";
    inline constexpr auto vanilla_scala_map = L"/Game/Levels/bt/bt_01/bt_01";
    inline constexpr auto vanilla_scala_tag = L"bt_01_Lv_Start_01";

    // UE4 script names contain comparison index, display index and number,
    // including in shipping builds whose ordinary FName only has two fields.
    using PortalScriptName = std::array<uint32_t, 3>;
    enum class RemindPortalRouteResult { ignored, rejected, applied, already_applied };

    inline auto redirect_map_jump_bytecode(
            std::wstring_view caller, std::wstring_view expected_caller,
            uint8_t* script, size_t size, size_t argument_offset,
            uintptr_t map_jump_function,
            const PortalScriptName& original_map, const PortalScriptName& original_tag,
            const PortalScriptName& dlc_map, const PortalScriptName& dlc_tag,
            uint8_t fade_kind)
            -> RemindPortalRouteResult
    {
        if (caller != expected_caller) return RemindPortalRouteResult::ignored;
        constexpr size_t call_size = 1 + sizeof(uintptr_t);
        constexpr size_t name_size = 1 + sizeof(PortalScriptName);
        constexpr size_t argument_size = 2 * name_size + 3;
        if (!script || !map_jump_function || argument_offset < call_size ||
            argument_offset > size || size - argument_offset < argument_size)
            return RemindPortalRouteResult::rejected;

        auto* const args = script + argument_offset;
        uintptr_t target{};
        std::memcpy(&target, args - sizeof(target), sizeof(target));
        // Exact TresMapJump(NameConst, NameConst, ByteConst) call shape.
        if (args[-static_cast<ptrdiff_t>(call_size)] != 0x1c ||
            target != map_jump_function || args[0] != 0x21 || args[name_size] != 0x21 ||
            args[2 * name_size] != 0x24 || args[2 * name_size + 1] != fade_kind ||
            args[2 * name_size + 2] != 0x16)
            return RemindPortalRouteResult::rejected;

        const auto matches = [&](const PortalScriptName& map, const PortalScriptName& tag) {
            return std::memcmp(args + 1, map.data(), sizeof(map)) == 0 &&
                   std::memcmp(args + name_size + 1, tag.data(), sizeof(tag)) == 0;
        };
        if (matches(dlc_map, dlc_tag)) return RemindPortalRouteResult::already_applied;
        if (!matches(original_map, original_tag)) return RemindPortalRouteResult::rejected;

        // Validate the entire pair before writing; preserve instruction widths,
        // branch offsets, confirmation choice and the existing fade behavior.
        std::memcpy(args + 1, dlc_map.data(), sizeof(dlc_map));
        std::memcpy(args + name_size + 1, dlc_tag.data(), sizeof(dlc_tag));
        return RemindPortalRouteResult::applied;
    }

    inline auto redirect_remind_portal_bytecode(
            std::wstring_view caller,
            uint8_t* script, size_t size, size_t argument_offset,
            uintptr_t map_jump_function,
            const PortalScriptName& original_map, const PortalScriptName& original_tag,
            const PortalScriptName& dlc_map, const PortalScriptName& dlc_tag)
            -> RemindPortalRouteResult
    {
        return redirect_map_jump_bytecode(
                caller, remind_portal_caller, script, size, argument_offset,
                map_jump_function, original_map, original_tag, dlc_map, dlc_tag, 1);
    }

    auto try_redirect_remind_scala_portal(
            RC::Unreal::FFrame& stack, RC::Unreal::UFunction* map_jump_function,
            std::wstring_view caller) -> void;
}
