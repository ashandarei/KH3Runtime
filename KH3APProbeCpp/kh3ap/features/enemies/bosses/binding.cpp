#include "kh3ap/features/enemies/bosses/binding.h"
#include "kh3ap/features/enemies/titan_arena.h"
#include "kh3ap/features/enemies/level_entity_remap.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include <Unreal/FProperty.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>

#include <DynamicOutput/DynamicOutput.hpp>
#include <algorithm>
#include <cstring>

using namespace RC;
using namespace RC::Unreal;

namespace kh3ap::enemy_remap_detail
{
    namespace
    {
        constexpr auto boss_binding_observation_log_limit = 64;
    }

    [[nodiscard]] static auto resolve_loaded_boss_entity(std::wstring_view path) -> UObject*
    {
        // StaticFindObject's global cache can retain freed map exports after
        // Retry. Resolve the Serialize-tracked weak/slot-guarded instance.
        std::lock_guard lock{s_runtime_enemy_remap.loaded_level_entity_remaps_mutex};
        for (const auto& remap : s_runtime_enemy_remap.loaded_level_entity_remaps)
        {
            if (remap.kind != RuntimeLevelEntityRemapKind::boss || remap.object_path != path)
                continue;
            auto* entity = resolve_loaded_level_entity(remap);
            if (is_uobject_valid_fast(entity) && entity->GetPathName() == path)
                return entity;
        }
        return nullptr;
    }

    [[nodiscard]] auto read_boss_owner_property(
            UObject* owner, const wchar_t* property_name) -> UObject*
    {
        if (!is_uobject_valid_fast(owner))
        {
            return nullptr;
        }
        // Both links can be inherited by a replacement Blueprint pawn.
        // GetValuePtrByPropertyName checks only the immediate class.
        FProperty* const property = owner->GetPropertyByNameInChain(property_name);
        // The pinned pre-FField runtime reports false for
        // IsA<FObjectPropertyBase>() on these ObjectProperty fields.
        // Require their concrete reflected type, as the write path does.
        if (!property || property->GetClass().GetName() != STR("ObjectProperty") ||
            property->GetElementSize() != sizeof(UObject*))
        {
            return nullptr;
        }
        auto* const value = property->ContainerPtrToValuePtr<UObject*>(owner);
        return value && is_uobject_valid_fast(*value) ? *value : nullptr;
    }

    [[nodiscard]] static auto try_write_object_property(
            UObject* owner,
            std::wstring_view property_name,
            UObject* value) -> bool
    {
        if (!owner)
        {
            return false;
        }
        FProperty* const property = owner->GetPropertyByNameInChain(
                std::wstring{property_name}.c_str());
        if (!property ||
            property->GetClass().GetName() != STR("ObjectProperty") ||
            property->GetElementSize() <
                    static_cast<int32_t>(sizeof(UObject*)))
        {
            return false;
        }
        UObject** const object_value =
                property->ContainerPtrToValuePtr<UObject*>(owner);
        if (!object_value)
        {
            return false;
        }
        *object_value = value;
        return true;
    }

    [[nodiscard]] auto try_read_int32_object_property(
            UObject* owner,
            std::wstring_view property_name) -> std::optional<int32_t>
    {
        if (!owner)
        {
            return std::nullopt;
        }
        FProperty* const property = owner->GetPropertyByNameInChain(
                std::wstring{property_name}.c_str());
        const int32_t* const value =
                property &&
                        property->GetClass().GetName() == STR("IntProperty") &&
                        property->GetElementSize() ==
                                static_cast<int32_t>(sizeof(int32_t))
                ? property->ContainerPtrToValuePtr<int32_t>(owner)
                : nullptr;
        return value ? std::optional<int32_t>{*value} : std::nullopt;
    }

    [[nodiscard]] auto has_runtime_boss_replacement_class(UObject* pawn)
            -> bool
    {
        if (!pawn)
        {
            return false;
        }
        UClass* const pawn_class = pawn->GetClassPrivate();
        return pawn_class &&
               s_runtime_enemy_remap.boss.boss_replacement_class_names.contains(
                       pawn_class->GetNamePrivate());
    }

    [[nodiscard]] auto is_runtime_boss_replacement_pawn(UObject* pawn)
            -> bool
    {
        return pawn && has_runtime_boss_replacement_class(pawn);
    }

