#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace RC::Unreal { class UObject; }

namespace kh3ap
{
    inline constexpr auto runtime_rapunzel_hair_approach_class = L"ra_01_gimmick_HairACT_C";
    inline constexpr size_t rapunzel_hair_approach_script_size = 80910;
    inline constexpr size_t rapunzel_hair_approach_camera_gate = 114;
    inline constexpr uint32_t rapunzel_hair_approach_camera_resume = 875;
    inline constexpr size_t rapunzel_hair_approach_run_start = 61739;
    inline constexpr size_t rapunzel_hair_approach_run_end = 72674;
    using RapunzelApproachScriptName = std::array<uint32_t, 3>;
    enum class RapunzelHairApproachResult { rejected, applied, already_applied };

    inline auto patch_rapunzel_hair_approach(
            uint8_t* script, size_t size, uintptr_t lead_camera_property,
            const RapunzelApproachScriptName& restrict_run_name)
            -> RapunzelHairApproachResult
    {
        static_assert(sizeof(uintptr_t) == 8);
        if (!script || size != rapunzel_hair_approach_script_size ||
            !lead_camera_property || !restrict_run_name[0])
            return RapunzelHairApproachResult::rejected;

        const auto* gate = script + rapunzel_hair_approach_camera_gate;
        uint32_t destination{};
        uintptr_t property{};
        std::memcpy(&destination, gate + 1, sizeof(destination));
        std::memcpy(&property, gate + 6, sizeof(property));
        // The shared approach branch checks LeadCameraOK before spawning the
        // OTS camera. An unconditional jump to the same destination skips only
        // that camera setup, continuing Rapunzel's lead and hair interaction.
        if ((gate[0] != 0x07 && gate[0] != 0x06) ||
            destination != rapunzel_hair_approach_camera_resume ||
            gate[5] != 0x01 || property != lead_camera_property)
            return RapunzelHairApproachResult::rejected;

        // BP_SetRestrictRun is a virtual call: its linked script name contains
        // three uint32 fields, unlike the ordinary shipping FName layout.
        for (const auto offset : {rapunzel_hair_approach_run_start,
                                  rapunzel_hair_approach_run_end})
        {
            const auto* call = script + offset;
            if (call[0] != 0x1B ||
                std::memcmp(call + 1, restrict_run_name.data(), sizeof(restrict_run_name)) != 0 ||
                call[14] != 0x16 ||
                (call[13] != 0x28 &&
                 (offset != rapunzel_hair_approach_run_start || call[13] != 0x27)))
                return RapunzelHairApproachResult::rejected;
        }
        if (gate[0] == 0x06 && script[rapunzel_hair_approach_run_start + 13] == 0x28)
            return RapunzelHairApproachResult::already_applied;

        // Validate all sites before changing either byte. Keep the cinematic
        // crossings, progression, restrictions unrelated to running, and all
        // finish/cancel cleanup exactly as authored.
        script[rapunzel_hair_approach_camera_gate] = 0x06; // EX_Jump
        script[rapunzel_hair_approach_run_start + 13] = 0x28; // EX_False
        return RapunzelHairApproachResult::applied;
    }

    auto try_apply_rapunzel_hair_approach(RC::Unreal::UObject* object) -> bool;
}
