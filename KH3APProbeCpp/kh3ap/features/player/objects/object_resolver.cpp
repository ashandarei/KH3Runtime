#include "kh3ap/features/player/objects/object_resolver.h"

#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/features/player/state/sora_pawn_catalog.h"
#include "kh3ap/bridge/utilities.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <UnrealDef.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UObjectGlobals.hpp>

#include <array>
#include <algorithm>
#include <string>
#include <vector>

namespace kh3ap
{
    namespace
    {
        auto try_find_default_object(std::wstring_view default_object_path) -> RC::Unreal::UObject*
        {
            return RC::Unreal::UObjectGlobals::StaticFindObject<RC::Unreal::UObject*>(
                    nullptr,
                    nullptr,
                    default_object_path);
        }

        auto is_sora_runtime_pawn_name(
                std::wstring_view full_name,
                std::wstring_view class_name) -> bool
        {
            return runtime_sora_pawn_catalog_matches_any(full_name, class_name) ||
                   contains_any_wide(full_name, {STR("Sora")}) ||
                   contains_any_wide(class_name, {STR("Sora")});
        }
    }

    auto RuntimeObjectResolver::try_get_tres_game_blueprint_library_default_object(
            RuntimeObjectCache& object_cache,
            std::wstring_view default_object_path) -> RC::Unreal::UObject*
    {
        RC::Unreal::UObject* const cached_object = object_cache.tres_game_blueprint_library_default_object();
        // This is a class default object with process lifetime. Validate it when
        // first resolved, then trust the cache instead of walking UE4SS's global
        // object array again for every item in a startup replay batch.
        if (cached_object)
        {
            return cached_object;
        }

        const RuntimeHotPathProfileScope profile{L"reflection:blueprint_library_lookup"};
        RC::Unreal::UObject* const default_object = try_find_default_object(default_object_path);
        if (!default_object || !kh3ap::is_uobject_valid_fast(default_object))
        {
            return nullptr;
        }
        object_cache.set_tres_game_blueprint_library_default_object(default_object);
        return default_object;
    }

    auto RuntimeObjectResolver::cache_world_context_object(
            RuntimeObjectCache& object_cache,
            RC::Unreal::UObject* object) -> void
    {
        if (!object || !kh3ap::is_uobject_valid_fast(object))
        {
            return;
        }
        object_cache.set_last_world_context_object(object);
    }

    auto RuntimeObjectResolver::maybe_cache_world_context_from_object(
            RuntimeObjectCache& object_cache,
            RC::Unreal::UObject* object,
            RC::Unreal::UClass* actor_base_class) -> void
    {
        if (!object || !kh3ap::is_uobject_valid_fast(object))
        {
            return;
        }

        if (actor_base_class)
        {
            if (object->IsA(actor_base_class))
            {
                cache_world_context_object(object_cache, object);
                return;
            }

            if (RC::Unreal::UObject* const owner_actor = object->GetTypedOuter(actor_base_class);
                owner_actor && kh3ap::is_uobject_valid_fast(owner_actor))
            {
                cache_world_context_object(object_cache, owner_actor);
                return;
            }
        }

        cache_world_context_object(object_cache, object);
    }

    auto RuntimeObjectResolver::try_get_cached_world_context_object(
            RuntimeObjectCache& object_cache,
            RC::Unreal::UClass* actor_base_class) -> RC::Unreal::UObject*
    {
        RC::Unreal::UObject* const cached_object = object_cache.last_world_context_object();
        if (cached_object && kh3ap::is_uobject_valid_fast(cached_object))
        {
            return cached_object;
        }

        const std::array<std::wstring_view, 4> fallback_class_names{
                STR("TresPlayerController"),
                STR("TresPlayerPawn"),
                STR("GameViewportClient"),
                STR("World"),
        };
        for (std::wstring_view class_name : fallback_class_names)
        {
            std::vector<RC::Unreal::UObject*> objects{};
            RC::Unreal::UObjectGlobals::FindAllOf(class_name, objects);
            for (RC::Unreal::UObject* object : objects)
            {
                if (!object || !kh3ap::is_uobject_valid_fast(object))
                {
                    continue;
                }
                maybe_cache_world_context_from_object(object_cache, object, actor_base_class);
                RC::Unreal::UObject* const last_world_context_object = object_cache.last_world_context_object();
                if (last_world_context_object && kh3ap::is_uobject_valid_fast(last_world_context_object))
                {
                    return last_world_context_object;
                }
            }
        }
        return nullptr;
    }

    auto RuntimeObjectResolver::is_sora_ability_pawn_candidate(RC::Unreal::UObject* object) -> bool
    {
        if (!object || !kh3ap::is_uobject_valid_fast(object))
        {
            return false;
        }

        const std::wstring full_name = object->GetFullName();
        const std::wstring class_name = object->GetClassPrivate() ? object->GetClassPrivate()->GetName() : std::wstring{};
        if (full_name.find(STR("Default__")) != std::wstring::npos)
        {
            return false;
        }
        return is_sora_runtime_pawn_name(full_name, class_name) &&
               (contains_any_wide(class_name, {STR("Pawn")}) ||
                runtime_sora_pawn_catalog_match(class_name));
    }

