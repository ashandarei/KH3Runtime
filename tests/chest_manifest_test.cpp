#include "chest_fixture.h"
#include "kh3ap/bridge/transport/bridge_channels.h"
#include "kh3ap/features/treasure/chest_character_policy.h"
#include "kh3ap/features/treasure/chest_commands.h"
#include <cassert>
#include <iostream>
#include <source_location>

using namespace chest_fixture;
using kh3ap::chest_manifest::Catalog;
static void rejects(const auto& action, std::source_location location = std::source_location::current())
{
    bool failed{}; try { action(); } catch (const std::runtime_error&) { failed = true; }
    if (!failed) std::cerr << "Expected rejection at line " << location.line() << '\n';
    assert(failed);
}

int main()
{
    using namespace kh3ap;
    for (int command = 0; command <= 255; ++command)
    {
        const auto action = chest_command::action_for_reactor(static_cast<uint8_t>(command));
        assert(player_action_command_value(action) == (command == 3 ? 78 : command == 4 ? 79 : -1));
    }
    static_assert(channels::allows_client_operation(channels::ap_items, 4));
    static_assert(!channels::allows_client_operation(channels::runtime_music_chests, 1));
    static_assert(!channels::allows_client_operation(channels::runtime_keyblade_chests, 2));
    static_assert(channels::allows_client_operation(channels::runtime_receipt_ack_event, 3));
    static_assert(!channels::allows_client_operation(channels::runtime_receipt_ack_event, 4));
    static_assert(!channels::allows_client_operation("unknown.json", 1));
    static_assert(channels::durable_native_channel(channels::runtime_workshop));
    static_assert(!channels::allows_client_operation(channels::runtime_workshop, 4));
    static_assert(!channels::durable_native_channel(channels::runtime_music_chests));
    const auto widen = [](std::string_view value) { return std::wstring(value.begin(), value.end()); };
    const auto eligible_names = [](const auto& pool) {
        std::set<std::wstring> names;
        for (const auto& [name, token] : pool) { assert(token.starts_with(L"WEP_KEYBLADE_")); names.insert(name); }
        return names;
    };
    Catalog catalog;
    const auto shared = data();
    const auto converted = vanilla(); const auto added = placed();
    const auto converted_id = kh3ap::chest_manifest::integer(converted["ap_location_id"]);
    const auto placed_id = kh3ap::chest_manifest::integer(added["ap_location_id"]);
    catalog.configure(seed({added, converted}));
    auto output = catalog.music({converted_id, placed_id}, 42);
    const auto& rows = output["chests"].get_ref<const Json::array_t&>();
    assert(output["owner"].get_ref<const std::string&>() == "dll" && output["process_id"].get<double>() == 42);
    assert(rows.size() == 2 && rows[0]["ap_location_id"].get<double>() == converted_id);
    assert(rows[0]["cleared"].get<bool>() && !rows[1]["cleared"].get<bool>()); // Release never opens placed chests.
    assert(rows[1]["yaw"].get<double>() == shared["placements"]["MOM_CHEST_001"]["rotation"].get_ref<const Json::array_t&>()[1].get<double>());
    const auto before_clear = catalog.snapshot({converted_id, placed_id}, 42, "bridge", widen);
    assert(before_clear.process_id == 42 && before_clear.seed == L"test" && before_clear.slot == 1);
    const auto converted_key = widen(converted["source_id"].get_ref<const std::string&>());
    const auto placed_key = widen(added["source_id"].get_ref<const std::string&>());
    assert(before_clear.assignments.at(converted_key).cleared && !before_clear.assignments.at(placed_key).cleared);
    assert(before_clear.has_vanilla_assignments);
    assert(before_clear.assignments.at(placed_key).yaw == static_cast<float>(rows[1]["yaw"].get<double>()));
    assert(catalog.accept_clears(clears({placed_id})));
    assert(catalog.music({}, 42)["chests"].get_ref<const Json::array_t&>()[1]["cleared"].get<bool>());
    const auto after_clear = catalog.snapshot({converted_id, placed_id}, 42, "bridge", widen);
    assert(after_clear.assignments.at(placed_key).cleared && !before_clear.assignments.at(placed_key).cleared);
    assert(after_clear.same_layout(before_clear)); // Clear updates retain actor bindings.
    assert(!after_clear.same_layout(catalog.snapshot({}, 43, "bridge", widen)));
    assert(!after_clear.same_layout(catalog.snapshot({}, 42, "other-bridge", widen)));
    assert(catalog.accept_clears(clears({}))); // Delayed smaller publication cannot relock.
    assert(catalog.music({}, 42)["chests"].get_ref<const Json::array_t&>()[1]["cleared"].get<bool>());
    auto wrong = clears({converted_id}); wrong["seed_name"] = "other";
    assert(!catalog.accept_clears(wrong)); wrong = clears({converted_id}); wrong["slot"] = 2;
    assert(!catalog.accept_clears(wrong));
    rejects([&] { catalog.accept_clears(clears({converted_id, converted_id})); });
    rejects([&] { catalog.accept_clears(clears({999})); });
    assert(!catalog.music({}, 42)["chests"].get_ref<const Json::array_t&>()[0]["cleared"].get<bool>());
    catalog.accept_clears(clears({converted_id}));
    assert(catalog.music({}, 42)["chests"].get_ref<const Json::array_t&>()[0]["cleared"].get<bool>());

    auto invalid = added; invalid["music_chest_position"].get_ref<Json::array_t&>()[0] = 123;
    rejects([&] { catalog.configure(seed({invalid})); });
    invalid = added; invalid["mom_difficulty"] = 3;
    rejects([&] { catalog.configure(seed({invalid})); });
    invalid = converted; invalid["ap_location_id"] = 999;
    rejects([&] { catalog.configure(seed({invalid})); });
    rejects([&] { catalog.configure(seed({converted, converted})); });
    assert(catalog.music({}, 42)["chests"].get_ref<const Json::array_t&>().size() == 2); // Failed configuration stays atomic.

    Json::array_t blades;
    size_t index{};
    for (const auto& [name, palette] : shared["palettes"].get_ref<const Json::object_t&>())
    {
        auto row = vanilla(index++);
        row.get_ref<Json::object_t&>().erase("music_chest_kind");
        row.get_ref<Json::object_t&>().erase("mom_song_id");
        row["keyblade_chest_keyblade"] = name; row["keyblade_chest_token"] = palette["token"];
        row["keyblade_chest_native"] = false; blades.push_back(std::move(row));
    }
    auto native = blades.back(); native["keyblade_chest_native"] = true;
    blades.back() = native;
    auto melody = converted; melody["keyblade_chest_keyblade"] = "Invalid stale blade";
    blades.push_back(melody);
    auto colored_seed = seed(blades);
    colored_seed["chestsanity"] = 1;
    catalog.configure(colored_seed);
    const auto keys = catalog.keyblades(42)["chests"].get_ref<const Json::array_t&>();
    const auto typed_keys = catalog.snapshot({}, 42, "bridge", widen);
    assert(!typed_keys.roxas_chestsanity);
    // Only an explicitly enabled seed grants Roxas these two chest keys.
    for (int player = -1; player < 32; ++player)
    {
        for (const auto blade : {L"Oathkeeper", L"Oblivion", L"Classic Tone", L"Grand Chef", L""})
        {
            assert(!roxas_chest_key_allowed(false, player, blade));
            assert(roxas_chest_key_allowed(true, player, blade) ==
                   (player == 17 && (std::wstring_view(blade) == L"Oathkeeper" ||
                                     std::wstring_view(blade) == L"Oblivion")));
        }
    }
    for (const auto setting : {Json(1), Json(true)})
    {
        auto roxas_seed = colored_seed;
        roxas_seed["roxas_chestsanity"] = setting;
        catalog.configure(roxas_seed);
        const auto enabled = catalog.snapshot({}, 42, "bridge", widen);
        assert(enabled.roxas_chestsanity && !enabled.same_layout(typed_keys));
        roxas_seed["chestsanity"] = 0;
        catalog.configure(roxas_seed);
        assert(!catalog.snapshot({}, 42, "bridge", widen).roxas_chestsanity);
    }
    catalog.configure(colored_seed);
    assert(!catalog.snapshot({}, 42, "bridge", widen).roxas_chestsanity);
    assert(keys.size() == 14); // Fifteen palettes minus the native-match chest.
    assert(typed_keys.map_keyblades.size() == 14); // All blades, minus the Melody overlap.
    assert((eligible_names(typed_keys.eligible_keyblades) == std::set<std::wstring>{L"Classic Tone", L"Grand Chef", L"Hunny Spout"}));
    for (const auto& [name, palette] : shared["palettes"].get_ref<const Json::object_t&>())
        if (typed_keys.eligible_keyblades.contains(widen(name)))
            assert(typed_keys.eligible_keyblades.at(widen(name)) == widen(palette["token"].get_ref<const std::string&>()));
    assert(!typed_keys.map_keyblades.contains(converted_key));
    const auto native_key = widen(native["source_id"].get_ref<const std::string&>());
    assert(!typed_keys.assignments.contains(native_key)); // No new gate for native matches.
    assert(typed_keys.map_keyblades.at(native_key).name == widen(native["keyblade_chest_keyblade"].get_ref<const std::string&>()));
    assert(typed_keys.map_keyblades.at(native_key).token == widen(native["keyblade_chest_token"].get_ref<const std::string&>()));
    for (const auto& row : keys)
    {
        const auto palette = data()["palettes"][row["keyblade_name"].get_ref<const std::string&>()];
        assert(row["keyblade"].get_ref<const std::string&>() == palette["token"].get_ref<const std::string&>());
        assert(row["body_r"].get<double>() == palette["body"].get_ref<const Json::array_t&>()[0].get<double>());
        assert(row["trim_b"].get<double>() == palette["trim"].get_ref<const Json::array_t&>()[2].get<double>());
        const auto& typed = typed_keys.assignments.at(widen(row["treasure_id"].get_ref<const std::string&>()));
        if (row["treasure_id"].get_ref<const std::string&>() == converted["source_id"].get_ref<const std::string&>())
        {
            assert(typed.location == converted_id && !typed.song.empty() && typed.keyblade.empty());
            continue; // Melody retains precedence over the fixture's overlapping Keyblade row.
        }
        assert(typed.keyblade == widen(row["keyblade"].get_ref<const std::string&>()));
        const auto& map_key = typed_keys.map_keyblades.at(widen(row["treasure_id"].get_ref<const std::string&>()));
        assert(map_key.name == typed.keyblade_name && map_key.token == typed.keyblade);
        assert(typed.body[0] == static_cast<float>(row["body_r"].get<double>()));
        assert(typed.trim[2] == static_cast<float>(row["trim_b"].get<double>()));
        if (typed.keyblade_name == L"Ever After")
            assert((typed.trim == std::array<float,4>{1.0f, 0.215861f, 0.520996f, 1.0f}));
    }
    invalid = blades[1]; invalid["keyblade_chest_token"] = "WEP_KEYBLADE_WRONG";
    rejects([&] { catalog.configure(seed({invalid})); });
    invalid = blades[1]; invalid["keyblade_chest_native"] = 0;
    rejects([&] { catalog.configure(seed({invalid})); });
    assert(catalog.keyblades(42)["chests"].get_ref<const Json::array_t&>().size() == 14);
    assert(catalog.snapshot({}, 42, "bridge", widen).map_keyblades == typed_keys.map_keyblades);

    for (const auto setting : {Json(0), Json(false)}) {
        auto disabled = colored_seed;
        disabled["chestsanity"] = setting;
        catalog.configure(disabled);
        assert(catalog.snapshot({}, 42, "bridge", widen).map_keyblades.empty());
        assert(catalog.snapshot({}, 42, "bridge", widen).eligible_keyblades.empty());
    }
    catalog.configure(seed(blades)); // Missing option must never opt into coloring.
    assert(catalog.snapshot({}, 42, "bridge", widen).map_keyblades.empty());
    colored_seed["chestsanity"] = true;
    catalog.configure(colored_seed);
    assert(catalog.snapshot({}, 42, "bridge", widen).map_keyblades == typed_keys.map_keyblades);

    // The legend lists the entire eligible pool even with no assigned/loaded
    // chests. It follows door mode, never the player's inventory.
    auto character_seed = seed({});
    character_seed["chestsanity"] = 1;
    character_seed["portal_unlocks"] = 1;
    catalog.configure(character_seed);
    const auto character_pool = catalog.snapshot({}, 42, "bridge", widen).eligible_keyblades;
    std::set<std::wstring> expected_doors;
    for (const auto& name : shared["door_keyblades"].get_ref<const Json::array_t&>())
        expected_doors.insert(widen(name.get_ref<const std::string&>()));
    assert(eligible_names(character_pool) == expected_doors && character_pool.size() == 12);
    assert(!character_pool.contains(L"Ultima Weapon") && !character_pool.contains(L"Pandora's Power"));

    Json::array_t placements;
    for (const auto& [id, unused] : shared["placements"].get_ref<const Json::object_t&>()) placements.push_back(placed(id));
    catalog.configure(seed(placements));
    assert(catalog.music({}, 42)["chests"].get_ref<const Json::array_t&>().size() == 8);
    assert(!catalog.snapshot({}, 42, "bridge", widen).has_vanilla_assignments);
    catalog.configure(seed({}));
    assert(catalog.music({}, 42)["chests"].get_ref<const Json::array_t&>().empty());
    assert(catalog.keyblades(42)["chests"].get_ref<const Json::array_t&>().empty());
    assert(catalog.snapshot({}, 42, "bridge", widen).assignments.empty());
    assert(catalog.snapshot({}, 42, "bridge", widen).map_keyblades.empty());
    std::cout << "PASS: native chest palettes, placements, release/clear policy, identity guards and atomic configuration\n";
}
