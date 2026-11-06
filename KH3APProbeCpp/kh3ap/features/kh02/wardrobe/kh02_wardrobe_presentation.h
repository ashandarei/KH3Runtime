#pragma once
#include <array>

namespace kh3ap
{
    enum class Kh02WardrobePage
    {
        Categories,
        Items,
        ColorType,
        Gradient,
        Editor,
        Palette
    };
    inline constexpr auto kh02_wardrobe_costume_path = L"campMenu.contents.menuCostume.";
    inline constexpr std::array<const wchar_t*, 5> kh02_wardrobe_category_buttons{L"head", L"arm", L"back", L"pattern",
                                                                                  L"color"};
    inline constexpr std::array<std::array<int, 3>, 20> kh02_wardrobe_palette{
        {{0, 0, 0},       {96, 96, 96},    {128, 0, 0},     {255, 0, 0},     {255, 112, 24},
         {255, 255, 0},   {0, 160, 64},    {0, 160, 224},   {48, 48, 192},   {144, 48, 160},
         {255, 255, 255}, {176, 176, 176}, {160, 96, 64},   {255, 160, 192}, {255, 192, 0},
         {232, 224, 144}, {160, 224, 0},   {128, 208, 224}, {96, 128, 176},  {192, 176, 224}}};
} // namespace kh3ap
