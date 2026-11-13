#include <utility>
#include "kh3ap/bridge/catalog/catalog.h"
#include "kh3ap/bridge/catalog/catalog_data.h"
#include "kh3ap/bridge/serialization/json.h"
#define NOMINMAX
#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace kh3ap::native_catalog
{
    namespace
    {
        constexpr auto missing_event_threshold = -2;
        constexpr std::string_view grant_kind_prefix = "grant_";
        constexpr std::string_view unlock_name_prefix = "Unlock: ";

        constexpr auto min_ultima_shard_count = 2;
        constexpr auto max_ultima_shard_count = 13;
        constexpr auto default_ultima_shard_count = 7;
        constexpr auto max_native_check_seed_rows = 4096;
        constexpr auto max_exact_json_integer = 9007199254740991.0;

        using Json = kh3ap::json::Value;
        auto parse(std::string_view text) -> Json
        {
            Json value;
            if (!kh3ap::json::parse(value, text))
            {
                throw std::runtime_error("native_catalog_json");
            }
            return value;
        }
        auto field(const Json& value, std::string_view key) -> const Json&
        {
            static const Json empty;
            return value.is_object() && value.contains(key) ? value[key] : empty;
        }
        auto text(const Json& value) -> std::string
        {
            return value.is_string() ? value.get_ref<const std::string&>() : std::string{};
        }
        auto number(const Json& value, int64_t fallback = 0) -> int64_t
        {
            if (!value.is_number())
            {
                return fallback;
            }
            const auto v = value.get<double>();
            if (!std::isfinite(v) || std::floor(v) != v || v < -max_exact_json_integer ||
                v > max_exact_json_integer)
            {
                throw std::runtime_error("native_catalog_integer");
            }
            return static_cast<int64_t>(v);
        }
        auto flag(const Json& value) -> bool
        {
            return value.is_boolean() && value.get<bool>();
        }
        auto array(const Json& value) -> const Json::array_t&
        {
            static const Json::array_t empty;
            return value.is_array() ? value.get_ref<const Json::array_t&>() : empty;
        }
        auto wide(std::string_view value) -> std::wstring
        {
            if (value.empty())
            {
                return {};
            }
            const auto count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                                   static_cast<int>(value.size()), nullptr, 0);
            if (!count)
            {
                throw std::runtime_error("native_catalog_utf8");
            }
            std::wstring out(count, L'\0');
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                static_cast<int>(value.size()), out.data(), count);
            return out;
        }
        auto shared() -> const Json&
        {
            static const auto value = parse(shared_data);
            return value;
        }
        auto narrow(std::wstring_view value) -> std::string
        {
            if (value.empty())
            {
                return {};
            }
            const auto count =
                WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                                    static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
            if (!count)
            {
                throw std::runtime_error("native_catalog_utf8");
            }
            std::string out(count, '\0');
            WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                                static_cast<int>(value.size()), out.data(), count, nullptr,
                                nullptr);
            return out;
        }
        auto strings(const std::vector<std::wstring>& values) -> Json
        {
            Json::array_t out;
            for (const auto& value : values)
            {
                out.emplace_back(narrow(value));
            }
            return out;
        }
        auto token_text(const Json& value) -> std::string
        {
            auto token = value.is_string() ? text(value) : text(field(value, "token"));
            while (!token.empty() && token.back() == '\0')
            {
                token.pop_back();
            }
            return token;
        }
        auto valid_token(std::string_view token) -> bool
        {
            return !token.empty() && token != "None" && !token.ends_with("::NONE");
        }
        auto grant_method(std::string_view token, int64_t munny) -> std::wstring
        {
            if (!valid_token(token))
            {
                return {};
            }
            if (munny)
            {
                return L"bp_tres_get_item_add_munny";
            }
            if (token.starts_with("KH3AP_ULTIMA_WEAPON_SHARD"))
            {
                return L"ultima_weapon_shard";
            }
            if (token == "KH3AP_TRAP::BETRAYAL")
            {
                return L"betrayal_trap";
            }
            if (token == "KH3AP_TRAP::KING_MICKEY")
            {
                return L"king_mickey_trap";
            }
            if (token == "ETresBadStatusType::TRES_BADSTAT_SNEEZE" ||
                token == "ETresBadStatusType::TRES_BADSTAT_BURN")
            {
                return L"direct_player_bad_status";
            }
            if (token.starts_with("KH3AP_CONTROLLER_UNLOCK::"))
            {
                return L"controller_unlock";
            }
            if (token.starts_with("KH3AP_KEYBLADE_STYLE::"))
            {
                return L"keyblade_style";
            }
            if (token.starts_with("KH3AP_PARTY_MEMBER_UNLOCK::"))
            {
                return L"party_member_unlock";
            }
            if (token.starts_with("KH3AP_CHARACTER_UNLOCK::"))
            {
                return L"character_unlock";
            }
            if (token.starts_with("KH3AP_PROGRESSIVE_KEYBLADE_LEVEL"))
            {
                return L"progressive_keyblade_level";
            }
            if (token.starts_with("ETresAbilityKind::"))
            {
                return L"durable_sora_ability";
            }
            if (token.starts_with("ETresVictoryBonusKind::"))
            {
                return L"durable_sora_vbonus";
            }
            return L"bp_tres_get_item";
        }
        auto action_kind(std::string_view name, std::string_view token, std::wstring_view method)
            -> std::wstring
        {
            if (method == L"direct_player_bad_status" || method == L"king_mickey_trap" || method == L"betrayal_trap")
            {
                return L"trigger_trap";
            }
            if (method == L"controller_unlock")
            {
                return L"grant_controller_unlock";
            }
            if (method == L"party_member_unlock")
            {
                return L"grant_party_member_unlock";
            }
            if (method == L"character_unlock")
            {
                return L"grant_character_unlock";
            }
            if (method == L"keyblade_style")
            {
                return L"grant_keyblade_style";
            }
            if (method == L"progressive_keyblade_level")
            {
                return L"grant_progressive_keyblade_level";
            }
            if (method == L"ultima_weapon_shard")
            {
                return L"grant_ultima_weapon_shard";
            }
            if (name.starts_with("Magic: "))
            {
                return L"grant_magic";
            }
            if (name.starts_with("Ability: "))
            {
                return L"grant_ability";
            }
            if (name.starts_with("Proof of "))
            {
                return L"grant_proof";
            }
            if (name.starts_with("Secret Report "))
            {
                return L"grant_report";
            }
            if (name.ends_with("Heartbinder"))
            {
                return L"grant_heartbinder";
            }
            if (name.starts_with("Bonus: HP") || name.starts_with("Bonus: MP"))
            {
                return L"grant_stat_bonus";
            }
            if (name.starts_with("Bonus: "))
            {
                return L"grant_slot_bonus";
            }
            if (name == "Gummiphone" || name == "Archipelago Item" ||
                method == L"bp_tres_get_item_add_munny")
            {
                return L"grant_key_item";
            }
            if (name.starts_with("Map: "))
            {
                return L"grant_map";
            }
            if (token.starts_with("WEP_KEYBLADE_"))
            {
                return L"grant_keyblade";
            }
            return L"add_inventory_item";
        }
    } // namespace

    auto runtime_token_for_item(int64_t item_id) -> std::wstring
    {
        return wide(text(field(field(field(shared(), "items"), std::to_string(item_id)), "token")));
    }

    auto check_setup(std::string_view slot_data, CheckKind kind) -> CheckSetup
    {
        const auto seed = parse(slot_data);
        const auto& identity = field(seed, "kh3_seed");
        CheckSetup result{wide(text(field(identity, "seed_name"))),
                          number(field(identity, "slot"))};
        if (result.seed_name.empty() || result.slot <= 0)
        {
            throw std::runtime_error("native_check_seed_scope");
        }
        std::set<std::string> ids;
        for (const auto& row : array(field(field(seed, "bridge"), "locations")))
        {
            const auto id = text(field(row, "source_id"));
            const auto table = text(field(row, "source_table"));
            const bool selected =
                kind == CheckKind::Event ? table == "Event" && id.starts_with("EVENT_")
                : kind == CheckKind::VBonus
                    ? table == "VBonus"
                    : text(field(row, "type")) == "treasure" ||
                          number(field(row, "music_chest_placement_version")) == 1;
            if (!selected)
            {
                continue;
            }
            if (id.empty() || !ids.insert(id).second || ids.size() > max_native_check_seed_rows)
            {
                throw std::runtime_error("native_check_seed_row");
            }
            CheckRow check{.source_id = wide(id)};
            if (kind != CheckKind::Chest)
            {
                const auto& entry = field(
                    field(shared(), kind == CheckKind::Event ? "event_flags" : "vbonus_flags"), id);
                const auto name =
                    text(kind == CheckKind::Event ? field(entry, "flag_name") : entry);
                if (name.empty() || name == "None" || name.starts_with("("))
                {
                    throw std::runtime_error("native_check_flag_missing");
                }
                check.flag_name = wide(name);
                check.threshold = kind == CheckKind::Event
                                      ? number(field(entry, "threshold"), missing_event_threshold)
                                      : -1;
                check.requires_tt_clear = flag(field(entry, "requires_tt_clear"));
                if (check.threshold < -1 || check.threshold > INT32_MAX)
                {
                    throw std::runtime_error("native_check_threshold");
                }
            }
            result.rows.push_back(std::move(check));
        }
        return result;
    }

    void Seed::configure(const Json& slot_data)
    {
        data = slot_data;
        locations.clear();
        bundles.clear();
        for (const auto& row : array(field(field(data, "bridge"), "locations")))
        {
            const auto id = number(field(row, "ap_location_id"), -1);
            if (id >= 0 && !locations.emplace(id, row).second)
            {
                throw std::runtime_error("native_item_seed_location_duplicate");
            }
        }
        for (const auto& bundle : array(field(data, "runtime_reward_bundles")))
        {
            const auto key = text(field(bundle, "bridge_key"));
            if (key.empty() || !bundles.emplace(key, bundle).second)
            {
                throw std::runtime_error("native_item_bundle_duplicate");
            }
        }
    }

    auto Seed::reward(std::string_view token, const Json& receipt) const
        -> item_delivery::RewardTemplate
    {
        const auto& descriptor = field(field(shared(), "tokens"), token);
        item_delivery::RewardTemplate result;
        auto& grant = result.grant;
        grant.index = number(field(receipt, "index"), -1);
        const auto name = text(field(descriptor, "name"));
        grant.item_name = wide(name.empty() ? std::string(token) : name);
        grant.runtime_token = wide(token);
        grant.munny_delta = number(field(descriptor, "munny"));
        grant.grant_method = grant_method(token, grant.munny_delta);
        grant.action_kind = action_kind(name, token, grant.grant_method);
        grant.from_player_name = wide(text(field(receipt, "from_player_name")));
        grant.location_name = wide(text(field(receipt, "location_name")));
        for (const auto& row : array(field(descriptor, "flags")))
        {
            result.flag_rows.push_back(wide(text(row)));
        }
        if (!result.flag_rows.empty())
        {
            grant.runtime_flag_name = result.flag_rows.front();
        }
        if (grant.grant_method == L"progressive_keyblade_level")
        {
            result.progressive_base = grant.runtime_token;
        }
        result.shard_required =
            std::clamp(number(field(data, "ultima_weapon_shard_count"), default_ultima_shard_count),
                       int64_t{min_ultima_shard_count}, int64_t{max_ultima_shard_count});
        return result;
    }

    auto Seed::decode(const Json& receipt) const -> item_delivery::ReceiptTemplate
    {
        const auto& descriptor =
            field(field(shared(), "items"), std::to_string(number(field(receipt, "item_id"), -1)));
        const auto token = text(field(descriptor, "token"));
        item_delivery::ReceiptTemplate result;
        result.supported = valid_token(token);
        if (result.supported)
        {
            result.primary = reward(token, receipt);
        }
        result.inventory = result.primary.grant.action_kind == L"add_inventory_item";
        const auto slot = number(field(field(data, "kh3_seed"), "slot"));
        const auto location = locations.find(number(field(receipt, "location_id"), -1));
        result.local =
            number(field(receipt, "from_player"), -1) == slot && location != locations.end();
        if (!result.local)
        {
            return result;
        }
        const auto& row = location->second;
        result.bridge_key = text(field(row, "bridge_key"));
        result.level_up = text(field(row, "source_table")) == "LevelUp";
        const auto name = text(field(row, "ap_location_name"));
        result.always_on =
            text(field(row, "world")) == "Always On" || name.starts_with("Always On - ");
        const auto& method = result.primary.grant.grant_method;
        result.bridge_required =
            result.supported &&
            (flag(field(row, "bridge_delivery_required")) ||
             method == L"direct_player_bad_status" || method == L"king_mickey_trap" || method == L"betrayal_trap" ||
             method == L"controller_unlock" || method == L"party_member_unlock" ||
             method == L"keyblade_style" || method == L"progressive_keyblade_level" ||
             method == L"character_unlock");
        if (!result.supported)
        {
            return result;
        }
        const auto bundle = bundles.find(result.bridge_key);
        if (bundle == bundles.end())
        {
            return result;
        }
        auto& grant = result.primary.grant;
        std::vector<std::string> normalized;
        for (const auto& entry : array(field(bundle->second, "randomized_tokens")))
        {
            const auto reward_token = token_text(entry);
            if (!valid_token(reward_token))
            {
                continue;
            }
            grant.randomized_tokens.push_back(wide(reward_token));
            normalized.push_back(reward_token);
            auto display = text(field(entry, "display"));
            if (display.empty())
            {
                display = text(field(field(field(shared(), "tokens"), reward_token), "name"));
            }
            grant.bundle_display.push_back(wide(display.empty() ? reward_token : display));
            if (reward_token != token)
            {
                result.rewards.push_back(reward(reward_token, receipt));
            }
        }
        for (const auto& entry : array(field(bundle->second, "vanilla_tokens")))
        {
            grant.vanilla_tokens.push_back(wide(token_text(entry)));
        }
        grant.randomized_summary = wide(text(field(bundle->second, "randomized_summary")));
        grant.vanilla_summary = wide(text(field(bundle->second, "vanilla_summary")));
        std::sort(normalized.begin(), normalized.end());
        for (const auto& entry : array(field(shared(), "vbonus_bundles")))
        {
            std::vector<std::string> tokens;
            for (const auto& value : array(field(entry, "tokens")))
            {
                tokens.push_back(text(value));
            }
            if (tokens == normalized)
            {
                grant.runtime_vbonus_source_id = wide(text(field(entry, "source_id")));
                break;
            }
        }
        return result;
    }

    auto describe(const Json& receipt, const item_delivery::ReceiptTemplate& item,
                  const std::vector<RuntimeItemGrantRequest>& grants) -> Json
    {
        Json result = Json::object_t{};
        for (const auto key : {"index", "item_id", "location_id", "from_player"})
        {
            if (receipt.contains(key))
            {
                result[key] = receipt[key];
            }
        }
        const auto& primary = item.supported ? grants.at(0) : item.primary.grant;
        const auto name =
            item.supported ? narrow(primary.item_name) : text(field(receipt, "item_name"));
        const auto kind = item.supported ? narrow(primary.action_kind) : "unavailable";
        auto category = kind.starts_with(grant_kind_prefix) ? kind.substr(grant_kind_prefix.size())
                        : kind == "trigger_trap"            ? "trap"
                                                            : "inventory";
        Json action{{"kind", kind}, {"category", category}, {"item_name", name}};
        if (kind == "grant_controller_unlock")
        {
            action["unlock_display_name"] = name.starts_with(unlock_name_prefix)
                                                ? name.substr(unlock_name_prefix.size())
                                                : name;
        }
        if (primary.munny_delta)
        {
            action["munny_amount"] = primary.munny_delta;
        }
        Json::array_t rewards, extras;
        for (size_t i = 0; i < grants.size(); ++i)
        {
            const auto& grant = grants[i];
            if (i)
            {
                rewards.emplace_back(
                    Json{{"kh3_runtime_token", narrow(grant.runtime_token)},
                         {"kh3_runtime_flag_name", narrow(grant.runtime_flag_name)},
                         {"kh3_grant_method", narrow(grant.grant_method)},
                         {"item_name", narrow(grant.item_name)},
                         {"kind", narrow(grant.action_kind)}});
                extras.emplace_back(narrow(grant.item_name));
            }
        }
        result["item_name"] = name;
        result["location_name"] =
            item.supported ? narrow(primary.location_name) : text(field(receipt, "location_name"));
        result["bridge_key"] = item.local ? Json(item.bridge_key) : Json{};
        result["native_supported"] = item.supported;
        result["kh3_runtime_token"] = narrow(primary.runtime_token);
        result["kh3_runtime_flag_name"] = narrow(primary.runtime_flag_name);
        result["kh3_grant_method"] = narrow(primary.grant_method);
        if (!item.primary.flag_rows.empty())
        {
            result["kh3_runtime_reward_row_names"] = strings(item.primary.flag_rows);
        }
        if (!primary.runtime_vbonus_source_id.empty())
        {
            result["kh3_runtime_vbonus_source_id"] = narrow(primary.runtime_vbonus_source_id);
        }
        if (!primary.randomized_tokens.empty())
        {
            result["kh3_randomized_tokens"] = strings(primary.randomized_tokens);
        }
        if (!primary.bundle_display.empty())
        {
            result["kh3_bundle_display"] = strings(primary.bundle_display);
        }
        if (!primary.vanilla_tokens.empty())
        {
            result["kh3_vanilla_tokens"] = strings(primary.vanilla_tokens);
        }
        if (!primary.randomized_summary.empty())
        {
            result["kh3_randomized_summary"] = narrow(primary.randomized_summary);
        }
        if (!primary.vanilla_summary.empty())
        {
            result["kh3_vanilla_summary"] = narrow(primary.vanilla_summary);
        }
        result["kh3_bridge_delivery_required"] = item.bridge_required;
        if (!extras.empty())
        {
            result["kh3_secondary_rewards"] = std::move(extras);
        }
        if (!rewards.empty())
        {
            result["native_secondary_rewards"] = std::move(rewards);
        }
        result["action"] = std::move(action);
        return result;
    }
} // namespace kh3ap::native_catalog