    [[nodiscard]] static auto tracked_boss_entity_class(
            UObject* level_entity,
            std::wstring* level_entity_path)
            -> std::optional<std::wstring>
    {
        if (!is_uobject_valid_fast(level_entity))
        {
            return std::nullopt;
        }
        const std::wstring object_path = level_entity->GetPathName();
        std::lock_guard lock{s_runtime_enemy_remap.boss.tracking.boss_state_mutex};
        const auto tracked =
                s_runtime_enemy_remap.boss.tracking.remapped_boss_entities.find(
                        object_path);
        if (tracked ==
            s_runtime_enemy_remap.boss.tracking.remapped_boss_entities.end())
        {
            return std::nullopt;
        }
        if (level_entity_path)
        {
            *level_entity_path = object_path;
        }
        return tracked->second;
    }

    [[nodiscard]] static auto bind_runtime_boss_pawn(
            UObject* pawn,
            UObject* level_entity,
            std::wstring level_entity_path)
            -> RuntimeBossEntityBinding
    {
        RuntimeBossEntityBinding binding{
                .level_entity = level_entity,
                .level_entity_path = std::move(level_entity_path),
        };
        if (!is_uobject_valid_fast(pawn) || !is_uobject_valid_fast(level_entity))
        {
            return {};
        }

        UObject* const pawn_level_entity = read_boss_owner_property(
                pawn,
                pawn_level_entity_property_name.data());
        UObject* const entity_actor = read_boss_owner_property(
                level_entity,
                level_entity_actor_property_name.data());
        const bool pawn_linked = pawn_level_entity == level_entity;
        const bool entity_linked = entity_actor == pawn;
        const bool pawn_write = pawn_linked || try_write_object_property(
                pawn,
                pawn_level_entity_property_name,
                level_entity);
        const bool entity_write = entity_linked || try_write_object_property(
                level_entity,
                level_entity_actor_property_name,
                pawn);
        if (!pawn_write || !entity_write)
        {
            Output::send<LogLevel::Error>(
                    STR("[KH3APProbeCpp] Boss runtime ownership binding failed "
                        "pawn_linked={} entity_linked={} pawn_write={} "
                        "entity_write={} entity={} polling=false\n"),
                    pawn_linked,
                    entity_linked,
                    pawn_write,
                    entity_write,
                    binding.level_entity_path);
            return {};
        }

        binding.repaired = !pawn_linked || !entity_linked;
        {
            std::lock_guard lock{s_runtime_enemy_remap.boss.tracking.boss_state_mutex};
            FWeakObjectPtr& previous_actor =
                    s_runtime_enemy_remap.boss.tracking.boss_bound_actors[
                            binding.level_entity_path];
            binding.new_actor = previous_actor.Get() != pawn;
            if (binding.new_actor)
            {
                previous_actor = pawn;
                s_runtime_enemy_remap.boss.tracking.boss_dispatched_wipe_out_groups.erase(
                        binding.level_entity_path);
            }
        }

        if (binding.repaired || binding.new_actor)
        {
            notify_titan_arena_binding(binding.level_entity_path);
            const uint32_t observation =
                    s_runtime_enemy_remap.boss.tracking.boss_binding_observation_count
                            .fetch_add(1) +
                    1;
            if (observation <= boss_binding_observation_log_limit)
            {
                Output::send<LogLevel::Warning>(
                        STR("[KH3APProbeCpp] Boss runtime ownership bound "
                            "sequence={} entity={} pawn={} repaired={} "
                            "new_actor={} pawn_was_linked={} "
                            "entity_was_linked={} "
                            "entity_get_actor_is_replacement=true "
                            "pawn_level_entity_is_source=true polling=false\n"),
                        observation,
                        binding.level_entity_path,
                        pawn->GetPathName(),
                        binding.repaired,
                        binding.new_actor,
                        pawn_linked,
                        entity_linked);
            }
        }
        return binding;
    }

