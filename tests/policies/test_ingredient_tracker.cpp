#include "kh3ap/features/ingredients/ingredient_tracker.h"
#include <cassert>
#include <set>

int main()
{
    using namespace kh3ap;
    IngredientCounts before{}, after{};
    before[27] = 7; // Stock bought earlier must not prevent a natural pickup.
    after = before;
    after[27] += 2;
    assert(acquired_field_ingredients(before, after, true) == std::vector<int>{28});
    assert(ingredient_spot_message(L"RA_01_F_003")==L"Ingredient spot: RA_01_F_003");
    assert(ingredient_spot_message(L"MI_01_PF_001")==L"Ingredient spot: MI_01_PF_001");
    assert(ingredient_spot_message(L"").empty());
    assert(ingredient_spot_message(L"FST_ITEM28").empty());
    assert(ingredient_spot_message(L"UNKNOWN").empty());
    assert(acquired_field_ingredients(before, after, false).empty()); // Shop/AP source.
    assert(acquired_field_ingredients(after, after, true).empty()); // No grant.
    assert(acquired_field_ingredients(after, before, true).empty()); // Consumption/load.
    before[27] = -1;
    assert(acquired_field_ingredients(before, after, true).empty()); // Failed read.
    before.fill(0);
    after.fill(1);
    const auto all = acquired_field_ingredients(before, after, true);
    assert(all.size() == 51);
    std::set<std::wstring> seen;
    for (int id : all)
    {
        assert(valid_ingredient_key(ingredient_key(id)));
        seen.insert(ingredient_key(id));
    }
    assert(seen.size() == 51);
    assert(!valid_ingredient_key(L"FST_ITEM00"));
    assert(!valid_ingredient_key(L"FST_ITEM48"));
    assert(!valid_ingredient_key(L"FST_ITEM55"));
    assert(!valid_ingredient_key(L"FST_ITEM60"));
    assert(!valid_ingredient_key(L"FST_ITEM2x"));
    assert(!valid_ingredient_key(L"FOOD_ITEM28"));
    const auto* herb1 = L"/Game/Levels/ra/ra_01/umap/ra_01_food_AreaA.ra_01_food_AreaA:PersistentLevel.g_ra_herb_001";
    const auto* herb2 = L"/Game/Levels/ra/ra_01/umap/ra_01_food_AreaA.ra_01_food_AreaA:PersistentLevel.g_ra_herb_002";
    assert(ingredient_spot_key(herb1) == L"RA_01_F_001");
    assert(ingredient_spot_key(herb2) == L"RA_01_F_002");
    assert(ingredient_spot_key(std::wstring(herb1) + L"_0").empty());
    assert(ingredient_spot_key(L"g_ra_herb_001").empty());
    assert(ingredient_spot_key(L"/Game/Other:PersistentLevel.g_ra_herb_001").empty());
    assert(ingredient_spot_key(L"/Game/Levels/he/he_03/umap/he_03_food.he_03_food:PersistentLevel.g_he_herb_007") ==
           ingredient_spot_key(L"/Game/Levels/he/he_04/umap/he_04_food.he_04_food:PersistentLevel.g_he_herb_007"));
    std::set<std::wstring_view> spot_ids, paths;
    for (const auto& spot : ingredient_spot_actors) {
        assert(paths.insert(spot.path).second);
        assert(valid_ingredient_spot_key(spot.key));
        assert(spot.path.find(L"tutorial") == std::wstring_view::npos);
        spot_ids.insert(spot.key);
    }
    assert(spot_ids.size() == 298);
    assert(!valid_ingredient_spot_key(L"TT_01_PF_TUTO_001"));
    assert(!valid_ingredient_spot_key(L"FST_ITEM26"));
}
