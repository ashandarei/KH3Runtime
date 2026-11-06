#include "kh02_enemy_weakness.h"
#include "kh02_challenge_policy.h"
#include "kh3ap/features/kh02/core/kh02_data_table.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UScriptStruct.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
extern "C" bool kh02_table_rows_visit(void*, void (*)(unsigned long long, void*, void*), void*);

namespace kh3ap
{
    using namespace RC::Unreal;

    namespace
    {
        constexpr int first_weakness_element = 1;
        constexpr int last_weakness_element = 5;
    } // namespace

    bool kh02_enemy_has_weakness(UObject* enemy, int element)
    {
        if (!is_uobject_valid_fast(enemy) || element < first_weakness_element || element > last_weakness_element)
        {
            return false;
        }
        const auto type = enemy->GetClassPrivate()->GetName();
        const auto split = type.find(L"_Pawn");
        if (split == std::wstring::npos || kh02_tracking::species(type) < 0)
        {
            return false;
        }
        const auto family = type.substr(0, split);
        const auto path = L"/Game/Zlueprints/Enemy/" + family + L"/DataTable/" + family + L"_BaseDataTable." + family +
                          L"_BaseDataTable";
        auto* table = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, path.c_str());
        if (!is_uobject_valid_fast(table))
        {
            return false;
        }
        auto* schema = try_read_reflected_object_property_value(table, L"RowStruct");
        if (!is_uobject_valid_fast(schema) || schema->GetPathName() != L"/Script/TresGame.TresChrBaseParamDataTable")
        {
            return false;
        }
        const wchar_t* names[]{L"",
                               L"m_AttrWeekPointFire",
                               L"m_AttrWeekPointBlizzard",
                               L"m_AttrWeekPointThunder",
                               L"m_AttrWeekPointWater",
                               L"m_AttrWeekPointAero"};
        auto* field = find_struct_field_by_name(static_cast<UScriptStruct*>(schema), names[element]);
        if (!field || !field->IsA<FBoolProperty>())
        {
            return false;
        }

        struct Evidence
        {
            FBoolProperty* field;
            bool all{true};
            int rows{};
        } evidence{static_cast<FBoolProperty*>(field)};

        // A family only qualifies if its authored rows agree. Do not infer
        // weakness merely from a high damage multiplier or an enemy name.
        kh02_table_rows_visit(
            reinterpret_cast<std::byte*>(table) + Kh02DataTableView::row_map_offset,
            [](unsigned long long, void* row, void* context)
            {
                auto& proof = *static_cast<Evidence*>(context);
                ++proof.rows;
                proof.all &= proof.field->GetPropertyValueInContainer(row);
            },
            &evidence);
        return evidence.rows > 0 && evidence.all;
    }

} // namespace kh3ap
