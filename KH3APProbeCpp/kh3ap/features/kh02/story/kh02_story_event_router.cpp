#include "kh02_story_event_router.h"
#include "kh02_mirror_gameplay_session.h"
#include "kh02_story_targets.h"
#include "kh02_map_session.h"
#include "kh3ap/features/kh02/world/kh02_forest_vines.h"
#include "kh3ap/features/kh02/world/kh02_doors.h"
#include "kh3ap/features/kh02/core/kh02_world_identity.h"
#include "kh3ap/features/kh02/cutscenes/kh02_cutscene_reflection.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/Hooks.hpp>
#include <array>
#include <algorithm>
#include <utility>
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
#include "kh3ap/features/kh02/diagnostics/kh02_mirror_transition_diagnostics.h"
#endif

namespace kh3ap
{
    using namespace RC::Unreal;
    using namespace kh02cutscene_detail;

    struct Kh02StoryEventRouter::Impl
    {
        inline static Impl* active{};
        inline static bool listener_registered{};
        Kh02MirrorGameplaySession& mirror;
        Kh02StoryTargets& story_targets;
        Kh02MapSession& map_session;
        std::string& compat_error;
        Callbacks callbacks;
        const FName begin{L"ReceiveBeginPlay", FNAME_Add};
        const FName corridor_execution{L"ExecuteUbergraph_dw_05_gameflow", FNAME_Add};
        const FName forest_resume_start{L"dw_08_Persistent_REvt_dw_08LevelStart_dw024", FNAME_Add};
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        const FName hub_execution{L"ExecuteUbergraph_dw_03_gameflow", FNAME_Add};
        const std::array<FName, 8> hub_mirror_events{
            FName(L"dw_03_gimmick_RGim_MirrorGoBattle01_Touch", FNAME_Add),
            FName(L"dw_03_gimmick_RGim_MirrorGoBattle02_Touch", FNAME_Add),
            FName(L"dw_03_gimmick_RGim_MirrorGoBattle03_Touch", FNAME_Add),
            FName(L"dw_03_gimmick_RGim_MirrorGoBattle04_Touch", FNAME_Add),
            FName(L"dw_03_gimmick_BattleArea_RGim_MirrorBattle1_Touch", FNAME_Add),
            FName(L"dw_03_gimmick_BattleArea_RGim_MirrorBattle2_Touch", FNAME_Add),
            FName(L"dw_03_gimmick_BattleArea_RGim_MirrorBattle3_Touch", FNAME_Add),
            FName(L"dw_03_gimmick_BattleArea_RGim_MirrorBattle4_Touch", FNAME_Add)};
#endif
        const std::array<FName, 2> entrance_end_events{
            FName(L"cutscene_dw03_dw016_Evt_dw_03_gameflow", FNAME_Add),
            FName(L"cutscene_dw03_dw016_Evt_dw_03_gameflow_skip", FNAME_Add)};
        const std::array<FName, 2> overlap_events{FName(L"ReceiveActorBeginOverlap", FNAME_Add),
                                                  FName(L"ReceiveActorEndOverlap", FNAME_Add)};
        const std::array<FName, 4> forest_end_events{FName(L"cutscene_dw08_dw021_Evt_dw_08_gameflow", FNAME_Add),
                                                     FName(L"cutscene_dw08_dw022_Evt_dw_08_gameflow", FNAME_Add),
                                                     FName(L"cutscene_dw08_dw023_Evt_dw_08_gameflow", FNAME_Add),
                                                     FName(L"cutscene_dw08_dw024_Evt_dw_08_gameflow", FNAME_Add)};
        const std::array<FName, 4> target_events{
            FName(L"OnSetActorHiddenInGame", FNAME_Add), FName(L"BPEV_OnShowGimmickForPlan", FNAME_Add),
            FName(L"BPEV_OnHideGimmickForPlan", FNAME_Add), FName(L"OnTresBeginPlayGimmick", FNAME_Add)};

        Impl(Kh02MirrorGameplaySession& session, Kh02StoryTargets& targets, Kh02MapSession& maps,
             std::string& compatibility_error, Callbacks routes)
            : mirror(session), story_targets(targets), map_session(maps), compat_error(compatibility_error),
              callbacks(std::move(routes))
        {
        }

