#include "kh02_collapse_preparation.h"
#include "kh02_cutscene_session.h"
#include "kh3ap/features/kh02/movies/kh02_movie_bridge.h"
#include "kh02_matinee_binding.h"
#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/actor_discovery.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include <Unreal/UObject.hpp>
#include <Unreal/AActor.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UObjectGlobals.hpp>

namespace kh3ap
{
    using namespace RC::Unreal;

    namespace
    {
        constexpr int class_actor_scan_limit = 256;
        constexpr std::size_t reflected_parameter_limit_bytes = 2'048;

        struct ReflectedCall : Kh02ReflectedCall
        {
            ReflectedCall(UObject* owner, const wchar_t* method)
                : Kh02ReflectedCall(owner, method, "kh02_story", reflected_parameter_limit_bytes)
            {
            }
        };
    } // namespace

    void kh02_prepare_collapse(UObject* context, UObject* world, UObject* event_aqua, Kh02CutsceneSession& cutscenes,
                               Kh02MovieBridge& movie_bridge, nlohmann::json& loading_proof)
    {
        if (!is_uobject_valid_fast(context) || !is_uobject_valid_fast(event_aqua))
        {
            throw std::runtime_error("kh02_collapse_aqua_not_prepared");
        }
        auto* matinee_class =
            UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, L"/Script/Engine.MatineeActor");
        UObject* match = nullptr;
        for (auto* actor : find_live_actors_of_class(context, matinee_class, class_actor_scan_limit))
        {
            if (actor->GetName() == L"dw012_1" &&
                actor->GetPathName().starts_with(L"/Game/Zutscene/Localization/en/dw/cutscene_dw02_dw012."))
            {
                if (match)
                {
                    throw std::runtime_error("kh02_collapse_ambiguous");
                }
                match = actor;
            }
        }
        if (!is_uobject_valid_fast(match) || kh02_actor_world(match) != world)
        {
            throw std::runtime_error("kh02_collapse_matinee_missing");
        }
        auto** slot = kh02_matinee_actor_slot(match, L"TresInterpGroup_0");
        if (*slot && *slot != event_aqua)
        {
            throw std::runtime_error("kh02_collapse_aqua_slot_occupied");
        }
        kh02_bind_event_aqua(match, event_aqua, L"TresInterpGroup_0");
        cutscenes.track_event_aqua(match, event_aqua);
        auto* script = try_read_reflected_object_property_value(match->GetOuterPrivate(), L"LevelScriptActor");
        if (!is_uobject_valid_fast(script))
        {
            throw std::runtime_error("kh02_collapse_script_missing");
        }
        cutscenes.bind_collapse(match, script);
        ReflectedCall visible(event_aqua, L"SetActorHiddenInGame");
        visible.boolean(L"bNewHidden", false);
        visible.run();
        // The first clip is deterministic. Decode/upload it while the original
        // 94-second collapse plays, without showing it or advancing any graph.
        // The real gear request later binds its actual source callback.
        try
        {
            auto* pawn = context;
            if (!is_uobject_valid_fast(pawn) || kh02_actor_world(pawn) != world)
            {
                throw std::runtime_error("kh02_prefetch_pawn");
            }
            movie_bridge.prewarm(pawn, L"Rebellis02/gear_a.usm");
            loading_proof["first_movie_prefetch"] = true;
        }
        catch (const std::exception& e)
        {
            loading_proof["first_movie_prefetch_error"] = e.what();
        }
    }
} // namespace kh3ap
