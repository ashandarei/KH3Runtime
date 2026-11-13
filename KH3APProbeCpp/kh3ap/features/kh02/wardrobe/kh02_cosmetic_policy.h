#pragma once
#include "kh02_wardrobe_color_mode.h"
#include <array>
#include <algorithm>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace kh3ap::kh02cosmetics
{
    inline constexpr std::array<int, 4> item_counts{12, 9, 9, 22};

    inline auto default_selection() -> nlohmann::json
    {
        return {{"equipped", {0, 0, 0, 0}},
                {"color_mode", static_cast<int>(WardrobeColorMode::Default)},
                {"top", {255, 255, 255}},
                {"bottom", {0, 0, 0}}};
    }

    inline auto adjust_channel(nlohmann::json& selection, int row, int delta) -> bool
    {
        if (row < 3 || row > 8)
        {
            return false;
        }
        const auto* region = row < 6 ? "top" : "bottom";
        const int channel = (row - 3) % 3;
        const int current = selection[region][channel];
        const int adjusted = static_cast<int>(std::clamp(int64_t(current) + delta, int64_t(0), int64_t(255)));
        const auto mode = selection["color_mode"].get<WardrobeColorMode>();
        const auto adjusted_mode = row >= 6                             ? WardrobeColorMode::Gradient
                                   : mode == WardrobeColorMode::Default ? WardrobeColorMode::SingleColor
                                                                        : mode;
        if (current == adjusted && mode == adjusted_mode)
        {
            return false;
        }
        selection[region][channel] = adjusted;
        selection["color_mode"] = static_cast<int>(adjusted_mode);
        return true;
    }

    inline void validate(const nlohmann::json& value)
    {
        const auto integer = [](const auto& number, int maximum)
        {
            if (number.is_number_unsigned())
            {
                return number.template get<uint64_t>() <= static_cast<uint64_t>(maximum);
            }
            return number.is_number_integer() && number.template get<int64_t>() >= 0 &&
                   number.template get<int64_t>() <= maximum;
        };
        if (!value.is_object() || value.size() != 4 || !value.contains("equipped") || !value.contains("color_mode") ||
            !value.contains("top") || !value.contains("bottom") || !value["equipped"].is_array() ||
            value["equipped"].size() != 4 ||
            !integer(value["color_mode"], static_cast<int>(WardrobeColorMode::Gradient)))
        {
            throw std::runtime_error("Invalid 0.2 wardrobe selection");
        }
        for (size_t part = 0; part < item_counts.size(); ++part)
        {
            if (!integer(value["equipped"][part], item_counts[part]))
            {
                throw std::runtime_error("Unknown 0.2 wardrobe item");
            }
        }
        for (const auto* channel : {"top", "bottom"})
        {
            const auto& color = value[channel];
            if (!color.is_array() || color.size() != 3)
            {
                throw std::runtime_error("Invalid 0.2 wardrobe color");
            }
            for (const auto& number : color)
            {
                if (!integer(number, 255))
                {
                    throw std::runtime_error("Invalid 0.2 wardrobe color channel");
                }
            }
        }
    }
} // namespace kh3ap::kh02cosmetics
