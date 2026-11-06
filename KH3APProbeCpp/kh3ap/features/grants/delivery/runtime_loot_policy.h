#pragma once
#include "kh3ap/features/grants/delivery/item_delivery_catalog.h"
#include <map>
#include <set>
#include <string>

namespace kh3ap::runtime_loot
{
    struct Location
    {
        std::string name, item, recipient, type, source_id;
        bool foreign{}, priority{}, always_on{};
    };
    inline auto foreign_message(const Location& location) -> std::string
    {
        if (!location.foreign || location.item.empty()) return {};
        return "AP: Found " + (location.recipient.empty() ? "" : location.recipient + "'s ") +
            location.item + (location.priority ? " (p)" : "");
    }
    struct Observation
    {
        std::string key, type, source, resolution, mapping;
        int64_t epoch{};
    };
    enum class Decision { ignored, duplicate, fresh, repeated };
    class Policy
    {
        std::map<std::string, int64_t> observed_epochs;
    public:
        std::set<std::string> checked;
        auto complete(const std::string& key, int64_t epoch) -> bool
        {
            if (!checked.insert(key).second) return false;
            observed_epochs[key] = epoch;
            return true;
        }
        auto observe(const Observation& event, const Location& location) -> Decision
        {
            if (event.key.empty() || location.type == "level_up" || location.type == "lucky_mark" || location.type == "treasure" ||
                (event.type != "event" && event.type != "vbonus" && event.type != "shop") ||
                (event.type == "event" && event.source == "UTresGameBlueprintLibrary::BP_TresGetItem" &&
                 event.resolution == "item_direct" && event.mapping == "resolved from slot_data.json event vanilla_item"))
                return Decision::ignored;
            if (complete(event.key, event.epoch)) return Decision::fresh;
            // Only a load/death/new-game event can make an observed reward a
            // rollback repair. Repeated callbacks in the same gameplay epoch
            // cannot become eligible just because time has passed.
            const auto prior = observed_epochs.find(event.key);
            if (location.always_on || (prior != observed_epochs.end() && prior->second == event.epoch))
                return Decision::duplicate;
            observed_epochs[event.key] = event.epoch;
            return Decision::repeated;
        }
    };
}
