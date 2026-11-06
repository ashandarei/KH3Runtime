#include "kh3ap/features/enemies/bosses/completion.h"
#include "kh3ap/features/enemies/bosses/hp_limits.h"
#include "kh3ap/features/enemies/level_entity_remap.h"
#include "kh3ap/features/enemies/fun_heartless_service.h"
#include "kh3ap/features/enemies/titan_arena.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include <Unreal/FArchive.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/FString.hpp>
#include <Unreal/Property/FClassProperty.hpp>
#include <Unreal/Property/FObjectProperty.hpp>
#include <Unreal/Property/FStructProperty.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UScriptStruct.hpp>

#include <DynamicOutput/DynamicOutput.hpp>
#include <algorithm>
#include <cstring>

using namespace RC;
using namespace RC::Unreal;

namespace kh3ap::enemy_remap_detail
{
    namespace
    {
        constexpr auto enemy_class_rebind_interval = std::chrono::milliseconds(100);
        constexpr auto enemy_replacement_deadline = std::chrono::seconds(15);
        constexpr auto enemy_remap_observation_log_limit = 64;
    }

    [[nodiscard]] auto resolve_loaded_level_entity(
            const RuntimeLoadedLevelEntityRemap& remap) -> UObject*
    {
        if (UObject* const weak_object = remap.level_entity.Get();
            is_uobject_valid_fast(weak_object))
        {
            return weak_object;
        }

        // Some KH3 map exports retain a zero weak serial, which makes
        // FWeakObjectPtr::Get fail even while the UObject remains live.
        // The fallback is still protected: is_uobject_valid_fast checks
        // the exact GUObjectArray slot in O(1), and the saved index guards
        // against an address being recycled for another UObject.
        UObject* const slot_guarded = remap.slot_guarded_level_entity;
        if (!is_uobject_valid_fast(slot_guarded) ||
            slot_guarded->GetInternalIndex() != remap.object_index)
        {
            return nullptr;
        }
        return slot_guarded;
    }

    static auto resolve_actor_class_path_property(UObject* object) -> FProperty*
    {
        if (!object)
        {
            return nullptr;
        }
        FProperty* const property = object->GetPropertyByNameInChain(
                actor_class_path_property_name.data());
        if (!property ||
            property->GetClass().GetName() != STR("StructProperty") ||
            property->GetElementSize() < static_cast<int32_t>(sizeof(FString)))
        {
            return nullptr;
        }

        auto* const struct_property =
                static_cast<FStructProperty*>(property);
        UScriptStruct* const script_struct = struct_property->GetStruct();
        if (!script_struct ||
            script_struct->GetName() != string_asset_reference_struct_name)
        {
            return nullptr;
        }
        return property;
    }

    static auto resolve_actor_class_property(UObject* object)
            -> FObjectPropertyBase*
    {
        if (!object)
        {
            return nullptr;
        }
        FProperty* const property = object->GetPropertyByNameInChain(
                actor_class_property_name.data());
        if (!property || !property->IsA<FClassProperty>() ||
            property->GetElementSize() <
                    static_cast<int32_t>(sizeof(UObject*)))
        {
            return nullptr;
        }
        return static_cast<FObjectPropertyBase*>(property);
    }

    [[nodiscard]] static auto as_actor_class(UObject* object) -> UClass*
    {
        // StaticFindObject can hand back an entry that was valid when its
        // global cache was populated but has since been collected. A raw
        // UObject::IsA dereferences that stale object's class chain and
        // can fatally fault while the GoA menu prewarms toggle classes.
        return is_uobject_a_fast(object, UClass::StaticClass())
                ? static_cast<UClass*>(object)
                : nullptr;
    }

    [[nodiscard]] static auto find_loaded_actor_class(
            std::wstring_view class_path) -> UClass*
    {
        // A typed lookup bypasses UE4SS's unguarded global path cache,
        // which can retain the old class after GC and a subsequent reload.
        return as_actor_class(
                UObjectGlobals::StaticFindObject<UObject*>(
                        UClass::StaticClass(),
                        nullptr,
                        std::wstring{class_path}.c_str()));
    }

