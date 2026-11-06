#include "kh3ap/features/music/shuffle/form_theme.h"

#include "kh3ap/features/music/shuffle/bgm_shuffle_table.h"

#include <array>
#include <cwctype>
#include <utility>

namespace kh3ap
{
    namespace
    {
        // A swapped-out weapon can reappear for a few seconds inside a form,
        // during special moves (FR_ROCKETLASER showed Wheel of Fate's base
        // weapon for ~5 s). Idle, running or jumping with it means the form
        // is over.
        constexpr auto base_weapon_grace = std::chrono::seconds(8);
        constexpr auto base_weapon_grace_at_rest = std::chrono::milliseconds(1500);
        constexpr uint8_t idle_state = 1;
        constexpr uint8_t run_state = 2;
        constexpr uint8_t jump_state = 3;
        // A form that keeps its weapon rests its theme after this long with
        // Sora only idling, running or jumping (user choice, 2026-09-29).
        constexpr auto kept_weapon_rest = std::chrono::seconds(8);

        auto at_rest(std::optional<uint8_t> state) -> bool
        {
            return !state || state == idle_state || state == run_state || state == jump_state;
        }

        auto grace_for(std::optional<uint8_t> state) -> FormThemeTracker::Clock::duration
        {
            return at_rest(state) ? FormThemeTracker::Clock::duration{base_weapon_grace_at_rest}
                                  : FormThemeTracker::Clock::duration{base_weapon_grace};
        }

        // Keyblade -> world whose music its forms play.
        constexpr std::array<std::pair<std::wstring_view, std::wstring_view>, 9> keyblade_worlds{{
                {L"WEP_KEYBLADE01", L"TT"},  // Shooting Star
                {L"WEP_KEYBLADE02", L"HE"},  // Hero's Origin
                {L"WEP_KEYBLADE03", L"TS"},  // Favorite Deputy
                {L"WEP_KEYBLADE04", L"RA"},  // Ever After
                {L"WEP_KEYBLADE05", L"CA"},  // Wheel of Fate
                {L"WEP_KEYBLADE06", L"FZ"},  // Crystal Snow
                {L"WEP_KEYBLADE07", L"PO"},  // Hunny Spout
                {L"WEP_KEYBLADE08", L"BX"},  // Nano Gear
                {L"WEP_KEYBLADE09", L"MI"},  // Happy Gear
        }};

        struct KeybladeTrack
        {
            std::wstring_view keyblade;
            std::wstring_view track;
            // Seconds of intro skipped.
            float start{};
        };

        // Keyblades without a world use individually selected tracks.
        constexpr std::array<KeybladeTrack, 9> keyblade_tracks{{
                {L"WEP_KEYBLADE00", L"/Game/Sound/BGM/ca/BGM_han_bat_boss_ca_2.BGM_han_bat_boss_ca_2"},  // Kingdom Key
                // Hunny Spout: the 100 Acre Wood minigame, livelier than its field theme.
                {L"WEP_KEYBLADE07", L"/Game/Sound/BGM/BGM_poo_mini.BGM_poo_mini"},
                {L"WEP_KEYBLADE10", L"/Game/Sound/BGM/BGM_lsi_menu.BGM_lsi_menu"},  // Classic Tone
                {L"WEP_KEYBLADE11", L"/Game/Sound/BGM/BGM_cook_menu.BGM_cook_menu"},  // Grand Chef
                {L"WEP_KEYBLADE12", L"/Game/Sound/_DLC/BGM/BGM_kg_BattleAreaD1.BGM_kg_BattleAreaD1"},  // Oathkeeper
                {L"WEP_KEYBLADE13", L"/Game/Sound/BGM/kg_3/BGM_kg872_Battle.BGM_kg872_Battle", 13.0f},  // Oblivion
                {L"WEP_KEYBLADE14", L"/Game/Sound/_DLC/BGM/BGM_battle_yozora.BGM_battle_yozora", 10.0f},  // Ultima Weapon
                // Starlight: "Rise of the Union".
                {L"WEP_KEYBLADE17", L"/Game/Sound/BGM/kg_3/BGM_kg852b_battle.BGM_kg852b_battle", 10.0f},
                {L"WEP_KEYBLADE18", L"/Game/Sound/BGM/kg_3/BGM_kg852a_battle.BGM_kg852a_battle"},  // Pandora's Power
        }};

