#include "kh3ap/bridge/serialization/json.h"
#include "kh3ap/bridge/serialization/json_fields.h"
#include "kh3ap/bridge/transport/pipe_transport.h"
#include "kh3ap/bridge/utilities.h"
#include "kh3ap/core/settings/pak_seed_settings.h"
#include "kh3ap/features/grants/keyblades/progressive_keyblade_level_helpers.h"
#include "kh3ap/features/moogle_shop/moogle_shop_catalog.h"
#include "kh3ap/features/moogle_shop/moogle_shop_scan_state.h"
#include "kh3ap/features/moogle_shop/ultima_weapon_shop_unlock.h"

#include <cassert>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace
{
    kh3ap::JsonObject seed_options{};
    std::shared_ptr<const kh3ap::json::SeedDocument> seed_document{};
    uint64_t game_state_revision{};
    std::optional<std::string> game_state_text{};
    int game_state_reads{};
    int prerequisite_queries{};
    int row_updates{};
    bool query_succeeds{true};
    bool update_succeeds{true};
    bool prerequisites_maxed{false};
    int desired_level{-1};
    uint64_t observed_row_address{};
    std::vector<std::wstring> queried_keyblades{};

    auto read_seed(std::string_view name) -> std::shared_ptr<const kh3ap::json::SeedDocument>
    {
        assert(name == "slot_data.json");
        return seed_document;
    }

    auto set_seed_options(std::string_view text) -> void
    {
        seed_options = kh3ap::JsonObject{text};
    }

    auto test_catalog() -> void
    {
        kh3ap::json::bind_runtime_seed_source(read_seed);
        kh3ap::MoogleShopCatalog catalog{};
        kh3ap::JsonObject document{};
        assert(!catalog.seed_loaded());
        assert(!catalog.try_load_seed(L"unused/slot_data.json", document));
        assert(!catalog.seed_loaded());

        // These loose-document options must never override the installed seed options.
        seed_document = kh3ap::json::decode_seed_document("slot_data.json", R"({
            "moogle_shop_checks": 0,
            "ultima_weapon_mode": 0,
            "bridge": {"locations": [
                {"type":"shop","bridge_key":"Shop:1","source_id":"1","shop_item_token":"T1","shop_price":321},
                {"type":"shop","bridge_key":"Shop:1","source_id":"duplicate-key","shop_item_token":"T2"},
                {"type":"shop","bridge_key":"Shop:2","source_id":"reserved-token","shop_item_token":"T2"},
                {"type":"shop","bridge_key":"Shop:2","source_id":"2","shop_item_token":"T3","shop_price":-1},
                {"type":"shop","bridge_key":"Shop:3","source_id":"duplicate-token","shop_item_token":"T1"},
                {"type":"shop","bridge_key":"Shop:4","source_id":"4","shop_item_token":"T4","shop_price":2147483648},
                {"type":"treasure","bridge_key":"Other:1","source_id":"other","shop_item_token":"T5"},
                {"type":"shop","bridge_key":"Shop:empty","source_id":"empty","shop_item_token":""}
            ]}
        })");
        set_seed_options(R"({"moogle_shop_checks":3,"ultima_weapon_mode":1,"keyblade_levels":0,
                            "ultima_weapon_shop_required_keyblades":["Kingdom Key","Hero's Origin"]})");
        assert(catalog.try_load_seed(L"unused/slot_data.json", document));
        assert(catalog.seed_loaded());
        assert(!catalog.locations_loaded());
        assert(catalog.options().shop_check_count == 3);
        assert(catalog.options().checks_enabled);
        assert(catalog.options().ultima.shop_mode);
        assert(catalog.options().ultima.keyblade_levels_start_at_max);
        assert(catalog.options().ultima.required_keyblades.size() == 2);

        catalog.load_locations(document, 20);
        assert(catalog.locations_loaded());
        assert(catalog.locations().size() == 3);
        assert(catalog.locations()[0].bridge_key == L"Shop:1");
        assert(catalog.locations()[0].item_token == L"T1");
        assert(catalog.locations()[0].shop_price == 321);
        assert(catalog.locations()[1].bridge_key == L"Shop:2");
        assert(catalog.locations()[1].item_token == L"T3");
        assert(catalog.locations()[1].source_id == L"2");
        assert(catalog.locations()[1].shop_price == 0);
        assert(catalog.locations()[2].bridge_key == L"Shop:4");
        assert(catalog.locations()[2].shop_price == 0);
        assert(!catalog.checked_locations_valid());

        game_state_text = R"({"checked_locations":["Shop:1","Other:1","Shop:unknown","Shop:1"]})";
        assert(!catalog.refresh_checked_locations(L"unused").updated);
        assert(game_state_reads == 0);
        game_state_revision = 1;
        const auto first = catalog.refresh_checked_locations(L"unused");
        assert(first.updated && first.changed);
        assert(game_state_reads == 1);
        assert(catalog.checked_locations_valid());
        assert(catalog.checked_bridge_keys().size() == 1);
        assert(catalog.checked_bridge_keys().contains(L"Shop:1"));
        assert(!catalog.refresh_checked_locations(L"unused").updated);
        assert(game_state_reads == 1);

        game_state_revision = 2;
        game_state_text.reset();
        assert(!catalog.refresh_checked_locations(L"unused").updated);
        assert(catalog.checked_locations_valid());
        assert(catalog.checked_bridge_keys().contains(L"Shop:1"));
        game_state_text = R"({"checked_locations":["Shop:1"]})";
        const auto unchanged = catalog.refresh_checked_locations(L"unused");
        assert(unchanged.updated && !unchanged.changed);
        game_state_revision = 3;
        game_state_text = R"({"checked_locations":["Shop:2"]})";
        const auto changed = catalog.refresh_checked_locations(L"unused");
        assert(changed.updated && changed.changed);
        assert(!catalog.checked_bridge_keys().contains(L"Shop:1"));
        assert(catalog.checked_bridge_keys().contains(L"Shop:2"));

        catalog.load_locations(document, 2);
        assert(catalog.locations().size() == 2);
        assert(!catalog.checked_locations_valid());
        // A catalog invalidation must refresh even at the previously observed revision.
        assert(catalog.refresh_checked_locations(L"unused").updated);
        catalog.load_locations(document, 0);
        assert(catalog.locations().size() == 1);

        catalog.reset();
        assert(!catalog.seed_loaded());
        assert(!catalog.locations_loaded());
        assert(!catalog.checked_locations_valid());
        assert(catalog.locations().empty());
        assert(catalog.checked_bridge_keys().empty());
        assert(!catalog.options().checks_enabled);
        assert(!catalog.options().ultima.shop_mode);
        assert(catalog.options().ultima.required_keyblades.empty());

        set_seed_options(R"({"moogle_shop_checks":-1,"ultima_weapon_mode":"shop",
                            "keyblade_levels":"start_at_max"})");
        assert(catalog.try_load_seed(L"unused/slot_data.json", document));
        assert(!catalog.options().checks_enabled);
        assert(catalog.options().shop_check_count == -1);
        assert(catalog.options().ultima.shop_mode);
        assert(catalog.options().ultima.keyblade_levels_start_at_max);
        set_seed_options(R"({"moogle_shop_checks":0,"ultima_weapon_mode":0,"keyblade_levels":0,
                            "ultima_weapon_shop_required_keyblades":["ignored"]})");
        catalog.reset();
        assert(catalog.try_load_seed(L"unused/slot_data.json", document));
        assert(!catalog.options().checks_enabled);
        assert(!catalog.options().ultima.shop_mode);
        assert(!catalog.options().ultima.keyblade_levels_start_at_max);
        assert(catalog.options().ultima.required_keyblades.empty());
    }

    auto test_ultima_unlock() -> void
    {
        using namespace std::chrono_literals;
        const auto now = std::chrono::steady_clock::time_point{100s};
        kh3ap::RuntimeMoogleShopScanState scan_state{};
        kh3ap::UltimaWeaponShopUnlock unlock{scan_state, false};
        kh3ap::UltimaWeaponShopOptions options{.shop_mode = true,
                                               .required_keyblades = {L"Kingdom Key"}};
        unlock.process(now, false, options);
        assert(prerequisite_queries == 0 && row_updates == 0);
        assert(scan_state.ultima_refresh_requested());
        options.shop_mode = false;
        unlock.process(now, true, options);
        assert(!scan_state.ultima_refresh_requested());
        assert(prerequisite_queries == 0 && row_updates == 0);

        options.shop_mode = true;
        unlock.request_refresh(L"event");
        unlock.process(now, true, options);
        assert(prerequisite_queries == 1 && row_updates == 1);
        assert(queried_keyblades == options.required_keyblades);
        assert(desired_level == 0);
        assert(!scan_state.ultima_refresh_requested());
        unlock.process(now + 1s, true, options);
        assert(prerequisite_queries == 1 && row_updates == 1);

        prerequisites_maxed = true;
        unlock.request_refresh(L"level_applied");
        unlock.process(now, true, options);
        assert(desired_level == 1);
        assert(!scan_state.ultima_refresh_requested());

        query_succeeds = false;
        unlock.request_refresh(L"query_failure");
        const int queries_before_failure = prerequisite_queries;
        const int updates_before_failure = row_updates;
        for (int attempt = 0; attempt < 10; ++attempt)
        {
            unlock.process(now + std::chrono::seconds(attempt), true, options);
            assert(prerequisite_queries == queries_before_failure + attempt + 1);
            assert(row_updates == updates_before_failure);
            assert(scan_state.ultima_refresh_requested() == (attempt < 9));
            unlock.process(now + std::chrono::seconds(attempt) + 999ms, true, options);
            assert(prerequisite_queries == queries_before_failure + attempt + 1);
        }
        unlock.process(now + 1h, true, options);
        assert(prerequisite_queries == queries_before_failure + 10);

        // Repeated lifecycle events rearm an already-pending retry immediately.
        unlock.request_refresh(L"rearm_after_limit");
        unlock.process(now, true, options);
        const int before_rearm = prerequisite_queries;
        unlock.request_refresh(L"rearm_pending");
        unlock.process(now + 1ms, true, options);
        assert(prerequisite_queries == before_rearm + 1);

        query_succeeds = true;
        update_succeeds = false;
        unlock.request_refresh(L"update_failure");
        const int before_update_failure = row_updates;
        for (int attempt = 0; attempt < 10; ++attempt)
        {
            unlock.process(now + std::chrono::seconds(attempt), true, options);
            assert(row_updates == before_update_failure + attempt + 1);
        }
        assert(!scan_state.ultima_refresh_requested());

        // Start-at-max seeds bypass prerequisite discovery, including an empty required list.
        options.keyblade_levels_start_at_max = true;
        options.required_keyblades.clear();
        update_succeeds = true;
        const int before_start_at_max = prerequisite_queries;
        unlock.request_refresh(L"start_at_max");
        unlock.process(now, true, options);
        assert(prerequisite_queries == before_start_at_max);
        assert(desired_level == 1);
        assert(!scan_state.ultima_refresh_requested());

        // Seed/work resets retain the validated row cache, as the service did before extraction.
        unlock.reset();
        assert(!scan_state.ultima_refresh_requested());
        unlock.request_refresh(L"after_reset");
        unlock.process(now, true, options);
        assert(observed_row_address == 1234);
        update_succeeds = false;
        unlock.request_refresh(L"retry_window");
        unlock.process(now, true, options);
        const int before_window_reset = row_updates;
        unlock.reset_retry_window();
        unlock.process(now + 1ms, true, options);
        assert(row_updates == before_window_reset + 1);
    }
} // namespace