        void route(UObject* object, UFunction* function, void* parameters)
        {
            if (!is_uobject_valid_fast(function))
            {
                return;
            }
            const auto event_name = function->GetNamePrivate();
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            const bool hub_touch =
                std::find(hub_mirror_events.begin(), hub_mirror_events.end(), event_name) != hub_mirror_events.end();
            if ((event_name == hub_execution || hub_touch) &&
                is_uobject_valid_fast(object) && kh02_actor_world(object) == mirror.world.Get() &&
                object->GetPathName().starts_with(L"/Game/Zevels/dw/dw_03/umap/dw_03_gameflow."))
            {
                if (hub_touch)
                {
                    kh02_mirror_transition_diagnostics::observe(mirror.world.Get(), object, -1, "mirror_touch",
                                                                to_utf8(event_name.ToString()));
                }
                else if (parameters && function->GetParmsSize() == sizeof(int32_t))
                {
                    const auto linkage = read_function_int_param(function, parameters, L"EntryPoint");
                    if (linkage)
                    {
                        kh02_mirror_transition_diagnostics::observe(mirror.world.Get(), object,
                                                                    static_cast<int32_t>(*linkage), "continuation");
                    }
                }
            }
#endif
            const bool begin_play = event_name == begin;
            const bool corridor_resume = event_name == corridor_execution;
            const bool forest_resume = event_name == forest_resume_start;
            const bool overlap_changed =
                std::find(overlap_events.begin(), overlap_events.end(), event_name) != overlap_events.end();
            const bool forest_end =
                std::find(forest_end_events.begin(), forest_end_events.end(), event_name) != forest_end_events.end();
            const bool entrance_end = std::find(entrance_end_events.begin(), entrance_end_events.end(), event_name) !=
                                      entrance_end_events.end();
            const bool target_changed =
                std::find(target_events.begin(), target_events.end(), event_name) != target_events.end();
            // ProcessEvent also delivers animation, UI and tick events.
            // Reuse this listener for target notifications. Only mark
            // dirty here; inspect final state after the call returns.
            if ((!begin_play && !overlap_changed && !forest_end && !entrance_end && !target_changed &&
                 !corridor_resume && !forest_resume) ||
                !is_uobject_valid_fast(object))
            {
                return;
            }
            if (forest_resume)
            {
                auto* world = kh02_actor_world(object);
                if (is_uobject_valid_fast(world) && world == mirror.world.Get() && map_session.active() &&
                    world->GetPathName() == L"/Game/Maps/APKH02Mirrors/APKH02Forest.APKH02Forest" &&
                    object->GetPathName().starts_with(L"/Game/Zevels/dw/dw_08/umap/dw_08_gameflow.") &&
                    object->GetClassPrivate()->GetName() == L"dw_08_gameflow_C")
                {
                    // This source resume branch enters DW024 directly and never
                    // calls TresUI_CloseLoadingScreen. Arm the normal readiness
                    // checks; streams, source fade, native loading and character
                    // restoration must still finish before our cover releases.
                    map_session.source_loading_ready(object);
                    map_session.proof()["source_resume_event"] = to_utf8(function->GetName());
                }
                return;
            }
            if (corridor_resume)
            {
                if (parameters && function->GetParmsSize() == sizeof(int32_t) &&
                    object->GetPathName().starts_with(L"/Game/Zevels/dw/dw_05/umap/dw_05_gameflow.") &&
                    kh02_actor_world(object) == mirror.world.Get())
                {
                    const auto linkage = read_function_int_param(function, parameters, L"EntryPoint");
                    // Exact native mapset continuations already audited by the
                    // Corridor compatibility adapter. Reconcile after their
                    // source VM stack returns, once the new layout is applied.
                    if (linkage && (*linkage == 677 || *linkage == 2204 || *linkage == 4846))
                    {
                        callbacks.chest_mapset_finished(object);
                    }
                }
                return;
            }
            if (entrance_end && object->GetPathName().starts_with(L"/Game/Zevels/dw/dw_03/umap/dw_03_gameflow."))
            {
                mirror.source_entrance_finished(object);
            }
            if (begin_play || overlap_changed)
            {
                if (auto error = mirror.forest_visibility.watch(object))
                {
                    mirror.error = std::move(*error);
                }
            }
            if (target_changed)
            {
                story_targets.request_sync(object);
                // Flips can reset a retained chest without actor BeginPlay.
                // Reapply its recorded pose after the source event returns.
                callbacks.chest_opened(object);
                return;
            }
            if (forest_end && object->GetPathName().starts_with(L"/Game/Zevels/dw/dw_08/umap/dw_08_gameflow.") &&
                object->GetClassPrivate()->GetName() == L"dw_08_gameflow_C")
            {
                const auto name = function->GetName();
                for (const auto* scene : {L"dw021", L"dw022", L"dw023", L"dw024"})
                {
                    if (name != std::wstring(L"cutscene_dw08_") + scene + L"_Evt_dw_08_gameflow")
                    {
                        continue;
                    }
                    auto* world = kh02_actor_world(object);
                    if (is_uobject_valid_fast(world) && world == mirror.world.Get() &&
                        world->GetPathName() == L"/Game/Maps/APKH02Mirrors/APKH02Forest.APKH02Forest")
                    {
                        mirror.forest_handoff.enqueue(world, std::wstring(L"dw_08_Lv_Skip_") + scene);
                        mirror.forest_story_proof["natural_end_event"] = to_utf8(name);
                    }
                }
            }
            if (!begin_play)
            {
                return;
            }
            if (callbacks.projectile_begin_play)
            {
                callbacks.projectile_begin_play(object);
            }
            // Track script identity before BeginPlay can enqueue a
            // continuation. Retry can replace a streamed script
            // while retaining the same host World and package name.
            if (object->GetPathName().starts_with(L"/Game/Zevels/dw/") &&
                object->GetClassPrivate()->GetName().ends_with(L"_gameflow_C"))
            {
                map_session.bind_source_script(object);
            }
            story_targets.watch(object);
            callbacks.chest_opened(object);
            if (object->GetClassPrivate()->GetPathName() ==
                    L"/Game/Zlueprints/Gimmick/TresPlanHitCount.TresPlanHitCount_C" &&
                object->GetPathName().starts_with(L"/Game/Zevels/dw/dw_08/umap/dw_08_gimmick_thorn_"))
            {
                try
                {
                    kh02_configure_forest_vine_damage(object);
                }
                catch (const std::exception& e)
                {
                    mirror.error = e.what();
                }
            }
            // Native travel boxes bypass the Blueprint jump adapter. Their
            // FNames may serialize the final numeric suffix separately, so
            // rewrite the verified live property before overlap can travel.
            try
            {
                if (kh02_remap_native_door(object))
                {
                    return;
                }
            }
            catch (const std::exception& error)
            {
                compat_error = error.what();
                ReflectedCall disable(object, L"SetActorEnableCollision");
                disable.boolean(L"bNewActorEnableCollision", false);
                disable.run();
                return;
            }
            callbacks.host_begin_play(object);
        }

        void install()
        {
            active = this;
            if (listener_registered)
            {
                return;
            }
            Hook::RegisterProcessEventPreCallback(
                [](UObject* object, UFunction* function, void* parameters)
                {
                    if (auto* router = active)
                    {
                        router->route(object, function, parameters);
                    }
                });
            listener_registered = true;
        }
    };

    Kh02StoryEventRouter::Kh02StoryEventRouter(Kh02MirrorGameplaySession& mirror, Kh02StoryTargets& targets,
                                               Kh02MapSession& maps, std::string& compatibility_error,
                                               Callbacks callbacks)
        : impl(std::make_unique<Impl>(mirror, targets, maps, compatibility_error, std::move(callbacks)))
    {
    }

    Kh02StoryEventRouter::~Kh02StoryEventRouter()
    {
        if (Impl::active == impl.get())
        {
            Impl::active = nullptr;
        }
    }

    void Kh02StoryEventRouter::install()
    {
        impl->install();
    }
} // namespace kh3ap
