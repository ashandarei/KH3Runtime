#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace RC::Unreal { class UObject; }

namespace kh3ap
{
    // The endings grant weapons through WaitBPTresGetKeyitem independently of
    // TresEventData. AP owns the rewards and observes the original story flags.
    struct RuntimeWorldKeybladeProfile { std::wstring_view gameflow; uint8_t weapon_id; };
    inline constexpr std::array runtime_world_keyblade_profiles{
        RuntimeWorldKeybladeProfile{L"he_01_gameflow", 2},
        RuntimeWorldKeybladeProfile{L"he_02_gameflow", 2},
        RuntimeWorldKeybladeProfile{L"he_03_gameflow", 2},
        RuntimeWorldKeybladeProfile{L"he_04_gameflow", 2},
        RuntimeWorldKeybladeProfile{L"he_05_gameflow", 2},
        RuntimeWorldKeybladeProfile{L"he_06_gameflow", 2},
        RuntimeWorldKeybladeProfile{L"tt_01_gameflow", 3},
        RuntimeWorldKeybladeProfile{L"ts_01_gameflow", 4},
        RuntimeWorldKeybladeProfile{L"ts_02_gameflow", 4},
        RuntimeWorldKeybladeProfile{L"ts_03_gameflow", 4},
        RuntimeWorldKeybladeProfile{L"ts_04_gameflow", 4},
        RuntimeWorldKeybladeProfile{L"ra_01_gameflow", 5},
        RuntimeWorldKeybladeProfile{L"ra_02_gameflow", 5},
        RuntimeWorldKeybladeProfile{L"mi_01_gameflow", 6},
        RuntimeWorldKeybladeProfile{L"mi_02_gameflow", 6},
        RuntimeWorldKeybladeProfile{L"mi_03_gameflow", 6},
        RuntimeWorldKeybladeProfile{L"mi_04_gameflow", 6},
        RuntimeWorldKeybladeProfile{L"po_01_gameflow", 8},
        RuntimeWorldKeybladeProfile{L"po_02_gameflow", 8},
        RuntimeWorldKeybladeProfile{L"po_03_gameflow", 8},
        RuntimeWorldKeybladeProfile{L"po_04_gameflow", 8},
        RuntimeWorldKeybladeProfile{L"fz_01_gameflow", 7},
        RuntimeWorldKeybladeProfile{L"fz_02_gameflow", 7},
        RuntimeWorldKeybladeProfile{L"fz_03_gameflow", 7},
        RuntimeWorldKeybladeProfile{L"fz_04_gameflow", 7},
        RuntimeWorldKeybladeProfile{L"fz_05_gameflow", 7},
        RuntimeWorldKeybladeProfile{L"fz_06_gameflow", 7},
        RuntimeWorldKeybladeProfile{L"ca_01_gameflow", 10},
        RuntimeWorldKeybladeProfile{L"ca_02_gameflow", 10},
        RuntimeWorldKeybladeProfile{L"ca_03_gameflow", 10},
        RuntimeWorldKeybladeProfile{L"ca_04_gameflow", 10},
        RuntimeWorldKeybladeProfile{L"ca_05_gameflow", 10},
        RuntimeWorldKeybladeProfile{L"bx_01_gameflow", 9},
        RuntimeWorldKeybladeProfile{L"bx_02_gameflow", 9},
        RuntimeWorldKeybladeProfile{L"bx_03_gameflow", 9},
        RuntimeWorldKeybladeProfile{L"bx_04_gameflow", 9},
    };
    enum class RuntimeWorldKeybladeBypassResult { Patched, AlreadyPatched, NoMatchingGrant, Rejected };

    [[nodiscard]] inline auto try_patch_runtime_world_keyblade_bytecode(
        uint8_t* script, size_t size, uint8_t weapon_id,
        uint64_t conversion_function, uint64_t grant_function,
        uint32_t callback_name) noexcept -> RuntimeWorldKeybladeBypassResult
    {
        if (!script || size < 0x80 || size > 4 * 1024 * 1024 ||
            weapon_id < 2 || weapon_id > 10 || !conversion_function ||
            !grant_function || !callback_name)
            return RuntimeWorldKeybladeBypassResult::Rejected;
        const auto u64 = [script](size_t at) {
            uint64_t value{}; std::memcpy(&value, script + at, sizeof(value)); return value;
        };
        const auto u32 = [script](size_t at) {
            uint32_t value{}; std::memcpy(&value, script + at, sizeof(value)); return value;
        };
        // Match relocated native function pointers, weapon conversion, local
        // FName and original latent continuation (which may jump backwards).
        // Validate all
        // candidates before writing. Unknown bytecode layouts fail closed.
        std::array<size_t, 4> calls{};
        size_t count{};
        bool changed{};
        for (size_t at = 0x23; at + 0x1F < size; ++at)
        {
            if (script[at - 0xD] != 0x1C || u64(at - 0xC) != conversion_function ||
                script[at - 4] != 0x17 || script[at - 3] != 0x24 ||
                script[at - 2] != weapon_id || script[at - 1] != 0x16 ||
                script[at + 0x16] != 0x1C || u64(at + 0x17) != grant_function)
                continue;
            if (at + 0x51 >= size) return RuntimeWorldKeybladeBypassResult::Rejected;
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
                return RuntimeWorldKeybladeBypassResult::Rejected;
            calls[count++] = at;
            changed |= original;
        }
        if (!count) return RuntimeWorldKeybladeBypassResult::NoMatchingGrant;
        if (!changed) return RuntimeWorldKeybladeBypassResult::AlreadyPatched;
        for (size_t index = 0; index < count; ++index)
        {
            const size_t at = calls[index];
            const uint32_t resume = u32(at + 0x2E);
            script[at] = 0x06; // EX_Jump; all offsets and story state stay intact.
            std::memcpy(script + at + 1, &resume, sizeof(resume));
        }
        return RuntimeWorldKeybladeBypassResult::Patched;
    }
    [[nodiscard]] auto try_apply_runtime_world_keyblade_early_bypass(
        size_t index, RC::Unreal::UObject* object) -> bool;
}
