#include "kh3ap/features/kh02/progress/kh02_progress_policy.h"
#include "kh3ap/features/kh02/progress/kh02_check_policy.h"
#include "kh3ap/features/kh02/progress/kh02_gameflow_policy.h"
#include <cassert>
#include <deque>
#include <iostream>
#include <tuple>
#include <type_traits>
using namespace kh3ap::kh02_progress;

auto progress(int flow, int stairs) -> Json
{
    return Json{{"schema", "kh02.mirror_progress.v1"},
                {"flags", {{"gameflow_DW", flow}, {"gameflow_DW_MIRROR_STAIRS_CLEAR", stairs}}},
                {"challenges", {{"42", 1}}},
                {"counts", {{"29", 3}}},
                {"events", {{"42", true}}}};
}

template <class F> void rejected(F test)
{
    bool failed = false;
    try
    {
        test();
    }
    catch (const std::exception&)
    {
        failed = true;
    }
    assert(failed);
}

void gameflow_story_completion()
{
    using kh3ap::kh02_gameflow::read;
    using kh3ap::kh02_gameflow::update;
    // A start checkpoint must not award the battle it is about to start.
    for (const auto& [start, finish, flag] : {std::tuple{160, 161, "BattleBonus_DW_dw_02_DevilTower"},
                                              {320, 321, "BattleBonus_DW_dw_08_Darkside"},
                                              {322, 323, "BattleBonus_DW_dw_08_Darkball"},
                                              {340, 341, "gameflow_DW_AP_DARKSIDE_02_CLEAR"},
                                              {400, 401, "BattleBonus_DW_dw_09_DevilTower"},
                                              {520, 521, "BattleBonus_DW_dw_10_DevilWave"}})
    {
        assert(read(update(nullptr, "gameflow_DW", start), flag) == 0);
        assert(read(update(nullptr, "gameflow_DW", finish), flag) == 1);
    }
    for (const auto& [checkpoint, count] : {std::pair{110, 0}, {120, 10}, {130, 20}, {140, 30}, {150, 40}})
    {
        assert(read(update(nullptr, "gameflow_DW", checkpoint), "gameflow_DW_GEAR_GETCOUNT") == count);
    }
    const auto mirrors = update(nullptr, "gameflow_DW", 200);
    assert(read(mirrors, "gameflow_DW_VISITED_2") == 1);
    assert(read(mirrors, "BattleBonus_DW_dw_02_DevilTower") == 1);
    for (const auto* route : {"STAIRS", "CORRIDOR", "C_TUNNEL"})
    {
        const auto flag = std::string("gameflow_DW_MIRROR_") + route + "_CLEAR";
        assert(read(update(nullptr, "gameflow_DW", 260), flag) == 0);
        assert(read(update(nullptr, "gameflow_DW", 270), flag) == 1);
        assert(read(update(nullptr, "gameflow_DW", 300), flag) == 1);
    }
    const auto forest = update(nullptr, "gameflow_DW", 300);
    assert(read(forest, "gameflow_DW_VISITED_7") == 1);
    assert(read(forest, "gameflow_DW_VISITED_8") == 0);
    assert(read(update(nullptr, "gameflow_DW", 360), "gameflow_DW_VISITED_8") == 1);
    assert(read(update(nullptr, "gameflow_DW", 500), "gameflow_DW_VISITED_9") == 1);
    assert(read(update(nullptr, "gameflow_DW", 530), "gameflow_DW_VISITED_10") == 1);
    assert(read(update(nullptr, "gameflow_DW", 810), "gameflow_REBELLIS_CLEAR") == 0);

    auto existing = progress(110, 0);
    existing["flags"]["gameflow_DW_CHALLENGE_CHEST_DW_BOX003"] = 0;
    existing["flags"]["gameflow_DW_MIRROR_BATTLE01_CLEAR"] = 0;
    existing["flags"]["gameflow_DW_MIRROR_TREASURE_CLEAR"] = 0;
    existing["flags"]["gameflow_DW_MIRROR_CORRIDOR_IN"] = 0;
    existing["flags"]["GAMEFLOW_REBELLIS_CLEAR"] = 0;
    existing["cosmetics"] = kh3ap::kh02cosmetics::default_selection();
    const auto completed = update(existing, "GAMEFLOW_DW", 9999);
    assert(read(completed, "gameflow_REBELLIS_CLEAR") == 1);
    assert(!completed["flags"].contains("gameflow_REBELLIS_CLEAR"));
    assert(read(completed, "BattleBonus_DW_dw_10_DevilWave") == 1);
    for (int area = 2; area <= 10; ++area)
    {
        assert(read(completed, "gameflow_DW_VISITED_" + std::to_string(area)) == 1);
    }
    for (const auto* field : {"challenges", "counts", "events", "cosmetics"})
    {
        assert(completed.at(field) == existing.at(field));
    }
    for (const auto* flag : {"gameflow_DW_CHALLENGE_CHEST_DW_BOX003", "gameflow_DW_MIRROR_BATTLE01_CLEAR",
                             "gameflow_DW_MIRROR_TREASURE_CLEAR", "gameflow_DW_MIRROR_CORRIDOR_IN",
                             "BattleBonus_DW_dw_07_MirrorAqua_03", "BattleBonus_DW_dw_07_MirrorAqua_04"})
    {
        assert(read(completed, flag) == 0);
    }
    assert(update(completed, "gameflow_DW", 9999) == completed);
    auto rewound = update(completed, "gameflow_DW", 110);
    rewound["flags"]["gameflow_DW"] = 9999;
    assert(rewound == completed);
    const auto overridden = update(completed, "gameflow_rebellis_clear", 0);
    assert(read(overridden, "gameflow_REBELLIS_CLEAR") == 0);
    assert(read(overridden, "gameflow_DW") == 9999);
    assert(read(update(nullptr, "gameflow_DW_VISITED_8", 1), "gameflow_DW_VISITED_2") == 0);
    rejected([&] { update(existing, "gameflow_DW", 10000); });
    assert(existing["flags"]["GAMEFLOW_REBELLIS_CLEAR"] == 0);
}

