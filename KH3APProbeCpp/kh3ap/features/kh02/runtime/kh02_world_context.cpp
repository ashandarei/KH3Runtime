#include "kh02_world_context.h"
#include "kh3ap/features/kh02/core/kh02_world_identity.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/core/reflection/reflected_property_cache.h"
#include <Unreal/UObject.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/AActor.hpp>
#include <Unreal/World.hpp>
#include <Unreal/UObjectGlobals.hpp>

namespace kh3ap
{
    using namespace RC::Unreal;

    namespace
    {
        // Cache only immutable layouts. Read viewport/world/possession values
        // again on every call so map travel and factory swaps remain visible.
        thread_local ReflectedPropertyCache<32> context_properties;

        auto context_object(UObject* owner, const wchar_t* name) -> UObject*
        {
            return context_properties.read_object(owner, name);
        }
    }

    auto kh02_actor_world(UObject* actor) -> UObject*
    {
        if (!is_uobject_valid_fast(actor))
        {
            return nullptr;
        }
        // Streamed source actors retain their package World as an Outer. The
        // engine's level membership is the reflected runtime OwningWorld.
        auto* level = actor->GetOuterPrivate();
        if (auto* owning = context_object(level, L"OwningWorld");
            is_uobject_valid_fast(owning))
        {
            return owning;
        }
        return static_cast<AActor*>(actor)->GetWorld();
    }

    // Private story presentation follows the possessed hero. The AP grant resolver
    // continues to require its independently validated Sora context.
    auto kh02_story_player(UObject* fallback) -> UObject*
    {
        auto* viewport = kh02_current_viewport();
        auto* world = context_object(viewport, L"World");
        auto* controller = context_object(fallback, L"Controller");
        if (is_uobject_valid_fast(fallback) && is_uobject_valid_fast(controller) &&
            context_object(controller, L"Pawn") == fallback &&
            (!is_uobject_valid_fast(world) || kh02_actor_world(fallback) == world))
        {
            return fallback;
        }
        if (kh02_world_kind(world) != Kh02WorldKind::story)
        {
            return is_uobject_valid_fast(fallback) && kh02_actor_world(fallback) == world ? fallback : nullptr;
        }
        auto* instance = context_object(viewport, L"GameInstance");
        auto* manager = context_object(instance, L"m_UIManager");
        controller = context_object(manager, L"m_pPlayerController");
        auto* pawn = context_object(controller, L"Pawn");
        if (!is_uobject_valid_fast(pawn) || kh02_actor_world(pawn) != world ||
            context_object(pawn, L"Controller") != controller)
        {
            return nullptr;
        }
        auto* id = context_properties.resolve(pawn, L"m_PlayerUniqueID");
        if (!id || id->GetElementSize() != 1)
        {
            return nullptr;
        }
        constexpr uint8_t sora_player_id = 0;
        constexpr uint8_t aqua_player_id = 10;
        const auto value = *id->ContainerPtrToValuePtr<uint8_t>(pawn);
        return value == sora_player_id || value == aqua_player_id ? pawn : nullptr;
    }

    auto kh02_current_viewport() -> UObject*
    {
        static FWeakObjectPtr cached_engine;
        auto* engine = cached_engine.Get();
        if (!is_uobject_valid_fast(engine))
        {
            engine =
                UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, L"/Engine/Transient.TresGameEngine_0");
            cached_engine = make_uobject_weak(engine);
        }
        return context_object(engine, L"GameViewport");
    }

    auto kh02_current_world() -> UObject*
    {
        auto* world = context_object(kh02_current_viewport(), L"World");
        return is_uobject_valid_fast(world) ? world : nullptr;
    }

    auto kh02_world_kind(UObject* world) -> Kh02WorldKind
    {
        if (!is_uobject_valid_fast(world))
        {
            return Kh02WorldKind::unavailable;
        }
        static FWeakObjectPtr classified_world;
        static Kh02WorldKind kind{};
        if (classified_world.Get() != world)
        {
            classified_world = make_uobject_weak(world);
            const auto name = world->GetPathName();
            if (kh02_world_identity::mirror_room(name) ||
                name == L"/Game/Maps/APKH02CastleTown/APKH02CastleTown.APKH02CastleTown" ||
                name == L"/Game/Maps/APKH02WorldWithin/APKH02WorldWithin.APKH02WorldWithin")
            {
                kind = Kh02WorldKind::story;
            }
            else if (name == L"/Game/Maps/GardenOfAssemblage/umap/GoA_01.GoA_01")
            {
                kind = Kh02WorldKind::portal;
            }
            else
            {
                kind = Kh02WorldKind::other;
            }
        }
        return kind;
    }

    auto kh02_current_story_world() -> UObject*
    {
        auto* world = kh02_current_world();
        return kh02_world_kind(world) == Kh02WorldKind::story ? world : nullptr;
    }
} // namespace kh3ap