    [[nodiscard]] auto resolve_and_bind_runtime_boss_pawn(UObject* pawn)
            -> RuntimeBossEntityBinding
    {
        if (!is_runtime_boss_replacement_pawn(pawn))
        {
            return {};
        }

        UObject* const linked_entity = read_boss_owner_property(
                pawn,
                pawn_level_entity_property_name.data());
        std::wstring linked_entity_path{};
        if (tracked_boss_entity_class(
                    linked_entity,
                    &linked_entity_path).has_value())
        {
            return bind_runtime_boss_pawn(
                    pawn,
                    linked_entity,
                    std::move(linked_entity_path));
        }

        std::vector<std::pair<std::wstring, std::wstring>> candidates{};
        {
            std::lock_guard lock{s_runtime_enemy_remap.boss.tracking.boss_state_mutex};
            candidates.reserve(
                    s_runtime_enemy_remap.boss.tracking.remapped_boss_entities.size());
            for (const auto& candidate :
                 s_runtime_enemy_remap.boss.tracking.remapped_boss_entities)
            {
                candidates.emplace_back(candidate);
            }
        }

        UClass* const pawn_class = pawn->GetClassPrivate();
        UObject* const pawn_outer = pawn->GetOuterPrivate();
        const std::wstring pawn_class_path =
                pawn_class
                ? pawn_class->GetPathName()
                : std::wstring{};
        UObject* unique_unbound_entity = nullptr;
        std::wstring unique_unbound_path{};
        for (const auto& [candidate_path, replacement_class_path] :
             candidates)
        {
            UObject* const candidate = resolve_loaded_boss_entity(candidate_path);
            if (!is_uobject_valid_fast(candidate) || candidate->GetOuterPrivate() != pawn_outer)
            {
                continue;
            }
            UObject* const candidate_actor =
                    read_boss_owner_property(
                            candidate,
                            level_entity_actor_property_name.data());
            if (candidate_actor == pawn)
            {
                return bind_runtime_boss_pawn(
                        pawn,
                        candidate,
                        candidate_path);
            }
            if (!candidate_actor &&
                replacement_class_path == pawn_class_path)
            {
                if (unique_unbound_entity)
                {
                    unique_unbound_entity = nullptr;
                    unique_unbound_path.clear();
                    break;
                }
                unique_unbound_entity = candidate;
                unique_unbound_path = candidate_path;
            }
        }
        return unique_unbound_entity
                ? bind_runtime_boss_pawn(
                          pawn,
                          unique_unbound_entity,
                          std::move(unique_unbound_path))
                : RuntimeBossEntityBinding{};
    }

    [[nodiscard]] auto resolve_tracked_runtime_boss_pawn(UObject* pawn)
            -> RuntimeBossEntityBinding
    {
        if (!is_uobject_valid_fast(pawn))
        {
            return {};
        }
        std::wstring entity_path{};
        {
            std::lock_guard lock{s_runtime_enemy_remap.boss.tracking.boss_state_mutex};
            for (const auto& [candidate_path, candidate_actor] :
                 s_runtime_enemy_remap.boss.tracking.boss_bound_actors)
            {
                if (candidate_actor.Get() == pawn)
                {
                    entity_path = candidate_path;
                    break;
                }
            }
        }
        if (entity_path.empty())
        {
            return {};
        }
        UObject* const level_entity = resolve_loaded_boss_entity(entity_path);
        return is_uobject_valid_fast(level_entity) &&
                        level_entity->GetOuterPrivate() == pawn->GetOuterPrivate()
                ? RuntimeBossEntityBinding{
                          .level_entity = level_entity,
                          .level_entity_path = std::move(entity_path),
                  }
                : RuntimeBossEntityBinding{};
    }

    [[nodiscard]] auto try_read_integral_param(
            UFunction* function,
            void* params,
            std::wstring_view param_name) -> std::optional<uint64_t>
    {
        FProperty* const property = function && params
                ? find_function_param_by_name(function, param_name)
                : nullptr;
        const int32_t size = property ? property->GetElementSize() : 0;
        const void* const value = property
                ? property->ContainerPtrToValuePtr<void>(params)
                : nullptr;
        if (!value || size <= 0 || size > 8)
        {
            return std::nullopt;
        }
        uint64_t result = 0;
        std::memcpy(&result, value, static_cast<size_t>(size));
        return result;
    }

    [[nodiscard]] auto tracked_boss_source_class(
            std::wstring_view level_entity_path)
            -> std::optional<std::wstring>
    {
        std::lock_guard lock{s_runtime_enemy_remap.boss.tracking.boss_state_mutex};
        const auto source = s_runtime_enemy_remap
                                    .boss.tracking.remapped_boss_source_classes.find(
                                            std::wstring{
                                                    level_entity_path});
        return source != s_runtime_enemy_remap
                                 .boss.tracking.remapped_boss_source_classes.end()
                ? std::optional<std::wstring>{source->second}
                : std::nullopt;
    }
}
