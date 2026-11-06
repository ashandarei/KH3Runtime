#include "kh3ap/features/player/combat/combat_blackboard_handoff.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/reflection/reflected_property_cache.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include <Unreal/AActor.hpp>
#include <Unreal/GameplayStatics.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/FScriptArray.hpp>
#include <Unreal/NameTypes.hpp>
#include <Unreal/UScriptStruct.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#include <Unreal/Property/FStructProperty.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#include <algorithm>
#include <stdexcept>
#include <vector>

namespace kh3ap::combat_blackboard_handoff
{
    using namespace RC::Unreal;
    namespace
    {
        constexpr int maximum_blackboard_parent_depth = 16;
        constexpr int maximum_blackboard_keys_per_asset = 256;

        struct Key
        {
            FWeakObjectPtr board;
            std::wstring name;
        };
        struct Tick
        {
            FWeakObjectPtr object;
            bool actor;
            bool suspended{};
        };
        std::vector<Key> keys;
        std::vector<Tick> ticks;
        std::vector<FWeakObjectPtr> cameras;
        FWeakObjectPtr source;
        ReflectedPropertyCache<64> properties;
        UObject** camera_target(UObject* camera)
        {
            auto* info = properties.resolve(camera, L"m_TargetInfo");
            if (!info || !info->IsA<FStructProperty>())
            {
                throw std::runtime_error("swap_camera_target_info_schema");
            }
            auto* schema = static_cast<FStructProperty*>(info)->GetStruct();
            auto* target = find_struct_field_by_name(schema, L"m_TargetActor");
            if (!target || target->GetClass().GetName() != L"ObjectProperty" ||
                target->GetElementSize() != sizeof(UObject*))
            {
                throw std::runtime_error("swap_camera_target_actor_schema");
            }
            return target->ContainerPtrToValuePtr<UObject*>(info->ContainerPtrToValuePtr<void>(camera));
        }
        UObject* object(UObject* owner, const wchar_t* field)
        {
            return properties.read_object(owner, field);
        }
        UObject* value(UObject* board, const std::wstring& name)
        {
            Kh02ReflectedCall query(board, L"GetValueAsObject", "swap blackboard", 256);
            query.name(L"KeyName", name.c_str());
            query.run();
            auto* result = query.param(L"ReturnValue");
            if (!result || result->GetClass().GetName() != L"ObjectProperty" ||
                result->GetElementSize() != sizeof(UObject*))
            {
                throw std::runtime_error("swap_blackboard_object_result_schema");
            }
            return *result->ContainerPtrToValuePtr<UObject*>(query.parameters.data());
        }
        void capture_ticks(UObject* actor)
        {
            if (!is_uobject_valid_fast(actor)) return;
            const auto add = [](UObject* target, bool is_actor)
            {
                if (!is_uobject_valid_fast(target) || std::any_of(ticks.begin(), ticks.end(),
                    [&](const auto& entry) { return entry.object.Get() == target; })) return;
                const auto* getter = is_actor ? L"IsActorTickEnabled" : L"IsComponentTickEnabled";
                const auto* setter = is_actor ? L"SetActorTickEnabled" : L"SetComponentTickEnabled";
                if (!target->GetFunctionByNameInChain(getter) || !target->GetFunctionByNameInChain(setter)) return;
                Kh02ReflectedCall query(target, getter, "swap tick capture", 256);
                query.run();
                if (query.boolean_result()) ticks.push_back({make_uobject_weak(target), is_actor});
            };
            add(actor, true);
            auto* component_class = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, L"/Script/Engine.ActorComponent");
            if (!is_uobject_valid_fast(component_class)) throw std::runtime_error("swap_actor_component_class_missing");
            for (auto* component : static_cast<AActor*>(actor)->K2_GetComponentsByClass(component_class))
            {
                add(component, false);
            }
        }
    }
    void capture_cameras(UObject* outgoing)
    {
        source = make_uobject_weak(outgoing);
        auto* camera_class = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr,
                                                                     L"/Script/TresGame.TresCameraTarget");
        if (!is_uobject_valid_fast(camera_class))
        {
            throw std::runtime_error("swap_camera_target_class_missing");
        }
        std::vector<AActor*> actors;
        UGameplayStatics::GetAllActorsOfClass(outgoing, camera_class, actors);
        for (auto* camera : actors)
        {
            if (!is_uobject_valid_fast(camera) || kh02_actor_world(camera) != kh02_actor_world(outgoing) ||
                *camera_target(camera) != outgoing)
            {
                continue;
            }
            cameras.push_back(make_uobject_weak(camera));
            capture_ticks(camera);
        }
    }
    void capture(UObject* enemy, UObject* outgoing)
    {
        source = make_uobject_weak(outgoing);
        capture_ticks(enemy);
        auto* controller = object(enemy, L"Controller");
        capture_ticks(controller);
        auto* board = object(controller, L"Blackboard");
        if (!is_uobject_valid_fast(board)) return;
        auto* object_type = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, L"/Script/AIModule.BlackboardKeyType_Object");
        if (!is_uobject_valid_fast(object_type)) throw std::runtime_error("swap_blackboard_object_type_missing");
        auto* asset = object(board, L"BlackboardAsset");
        for (int depth = 0; is_uobject_valid_fast(asset) && depth < maximum_blackboard_parent_depth;
             ++depth)
        {
            auto* property = properties.resolve(asset, L"Keys");
            if (!property || property->GetClass().GetName() != L"ArrayProperty")
                throw std::runtime_error("swap_blackboard_keys_schema");
            auto* inner = const_cast<FProperty*>(static_cast<FArrayProperty*>(property)->GetInner());
            if (!inner || !inner->IsA<FStructProperty>()) throw std::runtime_error("swap_blackboard_entry_schema");
            auto* schema = static_cast<FStructProperty*>(inner)->GetStruct();
            auto* name = find_struct_field_by_name(schema, L"EntryName");
            auto* type = find_struct_field_by_name(schema, L"KeyType");
            if (!name || !type || name->GetClass().GetName() != L"NameProperty" ||
                type->GetClass().GetName() != L"ObjectProperty") throw std::runtime_error("swap_blackboard_entry_fields");
            auto* entries = property->ContainerPtrToValuePtr<FScriptArray>(asset);
            if (entries->Num() < 0 || entries->Num() > maximum_blackboard_keys_per_asset)
            {
                throw std::runtime_error("swap_blackboard_entry_count");
            }
            for (int index = 0; index < entries->Num(); ++index)
            {
                auto* entry = static_cast<std::byte*>(entries->GetData()) + index * inner->GetElementSize();
                auto* key_type = *type->ContainerPtrToValuePtr<UObject*>(entry);
                if (!is_uobject_valid_fast(key_type) || !key_type->IsA(object_type)) continue;
                auto key_name = name->ContainerPtrToValuePtr<FName>(entry)->ToString();
                if (value(board, key_name) != outgoing) continue;
                if (std::none_of(keys.begin(), keys.end(), [&](const auto& key) {
                    return key.board.Get() == board && key.name == key_name;
                })) keys.push_back({make_uobject_weak(board), std::move(key_name)});
            }
            asset = object(asset, L"Parent");
        }
    }
    void suspend()
    {
        try
        {
            for (auto& entry : ticks)
            {
                auto* target = entry.object.Get();
                if (!target) continue;
                Kh02ReflectedCall hold(target, entry.actor ? L"SetActorTickEnabled" : L"SetComponentTickEnabled", "swap AI hold", 256);
                hold.boolean(L"bEnabled", false);
                entry.suspended = true;
                hold.run();
            }
        }
        catch (...)
        {
            release();
            throw;
        }
    }
    void apply(UObject* incoming)
    {
        size_t camera_count = 0;
        for (const auto& reference : cameras)
        {
            auto* camera = reference.Get();
            if (!is_uobject_valid_fast(camera))
            {
                continue;
            }
            auto** target = camera_target(camera);
            if (*target == incoming || (is_uobject_valid_fast(*target) && *target != source.Get()))
            {
                continue;
            }
            // TresCameraTarget's native target setter is a single assignment
            // to this reflected member. Preserve authored offsets, camera
            // selection and its separate distance/encounter target.
            *target = incoming;
            ++camera_count;
        }
        if (camera_count)
        {
            RC::Output::send<RC::LogLevel::Normal>(STR("[KH3APProbeCpp] Encounter camera handoff: cameras={} incoming={}\n"),
                                                  camera_count, incoming->GetName());
        }
        size_t changed = 0;
        for (const auto& key : keys)
        {
            auto* board = key.board.Get();
            if (!board) continue;
            auto* current = value(board, key.name);
            if (current == incoming || (is_uobject_valid_fast(current) && current != source.Get())) continue;
            Kh02ReflectedCall set(board, L"SetValueAsObject", "swap blackboard", 256);
            set.name(L"KeyName", key.name.c_str());
            set.object(L"ObjectValue", incoming);
            set.run();
            if (value(board, key.name) != incoming) throw std::runtime_error("swap_blackboard_handoff_not_observed");
            ++changed;
        }
        if (changed)
        {
            RC::Output::send<RC::LogLevel::Normal>(STR("[KH3APProbeCpp] Blackboard player handoff: keys={} incoming={}\n"),
                                                  changed, incoming->GetName());
        }
    }
    void release()
    {
        for (auto entry = ticks.rbegin(); entry != ticks.rend(); ++entry)
        {
            if (!entry->suspended) continue;
            auto* target = entry->object.Get();
            if (!target) continue;
            try
            {
                Kh02ReflectedCall restore(target, entry->actor ? L"SetActorTickEnabled" : L"SetComponentTickEnabled", "swap AI release", 256);
                restore.boolean(L"bEnabled", true);
                restore.run();
            }
            catch (const std::exception& error)
            {
                const std::string message(error.what());
                RC::Output::send<RC::LogLevel::Error>(STR("[KH3APProbeCpp] Swap tick restoration failed: {}\n"),
                                                      std::wstring(message.begin(), message.end()));
            }
        }
        ticks.clear();
        keys.clear();
        cameras.clear();
        source = {};
    }
}