        // Anti Form ends this long after its last combo.
        constexpr auto anti_form_grace = std::chrono::seconds(8);

        auto is_style_finish(uint8_t state) -> bool
        {
            return state >= char_state::style_finish_first && state <= char_state::style_finish_last;
        }
    }

    auto form_theme_track(std::wstring_view keyblade) -> std::optional<std::wstring>
    {
        for (const KeybladeTrack& entry : keyblade_tracks)
        {
            if (entry.keyblade == keyblade)
            {
                return std::wstring{entry.track};
            }
        }
        for (const auto& [name, code] : keyblade_worlds)
        {
            if (name != keyblade)
            {
                continue;
            }
            for (const BgmShuffleWorld& world : bgm_shuffle_worlds())
            {
                if (world.code == code)
                {
                    return std::wstring{world.battles.empty() ? world.fields.front() : world.battles.front()};
                }
            }
        }
        return std::nullopt;
    }

    auto form_custom_targets() -> const std::vector<FormCustomTarget>&
    {
        static const std::vector<FormCustomTarget> targets{
                {L"WEP_KEYBLADE00", L"Form_KingdomKey", L"Kingdom Key"},
                {L"WEP_KEYBLADE01", L"Form_ShootingStar", L"Shooting Star"},
                {L"WEP_KEYBLADE02", L"Form_HerosOrigin", L"Hero's Origin"},
                {L"WEP_KEYBLADE03", L"Form_FavoriteDeputy", L"Favorite Deputy"},
                {L"WEP_KEYBLADE04", L"Form_EverAfter", L"Ever After"},
                {L"WEP_KEYBLADE05", L"Form_WheelOfFate", L"Wheel of Fate"},
                {L"WEP_KEYBLADE06", L"Form_CrystalSnow", L"Crystal Snow"},
                {L"WEP_KEYBLADE07", L"Form_HunnySpout", L"Hunny Spout"},
                {L"WEP_KEYBLADE08", L"Form_NanoGear", L"Nano Gear"},
                {L"WEP_KEYBLADE09", L"Form_HappyGear", L"Happy Gear"},
                {L"WEP_KEYBLADE10", L"Form_ClassicTone", L"Classic Tone"},
                {L"WEP_KEYBLADE11", L"Form_GrandChef", L"Grand Chef"},
                {L"WEP_KEYBLADE12", L"Form_Oathkeeper", L"Oathkeeper"},
                {L"WEP_KEYBLADE13", L"Form_Oblivion", L"Oblivion"},
                {L"WEP_KEYBLADE14", L"Form_UltimaWeapon", L"Ultima Weapon"},
                {L"WEP_KEYBLADE17", L"Form_Starlight", L"Starlight"},
                {L"WEP_KEYBLADE18", L"Form_PandorasPower", L"Pandora's Power"},
                {L"", anti_form_custom_target, L"Anti Form"},
        };
        return targets;
    }

    auto form_custom_target(std::wstring_view keyblade) -> std::wstring_view
    {
        for (const FormCustomTarget& target : form_custom_targets())
        {
            if (!keyblade.empty() && target.keyblade == keyblade)
            {
                return target.target;
            }
        }
        return {};
    }

    auto form_theme_start_seconds(std::wstring_view keyblade) -> float
    {
        for (const KeybladeTrack& entry : keyblade_tracks)
        {
            if (entry.keyblade == keyblade)
            {
                return entry.start;
            }
        }
        return 0.0f;
    }

    auto is_form_weapon(std::wstring_view weapon_class) -> bool
    {
        // "w_so" then three digits, the last one naming the form.
        return weapon_class.size() >= 7 && weapon_class.starts_with(L"w_so") &&
            std::iswdigit(weapon_class[4]) && std::iswdigit(weapon_class[5]) &&
            std::iswdigit(weapon_class[6]) && weapon_class[6] != L'0';
    }

    auto FormThemeTracker::keeps(Form& form, const Observation& observation) -> bool
    {
        const auto state = observation.state;
        const bool form_weapon = is_form_weapon(observation.weapon);
        form.weapon_swapped = form.weapon_swapped || form_weapon;
        if (state && is_style_finish(*state))
        {
            form.in_finisher = true;
        }
        else if (form.in_finisher && state)
        {
            end_reason_ = L"finisher";
            return false;
        }
        if (state == char_state::attack)
        {
            end_reason_ = L"plain_attack";
            return false;
        }
        if (form.weapon_swapped && !form_weapon && !observation.weapon.empty() && state != char_state::style_change)
        {
            if (!form.base_weapon_since)
            {
                form.base_weapon_since = observation.now;
            }
            else if (observation.now - *form.base_weapon_since >= grace_for(state))
            {
                end_reason_ = L"base_weapon";
                return false;
            }
        }
        else
        {
            form.base_weapon_since.reset();
        }
        return true;
    }