    static auto queue_actor_class_prewarm_path(
            std::wstring_view class_path) -> bool
    {
        if (!class_path.starts_with(STR("/Game/")) ||
            !class_path.ends_with(STR("_C")))
        {
            return false;
        }

        std::lock_guard lock{
                s_runtime_enemy_remap.class_prewarm_mutex};
        std::wstring owned_path{class_path};
        if (!s_runtime_enemy_remap.pending_class_prewarm_path_set
                     .emplace(owned_path)
                     .second)
        {
            return false;
        }
        s_runtime_enemy_remap.pending_class_prewarm_paths.emplace_back(
                std::move(owned_path));
        return true;
    }

    [[nodiscard]] static auto first_live_level_entity_context() -> UObject*
    {
        std::lock_guard lock{
                s_runtime_enemy_remap
                        .loaded_level_entity_remaps_mutex};
        for (const RuntimeLoadedLevelEntityRemap& remap :
             s_runtime_enemy_remap.loaded_level_entity_remaps)
        {
            if (UObject* const level_entity =
                        resolve_loaded_level_entity(remap))
            {
                return level_entity;
            }
        }
        return nullptr;
    }

    auto queue_loaded_toggle_class_prewarm_paths() -> uint32_t
    {
        std::unordered_set<std::wstring> unique_paths{};
        {
            std::lock_guard lock{
                    s_runtime_enemy_remap
                            .loaded_level_entity_remaps_mutex};
            unique_paths.reserve(
                    s_runtime_enemy_remap
                            .loaded_level_entity_remaps.size() *
                    2);
            for (const RuntimeLoadedLevelEntityRemap& remap :
                 s_runtime_enemy_remap.loaded_level_entity_remaps)
            {
                if (!resolve_loaded_level_entity(remap))
                {
                    continue;
                }
                unique_paths.emplace(remap.source_class_path);
                unique_paths.emplace(remap.replacement_class_path);
            }
        }

        uint32_t queued = 0;
        for (const std::wstring& class_path : unique_paths)
        {
            queued += queue_actor_class_prewarm_path(class_path) ? 1U
                                                                : 0U;
        }
        return queued;
    }

    [[nodiscard]] static auto pop_pending_class_prewarm_path()
            -> std::optional<std::wstring>
    {
        std::lock_guard lock{
                s_runtime_enemy_remap.class_prewarm_mutex};
        if (s_runtime_enemy_remap.pending_class_prewarm_paths.empty())
        {
            return std::nullopt;
        }
        std::wstring class_path = std::move(
                s_runtime_enemy_remap.pending_class_prewarm_paths.front());
        s_runtime_enemy_remap.pending_class_prewarm_paths.pop_front();
        s_runtime_enemy_remap.pending_class_prewarm_path_set.erase(
                class_path);
        return class_path;
    }

