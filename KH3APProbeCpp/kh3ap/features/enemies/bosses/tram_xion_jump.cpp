#include "tram_xion_jump.h"
#include "kh3ap/features/enemies/bosses/binding.h"
#include "kh3ap/core/reflection/reflected_function_call.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_weak.h"
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
#include "kh3ap/features/enemies/enemy_clear_service.h"
#endif
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/Property/FStructProperty.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <Unreal/UScriptStruct.hpp>
#include <cstring>

namespace kh3ap
{
    using namespace RC;
    using namespace RC::Unreal;
    using namespace enemy_remap_detail;
    namespace
    {
        using Position = std::array<float, 3>;
        constexpr auto entity_path =
            L"/Game/Levels/tt/tt_01/umap/"
            L"tt_01_enemy_02.tt_01_enemy_02:PersistentLevel.e_dw401_Pawn_s0_1";
        constexpr auto xion_class = L"/Game/Blueprints/Enemy/e_ex359/e_ex359_Pawn.e_ex359_Pawn_C";
        // Clear air over the verified Tram Common plaza floor (Z 575). The
        // Demon Tide entity's own spawn/center is at rooftop height (Z 2369).
        constexpr Position plaza{-1850.f, 1545.f, 675.f};

        enum class Landing { Clear, NoFloor, InsideWall };

        Landing inspect_landing(UObject* pawn, const Position& position)
        {
            // The pawn's authored root has a 50 cm radius. Sweep its lower
            // footprint, with 10 cm of edge clearance and 2 cm above the feet.
            constexpr float radius = 40.f;
            constexpr float center_height = radius + 2.f;
            ReflectedFunctionCall trace(L"/Script/Engine.Default__KismetSystemLibrary",
                                    L"SphereTraceSingleByProfile", "tram_xion_ground", 512);
            trace.object(L"WorldContextObject", pawn);
            trace.vector(L"Start", position[0], position[1], position[2] + center_height);
            trace.vector(L"End", position[0], position[1], position[2] - 6000.f);
            trace.number(L"Radius", radius);
            trace.name(L"ProfileName", L"TresEnemyPawn");
            trace.boolean(L"bIgnoreSelf", true);
            trace.run();
            if (!trace.boolean_result())
            {
                return Landing::NoFloor;
            }
            auto* out = trace.param(L"OutHit");
            if (!out->IsA<FStructProperty>())
            {
                throw std::runtime_error("tram_xion_hit_result_schema");
            }
            auto* schema = static_cast<FStructProperty*>(out)->GetStruct();
            auto* storage = out->ContainerPtrToValuePtr<void>(trace.parameters.data());
            auto* overlap = find_struct_field_by_name(schema, L"bStartPenetrating");
            auto* normal = find_struct_field_by_name(schema, L"ImpactNormal");
            if (!overlap || !overlap->IsA<FBoolProperty>() || !normal || !normal->IsA<FStructProperty>() ||
                normal->GetElementSize() != sizeof(Position))
            {
                throw std::runtime_error("tram_xion_hit_fields");
            }
            // A healthy grounded jump can already touch the floor (normal Z=1).
            // The captured blocked jump began inside a wall (normal Z=0).
            const bool inside = static_cast<FBoolProperty*>(overlap)->GetPropertyValueInContainer(storage);
            const float normal_z = normal->ContainerPtrToValuePtr<float>(storage)[2];
            return inside && normal_z <= 0.05f ? Landing::InsideWall : Landing::Clear;
        }

        bool has_nearby_navigation(UObject* pawn, const Position& position)
        {
            ReflectedFunctionCall nav(L"/Script/Engine.Default__NavigationSystem", L"K2_ProjectPointToNavigation",
                                  "tram_xion_navigation", 128);
            nav.object(L"WorldContextObject", pawn);
            nav.vector(L"Point", position[0], position[1], position[2]);
            nav.vector(L"QueryExtent", 80.f, 80.f, 350.f);
            nav.run();
            return nav.boolean_result();
        }

        Position vector_result(UObject* pawn, const wchar_t* method)
        {
            ReflectedFunctionCall call(pawn, method, "tram_xion_jump", 16);
            call.run();
            return call.result_vector();
        }
    } // namespace

    UObject* tram_xion_entity(UObject* pawn)
    {
        static const FName class_name(L"e_ex359_Pawn_C", FNAME_Add);
        if (!is_uobject_valid_fast(pawn) || pawn->GetClassPrivate()->GetNamePrivate() != class_name ||
            pawn->GetClassPrivate()->GetPathName() != xion_class)
        {
            return nullptr;
        }
        const auto binding = resolve_tracked_runtime_boss_pawn(pawn);
        if (!binding.level_entity || binding.level_entity_path != entity_path ||
            tracked_boss_source_class(binding.level_entity_path) != demon_tide_tt_source_class_path)
        {
            return nullptr;
        }
        return binding.level_entity;
    }

    void observe_tram_xion_jump(UObject* pawn)
    {
        if (!tram_xion_entity(pawn))
        {
            return;
        }
        try
        {
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            RuntimeEnemyClearService::stage_requested_tram_xion_jump(pawn);
#endif
            const auto position = vector_result(pawn, L"K2_GetActorLocation");
            const auto landing = inspect_landing(pawn, position);
            if (landing == Landing::Clear || has_nearby_navigation(pawn, position))
            {
                return;
            }
            const auto rotation = vector_result(pawn, L"K2_GetActorRotation");
            ReflectedFunctionCall teleport(pawn, L"K2_TeleportTo", "tram_xion_jump", 32);
            teleport.vector(L"DestLocation", plaza[0], plaza[1], plaza[2]);
            teleport.numbers(L"DestRotation", {rotation[0], rotation[1], rotation[2]});
            teleport.run();
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] Tram Data Xion invalid jump landing={} "
                    "position=({},{},{}) plaza_teleport={}\n"),
                landing == Landing::InsideWall ? L"wall_overlap" : L"no_floor",
                position[0], position[1], position[2], teleport.boolean_result());
        }
        catch (const std::exception& error)
        {
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] Tram Data Xion navigation ground check failed: {}\n"),
                std::wstring(error.what(), error.what() + std::strlen(error.what())));
        }
    }
} // namespace kh3ap
