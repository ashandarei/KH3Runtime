#pragma once
#include "kh3ap/features/player/player_state_ids.h"
#include <cmath>
#include <cstdint>
#include <optional>

namespace kh3ap::character_style
{
    // Shipping ETresCommandKind values; L1/L3 are distinct Second Form commands.
    inline constexpr int rage_form_command = 139;      // CHANGE_AN
    inline constexpr int second_form_l1_command = 148; // CHANGE_L1
    inline constexpr int second_form_l3_command = 150; // CHANGE_L3
    inline constexpr int spellweaver_command = 153;    // CHANGE_A1

    constexpr bool blocks_character_swap(int command, int state)
    {
        // Rage remains active between ComboAnti attacks. Its controller command
        // is authoritative even while Sora is idle, running or jumping.
        return command == rage_form_command || state == player_state_ids::combo_anti ||
               state == player_state_ids::style_change || state == player_state_ids::weapon_change;
    }

    struct Form
    {
        int command{}, level{}, weapon{}, weapon_id{};
        uint8_t form{}, wear{};
        float remaining{}, total{};
        bool finish_ready{};
    };

    inline bool resumable(const Form& form)
    {
        const bool style = (form.command >= 128 && form.command <= 154) ||
                           (form.command >= 221 && form.command <= 224);
        return style && form.command != rage_form_command && form.level > 0 && form.level <= 2 &&
               form.weapon > 0 &&
               form.weapon_id > 0 && form.form > 0 && form.form <= 24 && form.wear <= 16 &&
               std::isfinite(form.remaining) && std::isfinite(form.total) && form.remaining > 0 &&
               form.remaining <= form.total && form.total <= 3600;
    }
} // namespace kh3ap::character_style
