#include "kh3ap/features/ui/goa_options_menu.h"

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
    auto make_goa_credits_menu() -> RuntimeOptionsMenuDefinition
    {
        RuntimeOptionsMenuBuilder menu{
            STR("credits"), STR("AP Credits"),
            STR("The hearts behind the randomizer. Thank you for helping "
                "make Garden of Assemblage possible!")};
        auto page = menu.page();

        page.information(STR("lan144"), STR("Lan144"), STR("CODE"),
                         []()
                         {
                             return std::wstring{STR("Initial battle level computer options and "
                                                     "initial cutscene skips.")};
                         })
            .information(STR("lanahi"), STR("Lanahi"), STR("CODE"),
                         []()
                         {
                             return std::wstring{STR("The Stylesanity option.")};
                         })
            .information(STR("grizouille"), STR("Grizouille"), STR("CODE"),
                         []()
                         {
                             return std::wstring{STR("The Palettesanity setting.")};
                         })
            .information(STR("gusxandi"), STR("Gusxandi"), STR("LOGIC"),
                         []()
                         {
                             return std::wstring{STR("Most of the initial fight and chest logic.")};
                         })
            .information(STR("starlightkirito"), STR("StarlightKirito"), STR("LOGIC"),
                         []()
                         {
                             return std::wstring{STR("Setting up the Ingredientsanity logic.")};
                         })
            .information(STR("cee"), STR("Cee"), STR("ART"),
                         []()
                         {
                             return std::wstring{STR("The Archipelago icon and other shop icon "
                                                     "assets.")};
                         })
            .information(STR("raphael2512"), STR("Raphael2512"), STR("ART"),
                         []()
                         {
                             return std::wstring{STR("The inactive portal Keyblade images.")};
                         });

        return std::move(menu).build();
    }
} // namespace kh3ap
