#include "master_xehanort_queries.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/features/enemies/bosses/binding.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#include <Unreal/Property/FStructProperty.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UScriptStruct.hpp>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <string>
#include <vector>

namespace kh3ap
{
    using namespace RC;
    using namespace RC::Unreal;
    namespace
    {
        constexpr std::array<const wchar_t*, 7> query_names{
            L"e_ex367_EQ_CameraRotPosInMap_Q",       L"e_ex367_EQ_CameraRotPosInMustMap_Q",
            L"e_ex367_EQ_KeyBladeThrowEndWarpPos_Q", L"e_ex367_EQ_LastKeyBladeLaserPos_Q",
            L"e_ex367_EQ_MapCenterCameraRotPos_Q",   L"e_ex367_EQ_MapCenterPos_Q",
            L"e_ex367_EQ_MapCenterRotPos_Q"};
        constexpr std::array<size_t, 7> expected_refs{2, 2, 2, 1, 2, 2, 2};
        std::array<FName, 7> query_fnames{};
        FName query_class_name{};
        std::atomic<bool> configured{}, activated{};
        std::atomic<uint64_t> revision{1};
        uint64_t seen_revision{};
        using Clock = std::chrono::steady_clock;
        Clock::time_point deadline{}, next_attempt{};
        bool unfinished{};

        auto owned_by(UObject* node, UObject* root) -> bool
        {
            for (size_t depth = 0; depth < 16 && is_uobject_valid_fast(node); ++depth)
            {
                if (node == root)
                {
                    return true;
                }
                node = node->GetOuterPrivate();
            }
            return false;
        }

        auto object_array(UObject* object, const wchar_t* name) -> std::vector<UObject*>
        {
            auto* field = object->GetPropertyByNameInChain(name);
            if (!field || field->GetClass().GetName() != L"ArrayProperty")
            {
                return {};
            }
            auto* inner = const_cast<FProperty*>(static_cast<FArrayProperty*>(field)->GetInner());
            if (!inner || inner->GetClass().GetName() != L"ObjectProperty" ||
                inner->GetElementSize() != sizeof(UObject*))
            {
                return {};
            }
            struct Array
            {
                UObject** data;
                int32_t count, capacity;
            };
            Array values{};
            std::memcpy(&values, field->ContainerPtrToValuePtr<void>(object), sizeof(values));
            if (values.count < 0 || values.count > 64 || values.capacity < values.count ||
                (values.count && !values.data))
            {
                return {};
            }
            if (!values.count)
            {
                return {};
            }
            return {values.data, values.data + values.count};
        }

        auto patch_reference(FProperty* field, void* container, UObject* zero, UObject* center)
            -> size_t
        {
            if (!field || field->GetElementSize() != sizeof(UObject*) ||
                (field->GetClass().GetName() != L"ClassProperty" &&
                 field->GetClass().GetName() != L"ObjectProperty"))
            {
                return 0;
            }
            auto** value = field->ContainerPtrToValuePtr<UObject*>(container);
            if (!value || (*value != zero && *value != center))
            {
                return 0;
            }
            *value = center;
            return *value == center ? 1 : 0;
        }

        auto patch_node(UObject* node, UObject* root, UObject* zero, UObject* center, size_t depth)
            -> size_t
        {
            if (depth > 4 || !owned_by(node, root))
            {
                return 0;
            }
            size_t references = 0;
            for (const auto* name : {L"CircleCenter", L"DistanceTo"})
            {
                references +=
                    patch_reference(node->GetPropertyByNameInChain(name), node, zero, center);
            }
            auto* direction = node->GetPropertyByNameInChain(L"ArcDirection");
            if (direction && direction->GetClass().GetName() == L"StructProperty")
            {
                auto* structure = static_cast<FStructProperty*>(direction)->GetStruct();
                // Rotation is deliberately left alone: a fixed yaw is not
                // a world-space point. Only the LineFrom location moves.
                if (is_uobject_valid_fast(structure))
                {
                    references += patch_reference(find_struct_field_by_name(structure, L"LineFrom"),
                                                  direction->ContainerPtrToValuePtr<void>(node),
                                                  zero, center);
                }
            }
            if (auto* generator = enemy_remap_detail::read_boss_owner_property(node, L"Generator"))
            {
                references += patch_node(generator, root, zero, center, depth + 1);
            }
            for (const auto* list : {L"Options", L"Generators", L"Tests"})
            {
                for (auto* child : object_array(node, list))
                {
                    references += patch_node(child, root, zero, center, depth + 1);
                }
            }
            return references;
        }

