#include "kh3ap/features/story/world_fixes/toy_box_ufo_damage.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/features/player/characters/character_swap.h"
#include <Unreal/NameTypes.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UScriptStruct.hpp>
#include <cstring>

namespace kh3ap::toy_box_ufo
{
    using namespace RC::Unreal;

    namespace
    {
        bool hit_from_controlled_alternate(UObject* controller, UObject* causer)
        {
            auto* pawn = character_swap::current_hero();
            if (!pawn || !character_swap::owns_swap_pawn(pawn))
            {
                return false;
            }
            if (controller && try_read_reflected_object_property_value(pawn, L"Controller") != controller)
            {
                return false;
            }
            return causer == pawn || (is_uobject_valid_fast(causer) &&
                try_read_reflected_object_property_value(causer, L"Owner") == pawn);
        }
    }

    const void* damage_info_for_hit(UClass* hit_counter_owner, UObject* controller, UObject* causer,
                                    const void* info, DamageInfoCopy& copy)
    {
        static const FName ufo_class(L"g_ts_UFO_C", FNAME_Add);
        if (hit_counter_owner->GetNamePrivate() != ufo_class ||
            hit_counter_owner->GetPathName() != L"/Game/Blueprints/Gimmick/ts/g_ts_UFO/g_ts_UFO.g_ts_UFO_C" ||
            !hit_from_controlled_alternate(controller, causer))
        {
            return info;
        }
        auto* schema = UObjectGlobals::StaticFindObject<UScriptStruct*>(nullptr, nullptr, L"/Script/TresGame.TresDamageInfo");
        auto* property = is_uobject_valid_fast(schema) ? schema->GetPropertyByNameInChain(L"m_bIsKillerAttack") : nullptr;
        if (!property || !property->IsA<FBoolProperty>())
        {
            return info;
        }
        const auto size = schema->GetStructureSize();
        if (size <= 0 || static_cast<size_t>(size) > copy.bytes.size())
        {
            return info;
        }
        // The UFO checks this bit on its final hit. Other targets of the same swing use the original record.
        std::memcpy(copy.bytes.data(), info, size);
        static_cast<FBoolProperty*>(property)->SetPropertyValueInContainer(copy.bytes.data(), true);
        return copy.bytes.data();
    }
}