void typed_progress_handoff()
{
    static_assert(std::is_const_v<SnapshotPtr::element_type>);
    const auto initial = from_json(progress(210, 0));
    const std::string mixed_case_flag = "GAMEFLOW_DW_MIRROR_STAIRS_CLEAR";
    assert(flag_key(mixed_case_flag));
    assert(mixed_case_flag == "GAMEFLOW_DW_MIRROR_STAIRS_CLEAR");
    auto duplicate_names = *initial;
    duplicate_names.flags.emplace("GAMEFLOW_DW", 210);
    const auto unchanged_duplicate = duplicate_names;
    rejected([&] { validate(duplicate_names); });
    assert(duplicate_names == unchanged_duplicate);
    const auto unchanged_valid = *initial;
    validate(*initial);
    assert(*initial == unchanged_valid);
    auto producer = *initial;
    producer.cosmetics = CosmeticSelection{};
    producer.cosmetics->equipped = {12, 9, 9, 22};
    const auto queued = freeze(producer);
    producer.flags["gameflow_DW"] = 240;
    producer.counts[29] = 100;
    producer.events[42] = false;
    producer.cosmetics->equipped[0] = 0;
    assert(queued->flags.at("gameflow_DW") == 210);
    assert(queued->counts.at(29) == 3);
    assert(queued->events.at(42));
    assert(queued->cosmetics->equipped[0] == 12);
    assert(*from_json(to_json(queued)) == *queued);
    assert(!from_json(nullptr));
    assert(to_json(SnapshotPtr{}).is_null());

    const Binding bound{"seed-a", 2, 7, 12};
    assert(accepts(bound, "seed-a", 2, 7, 12, true, false));
    assert(!accepts(bound, "seed-b", 2, 7, 12, true, false));
    assert(!accepts(bound, "seed-a", 3, 7, 12, true, false));
    assert(!accepts(bound, "seed-a", 2, 8, 12, true, false));
    assert(!accepts(bound, "seed-a", 2, 7, 13, true, false));
    assert(!accepts(bound, "seed-a", 2, 7, 12, false, false));
    assert(!accepts(bound, "seed-a", 2, 7, 12, true, true));

    Bank bank;
    bank.revision = bound.revision;
    assert(bank.update(queued, bound.revision));
    // The same immutable capture is held by a published View and a native
    // autosave-begin/manual-save-open while the working bank keeps advancing.
    const auto save_boundary = bank.active;
    const auto visible_before = bank.active;
    assert(bank.update(freeze(producer), bound.revision));
    bank.slots[50] = save_boundary;
    bank.slots[51] = bank.active;
    assert(visible_before->flags.at("gameflow_DW") == 210);
    assert(bank.slots.at(50)->counts.at(29) == 3);
    assert(bank.slots.at(51)->counts.at(29) == 100);
    bank.load(50);
    assert(!bank.update(freeze(producer), bound.revision));
    assert(bank.active == save_boundary);
    assert(bank.active->events.at(42));

    // Process every FIFO update, including an event window that closes before
    // a subsequent publication. A coalescing mailbox would lose this evidence.
    auto earned = *queued;
    earned.challenges[42] = 2;
    auto cleared = earned;
    cleared.challenges[42] = 1;
    cleared.events[42] = false;
    const Binding current{"seed-a", 2, bank.revision, 13};
    std::deque<Update> updates{{current, freeze(earned)}, {current, freeze(cleared)}};
    bool observed_event = false;
    bool observed_check = false;
    for (const auto& update : updates)
    {
        assert(accepts(update.binding, "seed-a", 2, bank.revision, 13, true, false));
        assert(bank.update(update.snapshot, update.binding.revision));
        observed_event |= bank.active->events.at(42);
        observed_check |= kh3ap::kh02_checks::earned(bank.active, {"Objective:043", "", 42});
    }
    assert(observed_event && observed_check);
    assert(!bank.active->events.at(42));
    assert(bank.active->challenges.at(42) == 1);
    bank.new_game();
    assert(!accepts(current, "seed-a", 2, bank.revision, 13, true, false));
    assert(!bank.active);
    assert(bank.slots.at(50) == save_boundary);

    for (const auto checkpoint : {102, 210, 270, 320, 341, 410, 520, 810, 9999})
    {
        auto value = from_json(progress(checkpoint, 1));
        for (const auto area : {4, 5, 6})
        {
            const auto typed = mirror_room_entry_flags(value->flags, area);
            assert(Json(typed) == mirror_room_entry_flags(to_json(value)["flags"], area));
            assert(Json(hub_entry_flags(typed)) == hub_entry_flags(Json(typed)));
        }
        assert(Json(enter_flags(value->flags, 102)) == enter_flags(to_json(value)["flags"], 102));
        assert(Json(enter_flags(value->flags, 200)) == enter_flags(to_json(value)["flags"], 200));
        assert(Json(enter_flags(value->flags, 102, true)) == to_json(value)["flags"]);
    }

    auto invalid = *initial;
    invalid.flags["GAMEFLOW_DW"] = 210;
    rejected([&] { freeze(invalid); });
    invalid = *initial;
    invalid.flags["gameflow_DW"] = 211;
    rejected([&] { freeze(invalid); });
    invalid = *initial;
    invalid.challenges[51] = 1;
    rejected([&] { freeze(invalid); });
    invalid = *initial;
    invalid.challenges[1] = 3;
    rejected([&] { freeze(invalid); });
    invalid = *initial;
    invalid.counts[1] = -1;
    rejected([&] { freeze(invalid); });
    invalid = *initial;
    invalid.events[-1] = true;
    rejected([&] { freeze(invalid); });
    invalid = *initial;
    invalid.cosmetics = CosmeticSelection{};
    invalid.cosmetics->bottom[0] = 256;
    rejected([&] { freeze(invalid); });

    Snapshot largest;
    largest.flags["gameflow_DW"] = 9999;
    for (int index = 1; index < 256; ++index)
    {
        auto key = "gameflow_DW_" + std::to_string(index);
        key.resize(96, 'x');
        largest.flags.emplace(std::move(key), 1000000);
    }
    for (int index = 0; index <= 50; ++index)
    {
        largest.challenges[index] = 2;
        largest.counts[index] = 1000000;
        largest.events[index] = false;
    }
    largest.cosmetics = CosmeticSelection{{12, 9, 9, 22}, 2, {255, 255, 255}, {255, 255, 255}};
    const auto largest_json = to_json(freeze(largest));
    assert(largest_json.dump().size() < 32768);
    validate(largest_json);
}

