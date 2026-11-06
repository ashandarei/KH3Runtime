#include "kh3ap/features/music/shuffle/form_theme.h"
#include <cassert>
#include <iostream>

namespace
{
    using namespace kh3ap;
    using Clock = FormThemeTracker::Clock;

    constexpr uint8_t idle = 1;
    constexpr uint8_t run = 2;
    constexpr uint8_t jump = 3;
    constexpr uint8_t combo_hl = 87;
    constexpr uint8_t combo_fl = 88;
    constexpr uint8_t rocket_laser = 132;
    constexpr uint8_t style_finish_fl = 39;

    struct Replay
    {
        FormThemeTracker tracker{};
        Clock::time_point now{};

        auto step(uint8_t state, const wchar_t* weapon, const wchar_t* keyblade = L"WEP_KEYBLADE05",
                std::chrono::milliseconds after = std::chrono::milliseconds(250)) -> std::optional<std::wstring>
        {
            now += after;
            return tracker.update({state, std::wstring{keyblade}, weapon, now});
        }
    };

    const std::wstring caribbean = L"/Game/Sound/BGM/BGM_ca_battle.BGM_ca_battle";
    const std::wstring san_fransokyo = L"/Game/Sound/BGM/BGM_bx_battle.BGM_bx_battle";
}

