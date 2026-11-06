#pragma once

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    class RuntimeObjectCache
    {
    public:
        [[nodiscard]] auto last_world_context_object() const -> RC::Unreal::UObject*;
        auto set_last_world_context_object(RC::Unreal::UObject* object) -> void;

        [[nodiscard]] auto tres_game_blueprint_library_default_object() const -> RC::Unreal::UObject*;
        auto set_tres_game_blueprint_library_default_object(RC::Unreal::UObject* object) -> void;

    private:
        RC::Unreal::UObject* last_world_context_object_{nullptr};
        RC::Unreal::UObject* tres_game_blueprint_library_default_object_{nullptr};
    };
}
