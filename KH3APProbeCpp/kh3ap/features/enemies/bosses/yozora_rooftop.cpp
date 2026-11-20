#include "kh3ap/features/enemies/bosses/binding.h"
#include "kh3ap/features/enemies/bosses/encounter_repairs.h"
#include "kh3ap/core/objects/actor_discovery.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/core/objects/actor_owning_world.h"
#include "kh3ap/features/kh02/cutscenes/kh02_cutscene_reflection.h"
#include "kh3ap/features/kh02/cutscenes/kh02_matinee_binding.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/AActor.hpp>
#include <Unreal/FScriptArray.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#include <Unreal/Property/FStructProperty.hpp>
#include <Unreal/UScriptStruct.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <array>
#include <chrono>
#include <vector>

namespace kh3ap::enemy_remap_detail
{
    namespace
    {
        using namespace kh02cutscene_detail;
        using Point = std::array<float, 3>;
        using Clock = std::chrono::steady_clock;
        constexpr const wchar_t* layers[]{L"/Game/Levels/ss_DLC/ss_01/umap/ss_01_camera_ms010",
                                          L"/Game/Maps/ss_DLC/umap/ss_01/ss_01_Area_Battle",
                                          L"/Game/Maps/ss_DLC/umap/ss_01/ss_01_Area_Near",
                                          L"/Game/Maps/ss_DLC/umap/ss_01/ss_01_Area_Mid",
                                          L"/Game/Maps/ss_DLC/umap/ss_01/ss_01_Area_Far",
                                          L"/Game/Maps/ss_DLC/umap/ss_01/ss_01_Env"};
        enum class Phase
        {
            Idle,
            Preload,
            Standby,
            Entering,
            Playing
        };
        struct HostActor
        {
            FWeakObjectPtr actor;
            bool collision;
        };
        struct Rooftop
        {
            Phase phase{};
            FWeakObjectPtr pawn, world, player, sphere, shot;
            std::array<FWeakObjectPtr, std::size(layers)> streams{};
            Point center{}, player_start{}, boss_start{};
            std::vector<HostActor> host;
            Clock::time_point deadline{}, next_check{};
            bool moved{}, requested_shot{}, translated{};
        };
        Rooftop roof;
        struct RetiredStream
        {
            FWeakObjectPtr stream, world;
        };
        std::vector<RetiredStream> retiring;
        Clock::time_point next_retirement_check;

        AActor* local_player(UObject* context)
        {
            ReflectedFunctionCall local{L"/Script/Engine.Default__GameplayStatics", L"GetPlayerPawn",
                                    "yozora_rooftop", 64};
            local.object(L"WorldContextObject", context);
            local.run();
            return static_cast<AActor*>(local.result());
        }

        Point position(AActor* actor)
        {
            const auto p = actor->K2_GetActorLocation();
            return {static_cast<float>(p.X()), static_cast<float>(p.Y()),
                    static_cast<float>(p.Z())};
        }

        void relocate(AActor* actor, const Point& p)
        {
            FHitResult hit{};
            actor->K2_SetActorLocation(FVector{p[0], p[1], p[2]}, false, hit, true);
        }

        bool stream_state(UObject* stream, const wchar_t* method)
        {
            if (!is_uobject_valid_fast(stream))
            {
                return false;
            }
            ReflectedCall status{stream, method};
            status.run();
            return status.boolean_result();
        }

        void remove_retired_streams()
        {
            if (retiring.empty() || Clock::now() < next_retirement_check)
            {
                return;
            }
            next_retirement_check = Clock::now() + std::chrono::milliseconds(250);
            std::erase_if(retiring,
                          [](const RetiredStream& entry)
                          {
                              auto* stream = entry.stream.Get();
                              auto* world = entry.world.Get();
                              if (!is_uobject_valid_fast(stream) || !is_uobject_valid_fast(world))
                              {
                                  return true;
                              }
                              if (stream_state(stream, L"IsLevelLoaded") ||
                                  stream_state(stream, L"IsStreamingStatePending"))
                              {
                                  return false;
                              }
                              auto* field = world->GetPropertyByNameInChain(L"StreamingLevels");
                              if (field && field->IsA<FArrayProperty>())
                              {
                                  field->ContainerPtrToValuePtr<RC::Unreal::TArray<UObject*>>(world)
                                      ->RemoveSingle(stream);
                              }
                              return true;
                          });
        }

