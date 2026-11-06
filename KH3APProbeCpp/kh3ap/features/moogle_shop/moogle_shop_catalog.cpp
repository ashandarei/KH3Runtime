#include "moogle_shop_catalog.h"

#include "kh3ap/bridge/transport/bridge_channels.h"
#include "kh3ap/bridge/serialization/json_fields.h"
#include "kh3ap/bridge/transport/pipe_transport.h"
#include "kh3ap/bridge/utilities.h"
#include "kh3ap/core/settings/pak_seed_settings.h"
#include "moogle_shop_scan_state.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <UnrealDef.hpp>

#include <optional>
#include <utility>

namespace kh3ap
{
    using namespace RC;

    auto MoogleShopCatalog::reset() -> void
    {
        options_ = {};
        locations_.clear();
        checked_bridge_keys_.clear();
        seed_loaded_ = false;
        locations_loaded_ = false;
        checked_locations_valid_ = false;
    }

    auto MoogleShopCatalog::try_load_seed(const std::filesystem::path& slot_data_path,
                                          JsonObject& slot_document) -> bool
    {
        slot_document = JsonObject::read_seed(slot_data_path);
        if (!slot_document.valid())
        {
            return false;
        }

        const auto& seed_options = installed_seed_slot_data();
        const auto parsed_shop_check_count = seed_options.int64_field("moogle_shop_checks");
        options_.shop_check_count = parsed_shop_check_count.value_or(0);
        options_.checks_enabled = runtime_moogle_shop_checks_enabled(parsed_shop_check_count);

        const auto ultima_mode = seed_options.int64_field("ultima_weapon_mode");
        const auto ultima_mode_name = ultima_mode.has_value()
                                          ? std::optional<std::wstring>{}
                                          : seed_options.string_field("ultima_weapon_mode");
        options_.ultima.shop_mode =
            ultima_mode.value_or(0) == 1 ||
            (ultima_mode_name.has_value() && *ultima_mode_name == STR("shop"));

        options_.ultima.keyblade_levels_start_at_max = false;
        options_.ultima.required_keyblades.clear();
        if (options_.ultima.shop_mode)
        {
            const auto keyblade_levels_mode = seed_options.int64_field("keyblade_levels");
            const auto keyblade_levels_mode_name =
                keyblade_levels_mode.has_value() ? std::optional<std::wstring>{}
                                                 : seed_options.string_field("keyblade_levels");
            options_.ultima.keyblade_levels_start_at_max =
                (keyblade_levels_mode.has_value() && *keyblade_levels_mode == 0) ||
                (keyblade_levels_mode_name.has_value() &&
                 *keyblade_levels_mode_name == STR("start_at_max"));
            options_.ultima.required_keyblades =
                seed_options.string_array_field("ultima_weapon_shop_required_keyblades");
        }

        seed_loaded_ = true;
        locations_loaded_ = false;
        checked_locations_valid_ = false;
        locations_.clear();
        checked_bridge_keys_.clear();
        Output::send<LogLevel::Verbose>(
            STR("[KH3APProbeCpp] MoogleShop seed options checks={} enabled={} ultima_shop={}\n"),
            options_.shop_check_count, options_.checks_enabled ? STR("true") : STR("false"),
            options_.ultima.shop_mode ? STR("true") : STR("false"));
        return true;
    }

    auto MoogleShopCatalog::load_locations(const JsonObject& slot_document, size_t max_locations)
        -> void
    {
        auto parsed_locations = SlotDataLookupParser::shop_item_locations(slot_document);
        std::unordered_set<std::wstring> seen_tokens{};
        std::unordered_set<std::wstring> seen_bridge_keys{};
        locations_.clear();
        for (auto& location : parsed_locations)
        {
            if (location.item_token.empty() || location.bridge_key.empty() ||
                !seen_tokens.emplace(location.item_token).second ||
                !seen_bridge_keys.emplace(location.bridge_key).second)
            {
                continue;
            }
            locations_.emplace_back(std::move(location));
            if (locations_.size() >= max_locations)
            {
                break;
            }
        }
        locations_loaded_ = true;
        checked_locations_valid_ = false;
        checked_bridge_keys_.clear();
        Output::send<LogLevel::Verbose>(
            STR("[KH3APProbeCpp] MoogleShop catalog loaded locations={} configured_checks={}\n"),
            static_cast<uint64_t>(locations_.size()), options_.shop_check_count);
    }

    auto MoogleShopCatalog::refresh_checked_locations(const std::filesystem::path& bridge_dir)
        -> MoogleShopCheckedLocationsRefresh
    {
        const std::filesystem::path game_state_path = bridge_dir / channels::game_state_file;
        const auto next_revision = pipe::revision(game_state_path);
        if (!next_revision || (checked_locations_valid_ && next_revision == game_state_revision_))
        {
            return {};
        }

        const auto game_state_text = read_text_file(game_state_path);
        if (!game_state_text.has_value())
        {
            return {};
        }
        std::unordered_set<std::wstring> shop_bridge_keys{};
        shop_bridge_keys.reserve(locations_.size());
        for (const auto& location : locations_)
        {
            shop_bridge_keys.emplace(location.bridge_key);
        }

        std::unordered_set<std::wstring> next_checked_bridge_keys{};
        for (auto& bridge_key :
             JsonFieldReader::string_array_field(*game_state_text, "checked_locations"))
        {
            if (shop_bridge_keys.contains(bridge_key))
            {
                next_checked_bridge_keys.emplace(std::move(bridge_key));
            }
        }
        const bool changed = next_checked_bridge_keys != checked_bridge_keys_;
        checked_bridge_keys_ = std::move(next_checked_bridge_keys);
        game_state_revision_ = next_revision;
        checked_locations_valid_ = true;
        return {.updated = true, .changed = changed};
    }
} // namespace kh3ap