namespace kh3ap
{
    auto installed_seed_slot_data() -> const JsonObject&
    {
        return seed_options;
    }

    auto read_text_file(const std::filesystem::path& path) -> std::optional<std::string>
    {
        assert(path.filename() == L"game_state.json");
        ++game_state_reads;
        return game_state_text;
    }

    auto try_query_ultima_weapon_shop_prerequisites(
        const std::vector<std::wstring>& required_keyblade_names,
        UltimaWeaponShopPrerequisiteQueryResult* result, std::wstring*) -> bool
    {
        ++prerequisite_queries;
        queried_keyblades = required_keyblade_names;
        *result = {.maxed_flag_count = prerequisites_maxed ? 10 : 0,
                   .required_flag_count = 10,
                   .all_required_keyblades_maxed = prerequisites_maxed};
        return query_succeeds;
    }

    auto try_update_ultima_weapon_shop_level(int desired_shop_level,
                                             UltimaWeaponShopRowCache& cache,
                                             UltimaWeaponShopLevelUpdateResult* result,
                                             std::wstring*) -> bool
    {
        ++row_updates;
        desired_level = desired_shop_level;
        observed_row_address = cache.row_address;
        cache.row_address = 1234;
        *result = {.previous_shop_level = 0,
                   .desired_shop_level = desired_shop_level,
                   .changed = desired_shop_level != 0};
        return update_succeeds;
    }
} // namespace kh3ap

namespace kh3ap::pipe
{
    auto revision(const std::filesystem::path& path) -> uint64_t
    {
        assert(path.filename() == L"game_state.json");
        return game_state_revision;
    }
} // namespace kh3ap::pipe

int main()
{
    test_catalog();
    test_ultima_unlock();
}
