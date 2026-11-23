#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace RC::Unreal { class UObject; }

namespace kh3ap
{
    // World endings and Data arenas grant items through WaitBPTresGetKeyitem independently of
    // TresEventData. AP owns the rewards and observes the original story flags.
    struct GameflowRewardProfile
    {
        std::wstring_view gameflow;
        uint8_t item_id; // Ordinal within the specific Tres item enum.
        std::wstring_view conversion_suffix{L"Weapon"};
    };
    inline constexpr std::array runtime_gameflow_reward_profiles{
        GameflowRewardProfile{L"he_01_gameflow", 2},
        GameflowRewardProfile{L"he_02_gameflow", 2},
        GameflowRewardProfile{L"he_03_gameflow", 2},
        GameflowRewardProfile{L"he_04_gameflow", 2},
        GameflowRewardProfile{L"he_05_gameflow", 2},
        GameflowRewardProfile{L"he_06_gameflow", 2},
        GameflowRewardProfile{L"tt_01_gameflow", 3},
        GameflowRewardProfile{L"ts_01_gameflow", 4},
        GameflowRewardProfile{L"ts_02_gameflow", 4},
        GameflowRewardProfile{L"ts_03_gameflow", 4},
        GameflowRewardProfile{L"ts_04_gameflow", 4},
        GameflowRewardProfile{L"ra_01_gameflow", 5},
        GameflowRewardProfile{L"ra_02_gameflow", 5},
        GameflowRewardProfile{L"mi_01_gameflow", 6},
        GameflowRewardProfile{L"mi_02_gameflow", 6},
        GameflowRewardProfile{L"mi_03_gameflow", 6},
        GameflowRewardProfile{L"mi_04_gameflow", 6},
        GameflowRewardProfile{L"po_01_gameflow", 8},
        GameflowRewardProfile{L"po_02_gameflow", 8},
        GameflowRewardProfile{L"po_03_gameflow", 8},
        GameflowRewardProfile{L"po_04_gameflow", 8},
        GameflowRewardProfile{L"fz_01_gameflow", 7},
        GameflowRewardProfile{L"fz_02_gameflow", 7},
        GameflowRewardProfile{L"fz_03_gameflow", 7},
        GameflowRewardProfile{L"fz_04_gameflow", 7},
        GameflowRewardProfile{L"fz_05_gameflow", 7},
        GameflowRewardProfile{L"fz_06_gameflow", 7},
        GameflowRewardProfile{L"ca_01_gameflow", 10},
        GameflowRewardProfile{L"ca_02_gameflow", 10},
        GameflowRewardProfile{L"ca_03_gameflow", 10},
        GameflowRewardProfile{L"ca_04_gameflow", 10},
        GameflowRewardProfile{L"ca_05_gameflow", 10},
        GameflowRewardProfile{L"bx_01_gameflow", 9},
        GameflowRewardProfile{L"bx_02_gameflow", 9},
        GameflowRewardProfile{L"bx_03_gameflow", 9},
        GameflowRewardProfile{L"bx_04_gameflow", 9},
        GameflowRewardProfile{L"rg_10_gameflow", 52, L"Protector"},
        GameflowRewardProfile{L"rg_11_gameflow", 4, L"Camp"},
        GameflowRewardProfile{L"rg_12_gameflow", 50, L"Protector"},
        GameflowRewardProfile{L"rg_13_gameflow", 5, L"Camp"},
        GameflowRewardProfile{L"rg_14_gameflow", 51, L"Protector"},
        GameflowRewardProfile{L"rg_15_gameflow", 3, L"Camp"},
        GameflowRewardProfile{L"rg_16_gameflow", 51, L"Protector"},
        GameflowRewardProfile{L"rg_17_gameflow", 50, L"Protector"},
        GameflowRewardProfile{L"rg_18_gameflow", 2, L"Camp"},
        GameflowRewardProfile{L"rg_19_gameflow", 50, L"Protector"},
        GameflowRewardProfile{L"rg_20_gameflow", 5, L"Camp"},
        GameflowRewardProfile{L"rg_21_gameflow", 51, L"Protector"},
        GameflowRewardProfile{L"rg_22_gameflow", 111, L"Accessory"},
    };
    enum class GameflowRewardBypassResult { Patched, AlreadyPatched, NoMatchingGrant, Rejected };

