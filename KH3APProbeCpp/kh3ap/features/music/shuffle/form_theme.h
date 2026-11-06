#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kh3ap
{
    // Form themes: while Sora is in a keyblade's Formchange, the music
    // switches to that keyblade's theme, then back. Current detection and
    // ownership rules are documented in docs/MUSIC_DEVELOPMENT.md.

    // ETresStateID values used here, by their order in the enum.
    namespace char_state
    {
        inline constexpr uint8_t attack = 12;
        inline constexpr uint8_t style_change = 31;
        // Anti Form's combos: its only reliable sign (BP_IsNowAntiForm stays
        // false).
        inline constexpr uint8_t combo_anti = 86;
        inline constexpr uint8_t style_finish_first = 33;
        inline constexpr uint8_t style_finish_last = 39;
    }

    // The SEAD music a keyblade's forms play: its world's battle theme (100
    // Acre Wood, which has none, its field theme), or a track chosen by ear
    // for keyblades without a world. Nullopt for keyblades without a theme
    // yet.
    [[nodiscard]] auto form_theme_track(std::wstring_view keyblade) -> std::optional<std::wstring>;

    // Sora's Anti Form plays over any keyblade theme.
    inline constexpr std::wstring_view anti_form_theme_track =
            L"/Game/Sound/_DLC/BGM/BGM_battle_dw22_antiaqua.BGM_battle_dw22_antiaqua";

    // A custom music file named after a form target ("Form_UltimaWeapon",
    // in a file name or custom_music.json) plays while Sora is in that
    // keyblade's form, whether Form Themes is on or off.
    struct FormCustomTarget
    {
        std::wstring_view keyblade;
        std::wstring_view target;
        std::wstring_view name;
    };

    // Every keyblade with forms, then Anti Form (keyblade empty).
    [[nodiscard]] auto form_custom_targets() -> const std::vector<FormCustomTarget>&;

    // The target of a keyblade's forms, or empty for a keyblade without.
    [[nodiscard]] auto form_custom_target(std::wstring_view keyblade) -> std::wstring_view;

    inline constexpr std::wstring_view anti_form_custom_target = L"Form_AntiForm";

    // Seconds into the track a keyblade's theme starts from, to skip a long
    // intro; 0 for most.
    [[nodiscard]] auto form_theme_start_seconds(std::wstring_view keyblade) -> float;

    // A keyblade's form weapon: its base weapon is w_soNN0, its forms (and
    // the transition between them) w_soNN1 to w_soNN9.
    [[nodiscard]] auto is_form_weapon(std::wstring_view weapon_class) -> bool;

    // Follows Sora from state/weapon events and one-shot grace deadlines, keeping a form per
    // keyblade: KH3 keeps a keyblade's form while another keyblade is out,
    // and the form only runs down while its keyblade is held.
    //
    // A form starts on the STYLECHANGE state, or when Sora holds a form
    // weapon (a form carried over from another keyblade or a level load). It
    // ends, only while its keyblade is out, after a style finisher has played
    // out, on a plain ATTACK state (forms attack through their own combo
    // states), or once a form that swapped the weapon shows the base weapon
    // for a while (it briefly does during some team attacks). A form that
    // keeps its weapon shows nothing when it runs out: its theme rests after
    // a while with Sora only idling, running or jumping (it may be over),
    // comes back at his next other action, and the form ends at his next
    // plain attack.
    //
    // Anti Form starts with the same STYLECHANGE, so it first reads as the
    // held keyblade's form; its first COMBO_AN turns that into Anti Form. It
    // ends on a plain attack, a new STYLECHANGE, or once no COMBO_AN has come
    // for a while (idling or running reads the same in and out of it).
    class FormThemeTracker
    {
    public:
        using Clock = std::chrono::steady_clock;

        struct Observation
        {
            std::optional<uint8_t> state{};
            std::optional<std::wstring> keyblade{};
            // Class of the weapon Sora holds; empty when unknown.
            std::wstring weapon{};
            Clock::time_point now{};
        };

        // Returns the track to play: the held keyblade's theme while it is
        // in a form, nullopt otherwise.
        auto update(const Observation& observation) -> std::optional<std::wstring>;
        auto reset() -> void;
        [[nodiscard]] auto next_deadline() const -> std::optional<Clock::time_point> { return deadline_; }

        // A form plays its theme now.
        [[nodiscard]] auto active() const -> bool { return playing_; }
        // Sora is in Anti Form, whose theme plays over any keyblade's.
        [[nodiscard]] auto anti_form() const -> bool { return anti_form_; }
        [[nodiscard]] auto keyblade() const -> const std::optional<std::wstring>& { return held_; }
        // Why the theme last stopped, for the log: a form's end, or
        // "keyblade_switched" when its keyblade was put away.
        [[nodiscard]] auto end_reason() const -> std::wstring_view { return end_reason_; }

    private:
        auto observe(const Observation& observation) -> std::optional<std::wstring>;
        std::optional<Clock::time_point> deadline_{};

        struct Form
        {
            std::wstring track{};
            bool weapon_swapped{};
            bool in_finisher{};
            // Last time Sora did more than idle, run or jump in this form.
            Clock::time_point active_at{};
            std::optional<Clock::time_point> base_weapon_since{};
        };

        // Updates the held keyblade's form; false when it has just ended.
        auto keeps(Form& form, const Observation& observation) -> bool;

        std::map<std::wstring, Form> forms_{};
        bool anti_form_{};
        std::optional<Clock::time_point> anti_combo_at_{};
        std::optional<std::wstring> held_{};
        bool playing_{};
        std::wstring_view end_reason_{};
    };
}
