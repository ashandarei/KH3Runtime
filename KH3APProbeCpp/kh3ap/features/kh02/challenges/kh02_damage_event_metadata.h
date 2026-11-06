#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <array>

namespace kh3ap
{
    struct Kh02DamageEventMetadata
    {
        int command{};
        int element{};
        uint8_t magic_flags{};
        std::wstring attack;
    };

    struct Kh02DamageClassification
    {
        bool magic{};
        bool counter{};
        bool style{};
        bool spellweaver{};
    };

    inline bool kh02_sora_style_attack(std::wstring_view attack)
    {
        // Native Sora form attacks use the SC + command-family identity.
        // Keep Aqua's Spellweaver and Mickey's link attacks separate.
        constexpr std::array families{L"SCDW_", L"SCHM_", L"SCYO_", L"SCBZ_", L"SCDR_", L"SCSK_", L"SCGM_", L"SCSW_",
                                      L"SCWA_", L"SCSH_", L"SCCL_", L"SCHL_", L"SCFL_", L"SCTF_", L"SCDF_", L"SCLF_"};
        for (const auto* family : families)
        {
            if (attack.starts_with(family))
            {
                return true;
            }
        }
        return false;
    }

    // Classification only: no native reads, challenge mutations or damage execution.
    inline auto kh02_classify_damage_event(const Kh02DamageEventMetadata& metadata, bool aqua)
        -> Kh02DamageClassification
    {
        constexpr uint8_t magic_flag = 1;
        constexpr int fire_element = 1;
        constexpr int lightning_element = 3;
        constexpr int first_fire_magic_command = 29;
        constexpr int last_fire_magic_command = 32;
        constexpr int magic_commands_per_element = 4;
        constexpr int guard_counter_command = 6;
        constexpr int first_counter_command = 215;
        constexpr int last_counter_command = 217;
        constexpr int spellweaver_finish_command = 167;

        const bool magic = (metadata.magic_flags & magic_flag) && metadata.element >= fire_element &&
                           metadata.element <= lightning_element &&
                           metadata.command >= first_fire_magic_command +
                                                   magic_commands_per_element * (metadata.element - fire_element) &&
                           metadata.command <=
                               last_fire_magic_command + magic_commands_per_element * (metadata.element - fire_element);
        const bool counter = metadata.command == guard_counter_command ||
                             (metadata.command >= first_counter_command && metadata.command <= last_counter_command);
        return {magic, counter, aqua ? metadata.attack.starts_with(L"SCAQ_") : kh02_sora_style_attack(metadata.attack),
                aqua && metadata.command == spellweaver_finish_command && metadata.attack.starts_with(L"SCAQ_FIN")};
    }
} // namespace kh3ap
