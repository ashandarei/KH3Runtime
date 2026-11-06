#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace RC::Unreal { class UObject; }

namespace kh3ap
{
    inline constexpr auto runtime_rapunzel_water_class = L"ra_01_mission_Kando_Hill_C";
    enum class RapunzelWaterPatchResult { rejected, applied, already_applied };

    // Reviewed EGS ra_01_mission_Kando_Hill ubergraph entry points:
    // Rainbow_Start_4's overlap and lv_ms021_start's OnTresStartevent.
    // Return before either branch can start the camera or move the party.
    // Patch the ubergraph itself: optimized Blueprint events can bypass wrappers.
    inline constexpr std::array<std::array<uint32_t, 2>, 2> rapunzel_water_entries{{
        {128568, 128476}, {126877, 117147},
    }};

    inline auto patch_rapunzel_water_bytecode(uint8_t* script, size_t size)
            -> RapunzelWaterPatchResult
    {
        if (!script) return RapunzelWaterPatchResult::rejected;
        bool all_patched = true;
        for (const auto& entry : rapunzel_water_entries)
        {
            const size_t offset = entry[0];
            if (offset > size || size - offset < 5)
                return RapunzelWaterPatchResult::rejected;
            // Keep the unused tail of the original jump as a signature, too.
            uint8_t expected[5]{0x06}; // EX_Jump + uint32 destination
            std::memcpy(expected + 1, &entry[1], sizeof(uint32_t));
            const bool patched = script[offset] == 0x04 && script[offset + 1] == 0x0B;
            if ((!patched && std::memcmp(script + offset, expected, 5) != 0) ||
                (patched && std::memcmp(script + offset + 2, expected + 2, 3) != 0))
                return RapunzelWaterPatchResult::rejected;
            all_patched &= patched;
        }
        if (all_patched) return RapunzelWaterPatchResult::already_applied;
        // Validate every site before writing any bytes; retain all offsets.
        for (const auto& entry : rapunzel_water_entries)
        {
            script[entry[0]] = 0x04;     // EX_Return
            script[entry[0] + 1] = 0x0B; // EX_Nothing (void)
        }
        return RapunzelWaterPatchResult::applied;
    }

    auto try_apply_rapunzel_water_bypass(RC::Unreal::UObject* object) -> bool;
}
