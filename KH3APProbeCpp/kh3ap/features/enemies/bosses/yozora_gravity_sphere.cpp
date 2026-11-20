#include "kh3ap/features/enemies/bosses/binding.h"
#include "kh3ap/features/enemies/bosses/encounter_repairs.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/core/objects/actor_owning_world.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/core/platform/game_build.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/reflection/reflected_function_call.h"
#include "kh3ap/features/player/player_ids.h"
#include "kh3ap/features/world/garden_return/runtime_context.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/AActor.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/GameplayStatics.hpp>
#include <Unreal/FScriptArray.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#include <Unreal/Property/FStructProperty.hpp>
#include <Unreal/UScriptStruct.hpp>
#include <Unreal/Transform.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Windows.h>
#include <intrin.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <vector>

namespace kh3ap::enemy_remap_detail
{
    namespace
    {
        using Position = std::array<float, 3>;
        using Clock = std::chrono::steady_clock;
        constexpr auto yozora = L"/Game/Blueprints/Enemy/e_ex781/e_ex781_Pawn.e_ex781_Pawn_C";
        constexpr auto native_level =
            L"/Game/Levels/ss_DLC/ss_01/umap/ss_01_enemy_01.ss_01_enemy_01";
        constexpr auto native_volume =
            L"/Game/Levels/ss_DLC/ss_01/umap/"
            L"ss_01_enemy_01.ss_01_enemy_01:PersistentLevel.TresKHSBlendVolume_1";
        constexpr auto rooftop_wall =
            L"/Game/Maps/ss_DLC/uasset/Meshes/SMC_ss_InvisibleWallA.SMC_ss_InvisibleWallA";

        struct GravityScene
        {
            FWeakObjectPtr pawn, sphere, volume;
            Position center{};
            FProperty* orb_field{};
            FWeakObjectPtr last_orb;
            FWeakObjectPtr wall;
            bool needs_light{}, needs_wall{};
            Clock::time_point asset_deadline{}, next_asset_check{};
            FProperty* gravity_curve_field{};
            FWeakObjectPtr climax_curve;
            bool climax_started{};
            FWeakObjectPtr catch_queue, catch_asset, previous_asset;
        };
        // Touched only on the game thread. A scene ends when the pawn releases its sphere.
        std::vector<GravityScene> scenes;

        using GigasLocation = Position* (*)(UObject*, Position*);
        std::atomic<GigasLocation> original_gigas_location{};
        std::unique_ptr<RuntimeBossHpLimitOverride> gigas_location_override;
        uintptr_t destroy_distance_caller{}, tackle_distance_caller{};

        Position* gigas_location(UObject* gigas, Position* output)
        {
            const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
            auto* location = original_gigas_location.load(std::memory_order_acquire)(gigas, output);
            if ((caller != destroy_distance_caller && caller != tackle_distance_caller) ||
                location != output || scenes.empty())
            {
                return location;
            }
            // Both callers are synchronous game-thread distance tests. Only their
            // private return buffer uses local coordinates. Motion and targeting
            // still receive the real world position through this same getter.
            auto* world = actor_owning_world(gigas);
            for (const auto& scene : scenes)
            {
                auto* boss = scene.pawn.Get();
                if (world && is_uobject_valid_fast(boss) && actor_owning_world(boss) == world &&
                    scene.sphere.Get() && read_boss_owner_property(boss, L"m_pGravitySphere") == scene.sphere.Get())
                {
                    for (size_t axis = 0; axis < output->size(); ++axis)
                    {
                        (*output)[axis] -= scene.center[axis];
                    }
                    break;
                }
            }
            return location;
        }

