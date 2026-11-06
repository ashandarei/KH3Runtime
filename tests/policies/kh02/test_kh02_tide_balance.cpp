#include "kh3ap/features/kh02/combat/kh02_tide_balance.h"
#include "kh3ap/features/kh02/combat/kh02_boss_balance.h"
#include "kh3ap/features/kh02/combat/kh02_enemy_scaling_policy.h"

#include <cassert>
#include <limits>
#include <string>

int main()
{
    using namespace kh3ap::kh02_tide_balance;
    // Current EGS EnemyBaseStatus curve: levels 1, 2 and 50 are 24, 26, 118.
    // The Graveyard core's two rows both have 4,000 authored HP.
    assert(scaled_hp(24, 4000, 118) == 813);
    assert(scaled_hp(26, 4000, 118) == 881);
    assert(scaled_hp(118, 4000, 118) == 4000);
    assert(scaled_hp(137, 4000, 118) == 4644);
    // Use the installed seed's actual reference HP and curve, not cached
    // previous results. Repeated calls cannot compound scaling or round 4,000 down.
    assert(scaled_hp(26, 5000, 118) == 1101);
    assert(scaled_hp(118, 5000, 118) == 5000);
    assert(scaled_hp(1, 1, 100000) == 1);
    assert(!scaled_hp(0, 4000, 118));
    assert(!scaled_hp(26, 0, 118));
    assert(!scaled_hp(26, 4000, 0));
    assert(!scaled_hp(100001, 4000, 118));
    assert(!scaled_hp(26, 1000001, 118));
    assert(!scaled_hp(26, 4000, 100001));
    assert(!scaled_hp(100000, 1000000, 1));

    using kh3ap::kh02_boss_balance::reference_hp;
    using kh3ap::kh02_boss_balance::uses_tide_ratio;

    // Captured source rates preserve early/late and challenge encounter ratios.
    struct BossCase
    {
        const wchar_t* directory;
        const wchar_t* table;
        float rate;
        int expected_reference;
    };

    constexpr BossCase bosses[] = {{L"e_dw402", L"e_dw402_BaseDataTable", 27.89f, 2190},
                                   {L"e_dw402_s0", L"e_dw402_BaseDataTable_s0", 23.92f, 1878},
                                   {L"e_dw402_s100", L"e_dw402_BaseDataTable_s100", 21.78f, 1710},
                                   {L"e_dw405", L"e_dw405_BaseDataTable", 5.11f, 401},
                                   {L"e_dw405_s1", L"e_dw405_BaseDataTable_s1", 15.31f, 1202},
                                   {L"e_dw405_s100", L"e_dw405_BaseDataTable_s100", 32.26f, 2533},
                                   {L"e_dw407", L"e_dw407_BaseDataTable", 19.81f, 1555},
                                   {L"e_dw407a_s0", L"e_dw407a_BaseDataTable", 4.0f, 314},
                                   {L"e_dw407b", L"e_dw407b_BaseDataTable", 25.75f, 2022},
                                   {L"e_dw407_s100", L"e_dw407_BaseDataTable_s100", 20.165f, 1583}};
    for (const auto& boss : bosses)
    {
        const auto path =
            std::wstring(L"/Game/Zlueprints/Enemy/") + boss.directory + L"/DataTable/" + boss.table + L"." + boss.table;
        assert(uses_tide_ratio(path));
        const auto reference = reference_hp(boss.rate, 50.93f, 4000);
        assert(reference == boss.expected_reference);
        for (const int base_hp : {24, 26, 118, 137})
        {
            const auto adapted = scaled_hp(base_hp, *reference, 118);
            assert(adapted);
            assert(*adapted < kh3ap::kh02_scaled_enemy_hp(base_hp, boss.rate));
            // Two integer normalization steps differ from the exact ratio by <3 HP.
            const double exact = 4000.0 * base_hp / 118 * boss.rate / 50.93f;
            assert(exact - *adapted >= 0 && exact - *adapted < 3);
            assert(kh3ap::kh02_scaled_enemy_hp(base_hp, boss.rate, adapted) == *adapted);
        }
        assert(scaled_hp(118, *reference, 118) == boss.expected_reference);
        assert(reference_hp(boss.rate, 50.93f, 5000) > reference);
        assert(reference_hp(boss.rate, 50.93f, 4000) == reference);
    }
    assert(!uses_tide_ratio(L"/Game/Zlueprints/Enemy/e_dw401/DataTable/e_dw401_BaseDataTable.e_dw401_BaseDataTable"));
    assert(!uses_tide_ratio(
        L"/Game/Zlueprints/Enemy/e_ex016_s100/DataTable/e_ex016_BaseDataTable_s100.e_ex016_BaseDataTable_s100"));
    assert(!uses_tide_ratio(L"/Game/Zlueprints/Enemy/e_ex001/DataTable/e_ex001_BaseDataTable.e_ex001_BaseDataTable"));
    assert(!uses_tide_ratio(L"/Game/DataTable/Enemy/Base/e_dw402_BaseDataTable.e_dw402_BaseDataTable"));
    assert(reference_hp(50.93f, 50.93f, 4000) == 4000);
    assert(reference_hp(0.001f, 50.93f, 1) == 1);
    assert(!reference_hp(0, 50.93f, 4000));
    assert(!reference_hp(-1, 50.93f, 4000));
    assert(!reference_hp(1001, 50.93f, 4000));
    assert(!reference_hp(5, 0, 4000));
    assert(!reference_hp(5, 1001, 4000));
    assert(!reference_hp(5, 50.93f, 0));
    assert(!reference_hp(5, 50.93f, 1000001));
    assert(!reference_hp(1000, 0.001f, 1000000));
    assert(!reference_hp(std::numeric_limits<float>::infinity(), 50.93f, 4000));
    assert(!reference_hp(5, std::numeric_limits<float>::quiet_NaN(), 4000));

    using kh3ap::kh02_boss_balance::encounter_reference_hp;
    constexpr auto opening_tower =
        L"/Game/Zlueprints/Enemy/e_dw402_s0/DataTable/e_dw402_BaseDataTable_s0.e_dw402_BaseDataTable_s0";
    const auto opening_reference = encounter_reference_hp(opening_tower, 23.92f, 50.93f, 4000);
    assert(opening_reference == 900);
    assert(scaled_hp(118, *opening_reference, 118) == 900);
    assert(scaled_hp(26, *opening_reference, 118) == 198);
    assert(encounter_reference_hp(opening_tower, 23.92f, 50.93f, 5000) == 900);
    assert(!encounter_reference_hp(opening_tower, 0, 50.93f, 4000));
    assert(encounter_reference_hp(
               L"/Game/Zlueprints/Enemy/e_dw402/DataTable/e_dw402_BaseDataTable.e_dw402_BaseDataTable",
               27.89f, 50.93f, 4000) == 2190);
    assert(encounter_reference_hp(
               L"/Game/Zlueprints/Enemy/e_dw402_s100/DataTable/e_dw402_BaseDataTable_s100.e_dw402_BaseDataTable_s100",
               21.78f, 50.93f, 4000) == 1710);
}
