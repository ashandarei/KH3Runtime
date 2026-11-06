#pragma once
#include <cmath>
#include <cstdint>
#include <optional>

namespace kh3ap::character_style
{
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
        return style && form.level > 0 && form.level <= 2 && form.weapon > 0 &&
               form.weapon_id > 0 && form.form > 0 && form.form <= 24 && form.wear <= 16 &&
               std::isfinite(form.remaining) && std::isfinite(form.total) && form.remaining > 0 &&
               form.remaining <= form.total && form.total <= 3600;
    }
} // namespace kh3ap::character_style
