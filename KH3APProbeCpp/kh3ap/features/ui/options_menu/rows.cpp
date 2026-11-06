#include "kh3ap/features/ui/options_menu/host.h"
#include "kh3ap/core/gfx/gfx_reflection_helpers.h"
#include <string_view>

using namespace RC;
using namespace RC::Unreal;

namespace kh3ap
{
    namespace
    {
        constexpr int32_t maximum_option_label_children = 16;
    }

    using namespace options_menu_detail;
    auto RuntimeOptionsMenuService::Impl::set_button_selected(UObject* button, bool selected)
        -> void
    {
        if (button)
        {
            (void)try_gfx_set_member_value_bool(config_.gfx_paths, button, STR("selected"),
                                                selected);
        }
    }

    auto RuntimeOptionsMenuService::Impl::repaint_button_selected(UObject* button, bool selected)
        -> void
    {
        if (!button)
        {
            return;
        }
        // Scaleform's Button setter can retain its previous disabled timeline when
        // a detached row is reused. Force one real selected-state transition after
        // `enabled` has been updated so the authored normal/selected frame is
        // evaluated against the row's current availability.
        set_button_selected(button, !selected);
        set_button_selected(button, selected);
    }

    auto RuntimeOptionsMenuService::Impl::set_button_visible(UObject* button, bool visible) -> void
    {
        if (button)
        {
            (void)try_gfx_set_member_value_bool(config_.gfx_paths, button, STR("visible"), visible);
        }
    }

    auto RuntimeOptionsMenuService::Impl::prepare_detached_item(UObject* item) -> void
    {
        if (!item)
        {
            return;
        }

        // Dynamically constructed library symbols are not advanced by their
        // original List container. Pin them to the first (normal) frame and make
        // their display alpha explicit before Button::selected chooses the final
        // normal/select timeline. FocusManager can still position cursors for a
        // transparent symbol, so neither a valid anchor nor a visible cursor is
        // evidence that the row itself was painted.
        (void)try_gfx_set_member_value_number(config_.gfx_paths, item, STR("alpha"), 1.0);
        (void)try_gfx_invoke_int_arg(config_.gfx_paths, item, STR("gotoAndStop"), 1);
    }

    auto RuntimeOptionsMenuService::Impl::normalize_parent_button(UObject* button) -> void
    {
        if (!button)
        {
            return;
        }
        // clone() starts from the authored symbol defaults rather than the live
        // state of the Quit row. Make it a normal enabled category button so its
        // selected property drives the same hover/focus timeline as stock rows.
        prepare_detached_item(button);
        set_button_visible(button, true);
        (void)try_gfx_set_member_value_bool(config_.gfx_paths, button, STR("enabled"), true);
        (void)try_gfx_set_member_value_number(config_.gfx_paths, button, STR("alpha"), 1.0);
    }

    auto RuntimeOptionsMenuService::Impl::set_button_label(UObject* button, std::wstring_view label)
        -> bool
    {
        if (!button || label.empty())
        {
            return false;
        }
        const bool cached =
            try_gfx_set_member_value_string(config_.gfx_paths, button, STR("label"), label);
        UObject* const text_field =
            try_gfx_get_member_value_object(config_.gfx_paths, button, STR("textField"));
        const bool painted_member =
            text_field &&
            try_gfx_set_member_value_string(config_.gfx_paths, text_field, STR("text"), label);
        UObject* const timeline_text_field =
            try_gfx_child_path(config_.gfx_paths, button, {5, 0, 0});
        const bool painted_timeline =
            timeline_text_field && try_gfx_set_member_value_string(
                                       config_.gfx_paths, timeline_text_field, STR("text"), label);
        return cached && (painted_member || painted_timeline);
    }

    auto RuntimeOptionsMenuService::Impl::set_adjusted_text(UObject* object, std::wstring_view text)
        -> bool
    {
        if (!object)
        {
            return false;
        }
        const bool adjusted =
            try_gfx_set_member_value_string(config_.gfx_paths, object, STR("text"), text);
        UObject* const text_field = member_path(object, {STR("textField")});
        const bool painted = text_field && try_gfx_set_member_value_string(
                                               config_.gfx_paths, text_field, STR("text"), text);
        const bool replaced =
            text_field && try_gfx_invoke_replace_text(config_.gfx_paths, text_field, text);
        return adjusted || painted || replaced;
    }

