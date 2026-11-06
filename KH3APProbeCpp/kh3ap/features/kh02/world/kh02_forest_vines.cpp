#include "kh02_forest_vines.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/FScriptArray.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#include <cstdint>
#include <stdexcept>

namespace kh3ap
{
    using namespace RC::Unreal;

    void kh02_configure_forest_vine_damage(UObject* hits)
    {
        constexpr int damage_filter_entry_limit = 16;
        constexpr uint8_t fire_damage_attribute = 1; // Verified EGS ETresDamageAttribute::TRES_DMG_ATTR_FIRE.
        auto* vine = hits->GetOuterPrivate();
        auto* vine_type =
            find_loaded_runtime_asset_class(L"/Game/Zlueprints/Gimmick/dw/g_dw_Thorn/g_dw_ThornBase.g_dw_ThornBase_C");
        if (!is_uobject_a_fast(vine, vine_type))
        {
            return;
        }
        auto* thorn_type =
            find_loaded_runtime_asset_class(L"/Game/Zlueprints/Gimmick/dw/g_dw_Thorn/g_dw_ThornB.g_dw_ThornB_C");
        const bool has_thorns = is_uobject_a_fast(vine, thorn_type);
        auto* property = hits->GetPropertyByNameInChain(L"LimiteDamageKinds");
        if (!property || !property->IsA<FArrayProperty>() || property->GetElementSize() != sizeof(FScriptArray))
        {
            throw std::runtime_error("kh02_thorn_damage_array");
        }
        auto* inner = static_cast<FArrayProperty*>(property)->GetInner();
        if (!inner || inner->GetClass().GetName() != L"ByteProperty" || inner->GetElementSize() != sizeof(uint8_t))
        {
            throw std::runtime_error("kh02_thorn_damage_element");
        }
        auto* values = property->ContainerPtrToValuePtr<FScriptArray>(hits);
        if (values->Num() < 0 || values->Num() > damage_filter_entry_limit || values->Max() < values->Num())
        {
            throw std::runtime_error("kh02_thorn_damage_bounds");
        }
        // Ordinary A/C/D vines accept all attributes. B/B2/B3 remain fire-only.
        if (!has_thorns)
        {
            if (values->Num() != 0)
            {
                values->Empty(0, sizeof(uint8_t), alignof(uint8_t));
            }
            return;
        }
        if (values->Num() == 1 && static_cast<uint8_t*>(values->GetData())[0] == fire_damage_attribute)
        {
            return;
        }
        values->Empty(1, sizeof(uint8_t), alignof(uint8_t));
        values->AddZeroed(1, sizeof(uint8_t), alignof(uint8_t));
        static_cast<uint8_t*>(values->GetData())[0] = fire_damage_attribute;
    }
} // namespace kh3ap