        void finish()
        {
            if (roof.phase == Phase::Idle)
            {
                return;
            }
            auto* pawn = static_cast<AActor*>(roof.pawn.Get());
            auto* player = static_cast<AActor*>(roof.player.Get());
            auto* world = roof.world.Get();
            if (auto* shot = roof.shot.Get();
                is_uobject_valid_fast(shot) && flag(shot, L"bIsPlaying"))
            {
                ReflectedCall{shot, L"Stop"}.run();
            }
            for (const auto& h : roof.host)
            {
                auto* actor = static_cast<AActor*>(h.actor.Get());
                if (!is_uobject_valid_fast(actor))
                {
                    continue;
                }
                if (h.collision)
                {
                    actor->SetActorEnableCollision(true);
                }
                actor->SetActorHiddenInGame(false);
            }
            if (roof.phase == Phase::Entering || roof.phase == Phase::Playing)
            {
                if (is_uobject_valid_fast(pawn) && actor_owning_world(pawn) == world)
                {
                    relocate(pawn, roof.boss_start);
                }
                if (roof.moved && is_uobject_valid_fast(player) &&
                    actor_owning_world(player) == world && local_player(player) == player)
                {
                    relocate(player, roof.player_start);
                }
                Output::send<LogLevel::Normal>(
                    STR("[KH3APProbeCpp] Yozora DM restored host scenery={} player={}\n"),
                    roof.host.size(), roof.moved);
            }
            for (auto& weak : roof.streams)
            {
                if (auto* stream = weak.Get(); is_uobject_valid_fast(stream))
                {
                    set_flag(stream, L"bShouldBeVisible", false);
                    set_flag(stream, L"bShouldBeLoaded", false);
                    retiring.push_back({weak, roof.world});
                }
            }
            roof = {};
        }

        void load(UObject* pawn)
        {
            const auto center = read_boss_arena_center(pawn);
            if (!center)
            {
                throw std::runtime_error("rooftop center unavailable");
            }
            roof.center = *center;
            for (size_t i = 0; i < std::size(layers); ++i)
            {
                ReflectedFunctionCall call{L"/Script/Engine.Default__LevelStreamingKismet",
                                       L"LoadLevelInstance", "yozora_rooftop", 160};
                call.object(L"WorldContextObject", pawn);
                call.string(L"LevelName", layers[i]);
                call.vector(L"Location", roof.center[0], roof.center[1], roof.center[2]);
                call.vector(L"Rotation", 0, 0, 0);
                call.run();
                auto* stream = call.result();
                if (!call.boolean_result(L"bOutSuccess") || !is_uobject_valid_fast(stream))
                {
                    throw std::runtime_error("rooftop stream creation failed");
                }
                roof.streams[i] = make_uobject_weak(stream);
                set_flag(stream, L"bShouldBeVisible", false);
            }
            roof.phase = Phase::Standby;
            Output::send<LogLevel::Normal>(
                STR("[KH3APProbeCpp] Yozora DM rooftop preload requested\n"));
        }

        void hide_host_geometry(UObject* pawn)
        {
            // Keep the host's batched set dressing and ambient effects. The rooftop
            // replaces its solid scenery, while the original arena remains recognizable.
            for (auto* path :
                 {L"/Script/Engine.StaticMeshActor", L"/Script/Landscape.LandscapeProxy",
                  L"/Script/Foliage.InstancedFoliageActor", L"/Script/Engine.SkeletalMeshActor",
                  L"/Script/Engine.ExponentialHeightFog", L"/Script/Engine.AtmosphericFog",
                  L"/Script/Engine.Light"})
            {
                auto* cls = find_loaded_runtime_asset_class(path);
                if (!cls)
                {
                    continue;
                }
                for (auto* actor : find_live_actors_of_class(pawn, cls, 65536))
                {
                    auto* level = actor->GetOuterPrivate();
                    if (!is_uobject_valid_fast(level))
                    {
                        continue;
                    }
                    const auto package = level->GetPathName();
                    if (package.find(L"/ss_01_") != std::wstring::npos ||
                        package.find(L"_enemy_") != std::wstring::npos ||
                        read_boss_owner_property(actor, L"Owner") || flag(actor, L"bHidden"))
                    {
                        continue;
                    }
                    const bool collision = actor->GetActorEnableCollision();
                    roof.host.push_back({make_uobject_weak(actor), collision});
                    actor->SetActorHiddenInGame(true);
                    if (collision)
                    {
                        actor->SetActorEnableCollision(false);
                    }
                }
            }
        }

