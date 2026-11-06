#pragma once
#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>
#include "kh3ap/features/ingredients/ingredient_spots.h"

namespace kh3ap
{
    namespace ingredient_ids
    {
        inline constexpr int first_item = 1;
        inline constexpr int last_item = 59;
        // These item IDs do not qualify as field-pickup ingredient checks.
        inline constexpr int first_excluded_field_item = 48;
        inline constexpr int last_excluded_field_item = 55;
        inline constexpr std::size_t item_count = last_item - first_item + 1;
    } // namespace ingredient_ids

    // Only exact authored map paths qualify. Reused Olympus map variants are
    // explicit aliases in the catalog; dynamic spawn suffixes never match.
    inline std::wstring_view ingredient_spot_key(std::wstring_view path)
    {
        for (const auto& spot : ingredient_spot_actors)
            if (spot.path == path) return spot.key;
        return {};
    }

    inline bool valid_ingredient_spot_key(std::wstring_view key)
    {
        for (const auto& spot : ingredient_spot_actors)
            if (spot.key == key) return true;
        return false;
    }

    constexpr bool is_field_ingredient(int id)
    {
        return id >= ingredient_ids::first_item && id <= ingredient_ids::last_item &&
               (id < ingredient_ids::first_excluded_field_item ||
                id > ingredient_ids::last_excluded_field_item);
    }

    inline std::wstring ingredient_key(int id)
    {
        return L"FST_ITEM" + std::wstring(id < 10 ? L"0" : L"") + std::to_wstring(id);
    }

    inline bool valid_ingredient_key(std::wstring_view key)
    {
        return key.size() == 10 && key.starts_with(L"FST_ITEM") &&
            key[8] >= L'0' && key[8] <= L'9' && key[9] >= L'0' && key[9] <= L'9' &&
            is_field_ingredient((key[8] - L'0') * 10 + key[9] - L'0');
    }

    using IngredientCounts = std::array<int, ingredient_ids::item_count>;

    // Called only across a field-prize grant. Shop, inventory restoration and
    // AP grants never supply an eligible event boundary.
    inline std::vector<int> acquired_field_ingredients(
        const IngredientCounts& before, const IngredientCounts& after, bool eligible)
    {
        std::vector<int> changed;
        if (!eligible) return changed;
        for (int id = ingredient_ids::first_item; id <= ingredient_ids::last_item; ++id)
        {
            if (is_field_ingredient(id) && before[id - 1] >= 0 && after[id - 1] > before[id - 1])
                changed.push_back(id);
        }
        return changed;
    }

    inline std::wstring ingredient_spot_message(std::wstring_view spot)
    {
        return valid_ingredient_spot_key(spot) ? L"Ingredient spot: " + std::wstring(spot) : L"";
    }
}