int main()
{
    assert(form_theme_track(L"WEP_KEYBLADE02") == L"/Game/Sound/BGM/BGM_he_battle.BGM_he_battle");
    assert(form_theme_track(L"WEP_KEYBLADE07") == L"/Game/Sound/BGM/BGM_poo_mini.BGM_poo_mini");
    assert(form_theme_track(L"WEP_KEYBLADE17") == L"/Game/Sound/BGM/kg_3/BGM_kg852b_battle.BGM_kg852b_battle");
    assert(form_theme_start_seconds(L"WEP_KEYBLADE17") > 0.0f && form_theme_start_seconds(L"WEP_KEYBLADE02") == 0.0f);
    assert(!form_theme_track(L"WEP_KEYBLADE15"));
    assert(is_form_weapon(L"w_so172_Wep_C") && is_form_weapon(L"w_so091_Wep_C"));
    assert(!is_form_weapon(L"w_so170_Wep_C") && !is_form_weapon(L"w_so010_Wep_C") && !is_form_weapon(L""));

    // Wheel of Fate as logged: form 1 (w_so092), a team attack that shows
    // the base weapon for ~5 s, form 2 (w_so093), then a finisher.
    {
        Replay replay{};
        assert(!replay.step(char_state::attack, L"w_so090_Wep_C"));
        assert(replay.step(char_state::style_change, L"w_so091_Wep_C") == caribbean);
        assert(replay.step(jump, L"w_so092_Wep_C") == caribbean);
        assert(replay.step(combo_hl, L"w_so092_Wep_C") == caribbean);
        assert(replay.step(rocket_laser, L"w_so090_Wep_C") == caribbean);
        assert(replay.step(rocket_laser, L"w_so090_Wep_C", L"WEP_KEYBLADE05", std::chrono::milliseconds(4650)) == caribbean);
        assert(replay.step(rocket_laser, L"w_so092_Wep_C") == caribbean);
        assert(replay.step(char_state::style_change, L"w_so091_Wep_C") == caribbean);
        assert(replay.step(combo_fl, L"w_so093_Wep_C") == caribbean);
        assert(replay.step(style_finish_fl, L"w_so093_Wep_C") == caribbean);
        assert(!replay.step(idle, L"w_so090_Wep_C"));
        assert(replay.tracker.end_reason() == L"finisher");
    }

    // A form that swapped the weapon and runs out: the base weapon stays.
    {
        Replay replay{};
        assert(!replay.step(idle, L"w_so090_Wep_C"));
        assert(replay.step(char_state::style_change, L"w_so091_Wep_C"));
        assert(replay.step(combo_hl, L"w_so092_Wep_C"));
        assert(replay.step(idle, L"w_so090_Wep_C"));
        assert(replay.step(idle, L"w_so090_Wep_C", L"WEP_KEYBLADE05", std::chrono::seconds(1)));
        assert(!replay.step(idle, L"w_so090_Wep_C", L"WEP_KEYBLADE05", std::chrono::seconds(1)));
        assert(replay.tracker.end_reason() == L"base_weapon");
    }

    // Nano Gear as logged: its form holds while Kingdom Key and Wheel of
    // Fate are out, whatever the time spent away, and resumes on return.
    {
        Replay replay{};
        assert(replay.step(char_state::style_change, L"w_so172_Wep_C", L"WEP_KEYBLADE08") == san_fransokyo);
        assert(replay.step(run, L"w_so172_Wep_C", L"WEP_KEYBLADE08") == san_fransokyo);
        assert(!replay.step(run, L"w_so010_Wep_C", L"WEP_KEYBLADE00"));
        assert(replay.tracker.end_reason() == L"keyblade_switched");
        assert(!replay.step(char_state::attack, L"w_so090_Wep_C", L"WEP_KEYBLADE05", std::chrono::seconds(30)));
        assert(replay.step(run, L"w_so172_Wep_C", L"WEP_KEYBLADE08") == san_fransokyo);
        assert(replay.step(run, L"w_so170_Wep_C", L"WEP_KEYBLADE08", std::chrono::seconds(5)) == san_fransokyo);
        assert(!replay.step(idle, L"w_so170_Wep_C", L"WEP_KEYBLADE08", std::chrono::seconds(9)));
        assert(replay.tracker.end_reason() == L"base_weapon");
    }

    // Oblivion as logged: its form keeps w_so210; it holds while put away
    // and comes back with its keyblade, until a plain attack shows it over.
    {
        Replay replay{};
        const std::wstring oblivion = L"/Game/Sound/BGM/kg_3/BGM_kg872_Battle.BGM_kg872_Battle";
        assert(replay.step(char_state::style_change, L"w_so210_Wep_C", L"WEP_KEYBLADE13") == oblivion);
        assert(replay.step(run, L"w_so210_Wep_C", L"WEP_KEYBLADE13") == oblivion);
        assert(!replay.step(run, L"w_so150_Wep_C", L"WEP_KEYBLADE11"));
        assert(replay.step(idle, L"w_so210_Wep_C", L"WEP_KEYBLADE13", std::chrono::seconds(30)) == oblivion);
        assert(!replay.step(char_state::attack, L"w_so210_Wep_C", L"WEP_KEYBLADE13"));
        assert(replay.tracker.end_reason() == L"plain_attack");
    }

    // Anti Form as logged, holding Ultima Weapon: STYLECHANGE first reads as
    // Ultima's form, the first COMBO_AN turns it into Anti Form, and it ends
    // once no Anti combo has come for a while, without an attack.
    {
        Replay replay{};
        const std::wstring anti{anti_form_theme_track};
        assert(!replay.step(run, L"w_so190_Wep_C", L"WEP_KEYBLADE14"));
        assert(replay.step(char_state::style_change, L"w_so190_Wep_C", L"WEP_KEYBLADE14") ==
               L"/Game/Sound/_DLC/BGM/BGM_battle_yozora.BGM_battle_yozora");
        assert(replay.step(jump, L"w_so190_Wep_C", L"WEP_KEYBLADE14", std::chrono::milliseconds(2600)));
        assert(replay.step(char_state::combo_anti, L"w_so190_Wep_C", L"WEP_KEYBLADE14") == anti);
        assert(replay.tracker.anti_form());
        assert(replay.step(jump, L"w_so190_Wep_C", L"WEP_KEYBLADE14", std::chrono::seconds(4)) == anti);
        assert(replay.step(char_state::combo_anti, L"w_so190_Wep_C", L"WEP_KEYBLADE14") == anti);
        assert(replay.step(idle, L"w_so190_Wep_C", L"WEP_KEYBLADE14", std::chrono::seconds(3)) == anti);
        assert(!replay.step(run, L"w_so190_Wep_C", L"WEP_KEYBLADE14", std::chrono::seconds(6)));
        assert(replay.tracker.end_reason() == L"anti_form_ended" && !replay.tracker.anti_form());
        // The STYLECHANGE was Anti Form's, so no Ultima form is left over.
        assert(!replay.step(idle, L"w_so190_Wep_C", L"WEP_KEYBLADE14"));
    }

    // A form weapon alone starts the theme (a form carried over a level load).
    {
        Replay replay{};
        assert(replay.step(idle, L"w_so172_Wep_C", L"WEP_KEYBLADE08") == san_fransokyo);
    }

    // A form that keeps its weapon (one class shown) ends at the next plain
    // attack, never on the weapon alone; its theme rests after 8 s of idling
    // or running and comes back at the next other action.
    {
        Replay replay{};
        assert(!replay.step(idle, L"w_so030_Wep_C", L"WEP_KEYBLADE02"));
        assert(replay.step(char_state::style_change, L"w_so030_Wep_C", L"WEP_KEYBLADE02"));
        assert(replay.step(idle, L"w_so030_Wep_C", L"WEP_KEYBLADE02", std::chrono::seconds(5)));
        assert(replay.step(run, L"w_so030_Wep_C", L"WEP_KEYBLADE02", std::chrono::seconds(2)));
        assert(!replay.step(idle, L"w_so030_Wep_C", L"WEP_KEYBLADE02", std::chrono::seconds(2)));
        assert(replay.tracker.end_reason() == L"resting");
        assert(!replay.step(run, L"w_so030_Wep_C", L"WEP_KEYBLADE02", std::chrono::seconds(20)));
        assert(replay.step(159, L"w_so030_Wep_C", L"WEP_KEYBLADE02"));
        assert(!replay.step(char_state::attack, L"w_so030_Wep_C", L"WEP_KEYBLADE02"));
        assert(replay.tracker.end_reason() == L"plain_attack");
    }

    // A keyblade without a theme never starts one.
    {
        Replay replay{};
        assert(!replay.step(char_state::style_change, L"w_so100_Wep_C", L"WEP_KEYBLADE15"));
        assert(!replay.tracker.active());
    }
    // A single state event arms one expiry; no repeating observation is
    // required while Sora remains at rest with the base weapon.
    {
        Replay replay{};
        assert(replay.step(char_state::style_change, L"w_so031_Wep_C", L"WEP_KEYBLADE02"));
        assert(replay.step(idle, L"w_so030_Wep_C", L"WEP_KEYBLADE02"));
        const auto due = replay.tracker.next_deadline();
        assert(due);
        FormThemeTracker::Observation at_due{idle, L"WEP_KEYBLADE02", L"w_so030_Wep_C", *due};
        assert(!replay.tracker.update(at_due));
        assert(!replay.tracker.next_deadline());
        replay.tracker.reset();
        assert(!replay.tracker.next_deadline());
    }
    std::cout << "Form theme tests passed\n";
}
