#include "kh02_tunnel_mirrors.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include "kh3ap/core/objects/actor_discovery.h"
#include "kh3ap/core/reflection/reflected_function_call.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include <Unreal/AActor.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/Property/FBoolProperty.hpp>

namespace kh3ap
{
    using namespace RC::Unreal;

    void kh02_refresh_tunnel_mirrors_after_swap(UObject* pawn)
    {
        auto* world = kh02_current_story_world();
        if (!is_uobject_valid_fast(world) ||
            world->GetPathName() != L"/Game/Maps/APKH02Mirrors/APKH02Tunnel.APKH02Tunnel")
        {
            return;
        }
        auto* mirror_class = UObjectGlobals::StaticFindObject<UClass*>(
            nullptr, nullptr, L"/Game/Zlueprints/Gimmick/g_MirrorStaticBase.g_MirrorStaticBase_C");
        for (auto* mirror : find_live_actors_of_class(pawn, mirror_class, 64))
        {
            auto* trigger = try_read_reflected_object_property_value(mirror, L"ActivateTrigger");
            auto* in_range = mirror->GetPropertyByNameInChain(L"temp_InBound");
            if (!is_uobject_valid_fast(trigger) || !in_range || !in_range->IsA<FBoolProperty>() ||
                static_cast<FBoolProperty*>(in_range)->GetPropertyValueInContainer(mirror))
            {
                continue;
            }
            ReflectedFunctionCall overlap(trigger, L"IsOverlappingActor", "kh02_combat_swap", 256);
            overlap.object(L"Other", pawn);
            overlap.run();
            if (overlap.boolean_result())
            {
                // The departing pawn can end its overlap after the incoming pawn entered.
                // Let the original handler restore capture and notify the puzzle platforms.
                ReflectedFunctionCall enter(mirror,
                                            L"BndEvt__ActivateTrigger_K2Node_ComponentBoundEvent_19_"
                                            L"ComponentBeginOverlapSignature__DelegateSignature",
                                            "kh02_combat_swap", 256);
                enter.object(L"OverlappedComponent", trigger);
                enter.object(L"OtherActor", pawn);
                enter.object(L"OtherComp", try_read_reflected_object_property_value(pawn, L"CapsuleComponent"));
                enter.run();
            }
        }
    }
} // namespace kh3ap