    auto FormThemeTracker::observe(const Observation& observation) -> std::optional<std::wstring>
    {
        const bool was_playing = playing_;
        playing_ = false;
        if (!observation.keyblade)
        {
            return std::nullopt;
        }
        const std::wstring& keyblade = *observation.keyblade;
        if (held_ != keyblade)
        {
            // The put-away keyblade's form holds (its time only runs while
            // held), so its theme comes back with it; its timing restarts on
            // return. A form that keeps its weapon (Oathkeeper, Oblivion,
            // Kingdom Key) is seen ending at Sora's next plain attack, as
            // without a switch.
            if (held_)
            {
                if (const auto found = forms_.find(*held_); found != forms_.end())
                {
                    found->second.base_weapon_since.reset();
                    found->second.in_finisher = false;
                }
            }
            // Its theme comes back with its keyblade, for a rest's length.
            if (const auto found = forms_.find(keyblade); found != forms_.end())
            {
                found->second.active_at = observation.now;
            }
            if (was_playing)
            {
                end_reason_ = L"keyblade_switched";
            }
            held_ = keyblade;
        }

        if (observation.state == char_state::combo_anti)
        {
            if (!anti_form_)
            {
                // The STYLECHANGE that started this was Anti Form, not a
                // keyblade form.
                forms_.erase(keyblade);
                anti_form_ = true;
            }
            anti_combo_at_ = observation.now;
        }
        if (anti_form_)
        {
            const bool over = observation.state == char_state::attack ||
                observation.state == char_state::style_change ||
                (anti_combo_at_ && observation.now - *anti_combo_at_ >= anti_form_grace);
            if (!over)
            {
                playing_ = true;
                return std::wstring{anti_form_theme_track};
            }
            anti_form_ = false;
            anti_combo_at_.reset();
            end_reason_ = L"anti_form_ended";
            if (observation.state != char_state::style_change)
            {
                return std::nullopt;
            }
        }

        auto found = forms_.find(keyblade);
        if (found == forms_.end() &&
            (observation.state == char_state::style_change || is_form_weapon(observation.weapon)))
        {
            if (auto track = form_theme_track(keyblade))
            {
                found = forms_.emplace(keyblade, Form{.track = std::move(*track), .active_at = observation.now}).first;
            }
        }
        if (found == forms_.end())
        {
            return std::nullopt;
        }
        if (!keeps(found->second, observation))
        {
            forms_.erase(found);
            return std::nullopt;
        }
        Form& form = found->second;
        if (!at_rest(observation.state))
        {
            form.active_at = observation.now;
        }
        if (!form.weapon_swapped && observation.now - form.active_at >= kept_weapon_rest)
        {
            if (was_playing)
            {
                end_reason_ = L"resting";
            }
            return std::nullopt;
        }
        playing_ = true;
        return found->second.track;
    }

    auto FormThemeTracker::update(const Observation& observation) -> std::optional<std::wstring>
    {
        deadline_.reset();
        auto result = observe(observation);
        const auto schedule = [&](Clock::time_point due) {
            if (due > observation.now && (!deadline_ || due < *deadline_)) deadline_ = due;
        };
        if (anti_form_ && anti_combo_at_ && observation.state != char_state::combo_anti)
            schedule(*anti_combo_at_ + anti_form_grace);
        if (held_)
        {
            if (const auto found = forms_.find(*held_); found != forms_.end())
            {
                const Form& form = found->second;
                if (form.base_weapon_since) schedule(*form.base_weapon_since + grace_for(observation.state));
                if (!form.weapon_swapped && at_rest(observation.state)) schedule(form.active_at + kept_weapon_rest);
            }
        }
        return result;
    }

    auto FormThemeTracker::reset() -> void
    {
        deadline_.reset();
        anti_form_ = false;
        anti_combo_at_.reset();
        forms_.clear();
        held_.reset();
        playing_ = false;
    }
}
