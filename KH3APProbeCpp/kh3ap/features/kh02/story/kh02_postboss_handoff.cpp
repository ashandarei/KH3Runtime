#include "kh02_postboss_handoff.h"
#include "kh3ap/features/kh02/cutscenes/kh02_cutscene_reflection.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include <nlohmann/json.hpp>

namespace kh3ap
{
    using RC::Unreal::UObject;
    using namespace kh02cutscene_detail;

    void Kh02PostBossHandoff::bind(UObject* actor)
    {
        matinee = make_uobject_weak(actor);
    }

    void Kh02PostBossHandoff::observe()
    {
        if (auto* actor = matinee.Get(); is_uobject_valid_fast(actor))
        {
            state.observe_playing(flag(actor, L"bIsPlaying"));
            state.observe_position(real(actor, L"InterpPosition"));
        }
    }

    void Kh02PostBossHandoff::clear_for_retry()
    {
        matinee = {};
        state.clear_observation();
    }

    auto Kh02PostBossHandoff::dispatch(UObject* current_world, UObject* source_world, nlohmann::json& proof) -> bool
    {
        if (!state.take_request(current_world == source_world))
        {
            return false;
        }
        ReflectedCall travel(kh02_require_loaded_object(L"/Script/TresGame.Default__TresGameBlueprintLibrary"),
                             L"TresMapJump");
        travel.name(L"MapName", L"/Game/Maps/APKH02WorldWithin/APKH02WorldWithin");
        travel.name(L"TargetTag", L"APKH02MirrorStart");
        travel.integer(L"InFadeKind", 0);
        travel.run();
        proof["world_within_travel_dispatched"] = true;
        return true;
    }
} // namespace kh3ap
