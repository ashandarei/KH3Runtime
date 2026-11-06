#include "kh02_ability_capacity.h"
#include "kh3ap/features/kh02/core/kh02_data_table.h"
#include "kh3ap/features/kh02/runtime/kh02_entry_assets.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/core/objects/uobject_weak.h"

namespace kh3ap
{
    using namespace RC::Unreal;

    namespace
    {
        constexpr auto base_path = L"/Game/DataTable/Player/Base/p_ex002_BaseParamData.p_ex002_BaseParamData";
        constexpr int aqua_base_ap = 200;

        auto scalar(Kh02DataTableView& table, const wchar_t* name, const wchar_t* type, int bytes) -> FProperty*
        {
            auto* property = table.schema->GetPropertyByNameInChain(name);
            if (!property || property->GetClass().GetName() != type || property->GetElementSize() != bytes ||
                property->GetArrayDim() != 1 || property->GetOffset_Internal() < sizeof(void*) ||
                property->GetOffset_Internal() + bytes > table.schema->GetStructureSize())
            {
                throw std::runtime_error("kh02_ability_capacity_field_schema");
            }
            return property;
        }

        auto base_ap(UObject* object) -> int*
        {
            Kh02DataTableView table(object, L"/Script/TresGame.TresChrBaseParamDataTable",
                                    "kh02_ability_capacity_base_schema");
            auto* row = table.find(L"Default");
            if (!row)
            {
                throw std::runtime_error("kh02_ability_capacity_base_row");
            }
            return scalar(table, L"m_AbilityPoint", L"IntProperty", sizeof(int))->ContainerPtrToValuePtr<int>(row);
        }

        auto load(UObject* context, const wchar_t* path, int request, bool& requested, Kh02EntryAssets& assets)
            -> UObject*
        {
            auto* object = find_loaded_runtime_asset(path);
            if (is_uobject_valid_fast(object))
            {
                assets.retain(object);
                return object;
            }
            if (!requested)
            {
                std::wstring reason;
                if (!request_runtime_asset_load(context, path, &reason, request))
                {
                    throw std::runtime_error("kh02_ability_capacity_asset_request");
                }
                requested = true;
            }
            return nullptr;
        }
    } // namespace

    bool Kh02AbilityCapacity::prepare(UObject* context, Kh02EntryAssets& assets)
    {
        auto* base = load(context, base_path, 0x4b4834c0, base_requested, assets);
        if (!base)
        {
            return false;
        }
        if (base_table.Get() == base)
        {
            return true;
        }

        release();
        auto* value = base_ap(base);
        original_ap = *value;
        base_table = make_uobject_weak(base);
        *value = aqua_base_ap;
        return true;
    }

    void Kh02AbilityCapacity::release()
    {
        if (auto* table = base_table.Get(); is_uobject_valid_fast(table))
        {
            *base_ap(table) = original_ap;
        }
        base_table = {};
        base_requested = false;
    }
} // namespace kh3ap
