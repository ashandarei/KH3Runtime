#pragma once

#include <optional>
#include <string>
#include <unordered_map>

namespace kh3ap::enemy_remap_detail
{
    auto parse_runtime_replacements(const std::string& metadata)
            -> std::optional<
                    std::unordered_map<std::wstring, std::wstring>>;

    auto parse_runtime_boss_replacements(const std::string& metadata, bool apply_arena_policy = true)
            -> std::optional<
                    std::unordered_map<std::wstring, std::wstring>>;
}