        void translate_shot(UObject* shot)
        {
            // Level streaming moves scene actors, while Matinee's absolute position
            // curves retain their cooked coordinates. Edit only this instance's keys.
            auto* data = read_boss_owner_property(shot, L"MatineeData");
            if (!data || data->GetOuterPrivate() != shot)
            {
                throw std::runtime_error("rooftop shot data ownership");
            }
            using Objects = RC::Unreal::TArray<UObject*>;
            auto children = [](UObject* owner, const wchar_t* name) -> Objects&
            {
                auto* field = owner->GetPropertyByNameInChain(name);
                if (!field || !field->IsA<FArrayProperty>())
                {
                    throw std::runtime_error("rooftop shot array");
                }
                return *field->ContainerPtrToValuePtr<Objects>(owner);
            };
            std::vector<Point*> keys;
            for (auto* group : children(data, L"InterpGroups"))
            {
                if (!is_uobject_valid_fast(group) || group->GetOuterPrivate() != data)
                {
                    continue;
                }
                for (auto* track : children(group, L"InterpTracks"))
                {
                    if (!is_uobject_valid_fast(track) || track->GetOuterPrivate() != group ||
                        track->GetClassPrivate()->GetName() != L"InterpTrackMove")
                    {
                        continue;
                    }
                    auto* curve = track->GetPropertyByNameInChain(L"PosTrack");
                    if (!curve || !curve->IsA<FStructProperty>())
                    {
                        throw std::runtime_error("rooftop position curve");
                    }
                    auto* points = find_struct_field_by_name(
                        static_cast<FStructProperty*>(curve)->GetStruct(), L"Points");
                    if (!points || !points->IsA<FArrayProperty>())
                    {
                        throw std::runtime_error("rooftop key array");
                    }
                    auto* inner = static_cast<FArrayProperty*>(points)->GetInner();
                    if (!inner || !inner->IsA<FStructProperty>())
                    {
                        throw std::runtime_error("rooftop key type");
                    }
                    auto* output = find_struct_field_by_name(
                        static_cast<FStructProperty*>(const_cast<FProperty*>(inner))->GetStruct(),
                        L"OutVal");
                    if (!output || !output->IsA<FStructProperty>() ||
                        output->GetElementSize() != sizeof(Point))
                    {
                        throw std::runtime_error("rooftop key position");
                    }
                    auto* array = points->ContainerPtrToValuePtr<FScriptArray>(
                        curve->ContainerPtrToValuePtr<void>(track));
                    if (array->Num() < 0 || array->Num() > 1024)
                    {
                        throw std::runtime_error("rooftop key count");
                    }
                    for (int i = 0; i < array->Num(); ++i)
                    {
                        keys.push_back(output->ContainerPtrToValuePtr<Point>(
                            static_cast<std::byte*>(array->GetData()) +
                            i * inner->GetElementSize()));
                    }
                }
            }
            if (keys.empty())
            {
                throw std::runtime_error("rooftop camera has no position keys");
            }
            for (auto* p : keys)
            {
                for (size_t j = 0; j < 3; ++j)
                {
                    (*p)[j] += roof.center[j];
                }
            }
            roof.translated = true;
        }

        bool opening_shot(UObject* pawn, UObject* level)
        {
            auto* player = static_cast<AActor*>(roof.player.Get());
            if (!is_uobject_valid_fast(player) || local_player(pawn) != player)
            {
                return false;
            }
            auto* shot = read_boss_owner_property(
                read_boss_owner_property(level, L"LevelScriptActor"),
                L"HighVoltage_ExecuteUbergraph_ss_01_camera_ms010_RefProperty");
            if (!is_uobject_valid_fast(shot) || shot->GetOuterPrivate() != level)
            {
                return false;
            }
            roof.shot = make_uobject_weak(shot);
            if (!roof.translated)
            {
                translate_shot(shot);
            }
            if (!flag(shot, L"bIsPlaying") && !roof.requested_shot)
            {
                *kh02_matinee_actor_slot(shot, L"TresInterpGroup_4") = pawn;
                *kh02_matinee_actor_slot(shot, L"TresInterpGroup_2") = player;
                ReflectedCall{shot, L"Play"}.run();
                roof.requested_shot = true;
            }
            if (!flag(shot, L"bIsPlaying"))
            {
                return false;
            }
            auto* ctrl = read_boss_owner_property(player, L"Controller");
            if (!ctrl)
            {
                return false;
            }
            ReflectedCall target{ctrl, L"GetViewTarget"};
            target.run();
            auto* camera = target.result();
            if (!is_uobject_valid_fast(camera) || camera->GetOuterPrivate() != level)
            {
                return false;
            }
            hide_host_geometry(pawn);
            // The camera already owns the view. Introduce the real player inside the
            // original wall while its director is showing Yozora and the orb.
            relocate(player, {roof.center[0], roof.center[1] - 650, roof.center[2] + 100});
            roof.moved = true;
            roof.phase = Phase::Playing;
            Output::send<LogLevel::Normal>(
                STR("[KH3APProbeCpp] Yozora DM rooftop camera={} host geometry={} player moved\n"),
                camera->GetName(), roof.host.size());
            return true;
        }
    } // namespace

