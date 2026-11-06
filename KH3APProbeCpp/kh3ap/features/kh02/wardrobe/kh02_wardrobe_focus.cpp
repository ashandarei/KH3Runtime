#include "kh02_wardrobe_focus.h"
#include "kh3ap/features/kh02/movies/kh02_menu_movie.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include <Unreal/UObject.hpp>

namespace kh3ap
{
    using namespace RC::Unreal;
    using namespace kh02menu;

    namespace
    {
        UObject* member(UObject* root, const std::wstring& relative)
        {
            return find_movie_member(root, (std::wstring(kh02_wardrobe_costume_path) + relative).c_str());
        }
    } // namespace

    auto Kh02WardrobeFocus::cursor(UObject* target, UObject* root, const std::wstring& target_path) -> bool
    {
        if (!root || !target)
        {
            return false;
        }
        auto* rect = invoke_movie(target, L"getAnchorRect");
        if (!rect)
        {
            return false;
        }
        const auto x = try_gfx_get_member_value_number(gfx, rect, L"x");
        const auto y = try_gfx_get_member_value_number(gfx, rect, L"y");
        const auto width = try_gfx_get_member_value_number(gfx, rect, L"width");
        const auto height = try_gfx_get_member_value_number(gfx, rect, L"height");
        if (!x || !y || !width || !height || *height <= 0)
        {
            return false;
        }
        const std::array position{*x, *y + *height / 2, *width};
        if (cursor_target == target_path && cursor_position == position)
        {
            return true;
        }
        for (int index = 0; index < 2; ++index)
        {
            auto* clip = find_movie_member(root, index == 0 ? L"anchorCursor" : L"lightCursor");
            if (!clip)
            {
                return false;
            }
            try_gfx_set_member_value_number(gfx, clip, L"x", *x + (index == 0 ? 0 : *width));
            try_gfx_set_member_value_number(gfx, clip, L"y", *y + *height / 2);
            try_gfx_set_member_value_bool(gfx, clip, L"visible", true);
        }
        cursor_target = target_path;
        cursor_position = position;
        return true;
    }

    auto Kh02WardrobeFocus::path(const Selection& selection) -> std::wstring
    {
        switch (selection.page)
        {
        case Kh02WardrobePage::Categories:
            return std::wstring(L"costumeCmdAnm.costumeCmd.") + kh02_wardrobe_category_buttons[selection.category];
        case Kh02WardrobePage::Items:
            return L"costumeItemWindow.costumeListAnm.costumeList";
        case Kh02WardrobePage::ColorType:
            return std::wstring(L"colorInfoAnm.colorInfo.") +
                   std::array{L"singleColorBtn", L"grdBtn", L"defaultBtn"}[selection.selected];
        case Kh02WardrobePage::Gradient:
            if (selection.selected < 2)
            {
                return std::wstring(L"grdColorSelectAnm.grdColorSelect.grdColorSelect.") +
                       (selection.selected == 0 ? L"choiceTopColorBtn" : L"choiceBottomColorBtn");
            }
            if (selection.selected == 2 + selection.gradient_history_count)
            {
                return L"grdColorSelectAnm.grdColorSelect.decisionColorBtn";
            }
            return L"grdColorSelectAnm.grdColorSelect.grdHistryPalet.histryGradation" +
                   std::to_wstring(selection.selected - 1);
        case Kh02WardrobePage::Editor:
            if (selection.selected < 3)
            {
                return std::wstring(L"makeColorBarAnm.makeColorBar.colorBar.sliderColor") +
                       std::array{L"R", L"G", L"B"}[selection.selected];
            }
            return std::wstring(L"makeColorBarAnm.makeColorBar.") +
                   (selection.selected == 3 ? L"choiceColorPaletBtn" : L"decisionColorBtn");
        case Kh02WardrobePage::Palette:
            if (selection.selected < 20)
            {
                return L"colorPaletSingleAnm.colorPaletSingle.systemColorPaletSingle.singleColor" +
                       std::to_wstring(selection.selected + 1);
            }
            return L"colorPaletSingleAnm.colorPaletSingle.histrySingle.singleColor" +
                   std::to_wstring(selection.selected - 19);
        }
        return {};
    }

