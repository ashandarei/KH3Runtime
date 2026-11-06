#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace kh3ap
{
    class RuntimeTextHelpers
    {
    public:
        static auto is_retry_source_label(std::wstring_view source_label) -> bool;
        static auto normalize_vbonus_actor_reward_token(std::wstring_view value) -> std::wstring;
        static auto contains_death_or_game_over_marker(std::wstring_view value) -> bool;
        static auto extract_level_path(std::wstring_view full_name) -> std::optional<std::wstring>;
        static auto extract_game_map_root(std::wstring_view full_name) -> std::optional<std::wstring>;
    };
}
