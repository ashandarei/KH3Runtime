#include "xemnas_center.h"

#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/features/enemies/bosses/binding.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/AActor.hpp>
#include <Unreal/GameplayStatics.hpp>
#include <Unreal/Property/FStructProperty.hpp>
#include <Unreal/Transform.hpp>
#include <Unreal/UScriptStruct.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <algorithm>
#include <cmath>
#include <vector>

namespace kh3ap
{
    using namespace RC;
    using namespace RC::Unreal;

    auto data_xemnas_center_slot(UObject* entity) -> UObject**
    {
        if (!is_uobject_valid_fast(entity))
        {
            return nullptr;
        }
        auto* data = entity->GetPropertyByNameInChain(L"m_CurrentUserData");
        if (!data || !data->IsA<FStructProperty>() || data->GetElementSize() != 24)
        {
            return nullptr;
        }
        auto* structure = static_cast<FStructProperty*>(data)->GetStruct();
        if (!is_uobject_valid_fast(structure) || structure->GetName() != L"TresLevelEntityUserData")
        {
            return nullptr;
        }
        auto* member = find_struct_field_by_name(structure, L"m_UserObject");
        if (!member || member->GetClass().GetName() != L"ObjectProperty" ||
            member->GetElementSize() != sizeof(UObject*) || member->GetOffset_Internal() != 0)
        {
            return nullptr;
        }
        return member->ContainerPtrToValuePtr<UObject*>(data->ContainerPtrToValuePtr<void>(entity));
    }

    auto data_xemnas_center_actor(UObject* pawn) -> std::optional<UObject*>
    {
        auto* function = pawn->GetFunctionByNameInChain(L"GetCenterActor");
        auto* result = is_uobject_valid_fast(function)
                           ? find_function_param_by_name(function, L"ReturnValue")
                           : nullptr;
        if (!result || result->GetClass().GetName() != L"ObjectProperty" ||
            result->GetElementSize() != sizeof(UObject*) || result->GetOffset_Internal() != 0 ||
            function->GetParmsSize() != sizeof(UObject*))
        {
            return std::nullopt;
        }
        UObject* actor = nullptr;
        pawn->ProcessEvent(function, &actor);
        return actor;
    }

    auto data_xemnas_center_location(UObject* pawn) -> std::optional<std::array<float, 3>>
    {
        std::array<float, 3> center{};
        auto* function = pawn->GetFunctionByNameInChain(L"GetCenterLocation");
        auto* result = is_uobject_valid_fast(function)
                           ? find_function_param_by_name(function, L"ReturnValue")
                           : nullptr;
        if (!result || !result->IsA<FStructProperty>() ||
            result->GetElementSize() != sizeof(center) || result->GetOffset_Internal() != 0 ||
            function->GetParmsSize() != sizeof(center))
        {
            return std::nullopt;
        }
        auto* structure = static_cast<FStructProperty*>(result)->GetStruct();
        if (!is_uobject_valid_fast(structure) || structure->GetName() != L"Vector")
        {
            return std::nullopt;
        }
        pawn->ProcessEvent(function, center.data());
        if (!std::all_of(center.begin(), center.end(),
                         [](float value)
                         {
                             return std::isfinite(value) && std::abs(value) <= 10000000.0f;
                         }))
        {
            return std::nullopt;
        }
        return center;
    }

    namespace
    {
        struct CenterBinding
        {
            FWeakObjectPtr pawn;
            FWeakObjectPtr entity;
            FWeakObjectPtr point;
        };

        // Accessed by the existing game-thread boss state hooks. The point
        // belongs to the boss's streamed level, which also destroys it on unload.
        std::vector<CenterBinding> center_bindings;

        auto is_data_xemnas(UObject* pawn) -> bool
        {
            static const FName class_name{L"e_ex353_Pawn_C", FNAME_Add};
            auto* klass = pawn ? pawn->GetClassPrivate() : nullptr;
            return klass && klass->GetNamePrivate() == class_name && is_uobject_valid_fast(pawn) &&
                   is_uobject_valid_fast(klass) && klass->GetPathName() == data_xemnas_class;
        }

        void release(CenterBinding& binding)
        {
            auto* point = binding.point.Get();
            if (!is_uobject_valid_fast(point))
            {
                return;
            }
            if (auto** slot = data_xemnas_center_slot(binding.entity.Get()); slot && *slot == point)
            {
                *slot = nullptr;
            }
            Output::send<LogLevel::Normal>(
                STR("[KH3APProbeCpp] Data Xemnas center released point={}\n"), point->GetPathName());
            static_cast<AActor*>(point)->K2_DestroyActor();
        }
    } // namespace