        auto patch_loaded_queries() -> bool
        {
            auto* zero = UObjectGlobals::StaticFindObject<UClass*>(
                nullptr, nullptr, L"/Script/TresGame.TresEnvQueryContext_NullLocation");
            auto* center = UObjectGlobals::StaticFindObject<UClass*>(
                nullptr, nullptr, L"/Script/TresGame.TresEnvQueryContext_XIIIL_CenterLocation");
            if (!is_uobject_valid_fast(zero) || !is_uobject_valid_fast(center))
            {
                return false;
            }
            bool complete = true;
            size_t total = 0;
            for (size_t index = 0; index < query_names.size(); ++index)
            {
                const std::wstring name{query_names[index]};
                const auto path = L"/Game/Blueprints/Enemy/e_ex367/EQS/" + name + L"." + name;
                auto* query = find_loaded_runtime_asset(path);
                if (!is_uobject_valid_fast(query) ||
                    query->GetClassPrivate()->GetNamePrivate() != query_class_name)
                {
                    complete = false;
                    continue;
                }
                const auto count = patch_node(query, query, zero, center, 0);
                total += count;
                complete = complete && count == expected_refs[index];
            }
            if (complete)
            {
                Output::send<LogLevel::Normal>(
                    STR("[KH3APProbeCpp] Master Xehanort query centers bound "
                        "assets=7 location_refs={} "
                        "context=TresEnvQueryContext_XIIIL_CenterLocation "
                        "rotation_contexts_preserved=true scope=master_query_templates "
                        "polling=false\n"),
                    total);
            }
            return complete;
        }
    } // namespace

    void configure_master_query_contexts()
    {
        query_class_name = FName{L"EnvQuery", FNAME_Add};
        for (size_t index = 0; index < query_names.size(); ++index)
        {
            query_fnames[index] = FName{query_names[index], FNAME_Add};
        }
        configured.store(true, std::memory_order_release);
    }

    void notify_master_query_created(UObject* object)
    {
        if (!configured.load(std::memory_order_acquire) || !object || !object->GetClassPrivate() ||
            object->GetClassPrivate()->GetNamePrivate() != query_class_name)
        {
            return;
        }
        const auto name = object->GetNamePrivate();
        for (const auto& expected : query_fnames)
        {
            if (name == expected)
            {
                revision.fetch_add(1, std::memory_order_release);
                return;
            }
        }
    }

    void activate_master_query_contexts()
    {
        activated.store(true, std::memory_order_release);
        process_pending_master_query_contexts();
    }

    void process_pending_master_query_contexts()
    {
        if (!activated.load(std::memory_order_acquire))
        {
            return;
        }
        const auto current = revision.load(std::memory_order_acquire);
        if (!unfinished && current == seen_revision)
        {
            return;
        }
        const auto now = Clock::now();
        if (current != seen_revision)
        {
            seen_revision = current;
            unfinished = true;
            deadline = now + std::chrono::seconds(2);
            next_attempt = now;
        }
        if (now < next_attempt)
        {
            return;
        }
        if (patch_loaded_queries() || now >= deadline)
        {
            unfinished = false;
        }
        next_attempt = now + std::chrono::milliseconds(100);
    }
} // namespace kh3ap
