#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string_view>

namespace kh3ap
{
    inline constexpr std::wstring_view monstropolis_mapset_caller =
            L"Function /Game/Levels/mi/mi_02/mi_02.mi_02_C:ExecuteUbergraph_mi_02";
    inline constexpr std::wstring_view monstropolis_before_mapset =
            L"/Game/Levels/mi/mi_02/mapset/ms_mi_02_before.ms_mi_02_before";
    using MonstropolisScriptName = std::array<uint32_t, 3>;

    // REAL5 resumes the laser mission away from these two savepoints. REAL4
    // still arrives at the selected savepoint and must retain its area preset.
    // Preserve all other arrivals, including the normal post-mission mapset.
    inline auto monstropolis_resume_preset(
            std::wstring_view caller, std::wstring_view mapset,
            int32_t story, std::wstring_view tag) -> std::wstring_view
    {
        if (caller != monstropolis_mapset_caller || mapset != monstropolis_before_mapset ||
            story != 3160) return {};
        if (tag == L"mi_02_Lv_Save_01") return L"MI_02_AREA_A";
        if (tag == L"mi_02_Lv_Save_02") return L"MI_02_AREA_C";
        return {};
    }

    // Locate only the preset operand in the verified startup call. Script names
    // have three fields even in shipping builds with an eight-byte FName.
    // Check the complete argument shape and resolved pointers before any write.
    inline auto monstropolis_preset_operand(
            std::span<uint8_t> script, size_t offset, uintptr_t function,
            uintptr_t latent_struct, uintptr_t mapset_property,
            const MonstropolisScriptName& callback,
            const MonstropolisScriptName& preset) -> std::optional<size_t>
    {
        constexpr size_t call_size = 9;
        constexpr size_t argument_size = 63;
        if (!function || !latent_struct || !mapset_property || offset < call_size ||
            offset > script.size() || script.size() - offset < argument_size) return {};
        const auto* args = script.data() + offset;
        const auto pointer = [](const uint8_t* at) {
            uintptr_t value{};
            std::memcpy(&value, at, sizeof(value));
            return value;
        };
        uint32_t struct_size{};
        std::memcpy(&struct_size, args + 10, sizeof(struct_size));
        if (args[-9] != 0x1c || pointer(args - 8) != function ||
            args[0] != 0x17 || args[1] != 0x2f || pointer(args + 2) != latent_struct ||
            struct_size != 32 ||
            (args[14] != 0x5b && args[14] != 0x1d) || args[19] != 0x1d ||
            args[24] != 0x21 || std::memcmp(args + 25, callback.data(), sizeof(callback)) ||
            args[37] != 0x17 || args[38] != 0x30 || args[39] != 0x01 ||
            pointer(args + 40) != mapset_property || args[48] != 0x21 ||
            std::memcmp(args + 49, preset.data(), sizeof(preset)) ||
            args[61] != 0x27 || args[62] != 0x16) return {};
        return offset + 49;
    }
}
