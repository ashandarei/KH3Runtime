#include "kh3ap/features/colors/color_palette.h"
#include <array>
#include <cassert>
#include <cmath>
#include <iostream>

int main()
{
    using namespace kh3ap;
    constexpr std::array slots{PaletteSlot::Nuno, PaletteSlot::Leather,
        PaletteSlot::Metal, PaletteSlot::Custom, PaletteSlot::Hair,
        PaletteSlot::HairTips, PaletteSlot::HairSS, PaletteSlot::Base,
        PaletteSlot::BaseForced, PaletteSlot::BaseSoft, PaletteSlot::Paint,
        PaletteSlot::Paint2, PaletteSlot::Paint3};
    for (auto mode : {RuntimeColorHarmonyMode::Coordinated, RuntimeColorHarmonyMode::Wild})
    {
        for (int seed = 0; seed < 1000; ++seed)
        {
            auto identity = std::to_wstring(seed);
            auto first = build_palette(identity, L"p_ex001", mode);
            auto repeated = build_palette(identity, L"p_ex001", mode);
            for (auto slot : slots)
            {
                auto a = palette_color(first, slot), b = palette_color(repeated, slot);
                assert(a.r == b.r && a.g == b.g && a.b == b.b);
                for (float channel : {a.r, a.g, a.b})
                    assert(std::isfinite(channel) && channel >= 0 && channel <= 1);
            }
        }
    }
    auto a = build_palette(L"seed", L"p_ex001", RuntimeColorHarmonyMode::Coordinated);
    auto b = build_palette(L"seed#reroll1", L"p_ex001", RuntimeColorHarmonyMode::Coordinated);
    assert(a.nuno.r != b.nuno.r || a.nuno.g != b.nuno.g || a.nuno.b != b.nuno.b);
    auto lifted = at_gain({0.2f, 0.4f, 0.1f}, 2.0f);
    assert(lifted.r == 1.0f && lifted.g == 2.0f && lifted.b == 0.5f);
    std::cout << "Color palette tests passed (2000 palettes)\n";
}
