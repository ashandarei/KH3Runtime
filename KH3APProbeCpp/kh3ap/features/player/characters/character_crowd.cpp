#include "kh3ap/features/player/characters/character_crowd.h"
#include "kh3ap/features/player/characters/character_profile.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include "kh3ap/core/objects/actor_discovery.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/AActor.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UObjectGlobals.hpp>

namespace kh3ap::character_crowd
{
    using namespace RC::Unreal;

    void rebind(UObject* pawn)
    {
        // InitializeComponent caches Controller->Pawn once. Map-entry character
        // restoration can replace that pawn after initialization, leaving the
        // weak reference stale. TickComponent then skips all crowd spawning.
        // Refresh only on the existing ownership-commit event, including returns
        // to Sora and travel rebinds. Later streamed crowds initialize normally.
        constexpr size_t player_reference_offset = 0x338;
        constexpr size_t maximum_crowd_actors = 64;
        if (!is_uobject_valid_fast(pawn) || kh02_current_story_world() ||
            kh02_actor_world(pawn) != kh02_current_world() ||
            !character_native::verify_sites({character_native::Site::crowd_player_binding,
                                             character_native::Site::crowd_player_tick}))
        {
            return;
        }
        auto* controller = try_read_reflected_object_property_value(pawn, L"Controller");
        auto* local = is_uobject_valid_fast(controller)
                          ? try_read_reflected_object_property_value(controller, L"Player")
                          : nullptr;
        if (!is_uobject_valid_fast(local) ||
            try_read_reflected_object_property_value(local, L"PlayerController") != controller ||
            try_read_reflected_object_property_value(controller, L"Pawn") != pawn)
        {
            return;
        }
        auto* actor_class = UObjectGlobals::StaticFindObject<UClass*>(
            nullptr, nullptr, L"/Script/TresGame.TresComNpcActionActor");
        auto* component_class = UObjectGlobals::StaticFindObject<UClass*>(
            nullptr, nullptr, L"/Script/TresGame.TresComNpcActionComponent");
        if (!is_uobject_valid_fast(actor_class) || !is_uobject_valid_fast(component_class))
        {
            return;
        }
        const auto player = make_uobject_weak(pawn);
        if (player.Get() != pawn)
        {
            return;
        }
        for (auto* actor : find_live_actors_of_class(pawn, actor_class, maximum_crowd_actors))
        {
            if (kh02_actor_world(actor) != kh02_actor_world(pawn))
            {
                continue;
            }
            auto* component = try_read_reflected_object_property_value(actor, L"MyComNpcAction");
            if (!is_uobject_a_fast(component, component_class) ||
                component->GetOuterPrivate() != actor)
            {
                continue;
            }
            auto& target = *reinterpret_cast<FWeakObjectPtr*>(
                reinterpret_cast<std::byte*>(component) + player_reference_offset);
            if (target.Get() != pawn)
            {
                target = player;
                RC::Output::send<RC::LogLevel::Normal>(
                    STR("[KH3APProbeCpp] CharacterCrowd rebound component={} pawn={}\n"),
                    component->GetPathName(), pawn->GetPathName());
            }
        }
    }
} // namespace kh3ap::character_crowd
