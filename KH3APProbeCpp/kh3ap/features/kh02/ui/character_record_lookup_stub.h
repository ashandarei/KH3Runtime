#pragma once
#include <cstdint>
#include <vector>

namespace kh3ap
{
    inline auto build_character_record_lookup_stub(uint64_t callback, uint64_t original_field) -> std::vector<uint8_t>
    {
        // Retail callers keep their equipment category in r10 across this
        // internal lookup. Its native implementation also leaves r11 and all
        // XMM registers untouched. Run the native lookup first, then preserve
        // its complete volatile-register outputs while replacing only rax.
        std::vector<uint8_t> code{0x48, 0x81, 0xec, 0xc8, 0,    0, 0, // shadow space + inputs + outputs + alignment
                                  0x48, 0x89, 0x4c, 0x24, 0x20,       // save original save-bank argument
                                  0x48, 0x89, 0x54, 0x24, 0x28,       // save original character argument
                                  0x48, 0xb8};
        const auto address = [&](uint64_t value)
        {
            for (unsigned shift = 0; shift < 64; shift += 8)
            {
                code.push_back(static_cast<uint8_t>(value >> shift));
            }
        };
        address(original_field);
        code.insert(code.end(), {0xff, 0x10, // call [rax]: original trampoline
                                 0x48, 0x89, 0x4c, 0x24, 0x30, 0x48, 0x89, 0x54, 0x24, 0x38});
        for (uint8_t index = 0; index < 4; ++index)
        {
            // Save r8..r11, including any native lookup output in r8/r9.
            code.insert(code.end(), {0x4c, 0x89, static_cast<uint8_t>(0x44 + index * 8), 0x24,
                                     static_cast<uint8_t>(0x40 + index * 8)});
        }
        for (uint8_t index = 0; index < 6; ++index)
        {
            // movups [rsp+60h+index*10h],xmmN.
            code.insert(code.end(), {0x0f, 0x11, static_cast<uint8_t>(0x84 + index * 8), 0x24,
                                     static_cast<uint8_t>(0x60 + index * 16), 0, 0, 0});
        }
        code.insert(code.end(), {0x49, 0x89, 0xc0, // third callback argument = native record
                                 0x48, 0x8b, 0x4c, 0x24, 0x20, 0x48, 0x8b, 0x54, 0x24, 0x28, 0x48, 0xb8});
        address(callback);
        code.insert(code.end(), {0xff, 0xd0}); // call rax; retain returned record in rax
        for (uint8_t index = 0; index < 6; ++index)
        {
            code.insert(code.end(), {0x0f, 0x10, static_cast<uint8_t>(0x84 + index * 8), 0x24,
                                     static_cast<uint8_t>(0x60 + index * 16), 0, 0, 0});
        }
        for (uint8_t index = 0; index < 4; ++index)
        {
            code.insert(code.end(), {0x4c, 0x8b, static_cast<uint8_t>(0x44 + index * 8), 0x24,
                                     static_cast<uint8_t>(0x40 + index * 8)});
        }
        code.insert(code.end(), {0x48, 0x8b, 0x4c, 0x24, 0x30, 0x48, 0x8b, 0x54, 0x24, 0x38, 0x48, 0x81, 0xc4, 0xc8, 0,
                                 0, 0, 0xc3});
        return code;
    }
} // namespace kh3ap