    auto RuntimeOptionsMenuService::Impl::set_adjusted_text_color(UObject* object, int32_t color)
        -> bool
    {
        UObject* const text_field = member_path(object, {STR("textField")});
        return text_field &&
               try_gfx_set_member_value_int(config_.gfx_paths, text_field, STR("textColor"), color);
    }

    auto RuntimeOptionsMenuService::Impl::show_adjustable_text_only(UObject* timeline,
                                                                    UObject* adjustable_text)
        -> void
    {
        if (!timeline || !adjustable_text)
        {
            return;
        }

        // McLabelOptName_68 contains nine authored bitmap/localized variants plus
        // one editable txText child. A cloned settings row can retain one of those
        // variants (for example "Deutsch") even after the timeline is returned to
        // its default frame, producing composites such as "OFFtsch". Hide every
        // timeline child, then explicitly reveal only txText.
        for (int32_t index = 0; index < maximum_option_label_children; ++index)
        {
            UObject* const child = try_gfx_invoke_get_child_at(config_.gfx_paths, timeline, index);
            if (!child)
            {
                break;
            }
            set_button_visible(child, false);
        }
        set_button_visible(adjustable_text, true);
    }

    auto RuntimeOptionsMenuService::Impl::set_setting_item(UObject* item, std::wstring_view name,
                                                           std::wstring_view value, bool enabled)
        -> void
    {
        if (!item)
        {
            return;
        }
        // These rows are reused for different settings. Reset the detached symbol
        // and its child timelines first. CoListItmSetting inherits KH3's stock
        // tres.controls.Button, whose enabled setter calls updateState() and picks
        // normal/select/disable/disableSelect. Availability must therefore be the
        // final paint operation; a later gotoAndStop(1) would overwrite the stock
        // disabled state with the normal (white) frame.
        prepare_detached_item(item);
        set_button_visible(item, true);
        (void)try_gfx_set_member_value_bool(config_.gfx_paths, item, STR("mouseEnabled"), true);
        (void)try_gfx_set_member_value_int(config_.gfx_paths, item, STR("categoryLevel"), 0);
        (void)try_gfx_set_member_value_string(config_.gfx_paths, item, STR("label"), name);

        // CoListItmSetting's authored instance name is labelText1. labelName was
        // used by an earlier prototype but does not exist on symbol187.
        UObject* const label_name = member_path(item, {STR("labelText1")});
        UObject* const name_text = member_path(item, {STR("labelText1"), STR("txText")});
        UObject* const label_option = member_path(item, {STR("labelOption")});
        UObject* const label_type = member_path(item, {STR("labelOption"), STR("labelType")});
        UObject* const value_text =
            member_path(item, {STR("labelOption"), STR("labelType"), STR("labelText")});
        UObject* const value_adjusted_text = member_path(
            item, {STR("labelOption"), STR("labelType"), STR("labelText"), STR("txText")});
        for (UObject* visible :
             {label_name, name_text, label_option, label_type, value_text, value_adjusted_text})
        {
            if (visible)
            {
                (void)try_gfx_set_member_value_bool(config_.gfx_paths, visible, STR("visible"),
                                                    true);
                (void)try_gfx_set_member_value_number(config_.gfx_paths, visible, STR("alpha"),
                                                      1.0);
            }
        }
        (void)try_gfx_invoke_int_arg(config_.gfx_paths, label_name, STR("gotoAndStop"), 1);
        (void)try_gfx_invoke_int_arg(config_.gfx_paths, label_option, STR("gotoAndStop"), 1);
        (void)try_gfx_invoke_int_arg(config_.gfx_paths, label_type, STR("gotoAndStop"), 1);
        (void)try_gfx_invoke_int_arg(config_.gfx_paths, value_text, STR("gotoAndStop"), 1);
        (void)set_adjusted_text(name_text, name);
        // McLabelOptName_68 paints through its txText TextFieldAdjust child.
        // Assigning a dynamic `text` member on the outer MovieClip leaves the
        // authored Japanese placeholder glyphs untouched.
        show_adjustable_text_only(value_text, value_adjusted_text);
        (void)set_adjusted_text(value_adjusted_text, value);

        UObject* const live_name_text = try_gfx_child_path(config_.gfx_paths, item, {7, 0, 0});
        if (live_name_text)
        {
            (void)try_gfx_set_member_value_string(config_.gfx_paths, live_name_text, STR("text"),
                                                  name);
            (void)try_gfx_invoke_replace_text(config_.gfx_paths, live_name_text, name);
        }
        UObject* const slider =
            member_path(item, {STR("labelOption"), STR("labelType"), STR("slider")});
        if (slider)
        {
            (void)try_gfx_set_member_value_bool(config_.gfx_paths, slider, STR("visible"), false);
        }
        (void)try_gfx_set_member_value_bool(config_.gfx_paths, item, STR("enabled"), enabled);
        // txText is forced visible so injected strings can replace the authored
        // localized glyph clips. That text field is not reliably recolored by the
        // parent timeline when a detached row is reused; paint its final color
        // directly, just as the authored disabled transform would render it.
        (void)set_adjusted_text_color(value_adjusted_text,
                                      enabled ? enabled_text_color : disabled_setting_text_color);
        (void)set_adjusted_text_color(name_text,
                                      enabled ? enabled_text_color : disabled_setting_text_color);
        if (live_name_text)
        {
            (void)try_gfx_set_member_value_int(config_.gfx_paths, live_name_text, STR("textColor"),
                                               enabled ? enabled_text_color
                                                       : disabled_setting_text_color);
        }
    }