        void install_gigas_distance_center()
        {
            if (gigas_location_override)
            {
                return;
            }
            // Called after supported_scene_layout has verified the host image.
            const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
            auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
            auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
            const bool egs = native_build::identify(nt->FileHeader.TimeDateStamp, nt->OptionalHeader.SizeOfImage)
                             == native_build::Build::Egs;
            const auto destroy = base + (egs ? 0xfd92a3 : 0xd7b153);
            const auto tackle = base + (egs ? 0xfd7495 : 0xd7de85);
            constexpr uint8_t get_location_call[]{0xff, 0x90, 0xb8, 0x06, 0x00, 0x00};
            constexpr uint8_t tackle_compare[]{0x0f, 0x2f, 0x87, 0xdc, 0x01, 0x00, 0x00};
            const uint8_t destroy_compare[]{0x0f, 0x2f, static_cast<uint8_t>(egs ? 0x83 : 0x87), 0x70, 0x0c, 0x00, 0x00};
            auto matches = [](uintptr_t at, const auto& bytes)
            {
                return std::memcmp(reinterpret_cast<const void*>(at), bytes, sizeof(bytes)) == 0;
            };
            if (!matches(destroy - sizeof(get_location_call), get_location_call) ||
                !matches(tackle - sizeof(get_location_call), get_location_call) ||
                !matches(destroy + 41, destroy_compare) || !matches(tackle + 41, tackle_compare))
            {
                return;
            }
            auto* klass = UObjectGlobals::StaticFindObject<UClass*>(
                nullptr, nullptr, L"/Script/TresGame.TresEnemyPawn_e_ex781_Gigas");
            auto* defaults = is_uobject_valid_fast(klass) ? klass->GetClassDefaultObject() : nullptr;
            if (!is_uobject_valid_fast(defaults))
            {
                return;
            }
            auto** slot = *reinterpret_cast<void***>(defaults) + 0x6b8 / sizeof(void*);
            auto guard = std::make_unique<RuntimeBossHpLimitOverride>();
            guard->slot = slot;
            guard->original = *slot;
            guard->replacement = reinterpret_cast<void*>(&gigas_location);
            PLH::MemAccessor memory{};
            PLH::MemoryProtector protect{reinterpret_cast<uint64_t>(slot), sizeof(void*),
                                         PLH::ProtFlag::R | PLH::ProtFlag::W, memory};
            if (!protect.isGood())
            {
                return;
            }
            destroy_distance_caller = destroy;
            tackle_distance_caller = tackle;
            original_gigas_location.store(reinterpret_cast<GigasLocation>(guard->original), std::memory_order_release);
            *slot = guard->replacement;
            gigas_location_override = std::move(guard);
        }

        bool supported_scene_layout()
        {
            const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
            if (!base)
            {
                return false;
            }
            const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
            if (dos->e_magic != IMAGE_DOS_SIGNATURE)
            {
                return false;
            }
            const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
            if (nt->Signature != IMAGE_NT_SIGNATURE)
            {
                return false;
            }
            const auto build = native_build::identify(nt->FileHeader.TimeDateStamp,
                                                      nt->OptionalHeader.SizeOfImage);
            if (build == native_build::Build::Unsupported)
            {
                return false;
            }
            // The sphere initializer copies the floor hit to its six scene positions
            // and derives the gravity height from the reflected m_GravitySceneHeight.
            constexpr uint8_t store_floor[] = {0xf2, 0x0f, 0x11, 0x83, 0xe0, 0x0a, 0x00, 0x00};
            constexpr uint8_t add_height[] = {0xf3, 0x0f, 0x58, 0x83, 0xd8, 0x09, 0x00, 0x00};
            const bool egs = build == native_build::Build::Egs;
            return !std::memcmp(reinterpret_cast<const void*>(base + (egs ? 0xfdc9b0 : 0xd5751d)),
                                store_floor, sizeof(store_floor)) &&
                   !std::memcmp(reinterpret_cast<const void*>(base + (egs ? 0xfdc9e7 : 0xd57563)),
                                add_height, sizeof(add_height));
        }

