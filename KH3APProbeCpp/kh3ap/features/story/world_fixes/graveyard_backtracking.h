#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>
#include <string_view>
#include <optional>

namespace RC::Unreal { class UObject; }

namespace kh3ap
{
    using GraveyardScriptName = std::array<uint32_t, 3>;
    struct GraveyardEntryCondition
    {
        GraveyardScriptName flag;
        GraveyardScriptName label;
    };

    namespace graveyard_progress
    {
        // gameflow_KG's paired-battle phase and the per-arena
        // gameflow_KG_13_A/B/C/D mission bounds; not general world completion.
        inline constexpr int32_t paired_battles_story = 13500;
        inline constexpr int32_t arena_mission_started = 1000;
        inline constexpr int32_t arena_mission_completed = 9999;
    }

    inline auto graveyard_resume_preset(int32_t story, int32_t area_a, int32_t area_b,
            int32_t area_c, int32_t area_d, std::wstring_view preset) -> std::wstring_view
    {
        using namespace graveyard_progress;
        // Correct an earlier save-point arrival during the second battle pair.
        // An active C/D fight must keep its authored retry/arena selection.
        if (story != paired_battles_story || area_a < arena_mission_completed || area_b < arena_mission_completed ||
                (area_c >= arena_mission_started && area_c < arena_mission_completed) ||
                (area_d >= arena_mission_started && area_d < arena_mission_completed) ||
                (area_c >= arena_mission_completed && area_d >= arena_mission_completed)) return {};
        for (const auto* old : {L"Init", L"KG_02_Mission_AB_Start", L"KG_02_Mission_AB_Camera",
                L"KG_02_Mission_AB", L"KG_02_AB_CLEAR", L"KG_02_Area_A_End", L"KG_02_Area_B_End"})
            if (preset == old) return L"KG_02_Mission_CD";
        return {};
    }

    // The KG startup graph uses an object constant for its mapset. Match the
    // entire native call shape, including its latent continuation, before
    // borrowing the preset operand for this synchronous call only.
    inline auto graveyard_preset_operand(std::span<uint8_t> script, size_t offset,
            uintptr_t function, uintptr_t latent, uintptr_t mapset,
            const GraveyardScriptName& callback, const GraveyardScriptName& preset) -> std::optional<size_t>
    {
        if (!function || !latent || !mapset || offset < 9 || offset > script.size() || script.size() - offset < 63) return {};
        const auto* args = script.data() + offset;
        const auto pointer = [](const uint8_t* p) { uintptr_t v{}; std::memcpy(&v, p, 8); return v; };
        uint32_t size{}; std::memcpy(&size, args + 10, 4);
        if (args[-9] != 0x1c || pointer(args - 8) != function || args[0] != 0x17 ||
                args[1] != 0x2f || pointer(args + 2) != latent || size != 32 ||
                (args[14] != 0x5b && args[14] != 0x1d) || args[19] != 0x1d || args[24] != 0x21 ||
                std::memcmp(args + 25, callback.data(), 12) || args[37] != 0x17 || args[38] != 0x30 ||
                args[39] != 0x20 || pointer(args + 40) != mapset || args[48] != 0x21 ||
                std::memcmp(args + 49, preset.data(), 12) || args[61] != 0x27 || args[62] != 0x16) return {};
        return offset + 49;
    }

    // Bound overlap wrappers only copy their parameters and call their
    // ubergraph at a constant entry point. They contain no latent or absolute
    // branches, so a prefix does not relocate any existing jump targets.
    inline auto guard_graveyard_entry(std::span<const uint8_t> original,
            GraveyardScriptName ubergraph, uintptr_t greater_equal, uintptr_t library, uintptr_t return_property,
            std::span<const GraveyardEntryCondition> completed) -> std::vector<uint8_t>
    {
        if (!greater_equal || !library || !return_property || completed.empty() || completed.size() > 2) return {};
        size_t offset{};
        while (offset + 18 <= original.size() && original[offset] == 0x64)
        {
            if (original[offset + 9] != 0x00) return {};
            offset += 18; // EX_LetValueOnPersistentFrame property*, EX_LocalVariable property*
        }
        if (original.size() - offset != 22 || original[offset] != 0x1b ||
                std::memcmp(original.data() + offset + 1, ubergraph.data(), 12) ||
                original[offset + 13] != 0x1d || original[offset + 18] != 0x16 ||
                original[offset + 19] != 0x04 || original[offset + 20] != 0x0b ||
                original[offset + 21] != 0x53) return {};

        std::vector<uint8_t> result;
        const auto append = [&result](const auto& value) {
            const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
            result.insert(result.end(), bytes, bytes + sizeof(value));
        };
        for (const auto& condition : completed)
        {
            const uint32_t next = static_cast<uint32_t>(result.size() + 65);
            result.push_back(0x07); append(next); // EX_JumpIfNot: run original when unfinished
            // Match the game's EX_Context + EX_FinalFunction dispatch. In KH3's
            // UE4.17 VM, EX_CallMath does not set CurrentNativeFunction for the
            // UE4SS native hook and can recursively re-enter the overlap event.
            result.push_back(0x19); result.push_back(0x20); append(library);
            const uint32_t skip = 36; append(skip); append(return_property);
            result.push_back(0x1c); append(greater_equal);
            result.push_back(0x21); append(condition.flag);
            result.push_back(0x21); append(condition.label);
            result.push_back(0x16); // EX_EndFunctionParms
            result.push_back(0x04); result.push_back(0x0b); // completed: return before any side effects
        }
        result.insert(result.end(), original.begin(), original.end());
        return result;
    }

    auto try_apply_graveyard_backtracking(RC::Unreal::UObject* object, size_t variant) -> bool;
}
