#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace kh3ap
{
    class RuntimeEventResolver
    {
    public:
        static auto find_bridge_key_for_vanilla_item(
                std::wstring_view vanilla_item,
                const std::unordered_map<std::wstring, std::wstring>& lookup,
                bool debug_logging) -> std::optional<std::wstring>;
    };
}
