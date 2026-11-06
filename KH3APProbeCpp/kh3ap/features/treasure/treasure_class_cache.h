#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace RC::Unreal
{
    struct FStaticConstructObjectParameters;
    class UClass;
    class UObject;
}

namespace kh3ap
{
    class TreasureClassCache
    {
    public:
        auto refresh(
                std::wstring_view player_state_class_path,
                std::wstring_view char_state_class_path,
                std::wstring_view treasure_logic_actor_class_path)
                -> std::optional<std::wstring>;

        [[nodiscard]] auto open_treasure_player_state_class() const -> RC::Unreal::UClass*;
        [[nodiscard]] auto open_treasure_char_state_class() const -> RC::Unreal::UClass*;
        [[nodiscard]] auto treasure_logic_actor_class() const -> RC::Unreal::UClass*;
        [[nodiscard]] auto actor_base_class() const -> RC::Unreal::UClass*;

    private:
        RC::Unreal::UClass* open_treasure_player_state_class_{nullptr};
        RC::Unreal::UClass* open_treasure_char_state_class_{nullptr};
        RC::Unreal::UClass* treasure_logic_actor_class_{nullptr};
        RC::Unreal::UClass* actor_base_class_{nullptr};
        std::wstring last_resolution_{};
    };

    auto refresh_open_treasure_state_class_cache(
            TreasureClassCache& class_cache,
            std::wstring_view open_treasure_player_state_class_path,
            std::wstring_view open_treasure_char_state_class_path,
            std::wstring_view treasure_logic_actor_class_path) -> void;

    auto maybe_log_interesting_treasure_construct(
            const RC::Unreal::FStaticConstructObjectParameters& params,
            RC::Unreal::UObject* constructed_object) -> void;
}
