#include "kh3ap/features/grants/events/event_resolver.h"

#include <DynamicOutput/DynamicOutput.hpp>

namespace kh3ap
{
    auto RuntimeEventResolver::find_bridge_key_for_vanilla_item(
            std::wstring_view vanilla_item,
            const std::unordered_map<std::wstring, std::wstring>& lookup,
            bool debug_logging) -> std::optional<std::wstring>
    {
        if (const auto found = lookup.find(std::wstring{vanilla_item}); found != lookup.end())
        {
            if (debug_logging)
            {
                RC::Output::send<RC::LogLevel::Verbose>(
                        STR("[KH3APProbeCpp] Event lookup hit vanilla_item={} -> bridge_key={}\n"),
                        std::wstring{vanilla_item},
                        found->second);
            }
            return found->second;
        }
        if (debug_logging)
        {
            RC::Output::send<RC::LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] Event lookup miss vanilla_item={}\n"),
                    std::wstring{vanilla_item});
        }
        return std::nullopt;
    }
}