    void preload_yozora_rooftop(UObject* pawn)
    {
        if (roof.pawn.Get() == pawn)
        {
            return;
        }
        finish();
        roof.pawn = make_uobject_weak(pawn);
        roof.world = make_uobject_weak(actor_owning_world(pawn));
        roof.phase = Phase::Preload;
    }

    bool enter_yozora_rooftop(UObject* pawn)
    {
        // Recreate unloaded layers if the same fight enters High Voltage again.
        if (roof.phase == Phase::Idle)
        {
            preload_yozora_rooftop(pawn);
        }
        if (roof.pawn.Get() != pawn)
        {
            return false;
        }
        if (roof.phase == Phase::Preload)
        {
            try
            {
                load(pawn);
            }
            catch (const std::exception&)
            {
                finish();
                return false;
            }
        }
        if (uses_yozora_rooftop(pawn))
        {
            return true;
        }
        if (roof.phase != Phase::Standby)
        {
            return false;
        }
        auto* player = local_player(pawn);
        auto center = read_boss_arena_center(pawn);
        if (!center || !is_uobject_valid_fast(player) ||
            actor_owning_world(player) != roof.world.Get())
        {
            return false;
        }
        // The streams may still be loading on immediate-opening DMs.
        // update_yozora_rooftop waits for visibility before touching the player.
        roof.player = make_uobject_weak(player);
        roof.player_start = position(player);
        roof.boss_start = position(static_cast<AActor*>(pawn));

        roof.phase = Phase::Entering;
        roof.deadline = Clock::now() + std::chrono::seconds(14);
        for (const auto& layer : roof.streams)
        {
            set_flag(layer.Get(), L"bShouldBeVisible", true);
        }
        Output::send<LogLevel::Normal>(
            STR("[KH3APProbeCpp] Yozora DM showing rooftop at host arena center\n"));
        return true;
    }

    bool uses_yozora_rooftop(UObject* pawn)
    {
        return pawn && roof.pawn.Get() == pawn &&
               (roof.phase == Phase::Entering || roof.phase == Phase::Playing);
    }

    void open_yozora_rooftop_shot(UObject* pawn, UObject* sphere)
    {
        if (uses_yozora_rooftop(pawn))
        {
            roof.sphere = make_uobject_weak(sphere);
        }
    }

    void close_yozora_rooftop(UObject* pawn)
    {
        if (uses_yozora_rooftop(pawn))
        {
            finish();
        }
    }

    void update_yozora_rooftop()
    {
        remove_retired_streams();
        if (roof.phase == Phase::Idle)
        {
            return;
        }
        try
        {
            auto* pawn = roof.pawn.Get();
            if (!is_uobject_valid_fast(pawn))
            {
                finish();
                return;
            }
            if (roof.phase == Phase::Preload)
            {
                load(pawn);
                return;
            }
            if (roof.phase != Phase::Entering || Clock::now() < roof.next_check)
            {
                return;
            }
            roof.next_check = Clock::now() + std::chrono::milliseconds(40);
            if (Clock::now() > roof.deadline)
            {
                Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] Yozora DM rooftop shot timed out\n"));
                finish();
                return;
            }
            if (!roof.sphere.Get())
            {
                return;
            }
            for (const auto& layer : roof.streams)
            {
                if (!stream_state(layer.Get(), L"IsLevelVisible"))
                {
                    return;
                }
            }
            auto* level = read_boss_owner_property(roof.streams.front().Get(), L"LoadedLevel");
            if (level)
            {
                opening_shot(pawn, level);
            }
        }
        catch (const std::exception& e)
        {
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] Yozora DM rooftop unavailable: {}\n"),
                std::wstring(e.what(), e.what() + std::char_traits<char>::length(e.what())));
            finish();
        }
    }
} // namespace kh3ap::enemy_remap_detail
