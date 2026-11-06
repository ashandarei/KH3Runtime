#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace kh3ap
{
    inline constexpr uint8_t goa_music_object_const_opcode = 0x20;
    inline constexpr size_t goa_music_object_operand_offset = 1;

    // Loaded EX_ObjectConst operands are pointer-sized. Match only the
    // Garden's known sound; unknown constants/expressions stay with the engine.
    template <typename IsGardenSound>
    auto silence_goa_music_constants(std::span<uint8_t> script, IsGardenSound is_garden_sound) -> size_t
    {
        size_t changed = 0;
        for (size_t offset = 0;
             offset + goa_music_object_operand_offset + sizeof(uintptr_t) <= script.size();
             ++offset)
        {
            if (script[offset] != goa_music_object_const_opcode)
            {
                continue;
            }
            uintptr_t sound{};
            std::memcpy(&sound, script.data() + offset + goa_music_object_operand_offset,
                        sizeof(sound));
            if (!sound || !is_garden_sound(sound)) continue;
            constexpr uintptr_t silence = 0;
            std::memcpy(script.data() + offset + goa_music_object_operand_offset, &silence,
                        sizeof(silence));
            ++changed;
            offset += sizeof(uintptr_t);
        }
        return changed;
    }
}