    [[nodiscard]] inline auto try_patch_gameflow_reward_bytecode(
        uint8_t* script, size_t size, uint8_t item_id,
        uint64_t conversion_function, uint64_t grant_function,
        uint32_t callback_name) noexcept -> GameflowRewardBypassResult
    {
        if (!script || size < 0x80 || size > 4 * 1024 * 1024 ||
            !item_id || !conversion_function ||
            !grant_function || !callback_name)
            return GameflowRewardBypassResult::Rejected;
        const auto u64 = [script](size_t at) {
            uint64_t value{}; std::memcpy(&value, script + at, sizeof(value)); return value;
        };
        const auto u32 = [script](size_t at) {
            uint32_t value{}; std::memcpy(&value, script + at, sizeof(value)); return value;
        };
        // Match relocated native function pointers, item conversion, local
        // FName and original latent continuation (which may jump backwards).
        // Validate all candidates before writing. Unknown bytecode layouts fail closed.
        std::array<size_t, 4> calls{};
        size_t count{};
        bool changed{};
        for (size_t at = 0x23; at + 0x1F < size; ++at)
        {
            if (script[at - 0xD] != 0x1C || u64(at - 0xC) != conversion_function ||
                script[at - 4] != 0x17 || script[at - 3] != 0x24 ||
                script[at - 2] != item_id || script[at - 1] != 0x16 ||
                script[at + 0x16] != 0x1C || u64(at + 0x17) != grant_function)
                continue;
            if (at + 0x51 >= size) return GameflowRewardBypassResult::Rejected;
            const uint32_t resume = u32(at + 0x2E);
            const bool original = script[at] == 0x19 && script[at + 1] == 0x20;
            const bool patched = script[at] == 0x06 && u32(at + 1) == resume;
            if ((!original && !patched) ||
                script[at - 0x23] != 0x19 || script[at - 0x22] != 0x20 ||
                (original && u64(at - 0x21) != u64(at + 2)) ||
                u32(at - 0x19) != 0xD || u32(at + 0xA) != 0x3A ||
                u64(at + 0xE) != 0 || script[at + 0x1F] != 0x17 ||
                script[at + 0x20] != 0x2F || !u64(at + 0x21) ||
                u32(at + 0x29) != 0x20 || script[at + 0x2D] != 0x5B ||
                resume >= size - 16 ||
                (resume >= at - 0x23 && resume < at + 0x51) ||
                (script[resume] != 0x06 && script[resume] != 0x07 &&
                 script[resume] != 0x0F && script[resume] != 0x14 && script[resume] != 0x19) ||
                script[at + 0x32] != 0x1D ||
                script[at + 0x37] != 0x21 || u32(at + 0x38) != callback_name ||
                u32(at + 0x3C) != callback_name || u32(at + 0x40) != 0 ||
                script[at + 0x44] != 0x17 || script[at + 0x45] != 0x30 ||
                script[at + 0x46] != 0x00 ||
                u64(at + 0x47) != u64(at - 0x15) ||
                script[at + 0x4F] != 0x16 || script[at + 0x50] != 0x4D ||
                count == calls.size())
                return GameflowRewardBypassResult::Rejected;
            calls[count++] = at;
            changed |= original;
        }
        if (!count) return GameflowRewardBypassResult::NoMatchingGrant;
        if (!changed) return GameflowRewardBypassResult::AlreadyPatched;
        for (size_t index = 0; index < count; ++index)
        {
            const size_t at = calls[index];
            const uint32_t resume = u32(at + 0x2E);
            script[at] = 0x06; // EX_Jump; all offsets and story state stay intact.
            std::memcpy(script + at + 1, &resume, sizeof(resume));
        }
        return GameflowRewardBypassResult::Patched;
    }
    [[nodiscard]] auto try_apply_gameflow_reward_bypass(
        size_t index, RC::Unreal::UObject* object) -> bool;
}