    auto RuntimeOptionsMenuService::Impl::set_option_item(UObject* item, std::wstring_view label,
                                                          bool checked, bool enabled) -> void
    {
        if (!item)
        {
            return;
        }
        // Choice rows use the same stock Button state machine. Finish resetting
        // their reused labels/check mark before assigning enabled so Friend 1 and
        // Friend 2 cannot retain one another's grey/white timeline state.
        prepare_detached_item(item);
        set_button_visible(item, true);
        (void)try_gfx_set_member_value_int(config_.gfx_paths, item, STR("categoryLevel"), 0);
        (void)try_gfx_set_member_value_string(config_.gfx_paths, item, STR("label"), label);

        UObject* const label_name = member_path(item, {STR("labelText")});
        UObject* const label_text = member_path(item, {STR("labelText"), STR("txText")});
        if (label_name)
        {
            (void)try_gfx_invoke_int_arg(config_.gfx_paths, label_name, STR("gotoAndStop"), 1);
            (void)try_gfx_set_member_value_bool(config_.gfx_paths, label_name, STR("visible"),
                                                true);
            (void)try_gfx_set_member_value_number(config_.gfx_paths, label_name, STR("alpha"), 1.0);
        }
        if (label_text)
        {
            (void)try_gfx_set_member_value_bool(config_.gfx_paths, label_text, STR("visible"),
                                                true);
            (void)try_gfx_set_member_value_number(config_.gfx_paths, label_text, STR("alpha"), 1.0);
        }
        show_adjustable_text_only(label_name, label_text);
        (void)set_adjusted_text(label_text, label);
        UObject* const live_label_field = try_gfx_child_path(config_.gfx_paths, item, {7, 0, 0});
        if (live_label_field)
        {
            (void)try_gfx_set_member_value_string(config_.gfx_paths, live_label_field, STR("text"),
                                                  label);
            (void)try_gfx_invoke_replace_text(config_.gfx_paths, live_label_field, label);
        }
        (void)try_gfx_invoke_int_arg(config_.gfx_paths, item, STR("setNewMark"), checked ? 1 : 0);
        UObject* const check_mark = member_path(item, {STR("checkMark")});
        if (check_mark)
        {
            (void)try_gfx_invoke_int_arg(config_.gfx_paths, check_mark, STR("gotoAndStop"),
                                         checked ? 5 : 1);
        }
        (void)try_gfx_set_member_value_bool(config_.gfx_paths, item, STR("enabled"), enabled);
        (void)set_adjusted_text_color(label_text,
                                      enabled ? enabled_text_color : disabled_option_text_color);
    }

    auto RuntimeOptionsMenuService::Impl::configure_anchor(UObject* object, int32_t up,
                                                           int32_t down) -> void
    {
        (void)try_gfx_set_member_value_int(config_.gfx_paths, object, STR("toUp"), up);
        (void)try_gfx_set_member_value_int(config_.gfx_paths, object, STR("toDown"), down);
        (void)try_gfx_set_member_value_int(config_.gfx_paths, object, STR("toLeft"), -1);
        (void)try_gfx_set_member_value_int(config_.gfx_paths, object, STR("toRight"), -1);
    }
} // namespace kh3ap
