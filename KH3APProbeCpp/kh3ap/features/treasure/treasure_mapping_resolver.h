#pragma once

#include "kh3ap/core/types.h"

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace kh3ap
{
    class TreasureMappingResolver
    {
    public:
        static auto infer_from_actor_name(
                std::wstring_view full_name,
                const std::unordered_map<std::wstring, std::wstring>& treasure_lookup)
                -> std::optional<TreasureMappingResult>;
    };
}