int main()
{
    gameflow_story_completion();
    typed_progress_handoff();
    Bank bank;
    bank.load(50);
    assert(!bank.active);
    const auto initial = bank.revision;
    assert(bank.update(from_json(progress(210, 0)), initial));
    const auto save_begin = bank.active;
    assert(bank.update(from_json(progress(240, 1)), initial));
    // Changes after serialization begins must not leak into that save.
    bank.slots[50] = save_begin;
    bank.slots[51] = bank.active;
    bank.load(50);
    assert(bank.active->flags.at("gameflow_DW_MIRROR_STAIRS_CLEAR") == 0);
    assert(!bank.update(from_json(progress(240, 1)), initial));
    bank.load(51);
    assert(bank.active->flags.at("gameflow_DW_MIRROR_STAIRS_CLEAR") == 1);
    assert(bank.active->counts.at(29) == 3 && bank.active->events.at(42) == true);
    Bank cold;
    cold.slots = bank.slots;
    cold.load(51);
    assert(cold.active == bank.active);
    cold.new_game();
    assert(!cold.active);
    cold.load(50);
    assert(cold.active == save_begin);
    cold.load(99);
    assert(!cold.active);
    auto route = progress(240, 1);
    route["flags"]["gameflow_DW_MIRROR_STAIRS_IN"] = 1;
    route["flags"]["gameflow_DW_MIRROR_CORRIDOR_IN"] = 1;
    route["flags"]["gameflow_DW_MIRROR_C_TUNNEL_IN"] = 1;
    auto entered = route;
    entered["flags"] = hub_entry_flags(route["flags"]);
    validate(entered);
    assert(entered["flags"]["gameflow_DW_MIRROR_STAIRS_IN"] == 0);
    assert(entered["flags"]["gameflow_DW_MIRROR_CORRIDOR_IN"] == 0);
    assert(entered["flags"]["gameflow_DW_MIRROR_C_TUNNEL_IN"] == 0);
    assert(entered["flags"]["gameflow_DW_MIRROR_STAIRS_CLEAR"] == 1);
    assert(entered["flags"]["gameflow_DW"] == 240);
    assert(entered["challenges"] == route["challenges"] && entered["counts"] == route["counts"] &&
           entered["events"] == route["events"]);
    assert(route["flags"]["gameflow_DW_MIRROR_STAIRS_IN"] == 1);
    for (int area : {4, 5, 6})
    {
        // Missing selectors caused the observed post-Phantom Japanese notice
        // and softlock; stale selectors can credit the wrong puzzle instead.
        for (const auto& before : {route["flags"], hub_entry_flags(route["flags"]), Json{{"gameflow_DW", 270}}})
        {
            const auto room = mirror_room_entry_flags(before, area);
            assert(room["gameflow_DW_MIRROR_STAIRS_IN"] == (area == 4 ? 1 : 0));
            assert(room["gameflow_DW_MIRROR_CORRIDOR_IN"] == (area == 5 ? 1 : 0));
            assert(room["gameflow_DW_MIRROR_C_TUNNEL_IN"] == (area == 6 ? 1 : 0));
            assert(room["gameflow_DW"] == before["gameflow_DW"]);
            if (before.contains("gameflow_DW_MIRROR_STAIRS_CLEAR"))
            {
                assert(room["gameflow_DW_MIRROR_STAIRS_CLEAR"] == before["gameflow_DW_MIRROR_STAIRS_CLEAR"]);
            }
            assert(mirror_room_entry_flags(room, area) == room);
            auto saved = progress(270, 1);
            saved["flags"] = room;
            validate(saved);
            Bank restored;
            restored.slots[99] = from_json(saved);
            restored.load(99);
            assert(to_json(restored.active)["flags"] == room);
            assert(to_json(restored.active)["counts"] == saved["counts"] &&
                   to_json(restored.active)["challenges"] == saved["challenges"]);
            const auto hub = hub_entry_flags(room);
            assert(hub["gameflow_DW_MIRROR_STAIRS_IN"] == 0 && hub["gameflow_DW_MIRROR_CORRIDOR_IN"] == 0 &&
                   hub["gameflow_DW_MIRROR_C_TUNNEL_IN"] == 0);
        }
    }
    auto mixed = Json{{"gameflow_DW", 270}, {"GAMEFLOW_DW_MIRROR_STAIRS_IN", 1}};
    mixed = mirror_room_entry_flags(mixed, 6);
    assert(mixed["GAMEFLOW_DW_MIRROR_STAIRS_IN"] == 0 && !mixed.contains("gameflow_DW_MIRROR_STAIRS_IN"));
    rejected([&] { mirror_room_entry_flags(route["flags"], 7); });
    rejected([&] { mirror_room_entry_flags(Json{}, 6); });
    auto town = enter_flags(route["flags"], 102);
    assert(town["gameflow_DW"] == 102 && town["gameflow_DW_CHALLENGE_MIRROR_CHECKPOINT"] == 240);
    const auto mirrors = enter_flags(town, 200);
    assert(mirrors["gameflow_DW"] == 240 && mirrors["gameflow_DW_MIRROR_STAIRS_CLEAR"] == 1);
    auto first_town = enter_flags(Json{{"gameflow_DW", 102}}, 102);
    assert(enter_flags(first_town, 200)["gameflow_DW"] == 200);
    assert(!resume_castle_entry(first_town));
    for (int phase : {0, 100, 101})
    {
        const Json interrupted{{"gameflow_DW", phase}, {"gameflow_DW_VISITED_2", 1}};
        assert(!resume_castle_entry(interrupted));
    }
    assert(resume_castle_entry(Json{{"gameflow_DW", 102}, {"gameflow_DW_VISITED_2", 1}}));
    for (int phase : {120, 140, 160, 270, 520, 9999})
    {
        auto revisited = progress(phase, 1)["flags"];
        revisited["gameflow_DW_VISITED_2"] = 1;
        revisited["gameflow_DW_GEAR_GETCOUNT"] = 20;
        assert(resume_castle_entry(revisited));
        assert(enter_flags(revisited, 102, resume_castle_entry(revisited)) == revisited);
    }
    entered["flags"]["gameflow_DW_MIRROR_CORRIDOR_IN"] = 1;
    bank.active = from_json(entered);
    const auto during_corridor = bank.active;
    const auto returned = hub_entry_flags(to_json(bank.active)["flags"]);
    assert(returned["gameflow_DW_MIRROR_CORRIDOR_IN"] == 0);
    assert(during_corridor->flags.at("gameflow_DW_MIRROR_CORRIDOR_IN") == 1);
    for (int checkpoint : {100, 101, 102, 110, 120, 130, 140, 150, 160, 161, 170, 180, 200, 210, 280, 300})
    {
        auto castle = progress(checkpoint, 0);
        validate(castle);
    }
    for (int checkpoint : {300, 310, 320, 321, 322, 323, 330, 340, 341, 350, 360, 361, 400, 401, 410,
                           500, 510, 520, 521, 530, 600, 610, 620, 630, 700, 710, 800, 810, 9999})
    {
        auto forest = progress(checkpoint, 1);
        forest["flags"]["battlebonus_dw_dw_09_deviltower"] = 1;
        forest["flags"]["battlebonus_dw_dw_10_devilwave"] = 1;
        forest["flags"]["battlebonus_dw_dw_08_darkside"] = 1;
        forest["flags"]["battlebonus_dw_dw_08_darkball"] = 1;
        forest["counts"]["32"] = 12;
        forest["flags"]["gameflow_CHALLENGE_34_RED_FLOWER"] = 1;
        forest["flags"]["gameflow_CHALLENGE_34_BLUE_FLOWER"] = 0;
        forest["flags"]["gameflow_CHALLENGE_34_GREEN_FLOWER"] = 1;
        forest["flags"]["gameflow_CHALLENGE_45_dw_08_MEMORY"] = 1;
        forest["flags"]["gameflow_DW_VISITED_2"] = 1;
        forest["flags"]["gameflow_DW_VISITED_8"] = 1;
        validate(forest);
        Bank saved;
        assert(saved.update(from_json(forest), saved.revision));
        saved.slots[99] = saved.active;
        Bank restarted;
        restarted.slots = saved.slots;
        restarted.load(99);
        assert(to_json(restarted.active) == forest);
        const auto backtrack_town = enter_flags(to_json(restarted.active)["flags"], 102, true);
        assert(backtrack_town == forest["flags"]);
        assert(backtrack_town["gameflow_DW_VISITED_2"] == 1 && backtrack_town["gameflow_DW_VISITED_8"] == 1);
        rejected([&] { enter_flags(to_json(restarted.active)["flags"], 200, true); });
        const auto replay_town = enter_flags(to_json(restarted.active)["flags"], 102);
        assert(enter_flags(replay_town, 200)["gameflow_DW"] == checkpoint);
    }
    for (int checkpoint : {281, 290, 301, 311, 319, 324, 329, 331, 339, 342, 349,
                           351, 359, 362, 399, 402, 411, 499, 501, 519, 522, 10000})
    {
        auto invalid = progress(checkpoint, 1);
        rejected([&] { validate(invalid); });
    }
    auto invalid_bonus = progress(320, 1);
    invalid_bonus["flags"]["battlebonus_dw_dw_08_unknown"] = 1;
    rejected([&] { validate(invalid_bonus); });
    for (const auto* key : {"gameflow_CHALLENGE_34_YELLOW_FLOWER", "gameflow_CHALLENGE_45_dw_09_MEMORY"})
    {
        auto invalid = progress(300, 1);
        invalid["flags"][key] = 1;
        rejected([&] { validate(invalid); });
    }
    auto bad = progress(240, 1);
    bad["flags"]["gameflow_HE"] = 1;
    rejected([&] { validate(bad); });
    bad = progress(240, 1);
    bad["flags"]["gameflow_DW"] = 103;
    rejected([&] { validate(bad); });
    bad = progress(240, 1);
    bad["flags"]["gameflow_DW"] = 240.0;
    rejected([&] { validate(bad); });
    bad = progress(240, 1);
    bad["counts"]["29"] = -1;
    rejected([&] { validate(bad); });
    bad = progress(240, 1);
    bad["challenges"]["42"] = 3;
    rejected([&] { validate(bad); });
    bad = progress(240, 1);
    bad["events"]["42"] = 1;
    rejected([&] { validate(bad); });
    bad = progress(240, 1);
    bad["counts"]["51"] = 1;
    rejected([&] { validate(bad); });
    bad = progress(240, 1);
    bad["counts"]["01"] = 1;
    rejected([&] { validate(bad); });
    bad = progress(240, 1);
    bad["unexpected"] = true;
    rejected([&] { validate(bad); });
    bad = progress(240, 1);
    bad["flags"]["GAMEFLOW_DW"] = 240;
    rejected([&] { validate(bad); });
    bad = progress(240, 1);
    bad["flags"]["gameflow_DW_CHALLENGE_42"] = -1;
    validate(bad);
    for (int i = 1; i <= 7; ++i)
    {
        const auto key = "gameflow_CHALLENGE_29_0" + std::to_string(i);
        assert(flag_key(key));
        auto gem = progress(270, 1);
        gem["flags"][key] = 1;
        gem["counts"]["28"] = i;
        validate(gem);
        Bank saved;
        saved.slots[99] = from_json(gem);
        saved.load(99);
        assert(to_json(saved.active) == gem);
    }
    for (const auto* key : {"gameflow_CHALLENGE_29_00", "gameflow_CHALLENGE_29_08", "gameflow_CHALLENGE_29_1",
                            "gameflow_CHALLENGE_29_001"})
    {
        assert(!flag_key(key));
        auto gem = progress(270, 1);
        gem["flags"][key] = 1;
        rejected([&] { validate(gem); });
    }
    Json gem_flags = {{"gameflow_DW", 270}};
    assert(white_gem_count(gem_flags) == 0);
    for (int i = 1; i <= 7; ++i)
    {
        gem_flags["gameflow_CHALLENGE_29_0" + std::to_string(i)] = 1;
        assert(white_gem_count(gem_flags) == i);
        // Replaying an already-collected actor cannot increase this total.
        gem_flags["gameflow_CHALLENGE_29_0" + std::to_string(i)] = 1;
        assert(white_gem_count(gem_flags) == i);
    }
    auto colors = kh3ap::kh02cosmetics::default_selection();
    assert(!kh3ap::kh02cosmetics::adjust_channel(colors, 2, 8));
    assert(colors == kh3ap::kh02cosmetics::default_selection());
    assert(kh3ap::kh02cosmetics::adjust_channel(colors, 3, -8));
    assert(colors["top"][0] == 247 && colors["color_mode"] == 1);
    assert(kh3ap::kh02cosmetics::adjust_channel(colors, 3, 1));
    assert(colors["top"][0] == 248);
    assert(kh3ap::kh02cosmetics::adjust_channel(colors, 3, 8));
    assert(colors["top"][0] == 255);
    assert(!kh3ap::kh02cosmetics::adjust_channel(colors, 3, 1));
    assert(kh3ap::kh02cosmetics::adjust_channel(colors, 6, -1));
    assert(colors["bottom"][0] == 0 && colors["color_mode"] == 2);
    assert(kh3ap::kh02cosmetics::adjust_channel(colors, 6, 8));
    assert(colors["bottom"][0] == 8);
    assert(kh3ap::kh02cosmetics::adjust_channel(colors, 4, -1));
    assert(colors["color_mode"] == 2);
    assert(colors["equipped"] == kh3ap::kh02cosmetics::default_selection()["equipped"]);
    kh3ap::kh02cosmetics::validate(colors);

    auto wardrobe = progress(700, 1);
    wardrobe["cosmetics"] = kh3ap::kh02cosmetics::default_selection();
    wardrobe["cosmetics"]["equipped"] = {12, 9, 9, 22};
    wardrobe["cosmetics"]["color_mode"] = 2;
    wardrobe["cosmetics"]["top"] = {8, 128, 255};
    validate(wardrobe);
    Bank wardrobe_save;
    assert(wardrobe_save.update(from_json(wardrobe), wardrobe_save.revision));
    wardrobe_save.slots[99] = wardrobe_save.active;
    wardrobe_save.new_game();
    wardrobe_save.load(99);
    assert(to_json(wardrobe_save.active) == wardrobe);
    auto travel = wardrobe;
    travel["flags"] = enter_flags(travel["flags"], 102, true);
    validate(travel);
    assert(travel["cosmetics"] == wardrobe["cosmetics"]);
    for (int category = 0; category < 4; ++category)
    {
        auto bad = wardrobe;
        bad["cosmetics"]["equipped"][category] = kh3ap::kh02cosmetics::item_counts[category] + 1;
        rejected([&] { validate(bad); });
    }
    for (const auto& invalid : {Json(-1), Json(256), Json(1.5), Json("8"), Json(UINT64_MAX)})
    {
        auto bad = wardrobe;
        bad["cosmetics"]["top"][0] = invalid;
        rejected([&] { validate(bad); });
    }
    auto invalid_color = wardrobe;
    invalid_color["cosmetics"]["color_mode"] = 3;
    rejected([&] { validate(invalid_color); });
    invalid_color = wardrobe;
    invalid_color["cosmetics"]["equipped"].push_back(0);
    rejected([&] { validate(invalid_color); });
    std::cout << "KH02 slot isolation, cold restore, stale-update and validation checks passed\n";
}