        UObject* owner_of_sphere(UObject* sphere)
        {
            auto& track = s_runtime_enemy_remap.boss.tracking;
            std::lock_guard lock{track.boss_state_mutex};
            for (const auto& [path, weak] : track.boss_bound_actors)
            {
                auto* pawn = weak.Get();
                const auto source = track.remapped_boss_source_classes.find(path);
                if (source != track.remapped_boss_source_classes.end() &&
                    source->second != yozora && is_uobject_valid_fast(pawn) &&
                    pawn->GetClassPrivate()->GetPathName() == yozora &&
                    read_boss_owner_property(pawn, L"m_pGravitySphere") == sphere)
                {
                    return pawn;
                }
            }
            return nullptr;
        }

        void move_from_origin(UObject* component, const Position& displacement)
        {
            if (!component)
            {
                return;
            }
            ReflectedFunctionCall get{component, L"K2_GetComponentLocation", "yozora_dm", 32};
            get.run();
            const auto p = get.result_vector();
            if (std::hypot(p[0], p[1]) > 1.0f)
            {
                return;
            }
            ReflectedFunctionCall set{component, L"K2_SetWorldLocation", "yozora_dm", 512};
            set.vector(L"NewLocation", p[0] + displacement[0], p[1] + displacement[1],
                       p[2] + displacement[2]);
            set.boolean(L"bTeleport", true);
            set.run();
        }

        UObject** object_slot(UObject* o, const wchar_t* key)
        {
            auto* field = o->GetPropertyByNameInChain(key);
            return field && field->GetClass().GetName() == L"ObjectProperty" &&
                           field->GetElementSize() == sizeof(UObject*)
                       ? field->ContainerPtrToValuePtr<UObject*>(o)
                       : nullptr;
        }

        void bind_climax_catch(GravityScene& scene, UObject* sphere)
        {
            auto* player = garden_return_detail::player();
            if (!is_alternate_keyblade_wielder(garden_return_detail::player_id(player)) ||
                actor_owning_world(player) != actor_owning_world(sphere))
            {
                return;
            }
            auto* queue = read_boss_owner_property(player, L"MyStateComp");
            auto** slot = queue ? object_slot(queue, L"MyStateAsset") : nullptr;
            auto* set_class = find_loaded_runtime_asset_class(L"/Script/TresGame.TresStateSet");
            auto* catch_class = find_loaded_runtime_asset_class(L"/Script/TresGame.TresPlayerState_Catch");
            if (!slot || !is_uobject_valid_fast(set_class) || !is_uobject_valid_fast(catch_class))
            {
                return;
            }
            auto* previous = *slot;
            if (previous && (!is_uobject_valid_fast(previous) || !is_uobject_a_fast(previous, set_class)))
            {
                return;
            }
            auto* field = set_class->GetPropertyByNameInChain(L"MyStates");
            if (!field || !field->IsA<FArrayProperty>())
            {
                return;
            }
            auto* entry = const_cast<FProperty*>(static_cast<FArrayProperty*>(field)->GetInner());
            if (!entry || !entry->IsA<FStructProperty>())
            {
                return;
            }
            auto* type = static_cast<FStructProperty*>(entry)->GetStruct();
            auto* id = find_struct_field_by_name(type, L"MyStateID");
            auto* klass = find_struct_field_by_name(type, L"MyStateClass");
            if (!id || id->GetElementSize() != 1 || !klass || klass->GetClass().GetName() != L"ClassProperty")
            {
                return;
            }
            constexpr uint8_t catch_state = 61; // ETresStateID::TSID_STATE_CATCH
            const auto stride = entry->GetElementSize();
            if (previous)
            {
                auto* states = field->ContainerPtrToValuePtr<FScriptArray>(previous);
                for (int i = 0; i < states->Num(); ++i)
                {
                    auto* row = static_cast<std::byte*>(states->GetData()) + i * stride;
                    if (*id->ContainerPtrToValuePtr<uint8_t>(row) == catch_state &&
                        *klass->ContainerPtrToValuePtr<UClass*>(row))
                    {
                        return;
                    }
                }
            }
            // The queue consults MyStateAsset before its per-character defaults. Give this
            // pawn Sora's native Catch state while the orb can grab it. The new asset's
            // outer keeps any displaced state set alive until the queue gets it back.
            FStaticConstructObjectParameters params(set_class, previous ? previous : queue);
            params.SetFlags = RF_Transient;
            auto* asset = UObjectGlobals::StaticConstructObject(params);
            if (!is_uobject_valid_fast(asset))
            {
                return;
            }
            if (previous)
            {
                field->CopyCompleteValue_InContainer(asset, previous);
            }
            auto* states = field->ContainerPtrToValuePtr<FScriptArray>(asset);
            const auto index = states->AddZeroed(1, stride, entry->GetMinAlignment());
            auto* row = static_cast<std::byte*>(states->GetData()) + index * stride;
            *id->ContainerPtrToValuePtr<uint8_t>(row) = catch_state;
            *klass->ContainerPtrToValuePtr<UClass*>(row) = catch_class;
            scene.catch_queue = make_uobject_weak(queue);
            scene.catch_asset = make_uobject_weak(asset);
            scene.previous_asset = make_uobject_weak(previous);
            *slot = asset;
        }

