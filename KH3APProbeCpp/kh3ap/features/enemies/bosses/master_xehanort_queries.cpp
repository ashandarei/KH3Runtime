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
#include <span>
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
        constexpr std::array<const wchar_t*, 5> yozora_queries{
            L"e_ex781_EQ_AttaackToCenterPos_Q", L"e_ex781_EQ_CameraRotPosInMap_Q",
            L"e_ex781_EQ_CameraRotPosInMustMap_Q", L"e_ex781_EQ_MapCenterCameraRotSidePos_Q",
            L"e_ex781_EQ_MapCenterPos_Q"};
        constexpr std::array<size_t, 5> yozora_counts{4, 2, 2, 3, 2};

        constexpr std::array<const wchar_t*, 3> vanitas_queries{
            L"e_ex360_EQ_AirWarpGround_Q", L"e_ex360_EQ_AirWarp_Q",
            L"e_ex360_EQ_PS3D_KBCloneBurst_Circle"};
        constexpr std::array<size_t, 3> vanitas_counts{2, 2, 2};
        using Clock = std::chrono::steady_clock;
        struct QueryGroup
        {
            const wchar_t* folder;
            std::span<const wchar_t* const> names;
            std::span<const size_t> counts;
            std::vector<FName> fnames;
            std::atomic<bool> configured{}, activated{};
            std::atomic<uint64_t> revision{1};
            uint64_t seen_revision{};
            Clock::time_point deadline{}, next_attempt{};
            bool unfinished{};
        };
        QueryGroup master{L"e_ex367", query_names, expected_refs};
        QueryGroup yozora{L"e_ex781", yozora_queries, yozora_counts};
        QueryGroup vanitas{L"e_ex360", vanitas_queries, vanitas_counts};
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
            for (const auto* key : {L"ArcDirection", L"LineA", L"LineB"})
            {
                auto* direction = node->GetPropertyByNameInChain(key);
                if (!direction || !direction->IsA<FStructProperty>())
                {
                    continue;
                }
                auto* structure = static_cast<FStructProperty*>(direction)->GetStruct();
                if (!is_uobject_valid_fast(structure))
                {
                    continue;
                }
                // Only location endpoints use the arena center. Keep authored rotations.
                for (const auto* endpoint : {L"LineFrom", L"LineTo"})
                {
                    references += patch_reference(find_struct_field_by_name(structure, endpoint),
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

        auto patch_loaded_queries(const QueryGroup& group) -> bool
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
            for (size_t index = 0; index < group.names.size(); ++index)
            {
                const std::wstring name{group.names[index]};
                const auto path = std::wstring{L"/Game/Blueprints/Enemy/"} + group.folder +
                                  L"/EQS/" + name + L"." + name;
                auto* query = find_loaded_runtime_asset(path);
                if (!is_uobject_valid_fast(query) ||
                    query->GetClassPrivate()->GetNamePrivate() != FName{L"EnvQuery", FNAME_Find})
                {
                    complete = false;
                    continue;
                }
                const auto count = patch_node(query, query, zero, center, 0);
                total += count;
                complete = complete && count == group.counts[index];
            }
            if (complete)
            {
                Output::send<LogLevel::Normal>(STR("[KH3APProbeCpp] {} arena query centers bound "
                                                   "assets={} location_refs={}\n"),
                                               group.folder, group.names.size(), total);
            }
            return complete;
        }
    } // namespace

    static void configure(QueryGroup& group)
    {
        if (group.configured.load(std::memory_order_acquire))
        {
            return;
        }
        for (const auto* name : group.names)
        {
            group.fnames.emplace_back(name, FNAME_Add);
        }
        group.configured.store(true, std::memory_order_release);
    }

    void configure_master_query_contexts()
    {
        configure(master);
    }

    void notify_master_query_created(UObject* object)
    {
        if (!master.configured.load(std::memory_order_acquire) &&
            !yozora.configured.load(std::memory_order_acquire) &&
            !vanitas.configured.load(std::memory_order_acquire))
        {
            return;
        }
        static const FName query_class_name{L"EnvQuery", FNAME_Add};
        if (!object || !object->GetClassPrivate() ||
            object->GetClassPrivate()->GetNamePrivate() != query_class_name)
        {
            return;
        }
        const auto name = object->GetNamePrivate();
        for (auto* group : {&master, &yozora, &vanitas})
        {
            if (!group->configured.load(std::memory_order_acquire))
            {
                continue;
            }
            for (const auto& expected : group->fnames)
            {
                if (name == expected)
                {
                    group->revision.fetch_add(1, std::memory_order_release);
                    return;
                }
            }
        }
    }

    static void process(QueryGroup& group)
    {
        if (!group.activated.load(std::memory_order_acquire))
        {
            return;
        }
        const auto current = group.revision.load(std::memory_order_acquire);
        if (!group.unfinished && current == group.seen_revision)
        {
            return;
        }
        const auto now = Clock::now();
        if (current != group.seen_revision)
        {
            group.seen_revision = current;
            group.unfinished = true;
            group.deadline = now + std::chrono::seconds(2);
            group.next_attempt = now;
        }
        if (now < group.next_attempt)
        {
            return;
        }
        group.unfinished = !patch_loaded_queries(group) && now < group.deadline;
        group.next_attempt = now + std::chrono::milliseconds(100);
    }

    void activate_master_query_contexts()
    {
        master.activated.store(true, std::memory_order_release);
        process(master);
    }

    void activate_yozora_query_contexts()
    {
        configure(yozora);
        yozora.activated.store(true, std::memory_order_release);
        process(yozora);
    }

    void activate_vanitas_query_contexts()
    {
        configure(vanitas);
        vanitas.activated.store(true, std::memory_order_release);
        process(vanitas);
    }

    void process_pending_master_query_contexts()
    {
        process(master);
        process(yozora);
        process(vanitas);
    }
} // namespace kh3ap
