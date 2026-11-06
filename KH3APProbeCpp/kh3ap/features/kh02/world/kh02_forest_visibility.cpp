#include "kh02_forest_visibility.h"
#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/bridge/utilities.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/TArray.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#include <stdexcept>

namespace kh3ap
{
    using namespace RC::Unreal;
    using Json = nlohmann::json;

    namespace
    {
        constexpr std::size_t reflected_parameter_limit_bytes = 2'048;
        constexpr std::size_t event_history_limit = 64;

        struct ReflectedCall : Kh02ReflectedCall
        {
            ReflectedCall(UObject* owner, const wchar_t* method)
                : Kh02ReflectedCall(owner, method, "kh02_story", reflected_parameter_limit_bytes) 
            {
            }
        };

        auto flag(UObject* object, const wchar_t* name) -> bool
        {
            auto* property = is_uobject_valid_fast(object) ? object->GetPropertyByNameInChain(name) : nullptr;
            if (!property || !property->IsA<FBoolProperty>())
            {
                throw std::runtime_error("kh02_story_bool");
            }
            return static_cast<FBoolProperty*>(property)->GetPropertyValueInContainer(object);
        }

        auto path(UObject* object) -> std::string
        {
            return is_uobject_valid_fast(object) ? to_utf8(object->GetPathName()) : "";
        }

        constexpr std::size_t forest_lod_volume_limit = 64;
        constexpr int interaction_gimmick_limit = 256;
    } // namespace

    auto Kh02ForestVisibility::watch(UObject* actor) -> std::optional<std::string>
    {
        if (!is_uobject_valid_fast(actor) ||
            !actor->GetPathName().starts_with(L"/Game/Zevels/dw/dw_08/umap/dw_08_gimmick_thorn_") ||
            actor->GetClassPrivate()->GetPathName() !=
                L"/Game/Zlueprints/Level/lv_LODTriggerVolume.lv_LODTriggerVolume_C")
        {
            return std::nullopt;
        }
        for (auto& volume : forest_lod_volumes)
        {
            if (volume.actor.Get() == actor)
            {
                volume.dirty = true;
                forest_lod_pending = true;
                return std::nullopt;
            }
        }
        if (forest_lod_volumes.size() >= forest_lod_volume_limit)
        {
            return "kh02_forest_lod_volume_limit";
        }
        forest_lod_volumes.push_back({make_uobject_weak(actor)});
        forest_lod_pending = true;
        return std::nullopt;
    }

    auto Kh02ForestVisibility::tick(UObject* pawn, UObject* world, bool ready) -> std::optional<std::string>
    {
        if (forest_lod_volumes.empty() || !ready || !is_uobject_valid_fast(pawn) || !is_uobject_valid_fast(world) ||
            kh02_actor_world(pawn) != world ||
            world->GetPathName() != L"/Game/Maps/APKH02Mirrors/APKH02Forest.APKH02Forest")
        {
            return std::nullopt;
        }
        if (forest_lod_player.Get() != pawn)
        {
            forest_lod_player = make_uobject_weak(pawn);
            for (auto& volume : forest_lod_volumes)
            {
                volume.dirty = true;
            }
            forest_lod_pending = true;
        }
        if (!forest_lod_pending)
        {
            return std::nullopt;
        }
        forest_lod_pending = false;
        std::optional<std::string> failure;
        for (auto& volume : forest_lod_volumes)
        {
            if (!volume.dirty)
            {
                continue;
            }
            auto* actor = volume.actor.Get();
            if (!is_uobject_valid_fast(actor) || kh02_actor_world(actor) != world)
            {
                volume.dirty = false;
                continue;
            }
            try
            {
                auto* property = actor->GetPropertyByNameInChain(L"LODGimmicks");
                if (!property || !property->IsA<FArrayProperty>())
                {
                    throw std::runtime_error("kh02_forest_lod_gimmick_array");
                }
                auto* gimmicks = property->ContainerPtrToValuePtr<TArray<UObject*>>(actor);
                if (gimmicks->Num() > interaction_gimmick_limit)
                {
                    throw std::runtime_error("kh02_forest_lod_gimmick_limit");
                }
                bool initialized = true;
                for (auto* gimmick : *gimmicks)
                {
                    if (is_uobject_valid_fast(gimmick) && gimmick->GetPropertyByNameInChain(L"temp_IsBeginPlayed") &&
                        !flag(gimmick, L"temp_IsBeginPlayed"))
                    {
                        initialized = false;
                    }
                }
                if (!initialized)
                {
                    forest_lod_pending = true;
                    continue;
                }
                volume.dirty = false;
                if (!flag(actor, L"Enable") || !flag(actor, L"bActorEnableCollision"))
                {
                    continue;
                }
                ReflectedCall overlap(actor, L"IsOverlappingActor");
                overlap.object(L"Other", pawn);
                overlap.run();
                const bool shown = overlap.boolean_result();
                // Streaming can deliver overlap before the linked vines'
                // BeginPlay resets their LOD state. Character replacement
                // can also end the old pawn's overlap after the new one enters.
                // Reconcile once after those events, using real collision and
                // the source visibility functions, never starting a vine.
                for (auto* gimmick : *gimmicks)
                {
                    if (!is_uobject_valid_fast(gimmick) || kh02_actor_world(gimmick) != world ||
                        !gimmick->GetPropertyByNameInChain(L"temp_IsShowLODGimmick") ||
                        flag(gimmick, L"temp_IsShowLODGimmick") == shown)
                    {
                        continue;
                    }
                    ReflectedCall visibility(gimmick, shown ? L"ShowLODGimmick" : L"HideLODGimmick");
                    visibility.run();
                    auto& events = reconciliation_events;
                    if (events.size() < event_history_limit)
                    {
                        events.push_back({{"volume", path(actor)},
                                          {"gimmick", path(gimmick)},
                                          {"player", path(pawn)},
                                          {"shown", shown}});
                    }
                }
            }
            catch (const std::exception& error)
            {
                volume.dirty = false;
                failure = error.what();
            }
        }
        return failure;
    }

    auto Kh02ForestVisibility::events() const -> const Json&
    {
        return reconciliation_events;
    }

    void Kh02ForestVisibility::clear_world()
    {
        forest_lod_volumes.clear();
        forest_lod_player = {};
        forest_lod_pending = false;
        reconciliation_events = Json::array();
    }
} // namespace kh3ap