    auto process_one_pending_class_prewarm() -> bool
    {
        const auto class_path = pop_pending_class_prewarm_path();
        if (!class_path.has_value())
        {
            return false;
        }

        if (find_loaded_actor_class(*class_path))
        {
            std::lock_guard lock{
                    s_runtime_enemy_remap.class_prewarm_mutex};
            s_runtime_enemy_remap.class_prewarm_requested_at.erase(
                    *class_path);
            return true;
        }

        const auto now = std::chrono::steady_clock::now();
        {
            std::lock_guard lock{
                    s_runtime_enemy_remap.class_prewarm_mutex};
            const auto previous = s_runtime_enemy_remap
                                          .class_prewarm_requested_at.find(
                                                  *class_path);
            if (previous != s_runtime_enemy_remap
                                    .class_prewarm_requested_at.end() &&
                now - previous->second <
                        class_prewarm_repeat_request_delay)
            {
                return true;
            }
        }

        UObject* const world_context =
                first_live_level_entity_context();
        if (!world_context)
        {
            return true;
        }

        const uint32_t uuid_sequence = s_runtime_enemy_remap
                                               .next_class_prewarm_uuid
                                               .fetch_add(
                                                       1,
                                                       std::memory_order_relaxed);
        const int32_t latent_uuid = static_cast<int32_t>(
                class_prewarm_latent_uuid_base +
                (uuid_sequence & 0x00000fffU));
        std::wstring resolution{};
        const bool requested = request_runtime_asset_class_load(
                world_context,
                *class_path,
                &resolution,
                latent_uuid);
        if (requested)
        {
            std::lock_guard lock{
                    s_runtime_enemy_remap.class_prewarm_mutex};
            s_runtime_enemy_remap.class_prewarm_requested_at[
                    *class_path] = now;
        }
        if (!requested)
        {
            Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] GoA class prewarm failed "
                        "path={} resolution={} blocking=false\n"),
                    *class_path,
                    resolution.empty() ? std::wstring{STR("<none>")}
                                       : resolution);
        }
        return true;
    }

    enum class ActorClassCacheRebindResult
    {
        unchanged,
        rebound,
        unavailable,
    };

    [[nodiscard]] static auto rebind_actor_class_cache(
            UObject* level_entity,
            UClass* desired_class) -> ActorClassCacheRebindResult
    {
        FObjectPropertyBase* property =
                s_runtime_enemy_remap.actor_class_property;
        if (!property)
        {
            property = resolve_actor_class_property(level_entity);
            s_runtime_enemy_remap.actor_class_property = property;
        }
        void* const value_ptr = property
                ? property->ContainerPtrToValuePtr<void>(level_entity)
                : nullptr;
        if (!value_ptr)
        {
            if (!s_runtime_enemy_remap.class_cache_failure_logged.exchange(
                        true))
            {
                Output::send<LogLevel::Error>(
                        STR("[KH3APProbeCpp] Level entity live toggle "
                            "could not resolve m_ActorClass cache\n"));
            }
            return ActorClassCacheRebindResult::unavailable;
        }

        UObject* const cached_class =
                property->GetObjectPropertyValue(value_ptr);
        if (cached_class == desired_class)
        {
            return ActorClassCacheRebindResult::unchanged;
        }
        property->SetObjectPropertyValue(value_ptr, desired_class);
        return property->GetObjectPropertyValue(value_ptr) == desired_class
                ? ActorClassCacheRebindResult::rebound
                : ActorClassCacheRebindResult::unavailable;
    }

    // Bounded, on-demand completion work only; no global prewarming.
    void process_pending_class_rebinds()
    {
        const auto now = std::chrono::steady_clock::now();
        std::lock_guard lock{s_runtime_enemy_remap.loaded_level_entity_remaps_mutex};
        auto& pending = s_runtime_enemy_remap.pending_class_rebinds;
        if (pending.empty() || now < s_runtime_enemy_remap.next_class_rebind_poll) return;
        s_runtime_enemy_remap.next_class_rebind_poll = now + enemy_class_rebind_interval;
        std::erase_if(pending, [now](const auto& entry) {
            auto* entity = resolve_loaded_level_entity(entry.remap);
            if (!entity) return true;
            const bool enabled = entry.remap.kind == RuntimeLevelEntityRemapKind::boss
                    ? s_runtime_enemy_remap.boss.boss_enabled.load() : s_runtime_enemy_remap.enemy_enabled.load();
            const auto& desired = enabled ? entry.remap.replacement_class_path : entry.remap.source_class_path;
            if (desired != entry.desired_class_path) return true;
            if (auto* klass = find_loaded_actor_class(desired))
            {
                const auto result = rebind_actor_class_cache(entity, klass);
                if (result != ActorClassCacheRebindResult::unavailable)
                {
                    Output::send<LogLevel::Warning>(
                            STR("[KH3APProbeCpp] Toggle class cache ready entity={} path={}\n"),
                            entry.remap.object_path, desired);
                    return true;
                }
            }
            if (now >= entry.expires)
            {
                if (entry.remap.kind == RuntimeLevelEntityRemapKind::boss)
                    s_runtime_enemy_remap.boss.boss_class_load_failed = true;
                Output::send<LogLevel::Error>(
                        STR("[KH3APProbeCpp] Toggle class cache timed out entity={} path={}\n"),
                        entry.remap.object_path, desired);
                return true;
            }
            return false;
        });
    }

    static auto remember_loaded_level_entity_remap(
            UObject* level_entity,
            std::wstring_view level_entity_path,
            std::wstring_view source_class_path,
            std::wstring_view replacement_class_path,
            RuntimeLevelEntityRemapKind kind) -> void
    {
        if (!level_entity)
        {
            return;
        }

        if constexpr (eager_toggle_class_prewarm_enabled)
        {
            // The map normally loads only the currently selected class.
            // Queue both directions so a later in-menu toggle only has to
            // swap resident class pointers.
            queue_actor_class_prewarm_path(source_class_path);
            queue_actor_class_prewarm_path(replacement_class_path);
        }

        const uintptr_t object_identity =
                reinterpret_cast<uintptr_t>(level_entity);
        std::lock_guard lock{
                s_runtime_enemy_remap.loaded_level_entity_remaps_mutex};
        auto& remaps = s_runtime_enemy_remap.loaded_level_entity_remaps;
        for (auto iterator = remaps.begin(); iterator != remaps.end();)
        {
            UObject* const tracked = resolve_loaded_level_entity(*iterator);
            if (!tracked)
            {
                iterator = remaps.erase(iterator);
                continue;
            }
            if (tracked == level_entity &&
                iterator->object_identity == object_identity)
            {
                iterator->level_entity = level_entity;
                iterator->slot_guarded_level_entity = level_entity;
                iterator->object_index =
                        level_entity->GetInternalIndex();
                iterator->object_path = level_entity_path;
                iterator->source_class_path = source_class_path;
                iterator->replacement_class_path = replacement_class_path;
                iterator->kind = kind;
                return;
            }
            ++iterator;
        }

        FWeakObjectPtr weak_level_entity{};
        weak_level_entity = level_entity;
        remaps.emplace_back(RuntimeLoadedLevelEntityRemap{
                .level_entity = weak_level_entity,
                .slot_guarded_level_entity = level_entity,
                .object_index = level_entity->GetInternalIndex(),
                .object_identity = object_identity,
                .object_path = std::wstring{level_entity_path},
                .source_class_path = std::wstring{source_class_path},
                .replacement_class_path =
                        std::wstring{replacement_class_path},
                .kind = kind,
        });
    }

    [[nodiscard]] auto reconcile_loaded_level_entity_remaps(
            RuntimeLevelEntityRemapKind kind,
            bool enabled) -> RuntimeLevelEntityReconcileResult
    {
        RuntimeLevelEntityReconcileResult result{};
        std::vector<RuntimeLoadedLevelEntityRemap> live_boss_remaps{};
        std::unordered_map<std::wstring, UClass*>
                resolved_actor_classes{};
        std::unordered_set<std::wstring>
                failed_actor_classes{};
        {
            std::lock_guard lock{
                    s_runtime_enemy_remap
                            .loaded_level_entity_remaps_mutex};
            std::erase_if(s_runtime_enemy_remap.pending_class_rebinds,
                    [kind](const auto& pending) { return pending.remap.kind == kind; });
            if (kind == RuntimeLevelEntityRemapKind::boss)
                s_runtime_enemy_remap.boss.boss_class_load_failed = false;
            auto& remaps =
                    s_runtime_enemy_remap.loaded_level_entity_remaps;
            for (auto iterator = remaps.begin();
                 iterator != remaps.end();)
            {
                UObject* const level_entity =
                        resolve_loaded_level_entity(*iterator);
                if (!level_entity)
                {
                    ++result.expired;
                    iterator = remaps.erase(iterator);
                    continue;
                }
                if (iterator->kind != kind)
                {
                    ++iterator;
                    continue;
                }

                ++result.tracked;
                ++result.live;
                FProperty* property =
                        s_runtime_enemy_remap.actor_class_path_property;
                if (!property)
                {
                    property = resolve_actor_class_path_property(
                            level_entity);
                    s_runtime_enemy_remap.actor_class_path_property =
                            property;
                }
                void* const value_ptr = property
                        ? property->ContainerPtrToValuePtr<void>(
                                  level_entity)
                        : nullptr;
                if (value_ptr)
                {
                    auto* const current_class_path =
                            static_cast<FString*>(value_ptr);
                    const std::wstring& desired_class_path = enabled
                            ? iterator->replacement_class_path
                            : iterator->source_class_path;
                    if (current_class_path->GetCharArray() !=
                        desired_class_path)
                    {
                        *current_class_path = FString(
                                desired_class_path.c_str());
                        ++result.changed;
                    }
                    {
                        UClass* desired_class = nullptr;
                        if (const auto resolved =
                                    resolved_actor_classes.find(
                                            desired_class_path);
                            resolved != resolved_actor_classes.end())
                        {
                            desired_class = resolved->second;
                        }
                        else if (!failed_actor_classes.contains(
                                         desired_class_path))
                        {
                            desired_class = find_loaded_actor_class(
                                    desired_class_path);
                            if (!desired_class)
                            {
                                std::wstring resolution;
                                const int32_t uuid = static_cast<int32_t>(class_prewarm_latent_uuid_base +
                                        (s_runtime_enemy_remap.next_class_prewarm_uuid.fetch_add(1) & 0xfffU));
                                if (!request_runtime_asset_class_load(level_entity, desired_class_path, &resolution, uuid))
                                {
                                    Output::send<LogLevel::Warning>(
                                            STR("[KH3APProbeCpp] Toggle class request failed path={} reason={}\n"),
                                            desired_class_path, resolution);
                                }
                            }
                            if (desired_class)
                            {
                                resolved_actor_classes.emplace(
                                        desired_class_path,
                                        desired_class);
                            }
                            else
                            {
                                failed_actor_classes.emplace(
                                        desired_class_path);
                            }
                        }

                        if (!desired_class)
                        {
                            ++result.class_cache_rebind_failed;
                            s_runtime_enemy_remap.pending_class_rebinds.push_back({
                                    *iterator, desired_class_path,
                                    std::chrono::steady_clock::now() + enemy_replacement_deadline});
                        }
                        else
                        {
                            switch (rebind_actor_class_cache(
                                    level_entity,
                                    desired_class))
                            {
                            case ActorClassCacheRebindResult::rebound:
                                ++result.class_cache_rebound;
                                break;
                            case ActorClassCacheRebindResult::unavailable:
                                ++result.class_cache_rebind_failed;
                                break;
                            case ActorClassCacheRebindResult::unchanged:
                                break;
                            }
                        }
                    }
                }
                if (kind == RuntimeLevelEntityRemapKind::boss)
                {
                    live_boss_remaps.emplace_back(*iterator);
                }
                ++iterator;
            }
        }

        if (!failed_actor_classes.empty())
        {
            std::vector<std::wstring> failed_paths{
                    failed_actor_classes.begin(),
                    failed_actor_classes.end()};
            std::sort(failed_paths.begin(), failed_paths.end());
            std::wstring summary{};
            constexpr size_t max_logged_failure_paths = 8;
            const size_t logged = std::min(
                    failed_paths.size(),
                    max_logged_failure_paths);
            for (size_t index = 0; index < logged; ++index)
            {
                if (!summary.empty())
                {
                    summary.append(STR("|"));
                }
                summary.append(failed_paths[index]);
            }
            if (failed_paths.size() > logged)
            {
                summary.append(STR("|..."));
            }
            Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] GoA class cache rebind "
                        "unresolved unique_paths={} paths={}\n"),
                    static_cast<uint64_t>(failed_paths.size()),
                    summary);
        }

        if (kind == RuntimeLevelEntityRemapKind::boss)
        {
            {
                std::lock_guard lock{
                        s_runtime_enemy_remap.boss.tracking.boss_state_mutex};
                for (const RuntimeLoadedLevelEntityRemap& remap :
                     live_boss_remaps)
                {
                    if (enabled)
                    {
                        s_runtime_enemy_remap.boss.tracking.remapped_boss_entities[
                                remap.object_path] =
                                remap.replacement_class_path;
                        s_runtime_enemy_remap
                                .boss.tracking.remapped_boss_source_classes[
                                        remap.object_path] =
                                remap.source_class_path;
                    }
                    else
                    {
                        s_runtime_enemy_remap.boss.tracking.remapped_boss_entities.erase(
                                remap.object_path);
                        s_runtime_enemy_remap
                                .boss.tracking.remapped_boss_source_classes.erase(
                                        remap.object_path);
                    }
                    s_runtime_enemy_remap.boss.tracking.boss_bound_actors.erase(
                            remap.object_path);
                    s_runtime_enemy_remap
                            .boss.tracking.boss_dispatched_wipe_out_groups.erase(
                                    remap.object_path);
                }
            }
            if (enabled)
            {
                for (const RuntimeLoadedLevelEntityRemap& remap :
                     live_boss_remaps)
                {
                    if (UObject* const level_entity =
                                resolve_loaded_level_entity(remap))
                    {
                        register_level_entity_wipe_out_groups(
                                level_entity,
                                remap.object_path);
                    }
                }
            }
        }
        return result;
    }

    static auto apply_runtime_enemy_replacement(
            UObject* object,
            std::wstring_view hook_route) -> void
    {
        if (!s_runtime_enemy_remap.active || !object)
        {
            return;
        }

        UClass* const object_class = object->GetClassPrivate();
        if (!object_class ||
            !s_runtime_enemy_remap.level_entity_class_f_name.has_value() ||
            object_class->GetNamePrivate() !=
                    *s_runtime_enemy_remap.level_entity_class_f_name)
        {
            return;
        }

        const std::wstring object_path = object->GetPathName();
        const bool enemy_enabled =
                s_runtime_enemy_remap.enemy_enabled.load();
        const bool boss_enabled =
                s_runtime_enemy_remap.boss.boss_enabled.load();
        if (!s_runtime_enemy_remap.boss.boss_class_replacements.empty())
        {
            register_luxord_source_entity(object, object_path);
            // Keep the current map's wipe-out membership available even
            // while boss randomization is disabled. This makes enabling
            // it later in the same loaded map deterministic and avoids a
            // GUObjectArray scan at toggle time.
            register_level_entity_wipe_out_groups(
                    object,
                    object_path);
        }

        FProperty* property =
                s_runtime_enemy_remap.actor_class_path_property;
        if (!property)
        {
            property = resolve_actor_class_path_property(object);
            s_runtime_enemy_remap.actor_class_path_property = property;
        }
        void* const value_ptr = property
                ? property->ContainerPtrToValuePtr<void>(object)
                : nullptr;
        if (!value_ptr)
        {
            if (!s_runtime_enemy_remap.property_failure_logged.exchange(
                        true))
            {
                Output::send<LogLevel::Error>(
                        STR("[KH3APProbeCpp] Level entity runtime remap disabled: "
                            "m_ActorClassPath is not a writable "
                            "StringAssetReference\n"));
            }
            s_runtime_enemy_remap.active = false;
            return;
        }

        // KH3's UE4.17 StringAssetReference is an FString-backed struct.
        // SerializePost runs immediately after the level entity's saved
        // properties are available, before PostLoad establishes any of
        // KH3's lazy-class or retry state. This mirrors the old baked umap
        // payload while keeping the replacement runtime-only. Retaining a
        // slot-validated binding here also lets an in-game toggle restore
        // or reapply the serialized class path and rebind KH3's resolved
        // m_ActorClass cache for the next encounter spawn without
        // trusting a stale UObject pointer across map transitions.
        auto* const current_class_path = static_cast<FString*>(value_ptr);
        const std::wstring source_class_path =
                current_class_path->GetCharArray();
        const auto enemy_replacement =
                s_runtime_enemy_remap.enemy_replacements.find(object_path);
        const bool enemy_match =
                enemy_replacement !=
                s_runtime_enemy_remap.enemy_replacements.end();
        const auto boss_replacement =
                s_runtime_enemy_remap.boss.boss_class_replacements.find(
                        source_class_path);
        const bool boss_match = !enemy_match &&
                boss_replacement !=
                        s_runtime_enemy_remap
                                .boss.boss_class_replacements.end();
        if (!s_runtime_enemy_remap.first_level_entity_callback_logged.exchange(
                    true))
        {
            Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] Level entity runtime remap first_level_entity "
                        "object={} enemy_match={} boss_match={} "
                        "enemy_enabled={} boss_enabled={} hook={} "
                        "polling=false\n"),
                    object_path,
                    enemy_match,
                    boss_match,
                    enemy_enabled,
                    boss_enabled,
                    hook_route);
        }
        if (!enemy_match && !boss_match)
        {
            return;
        }

        const std::wstring& replacement_path = enemy_match
                ? enemy_replacement->second
                : boss_replacement->second;
        const RuntimeLevelEntityRemapKind remap_kind = enemy_match
                ? RuntimeLevelEntityRemapKind::enemy
                : RuntimeLevelEntityRemapKind::boss;
        const bool enabled = enemy_match
                ? enemy_enabled
                : boss_enabled;
        const std::wstring_view replacement_kind = enemy_match
                ? STR("enemy")
                : STR("boss");
        remember_loaded_level_entity_remap(
                object,
                object_path,
                source_class_path,
                replacement_path,
                remap_kind);
        if (boss_match)
        {
            track_titan_arena_entity(object,source_class_path,replacement_path);
            std::lock_guard lock{
                    s_runtime_enemy_remap.boss.tracking.boss_state_mutex};
            if (enabled)
            {
                s_runtime_enemy_remap.boss.tracking.remapped_boss_entities[object_path] =
                        replacement_path;
                s_runtime_enemy_remap
                        .boss.tracking.remapped_boss_source_classes[object_path] =
                        source_class_path;
            }
            else
            {
                s_runtime_enemy_remap.boss.tracking.remapped_boss_entities.erase(
                        object_path);
                s_runtime_enemy_remap
                        .boss.tracking.remapped_boss_source_classes.erase(object_path);
            }
            s_runtime_enemy_remap.boss.tracking.boss_bound_actors.erase(
                    object_path);
            s_runtime_enemy_remap.boss.tracking.boss_dispatched_wipe_out_groups.erase(
                    object_path);
        }
        const std::wstring& desired_class_path = enabled
                ? replacement_path
                : source_class_path;
        const bool changed = source_class_path != desired_class_path;
        if (changed)
        {
            *current_class_path = FString(desired_class_path.c_str());
        }
        if (!enabled)
        {
            return;
        }

        const uint32_t observation =
                s_runtime_enemy_remap.replacement_observation_count
                        .fetch_add(1) +
                1;
        if (observation <= enemy_remap_observation_log_limit)
        {
            Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] Level entity runtime remap apply "
                        "sequence={} kind={} object={} previous_class={} "
                        "class={} changed={} hook={} polling=false\n"),
                    observation,
                    replacement_kind,
                    object_path,
                    source_class_path,
                    replacement_path,
                    changed,
                    hook_route);
        }

        if (!s_runtime_enemy_remap.first_replacement_logged.exchange(true))
        {
            Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] Level entity runtime remap first_apply "
                        "kind={} object={} class={} hook={} "
                        "mode=event_driven polling=false\n"),
                    replacement_kind,
                    object_path,
                    replacement_path,
                    hook_route);
        }
        if (boss_match &&
            !s_runtime_enemy_remap.first_boss_replacement_logged.exchange(
                    true))
        {
            Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] Boss runtime remap first_apply "
                        "object={} previous_class={} class={} hook={} "
                        "level_script_owns_replacement=true polling=false\n"),
                    object_path,
                    source_class_path,
                    replacement_path,
                    hook_route);
        }
    }

    // The native attack-target predicate special-cases story Terra-Xehanort
    // (character 38), then dereferences a TresAIController without checking
    // the cast. Every other character returns false. The KG Steam crash
    // reached this through a pawn outside the exact replacement-class gate;
    // validate the predicate's prerequisite, including auxiliary/stale pawns.

    using LevelEntitySerializeFn = void (*)(UObject* object, FArchive& archive);

    static auto level_entity_serialize_detour(
            UObject* object,
            FArchive& archive) -> void
    {
        const uint64_t trampoline =
                s_runtime_enemy_remap.level_entity_serialize_trampoline;
        if (trampoline != 0)
        {
            reinterpret_cast<LevelEntitySerializeFn>(trampoline)(
                    object,
                    archive);
        }

        if (!prepare_fun_heartless_serialized_entity(object))
            apply_runtime_enemy_replacement(
                    object,
                    STR("TresLevelEntity::SerializePost"));
    }

    auto install_level_entity_serialize_detour(UObject* exemplar) -> bool
    {
        if (!is_uobject_valid_fast(exemplar))
        {
            return false;
        }

        UClass* const actor_class = exemplar->GetClassPrivate();
        if (!actor_class ||
            actor_class->GetName() != level_entity_class_name)
        {
            return false;
        }

        std::lock_guard lock{s_runtime_enemy_remap.native_hook_mutex};
        if (s_runtime_enemy_remap.native_hook_installed)
        {
            return true;
        }
        if (s_runtime_enemy_remap.native_hook_unavailable)
        {
            return false;
        }

        const auto slot = UObject::VTableLayoutMap.find(STR("Serialize"));
        void** const vtable = *reinterpret_cast<void***>(exemplar);
        if (slot == UObject::VTableLayoutMap.end() || !vtable ||
            slot->second % sizeof(void*) != 0)
        {
            s_runtime_enemy_remap.native_hook_unavailable = true;
            Output::send<LogLevel::Error>(
                    STR("[KH3APProbeCpp] Enemy runtime remap native hook failed "
                        "reason=serialize_vtable_slot_unavailable\n"));
            return false;
        }

        const uintptr_t target = reinterpret_cast<uintptr_t>(
                vtable[slot->second / sizeof(void*)]);
        if (target == 0)
        {
            s_runtime_enemy_remap.native_hook_unavailable = true;
            Output::send<LogLevel::Error>(
                    STR("[KH3APProbeCpp] Enemy runtime remap native hook failed "
                        "reason=serialize_target_null\n"));
            return false;
        }

        s_runtime_enemy_remap.level_entity_serialize_trampoline = 0;
        auto detour = std::make_unique<PLH::x64Detour>(
                static_cast<uint64_t>(target),
                reinterpret_cast<uint64_t>(
                        &level_entity_serialize_detour),
                &s_runtime_enemy_remap.level_entity_serialize_trampoline);
        const bool hooked = detour->hook();
        if (!hooked ||
            s_runtime_enemy_remap.level_entity_serialize_trampoline == 0)
        {
            if (hooked)
            {
                detour->unHook();
            }
            s_runtime_enemy_remap.level_entity_serialize_trampoline = 0;
            s_runtime_enemy_remap.native_hook_unavailable = true;
            Output::send<LogLevel::Error>(
                    STR("[KH3APProbeCpp] Enemy runtime remap native hook failed "
                        "reason=polyhook_install target=0x{:X}\n"),
                    target);
            return false;
        }

        s_runtime_enemy_remap.level_entity_serialize_target = target;
        s_runtime_enemy_remap.level_entity_serialize_detour =
                std::move(detour);
        s_runtime_enemy_remap.native_hook_installed = true;
        Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] Enemy runtime remap native hook installed "
                    "class=TresLevelEntity function=Serialize target=0x{:X} "
                    "vtable_offset=0x{:X} mode=event_driven polling=false "
                    "per_frame_work=false\n"),
                target,
                slot->second);
        return true;
    }
}
