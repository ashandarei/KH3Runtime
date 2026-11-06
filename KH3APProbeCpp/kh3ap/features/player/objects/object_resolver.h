#pragma once

#include "kh3ap/features/player/objects/object_cache.h"

#include <functional>
#include <string_view>
#include <vector>

namespace RC::Unreal
{
    class UClass;
}

namespace kh3ap
{
    class RuntimeObjectResolver
    {
    public:
        static auto try_get_tres_game_blueprint_library_default_object(
                RuntimeObjectCache& object_cache,
                std::wstring_view default_object_path) -> RC::Unreal::UObject*;

        static auto cache_world_context_object(
                RuntimeObjectCache& object_cache,
                RC::Unreal::UObject* object) -> void;

        static auto maybe_cache_world_context_from_object(
                RuntimeObjectCache& object_cache,
                RC::Unreal::UObject* object,
                RC::Unreal::UClass* actor_base_class) -> void;

        static auto try_get_cached_world_context_object(
                RuntimeObjectCache& object_cache,
                RC::Unreal::UClass* actor_base_class) -> RC::Unreal::UObject*;

        static auto is_sora_ability_pawn_candidate(RC::Unreal::UObject* object) -> bool;

        static auto collect_sora_ability_pawn_candidates(
                RC::Unreal::UObject* preferred_object,
                RC::Unreal::UObject* last_world_context_object) -> std::vector<RC::Unreal::UObject*>;

        static auto collect_cached_runtime_grant_pawn_candidates(
                RC::Unreal::UObject* preferred_object,
                RC::Unreal::UObject* cached_runtime_grant_pawn,
                RC::Unreal::UObject* last_world_context_object,
                const std::function<bool(RC::Unreal::UObject*)>& is_sora_ability_pawn_candidate)
                -> std::vector<RC::Unreal::UObject*>;

        static auto collect_world_context_candidates(
                RuntimeObjectCache& object_cache,
                RC::Unreal::UClass* actor_base_class,
                RC::Unreal::UObject* preferred_object,
                const std::vector<RC::Unreal::UObject*>& sora_pawn_candidates)
                -> std::vector<RC::Unreal::UObject*>;

        static auto collect_fast_runtime_item_grant_world_context_candidates(
                RuntimeObjectCache& object_cache,
                RC::Unreal::UClass* actor_base_class,
                RC::Unreal::UObject* preferred_object,
                const std::vector<RC::Unreal::UObject*>& cached_runtime_grant_pawn_candidates)
                -> std::vector<RC::Unreal::UObject*>;
    };
}
