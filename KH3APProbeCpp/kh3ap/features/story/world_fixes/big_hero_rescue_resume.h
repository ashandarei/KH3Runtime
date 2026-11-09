#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <span>

namespace RC::Unreal { class UObject; }

namespace kh3ap
{
    enum class HeroRescueResumeResult { Rejected, Patched, AlreadyPatched };
    using HeroRescueScriptName = std::array<uint32_t, 3>;

    // The four saved-hero restore paths converge on a fade-in followed by
    // unconditional writes of ALL four rescue flags. A single rescued hero
    // therefore completes the mission on the next city visit. Skip only those
    // writes; retain each hero's restore event and the normal all-four check.
    inline auto patch_big_hero_rescue_resume(
            std::span<uint8_t> script, uint64_t library, uint64_t setter,
            const std::array<HeroRescueScriptName, 4>& flags) -> HeroRescueResumeResult
    {
        constexpr size_t script_size = 0xA6175;
        constexpr size_t first = 0x20275;
        constexpr size_t call_size = 0x2E;
        constexpr uint32_t resume = 0x2032D;
        if (script.size() != script_size || !library || !setter)
        {
            return HeroRescueResumeResult::Rejected;
        }
        const auto matches = [&](size_t at, const auto& value) {
            return std::memcmp(script.data() + at, &value, sizeof(value)) == 0;
        };
        const bool patched = script[first] == 0x06 && matches(first + 1, resume);
        if (script[resume] != 0x4D)
        {
            return HeroRescueResumeResult::Rejected;
        }
        // Validate every original call before changing anything, including the
        // relocated native targets and all three serialized FName words.
        for (size_t index = 0; index < flags.size(); ++index)
        {
            const size_t at = first + index * call_size;
            if ((!(patched && index == 0) &&
                    (script[at] != 0x19 || script[at + 1] != 0x20 || !matches(at + 2, library))) ||
                !matches(at + 0xA, uint32_t{24}) || !matches(at + 0xE, uint64_t{0}) ||
                script[at + 0x16] != 0x1C || !matches(at + 0x17, setter) ||
                script[at + 0x1F] != 0x21 || !matches(at + 0x20, flags[index]) ||
                script[at + 0x2C] != 0x27 || script[at + 0x2D] != 0x16)
            {
                return HeroRescueResumeResult::Rejected;
            }
        }
        if (patched)
        {
            return HeroRescueResumeResult::AlreadyPatched;
        }
        script[first] = 0x06; // EX_Jump to the original EX_PopExecutionFlow.
        std::memcpy(script.data() + first + 1, &resume, sizeof(resume));
        return HeroRescueResumeResult::Patched;
    }

    auto try_apply_big_hero_rescue_resume(RC::Unreal::UObject* object) -> bool;
}
