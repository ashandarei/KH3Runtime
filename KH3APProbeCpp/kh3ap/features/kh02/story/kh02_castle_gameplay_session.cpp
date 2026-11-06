#include "kh02_castle_gameplay_session.h"
#include "kh02_entry_session.h"
#include "kh02_story_flags.h"
#include "kh02_source_level_readiness.h"
#include "kh02_map_session.h"
#include "kh3ap/features/kh02/cutscenes/kh02_event_aqua.h"
#include "kh3ap/features/kh02/cutscenes/kh02_cutscene_reflection.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include "kh3ap/features/kh02/world/kh02_castle_boss_streams.h"
#include "kh3ap/features/kh02/world/kh02_castle_scenery.h"
#include "kh3ap/features/kh02/world/kh02_castle_entry_dispatch.h"
#include "kh3ap/features/kh02/world/kh02_castle_save_notice.h"
#include "kh3ap/features/kh02/diagnostics/kh02_runtime_inspection.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include <Unreal/UObject.hpp>
#include <Unreal/World.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#include <Unreal/Property/FMulticastDelegateProperty.hpp>
#define NOMINMAX
#include <Windows.h>

namespace kh3ap
{
    using namespace RC::Unreal;
    using namespace kh02cutscene_detail;

    namespace
    {
        constexpr int castle_gameplay_stream_begin = 18;
        constexpr int castle_gameplay_stream_end = 22;
        constexpr int maximum_host_stream_count = 64;
        constexpr std::size_t castle_ground_stream_count = 6;

    } // namespace

    void Kh02CastleGameplaySession::begin(UObject* world, Kh02EntrySession& entry, Kh02GameplayBinding& gameplay)
    {
        entry.initialized = false;
        gameplay.world = make_uobject_weak(world);
        gameplay.script = {};
        gimmick_script = {};
        streams.clear();
        gameplay.started = false;
        armed = false;

        entry.begin_preparation(std::chrono::steady_clock::now());
        proof["host_begin_play"] = true;
        proof["begin_play_at_ms"] = GetTickCount64();
        if (retry)
        {
            proof["retry_host_replaced"] = true;
        }
        ReflectedCall closed(kh02_require_loaded_object(L"/Script/TresGame.Default__TresGameBlueprintLibrary"),
                             L"TresUI_IsClosedLoadingScreen");
        closed.run();
        proof["closed_at_begin_play"] = closed.boolean_result();

        struct NativeStreamArray
        {
            UObject** data;
            int32_t count, capacity;
        };

        auto* property = world->GetPropertyByNameInChain(L"StreamingLevels");
        if (!property || !property->IsA<FArrayProperty>() || property->GetElementSize() != sizeof(NativeStreamArray))
        {
            throw std::runtime_error("kh02_entry_stream_array");
        }
        auto* array = property->ContainerPtrToValuePtr<NativeStreamArray>(world);
        if (!array->data || array->count < castle_gameplay_stream_end || array->count > maximum_host_stream_count)
        {
            throw std::runtime_error("kh02_entry_stream_count");
        }
        for (int index = castle_gameplay_stream_begin; index < castle_gameplay_stream_end; ++index)
        {
            auto* stream = array->data[index];
            if (!is_uobject_valid_fast(stream) || stream->GetOuterPrivate() != world)
            {
                throw std::runtime_error("kh02_entry_stream_owner");
            }
            streams.push_back(make_uobject_weak(stream));
            auto* shown = stream->GetPropertyByNameInChain(L"OnLevelShown");
            if (!shown || !shown->IsA<FMulticastDelegateProperty>() ||
                shown->GetElementSize() != sizeof(FMulticastScriptDelegate))
            {
                throw std::runtime_error("kh02_entry_shown_delegate");
            }
            shown->ContainerPtrToValuePtr<FMulticastScriptDelegate>(stream)->InvocationList.Add(
                FScriptDelegate{make_uobject_weak(stream), FName(L"KH02_EntryLevelsVisible", FNAME_Add)});
        }
    }