    auto Kh02WardrobeFocus::focus(UObject* root, UObject* cursor_root, const Selection& selection) -> bool
    {
        auto* target = member(root, path(selection));
        if (!target)
        {
            return false;
        }
        if (const auto index = try_gfx_get_member_value_number(gfx, target, L"index"))
        {
            invoke_movie(root, L"focus", {static_cast<int>(*index)});
        }
        if (selection.page == Kh02WardrobePage::Items)
        {
            try_gfx_set_member_value_int(gfx, target, L"focusIndex", selection.selected);
            invoke_movie(target, L"adjustDisplay", {selection.selected});
        }
        for (int index = 0; index < 5; ++index)
        {
            auto* button =
                member(root, std::wstring(L"costumeCmdAnm.costumeCmd.") + kh02_wardrobe_category_buttons[index]);
            try_gfx_set_member_value_bool(gfx, button, L"selected", index == selection.category);
        }
        if (selection.page == Kh02WardrobePage::Gradient)
        {
            invoke_movie(root, L"focusGradationType", {selection.selected < 2 ? selection.selected : 2});
        }
        const wchar_t* description = L"Choose a wardrobe category.";
        if (selection.page == Kh02WardrobePage::Items)
        {
            description = L"Select an item to equip.";
        }
        if (selection.page == Kh02WardrobePage::ColorType)
        {
            description = std::array{L"Select a single color.", L"Select a top and bottom color.",
                                     L"Restore the original colors."}[selection.selected];
        }
        if (selection.page == Kh02WardrobePage::Gradient)
        {
            description = selection.selected < 2 ? (selection.selected == 0 ? L"Set top color." : L"Set bottom color.")
                          : selection.selected == selection.count - 1 ? L"Save these colors."
                                                                      : L"Use a saved gradation.";
        }
        if (selection.page == Kh02WardrobePage::Editor)
        {
            description = selection.selected < 3    ? L"Adjust the values to change the color."
                          : selection.selected == 3 ? L"Select from the color palette."
                                                    : L"Save this color.";
        }
        if (selection.page == Kh02WardrobePage::Palette)
        {
            description = L"Select from the color palette.";
        }
        if (auto* help = find_movie_member(root, L"campMenu.frame.messageAnm.message.textField"))
        {
            try_gfx_set_member_value_string(gfx, help, L"text", description);
        }
        if (auto* guide = find_movie_member(root, L"campMenu.frame.btnInfoAnm.btnInfo.btnInfoText.textField"))
        {
            const auto hint = selection.gamepad ? (selection.page == Kh02WardrobePage::Editor
                                                       ? L"D-pad: Adjust    A: Select    B: Back"
                                                       : L"Right stick: Camera controls    A: Select    B: Back")
                                                : (selection.page == Kh02WardrobePage::Editor
                                                       ? L"Left/Right: Adjust    Enter: Select    Esc: Back"
                                                       : L"A/D: Camera controls    Enter: Select    Esc: Back");
            try_gfx_set_member_value_string(gfx, guide, L"text", hint);
        }
        return cursor(target, cursor_root, path(selection));
    }

    void Kh02WardrobeFocus::transition(UObject* root, UObject* cursor_root, const Selection& selection)
    {
        cursor_target.clear();
        focus_immediate = focus(root, cursor_root, selection);
        focus_pending = !focus_immediate;
    }

    void Kh02WardrobeFocus::tick(UObject* root, UObject* cursor_root, const Selection& selection)
    {
        if (focus_pending)
        {
            focus_pending = !focus(root, cursor_root, selection);
        }
        else
        {
            // Follow opening/closing animations without repeating focus writes.
            const auto target_path = path(selection);
            cursor(member(root, target_path), cursor_root, target_path);
        }
    }
} // namespace kh3ap