    void enemy_remap_detail::prepare_runtime_xemnas_center(UObject* pawn)
    {
        if (!is_data_xemnas(pawn) ||
            try_read_int32_object_property(pawn, L"m_HitPoint").value_or(0) <= 0)
        {
            return;
        }
        auto* entity = read_boss_owner_property(pawn, L"m_LevelEntity");
        auto** slot = data_xemnas_center_slot(entity);
        const auto existing = std::find_if(center_bindings.begin(), center_bindings.end(),
                                           [entity](const auto& record)
                                           {
                                               return record.entity.Get() == entity;
                                           });
        if (slot && *slot &&
            (existing == center_bindings.end() ||
             (existing->pawn.Get() == pawn && existing->point.Get() == *slot)))
        {
            return;
        }
        const auto binding = resolve_and_bind_runtime_boss_pawn(pawn);
        entity = binding.level_entity;
        slot = data_xemnas_center_slot(entity);
        const auto source = binding.level_entity_path.empty()
                                ? std::nullopt
                                : tracked_boss_source_class(binding.level_entity_path);
        if (!slot || !source || *source == data_xemnas_class)
        {
            return;
        }

        std::erase_if(center_bindings,
                      [&](auto& old)
                      {
                          auto* point = old.point.Get();
                          if (!is_uobject_valid_fast(point))
                          {
                              return true;
                          }
                          if (old.entity.Get() == entity &&
                              (old.pawn.Get() != pawn || *slot != point))
                          {
                              release(old);
                              return true;
                          }
                          return false;
                      });
        if (*slot || center_bindings.size() >= 32)
        {
            return;
        }
        const auto original = data_xemnas_center_actor(pawn);
        const auto center = data_xemnas_center_location(pawn);
        auto* owner = static_cast<AActor*>(pawn);
        auto* level = owner->GetLevel();
        auto* klass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr,
                                                                L"/Script/Engine.TargetPoint");
        if (!original || *original || !center || !is_uobject_valid_fast(level) ||
            !is_uobject_valid_fast(klass))
        {
            return;
        }
        const FTransform transform{
            FQuat{0, 0, 0, 1}, FVector{(*center)[0], (*center)[1], (*center)[2]}, FVector{1, 1, 1}};
        auto* point = UGameplayStatics::BeginDeferredActorSpawnFromClass(
            pawn, klass, transform, ESpawnActorCollisionHandlingMethod::AlwaysSpawn, owner);
        if (!is_uobject_valid_fast(point))
        {
            return;
        }
        UGameplayStatics::FinishSpawningActor(point, transform);
        CenterBinding record{make_uobject_weak(pawn), make_uobject_weak(entity),
                             make_uobject_weak(point)};
        slot = data_xemnas_center_slot(record.entity.Get());
        if (record.pawn.Get() != pawn || record.entity.Get() != entity ||
            record.point.Get() != point || point->GetLevel() != level || !slot || *slot ||
            read_boss_owner_property(entity, L"m_EntityActor") != pawn)
        {
            if (is_uobject_valid_fast(point))
            {
                point->K2_DestroyActor();
            }
            return;
        }
        const auto actual = point->K2_GetActorLocation();
        if (!(std::abs(actual.X() - (*center)[0]) <= 1.0 &&
              std::abs(actual.Y() - (*center)[1]) <= 1.0 &&
              std::abs(actual.Z() - (*center)[2]) <= 1.0))
        {
            point->K2_DestroyActor();
            return;
        }
        *slot = point;
        if (data_xemnas_center_actor(pawn) != point)
        {
            release(record);
            return;
        }
        center_bindings.push_back(record);
        Output::send<LogLevel::Normal>(STR("[KH3APProbeCpp] Data Xemnas center bound entity={} "
                                           "point={} center=({},{},{}) polling=false\n"),
                                       binding.level_entity_path, point->GetPathName(),
                                       (*center)[0], (*center)[1], (*center)[2]);
    }

    void enemy_remap_detail::retire_runtime_xemnas_center(UObject* pawn)
    {
        if (center_bindings.empty() || !is_data_xemnas(pawn))
        {
            return;
        }
        std::erase_if(center_bindings,
                      [&](auto& binding)
                      {
                          if (binding.pawn.Get() != pawn)
                          {
                              return false;
                          }
                          release(binding);
                          return true;
                      });
    }
} // namespace kh3ap
