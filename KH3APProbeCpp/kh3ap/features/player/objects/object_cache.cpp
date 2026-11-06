#include "kh3ap/features/player/objects/object_cache.h"

namespace kh3ap
{
    auto RuntimeObjectCache::last_world_context_object() const -> RC::Unreal::UObject*
    {
        return last_world_context_object_;
    }

    auto RuntimeObjectCache::set_last_world_context_object(RC::Unreal::UObject* object) -> void
    {
        last_world_context_object_ = object;
    }

    auto RuntimeObjectCache::tres_game_blueprint_library_default_object() const -> RC::Unreal::UObject*
    {
        return tres_game_blueprint_library_default_object_;
    }

    auto RuntimeObjectCache::set_tres_game_blueprint_library_default_object(RC::Unreal::UObject* object) -> void
    {
        tres_game_blueprint_library_default_object_ = object;
    }

}
