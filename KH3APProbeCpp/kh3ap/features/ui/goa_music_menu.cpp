#include "kh3ap/features/ui/goa_options_menu.h"
#include "kh3ap/core/settings/goa_settings_service.h"

#include <UnrealDef.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <utility>
#include <vector>

namespace kh3ap
{
    auto make_music_runtime_options_menu(RuntimeGoaSettingsService& goa_settings)
        -> RuntimeOptionsMenuDefinition
    {
        RuntimeOptionsMenuBuilder menu{
            STR("music_rando"), STR("Music Rando"),
            STR("Configure music shuffle, custom music and form themes.")};
        auto page = menu.page();
        const auto music_help = []()
        {
            return std::wstring{STR("Shuffle Worlds gives each world the field and battle music "
                                    "of another world. Chaos mixes everything.")};
        };
        page.choice(
            STR("music"), STR("Music Shuffle"),
            {
                {STR("0"), STR("VANILLA")},
                {STR("1"), STR("SHUFFLE WORLDS")},
                {STR("2"), STR("SHUFFLE BATTLE THEMES")},
                {STR("3"), STR("CHAOS")},
            },
            // The row index is the mode, as for Palettesanity.
            [&goa_settings]() -> std::size_t
            {
                return static_cast<std::size_t>(goa_settings.music_shuffle_mode());
            },
            [&goa_settings](std::size_t index)
            {
                (void)goa_settings.set_music_shuffle_mode(static_cast<int32_t>(index));
            },
            []()
            {
                return true;
            },
            music_help, music_help);

        const auto custom_music_help = []()
        {
            return std::wstring{
                STR("Adds WAV and OGG files from your music directory when Music Shuffle "
                    "is set to Chaos. Choose the folder on the KH3 client's Config tab.")};
        };
        page.toggle(
            STR("include_custom_music"), STR("Include Custom Music"),
            [&goa_settings]()
            {
                return goa_settings.include_custom_music();
            },
            [&goa_settings](bool enabled)
            {
                (void)goa_settings.set_include_custom_music(enabled);
            },
            {}, custom_music_help, custom_music_help);

        const auto form_themes_help = []()
        {
            return std::wstring{
                STR("While Sora is in a keyblade's Formchange, the music switches to "
                    "that keyblade's theme, and Anti Form has its own. The music "
                    "resumes when the form ends.")};
        };
        page.toggle(
            STR("form_themes"), STR("Form Themes"),
            [&goa_settings]()
            {
                return goa_settings.form_themes_enabled();
            },
            [&goa_settings](bool enabled)
            {
                (void)goa_settings.set_form_themes_enabled(enabled);
            },
            {}, form_themes_help, form_themes_help);

        return std::move(menu).build();
    }
} // namespace kh3ap