    auto Kh02CastleGameplaySession::finish(Services services) -> bool
    {
        if (services.entry.initialized)
        {
            return true;
        }
        auto* world = services.gameplay.world.Get();
        if (!kh02_castle_source_levels_ready(world, streams, retry, services.entry.first_castle_intro,
                                             {services.gameplay.script, gimmick_script, services.map_session}, proof))
        {
            return false;
        }
        auto* script = services.gameplay.script.Get();
        auto* gimmick = gimmick_script.Get();
        ReflectedCall local(kh02_require_loaded_object(L"/Script/TresGame.Default__TresGameBlueprintLibrary"),
                            L"GetLocalTresPlayerPawnBase");
        local.object(L"WorldContextObject", script);
        local.run();
        auto* pawn = local.result();
        if (is_uobject_valid_fast(pawn) && kh02_actor_world(pawn) == world)
        {
            services.gameplay.pawn = make_uobject_weak(pawn);
        }
        if (!services.event_aqua.prepare(script))
        {
            return false;
        }
        if (retry)
        {
            services.map_session.observe_nonlatent_arrival_fade();
        }
        const bool boss_streams_required = kh02_castle_boss_streams_required(
            services.source_flags.value(L"gameflow_DW"), services.source_flags.value(L"gameflow_DW_GEAR_GETCOUNT"));
        if (boss_streams_required &&
            !kh02_prepare_castle_boss_streams(world, services.source_flags, services.encounter_proof))
        {
            return false;
        }
        proof["boss_streams_ready"] = boss_streams_required;
        ReflectedCall hide(gimmick, L"dw_02_gameflow_REvt_hideGear");
        hide.run();
        kh02_restore_castle_scenery(script, gimmick, services.source_flags, backtrack, proof);
        if (retry)
        {
            ReflectedCall gear_state(gimmick, L"dw_02_gameflow_REvt_CheckGear");
            gear_state.run();
        }
        kh02_dispatch_castle_entry(script, services.entry.first_castle_intro, retry, backtrack,
                                   services.map_session.active() && services.map_session.world() == world, proof);
        ReflectedCall hidden(services.event_aqua.actor(), L"SetActorHiddenInGame");
        hidden.boolean(L"bNewHidden", true);
        hidden.run();
        const auto grounds = kh02_ground_streams(world);
        if (grounds.size() != castle_ground_stream_count)
        {
            throw std::runtime_error("kh02_entry_ground_count");
        }
        if (!backtrack)
        {
            for (const auto& ground : grounds)
            {
                if (!ground["loaded"].get<bool>() || !ground["visible"].get<bool>())
                {
                    throw std::runtime_error("kh02_entry_bridge_not_formed");
                }
            }
        }
        auto* save = kh02_disable_castle_save_notice(world, script, proof);
        path_trigger.configure(pawn, true);
        proof["castle_path_trigger"] = path_trigger.snapshot();
        proof["save_notice_disabled"] = true;
        ReflectedCall closed(kh02_require_loaded_object(L"/Script/TresGame.Default__TresGameBlueprintLibrary"),
                             L"TresUI_IsClosedLoadingScreen");
        closed.run();
        proof["closed_before_release"] = closed.boolean_result();
        proof["save_point"] = path(save);
        proof["ready_at_ms"] = GetTickCount64();
        proof["gameplay_levels"] = streams.size();
        proof["bridge_formed"] = true;
        if (closed.boolean_result() && !retry && !services.map_session.active())
        {
            throw std::runtime_error("kh02_entry_levels_ready_after_loading_closed");
        }
        if (retry)
        {
            services.encounter_proof["retry_initialized"] = true;
            services.encounter_proof["retry_source_event"] = "dw_02_Persistent_REvt_dw_02LevelStart_DevilTower_Retry";
        }
        services.entry.initialized = services.gameplay.started = true;
        loading_held = false;
        // Gameplay assets and the real loading handoff must be ready before
        // DW009 starts. Keep its existing source dispatch, animation binding
        // and fade cleanup; revisits resume their saved story immediately.
        services.entry.preparation_finished();
        proof["first_castle_intro"] = services.entry.first_castle_intro;
        if (services.entry.first_castle_intro)
        {
            services.map_session.source_loading_ready(script);
        }
        return true;
    }
} // namespace kh3ap