        void update_climax_catch(GravityScene& scene, UObject* sphere, bool ending = false)
        {
            auto* curve = !ending && scene.gravity_curve_field
                              ? *scene.gravity_curve_field->ContainerPtrToValuePtr<UObject*>(sphere)
                              : nullptr;
            const bool active = curve && curve == scene.climax_curve.Get();
            if (active == scene.climax_started)
            {
                return;
            }
            scene.climax_started = active;
            if (active)
            {
                bind_climax_catch(scene, sphere);
                return;
            }
            if (auto* queue = scene.catch_queue.Get(); is_uobject_valid_fast(queue))
            {
                if (auto** slot = object_slot(queue, L"MyStateAsset"); slot && *slot == scene.catch_asset.Get())
                {
                    *slot = scene.previous_asset.Get();
                }
            }
            scene.catch_queue = {};
            scene.catch_asset = {};
            scene.previous_asset = {};
        }

        bool create_rooftop_wall(GravityScene& scene, UObject* pawn)
        {
            auto* mesh = find_loaded_runtime_asset(rooftop_wall);
            if (!is_uobject_valid_fast(mesh))
            {
                return false;
            }
            ReflectedFunctionCall local{L"/Script/Engine.Default__GameplayStatics", L"GetPlayerPawn",
                                    "yozora_dm", 64};
            local.object(L"WorldContextObject", pawn);
            local.run(); // PlayerIndex defaults to the local player at index zero.
            auto* player = static_cast<AActor*>(local.result());
            auto* world = actor_owning_world(player);
            if (!world || world != actor_owning_world(pawn))
            {
                return false;
            }
            auto* klass = UObjectGlobals::StaticFindObject<UClass*>(
                nullptr, nullptr, L"/Script/Engine.StaticMeshActor");
            if (!klass)
            {
                return true;
            }
            const auto& c = scene.center;
            const FTransform transform{FQuat{0, 0, 0, 1}, FVector{c[0], c[1], c[2]},
                                       FVector{1, 1, 1}};
            auto* wall = UGameplayStatics::BeginDeferredActorSpawnFromClass(
                pawn, klass, transform, ESpawnActorCollisionHandlingMethod::AlwaysSpawn,
                static_cast<AActor*>(pawn));
            if (!is_uobject_valid_fast(wall))
            {
                return false;
            }
            auto* component = read_boss_owner_property(wall, L"StaticMeshComponent");
            auto* mobility = component ? component->GetPropertyByNameInChain(L"Mobility") : nullptr;
            if (!mobility || mobility->GetElementSize() != 1 ||
                mobility->GetClass().GetName() != L"ByteProperty")
            {
                wall->K2_DestroyActor();
                Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] Yozora DM wall component unavailable\n"));
                return true;
            }
            wall->SetActorHiddenInGame(true);
            // KH3 exposes Mobility as a property. Configure this deferred component
            // like the original movable rooftop mesh before finalizing the actor.
            *mobility->ContainerPtrToValuePtr<uint8_t>(component) = 2;
            ReflectedFunctionCall set_mesh{component, L"SetStaticMesh", "yozora_dm", 32};
            set_mesh.object(L"NewMesh", mesh);
            set_mesh.run();
            if (!set_mesh.boolean_result())
            {
                wall->K2_DestroyActor();
                Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] Yozora DM wall mesh rejected\n"));
                return true;
            }
            ReflectedFunctionCall profile{component, L"SetCollisionProfileName", "yozora_dm", 32};
            profile.name(L"InCollisionProfileName", L"TresInvisibleWall");
            profile.run();
            // The original rooftop's circular pawn wall has a 2500-unit radius. A
            // larger host can start the move with its player already beyond that wall.
            const auto p = player->K2_GetActorLocation();
            const double dx = p.X() - c[0], dy = p.Y() - c[1];
            const double radius = std::hypot(dx, dy);
            if (radius > 2400.)
            {
                const auto scale = 2000. / radius;
                if (!player->K2_TeleportTo(FVector{c[0] + dx * scale, c[1] + dy * scale, p.Z()},
                                           player->K2_GetActorRotation()))
                {
                    wall->K2_DestroyActor();
                    return false;
                }
            }
            UGameplayStatics::FinishSpawningActor(wall, transform);
            scene.wall = make_uobject_weak(wall);
            Output::send<LogLevel::Normal>(
                STR("[KH3APProbeCpp] Yozora DM rooftop wall bound; player radius={}\n"), radius);
            return true;
        }

        bool bind_lighting(GravityScene& scene, UObject* pawn, UObject* sphere)
        {
            auto** pawn_slot = object_slot(pawn, L"m_KHSBlendVolume");
            auto** sphere_slot = object_slot(sphere, L"m_pBlendVolumeTarget");
            if (!pawn_slot || !sphere_slot || *pawn_slot || *sphere_slot)
            {
                return true;
            }
            auto* source =
                find_loaded_runtime_asset(native_volume);
            if (!is_uobject_valid_fast(source))
            {
                return false;
            }
            auto* klass = source->GetClassPrivate();
            auto* src_brush = read_boss_owner_property(source, L"BrushComponent");
            if (klass->GetPathName() != L"/Script/TresGame.TresKHSBlendVolume" || !src_brush)
            {
                return false;
            }
            const auto& c = scene.center;
            const FTransform transform{FQuat{0, 0, 0, 1}, FVector{c[0], c[1], c[2]},
                                       FVector{1, 1, 1}};
            auto* volume = UGameplayStatics::BeginDeferredActorSpawnFromClass(
                pawn, klass, transform, ESpawnActorCollisionHandlingMethod::AlwaysSpawn,
                static_cast<AActor*>(pawn));
            if (!is_uobject_valid_fast(volume))
            {
                return false;
            }
            auto* brush = read_boss_owner_property(volume, L"BrushComponent");
            if (!brush || brush->GetClassPrivate() != src_brush->GetClassPrivate())
            {
                volume->K2_DestroyActor();
                return true;
            }
            // Use just the source brush's immutable geometry and authored lighting values.
            // Registration, components, world ownership and Matinee playback belong to this
            // encounter.
            for (auto* f : klass->ForEachProperty())
            {
                f->CopyCompleteValue_InContainer(volume, source);
            }
            for (const auto* key : {L"Brush", L"BrushType"})
            {
                if (auto* f = volume->GetPropertyByNameInChain(key))
                {
                    f->CopyCompleteValue_InContainer(volume, source);
                }
            }
            for (const auto* key : {L"Brush", L"BrushBodySetup"})
            {
                if (auto* f = brush->GetPropertyByNameInChain(key))
                {
                    f->CopyCompleteValue_InContainer(brush, src_brush);
                }
            }
            UGameplayStatics::FinishSpawningActor(volume, transform);
            *pawn_slot = volume;
            *sphere_slot = volume;
            scene.volume = make_uobject_weak(volume);
            Output::send<LogLevel::Normal>(STR("[KH3APProbeCpp] Yozora DM lighting bound {}\n"),
                                           volume->GetPathName());
            return true;
        }
    } // namespace

    bool initialize_yozora_gravity_sphere(UObject* sphere)
    {
        static const bool supported = supported_scene_layout();
        if (!supported || sphere->GetClassPrivate()->GetPathName() !=
                              L"/Game/Blueprints/Enemy/e_ex781/Gimmick/"
                              L"e_ex781_GravitySphere.e_ex781_GravitySphere_C")
        {
            return true;
        }
        auto* pawn = owner_of_sphere(sphere);
        const auto center = read_boss_arena_center(pawn);
        if (!center)
        {
            return false;
        }
        if (scenes.size() >= 32 || std::any_of(scenes.begin(), scenes.end(),
                                               [sphere](const auto& s)
                                               {
                                                   return s.sphere.Get() == sphere;
                                               }))
        {
            return true;
        }
        auto* height = sphere->GetPropertyByNameInChain(L"m_GravitySceneHeight");
        auto* orb = sphere->GetPropertyByNameInChain(L"m_pGrbObjEffect");
        if (!height || height->GetOffset_Internal() != 0x9d8 || !orb ||
            orb->GetOffset_Internal() != 0xbe8 || orb->GetClass().GetName() != L"ObjectProperty" ||
            orb->GetElementSize() != sizeof(UObject*) ||
            sphere->GetClassPrivate()->GetPropertiesSize() < 0xc10)
        {
            return true;
        }
        constexpr size_t anchors_offset = 0xac8;
        std::array<Position, 6> anchors{};
        auto* memory = reinterpret_cast<std::byte*>(sphere) + anchors_offset;
        std::memcpy(anchors.data(), memory, sizeof(anchors));
        if (!std::all_of(anchors.begin(), anchors.end(),
                         [](const Position& p)
                         {
                             return std::isfinite(p[2]) && std::abs(p[2]) < 10000000 &&
                                    std::hypot(p[0], p[1]) < 1.0f;
                         }))
        {
            return true;
        }
        const Position delta{(*center)[0], (*center)[1], (*center)[2] - anchors.front()[2]};
        for (auto& p : anchors)
        {
            p[0] += delta[0];
            p[1] += delta[1];
            p[2] += delta[2];
        }
        // A floor trace at world zero can miss altogether in a replacement map.
        if (anchors[1][2] == anchors[0][2])
        {
            auto* climax = sphere->GetPropertyByNameInChain(L"m_CrimaxSceneHeight");
            if (!climax || climax->GetOffset_Internal() != 0xa40)
            {
                return true;
            }
            anchors[1][2] += *height->ContainerPtrToValuePtr<float>(sphere);
            anchors[3][2] += 1.0f;
            anchors[4][2] += *climax->ContainerPtrToValuePtr<float>(sphere);
            anchors[5][2] += 1.0f;
        }
        std::memcpy(memory, anchors.data(), sizeof(anchors));
        for (const auto* key :
             {L"RootComponent", L"MyDomeCollision", L"MyFloorCollision", L"m_pDomeEffect"})
        {
            move_from_origin(read_boss_owner_property(sphere, key), delta);
        }
        GravityScene scene{
            make_uobject_weak(pawn), make_uobject_weak(sphere), {}, *center, orb, {}};
        if (object_slot(sphere, L"m_pGravityEffectCurve"))
        {
            scene.gravity_curve_field = sphere->GetPropertyByNameInChain(L"m_pGravityEffectCurve");
            scene.climax_curve = make_uobject_weak(read_boss_owner_property(sphere, L"m_CrimaxSceneRateCurve"));
        }
        scene.needs_light = !bind_lighting(scene, pawn, sphere);
        if (scene.needs_light)
        {
            request_runtime_asset_load(pawn, native_level, nullptr, 0x781d00);
        }
        if (uses_yozora_rooftop(pawn))
        {
            open_yozora_rooftop_shot(pawn, sphere);
        }
        else
        {
            scene.needs_wall = !create_rooftop_wall(scene, pawn);
        }
        if (scene.needs_wall)
        {
            request_runtime_asset_load(pawn, rooftop_wall, nullptr, 0x781d01);
        }
        scene.asset_deadline = Clock::now() + std::chrono::seconds(8);
        install_gigas_distance_center();
        scenes.emplace_back(scene);
        Output::send<LogLevel::Normal>(
            STR("[KH3APProbeCpp] Yozora DM sphere centered ({},{},{})\n"), (*center)[0],
            (*center)[1], (*center)[2]);
        return true;
    }

    void update_yozora_gravity_effects()
    {
        update_yozora_rooftop();
        if (scenes.empty())
        {
            return;
        }
        std::erase_if(
            scenes,
            [](GravityScene& scene)
            {
                auto* pawn = scene.pawn.Get();
                auto* sphere = scene.sphere.Get();
                if (!is_uobject_valid_fast(pawn) || !is_uobject_valid_fast(sphere) ||
                    read_boss_owner_property(pawn, L"m_pGravitySphere") != sphere)
                {
                    update_climax_catch(scene, sphere, true);
                    close_yozora_rooftop(pawn);
                    if (auto* wall = scene.wall.Get(); is_uobject_valid_fast(wall))
                    {
                        static_cast<AActor*>(wall)->K2_DestroyActor();
                    }
                    if (auto* volume = scene.volume.Get(); is_uobject_valid_fast(volume))
                    {
                        if (is_uobject_valid_fast(pawn))
                        {
                            if (auto** slot = object_slot(pawn, L"m_KHSBlendVolume");
                                slot && *slot == volume)
                            {
                                *slot = nullptr;
                            }
                        }
                        if (is_uobject_valid_fast(sphere))
                        {
                            if (auto** slot = object_slot(sphere, L"m_pBlendVolumeTarget");
                                slot && *slot == volume)
                            {
                                *slot = nullptr;
                            }
                        }
                        static_cast<AActor*>(volume)->K2_DestroyActor();
                    }
                    return true;
                }
                update_climax_catch(scene, sphere);
                // The orb is pooled and assigned by native code after scene creation,
                // with no creation notification on reuse. Observe its owner slot only
                // during this DM.
                auto* orb = *scene.orb_field->ContainerPtrToValuePtr<UObject*>(sphere);
                if (orb != scene.last_orb.Get())
                {
                    scene.last_orb =
                        is_uobject_valid_fast(orb) ? make_uobject_weak(orb) : FWeakObjectPtr{};
                    if (scene.last_orb.Get())
                    {
                        move_from_origin(orb, scene.center);
                    }
                }
                const auto now = Clock::now();
                if ((scene.needs_light || scene.needs_wall) && now < scene.asset_deadline &&
                    now >= scene.next_asset_check)
                {
                    scene.needs_light = scene.needs_light && !bind_lighting(scene, pawn, sphere);
                    scene.needs_wall = scene.needs_wall && !create_rooftop_wall(scene, pawn);
                    scene.next_asset_check = now + std::chrono::milliseconds(100);
                }
                return false;
            });
    }
} // namespace kh3ap::enemy_remap_detail