    auto RuntimeObjectResolver::collect_sora_ability_pawn_candidates(
            RC::Unreal::UObject* preferred_object,
            RC::Unreal::UObject* last_world_context_object) -> std::vector<RC::Unreal::UObject*>
    {
        std::vector<RC::Unreal::UObject*> candidates{};
        auto append_if_sora = [&candidates](RC::Unreal::UObject* object) {
            if (!is_sora_ability_pawn_candidate(object))
            {
                return;
            }
            if (std::find(candidates.begin(), candidates.end(), object) == candidates.end())
            {
                candidates.push_back(object);
            }
        };

        append_if_sora(preferred_object);
        append_if_sora(last_world_context_object);

        return candidates;
    }

    auto RuntimeObjectResolver::collect_cached_runtime_grant_pawn_candidates(
            RC::Unreal::UObject* preferred_object,
            RC::Unreal::UObject* cached_runtime_grant_pawn,
            RC::Unreal::UObject* last_world_context_object,
            const std::function<bool(RC::Unreal::UObject*)>& is_sora_ability_pawn_candidate)
            -> std::vector<RC::Unreal::UObject*>
    {
        std::vector<RC::Unreal::UObject*> candidates{};
        const auto append_if_sora = [&](RC::Unreal::UObject* object) {
            if (!is_sora_ability_pawn_candidate(object))
            {
                return;
            }
            if (std::find(candidates.begin(), candidates.end(), object) == candidates.end())
            {
                candidates.push_back(object);
            }
        };

        append_if_sora(preferred_object);
        append_if_sora(cached_runtime_grant_pawn);
        append_if_sora(last_world_context_object);
        return candidates;
    }

    auto RuntimeObjectResolver::collect_world_context_candidates(
            RuntimeObjectCache& object_cache,
            RC::Unreal::UClass* actor_base_class,
            RC::Unreal::UObject* preferred_object,
            const std::vector<RC::Unreal::UObject*>& sora_pawn_candidates)
            -> std::vector<RC::Unreal::UObject*>
    {
        std::vector<RC::Unreal::UObject*> candidates{};

        append_world_context_candidate(candidates, preferred_object);
        if (preferred_object && actor_base_class)
        {
            append_world_context_candidate(candidates, preferred_object->GetTypedOuter(actor_base_class));
        }

        RC::Unreal::UObject* const last_world_context_object = object_cache.last_world_context_object();
        append_world_context_candidate(candidates, last_world_context_object);

        if (last_world_context_object && actor_base_class)
        {
            append_world_context_candidate(candidates, last_world_context_object->GetTypedOuter(actor_base_class));
        }

        for (RC::Unreal::UObject* pawn_object : sora_pawn_candidates)
        {
            append_world_context_candidate(candidates, pawn_object);
            if (pawn_object && actor_base_class)
            {
                append_world_context_candidate(candidates, pawn_object->GetTypedOuter(actor_base_class));
            }
        }

        const std::array<std::wstring_view, 4> fallback_class_names{
                STR("TresPlayerController"),
                STR("TresPlayerPawn"),
                STR("GameViewportClient"),
                STR("World"),
        };
        for (std::wstring_view class_name : fallback_class_names)
        {
            std::vector<RC::Unreal::UObject*> objects{};
            RC::Unreal::UObjectGlobals::FindAllOf(class_name, objects);
            for (RC::Unreal::UObject* object : objects)
            {
                append_world_context_candidate(candidates, object);
                if (object && actor_base_class)
                {
                    append_world_context_candidate(candidates, object->GetTypedOuter(actor_base_class));
                }
            }
        }

        if (candidates.empty())
        {
            append_world_context_candidate(candidates, try_get_cached_world_context_object(object_cache, actor_base_class));
        }

        return candidates;
    }

    auto RuntimeObjectResolver::collect_fast_runtime_item_grant_world_context_candidates(
            RuntimeObjectCache& object_cache,
            RC::Unreal::UClass* actor_base_class,
            RC::Unreal::UObject* preferred_object,
            const std::vector<RC::Unreal::UObject*>& cached_runtime_grant_pawn_candidates)
            -> std::vector<RC::Unreal::UObject*>
    {
        (void)object_cache;
        (void)actor_base_class;
        (void)preferred_object;
        std::vector<RC::Unreal::UObject*> candidates{};
        const auto append_if_sora_pawn = [&candidates](RC::Unreal::UObject* object) {
            if (!is_sora_ability_pawn_candidate(object))
            {
                return;
            }
            append_world_context_candidate(candidates, object);
        };

        for (RC::Unreal::UObject* pawn_object : cached_runtime_grant_pawn_candidates)
        {
            append_if_sora_pawn(pawn_object);
        }

        return candidates;
    }
}
