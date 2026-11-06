#include "kh3ap/features/ui/options_menu/host.h"
#include "kh3ap/core/gfx/gfx_reflection_helpers.h"
#include <algorithm>
#include <string>
#include <string_view>

using namespace RC;
using namespace RC::Unreal;

namespace kh3ap
{
    using namespace options_menu_detail;
    auto RuntimeOptionsMenuService::Impl::refresh_parent_labels(UObject* root) -> void
    {
        for (size_t i = 0; i < config_.menus.size(); ++i)
        {
            UObject* const parent = live_parent_button(root, i);
            if (parent)
            {
                parent_buttons_[i] = parent;
                (void)set_button_label(parent, config_.menus[i].label);
            }
        }
    }

    auto RuntimeOptionsMenuService::Impl::refresh_help_text(UObject* root, std::wstring_view text)
        -> void
    {
        UObject* const message =
            member_path(root, {STR("helpText"), STR("body"), STR("txMessage")});
        if (message)
        {
            (void)set_adjusted_text(message, text);
        }
    }

    auto RuntimeOptionsMenuService::Impl::hide_detached_items(UObject* root) -> void
    {
        set_button_visible(page_scroll_host(root), false);
        for (size_t i = 0; i < page_capacity_; ++i)
        {
            UObject* const item = page_row(root, i);
            set_button_selected(item, false);
            set_button_visible(item, false);
        }
        for (size_t i = 0; i < choice_capacity_; ++i)
        {
            UObject* const item = choice_row(root, i);
            set_button_selected(item, false);
            set_button_visible(item, false);
        }
    }

    auto RuntimeOptionsMenuService::Impl::set_active_parent_selected(UObject* root) -> void
    {
        for (size_t i = 0; i < config_.menus.size(); ++i)
        {
            set_button_selected(live_parent_button(root, i), i == active_menu_index_);
        }
    }

    auto RuntimeOptionsMenuService::Impl::update_page_scrollbar(UObject* root, size_t item_count,
                                                                size_t visible_count,
                                                                size_t scroll_position) -> void
    {
        UObject* const scroll_host = page_scroll_host(root);
        UObject* const scrollbar = member_path(scroll_host, {STR("scrollbar")});
        const bool scrollable =
            scroll_host && scrollbar && visible_count > 0 && item_count > visible_count;
        set_button_visible(scroll_host, scrollable);
        if (!scrollbar)
        {
            return;
        }
        if (!scrollable)
        {
            (void)try_gfx_invoke_no_args(config_.gfx_paths, scrollbar, STR("hide"));
            set_button_visible(scrollbar, false);
            return;
        }

        apply_display_transform(scroll_host, page_scroll_host_transform_);
        set_button_visible(scroll_host, true);
        set_button_visible(scrollbar, true);
        (void)try_gfx_set_member_value_bool(config_.gfx_paths, scrollbar, STR("enabled"), true);
        (void)try_gfx_invoke_no_args(config_.gfx_paths, scrollbar, STR("show"));
        (void)try_gfx_invoke_int_int_number_args(
            config_.gfx_paths, scrollbar, STR("setScrollProperty"), 0,
            static_cast<int32_t>(item_count - visible_count),
            static_cast<double>(visible_count) / static_cast<double>(item_count));
        (void)try_gfx_invoke_int_arg(config_.gfx_paths, scrollbar, STR("setScrollPosition"),
                                     static_cast<int32_t>(scroll_position));
    }

    auto RuntimeOptionsMenuService::Impl::paint_page_rows(UObject* root) -> void
    {
        RuntimeOptionsPageFrame* const frame = active_page_frame();
        const size_t item_count = frame && frame->items ? frame->items->size() : 0;
        const size_t visible_count = std::min(item_count, max_visible_page_rows);
        if (frame && item_count > 0)
        {
            frame->selected_index = std::min(frame->selected_index, item_count - 1);
            const size_t max_scroll_position = item_count - visible_count;
            frame->scroll_position = std::min(frame->scroll_position, max_scroll_position);
            if (frame->selected_index < frame->scroll_position)
            {
                frame->scroll_position = frame->selected_index;
            }
            else if (frame->selected_index >= frame->scroll_position + visible_count)
            {
                frame->scroll_position = frame->selected_index - visible_count + 1;
            }
        }
        const size_t window_start = frame ? frame->scroll_position : 0;
        const size_t window_end = window_start + visible_count;
        for (size_t i = 0; i < page_capacity_; ++i)
        {
            UObject* const row = page_row(root, i);
            if (i < window_start || i >= window_end)
            {
                set_button_selected(row, false);
                set_button_visible(row, false);
                continue;
            }
            apply_display_transform(row, extrapolated_transform(page_first_transform_,
                                                                page_second_transform_,
                                                                i - window_start));
            const RuntimeOptionsItemDefinition& item = (*frame->items)[i];
            const bool available = item_available(item);
            set_setting_item(row, item.label, item_value_text(item), available);
            repaint_button_selected(row, frame->selected_index == i);
            configure_anchor(row, page_anchor_index((i + item_count - 1) % item_count),
                             page_anchor_index((i + 1) % item_count));
        }
        if (frame && item_count > visible_count)
        {
            // Keyboard arrows can be consumed by Scaleform's native FocusManager
            // before FViewportClient::InputKey reaches the custom navigation hook.
            // Keep the logical rows immediately outside the painted window eligible
            // for keyboard focus. Their OnAnchorIn callback repaints the next window
            // before focus is shown. Alpha and mouse disabling make these sentinels
            // invisible and prevent them from intercepting clicks over painted rows.
            const auto expose_keyboard_focus_sentinel = [&](size_t index)
            {
                if (index >= window_start && index < window_end)
                {
                    return;
                }
                UObject* const row = page_row(root, index);
                set_button_selected(row, false);
                set_button_visible(row, true);
                (void)try_gfx_set_member_value_bool(config_.gfx_paths, row, STR("enabled"), true);
                (void)try_gfx_set_member_value_bool(config_.gfx_paths, row, STR("mouseEnabled"),
                                                    false);
                (void)try_gfx_set_member_value_number(config_.gfx_paths, row, STR("alpha"), 0.0);
            };
            expose_keyboard_focus_sentinel((window_start + item_count - 1) % item_count);
            expose_keyboard_focus_sentinel(window_end % item_count);
        }
        update_page_scrollbar(root, item_count, visible_count, window_start);

        if (const RuntimeOptionsItemDefinition* const item = selected_item())
        {
            std::wstring help = item_help_text(*item, false);
            if (help.empty() && !item_available(*item))
            {
                help = STR("This option is unavailable.");
            }
            refresh_help_text(root, help);
        }
    }

    auto RuntimeOptionsMenuService::Impl::paint_choice_rows(UObject* root) -> void
    {
        const RuntimeOptionsItemDefinition* const item = selected_item();
        const size_t choice_count =
            item && item->kind == RuntimeOptionsItemKind::choice ? item->choices.size() : 0;
        const size_t visible_count = std::min(choice_count, max_visible_choice_rows);
        const size_t centered_start = selected_choice_index_ > visible_count / 2
                                          ? selected_choice_index_ - visible_count / 2
                                          : 0;
        const size_t window_start = choice_count > visible_count
                                        ? std::min(centered_start, choice_count - visible_count)
                                        : 0;
        const size_t window_end = window_start + visible_count;
        const auto current_choice = item ? current_choice_index(*item) : std::nullopt;
        const bool available = item && item_available(*item) && current_choice.has_value();

        for (size_t i = 0; i < choice_capacity_; ++i)
        {
            UObject* const choice = choice_row(root, i);
            if (i < window_start || i >= window_end)
            {
                set_button_selected(choice, false);
                set_button_visible(choice, false);
                continue;
            }
            apply_display_transform(choice, extrapolated_transform(choice_first_transform_,
                                                                   choice_second_transform_,
                                                                   i - window_start));
            const bool choice_is_available = available && choice_available(item->choices[i]);
            set_option_item(choice, item->choices[i].label, current_choice && *current_choice == i,
                            choice_is_available);
            repaint_button_selected(choice, selected_choice_index_ == i);
            configure_anchor(choice, choice_anchor_index((i + choice_count - 1) % choice_count),
                             choice_anchor_index((i + 1) % choice_count));
        }

        if (item && !available)
        {
            refresh_help_text(root, STR("This option is unavailable."));
        }
        else if (item)
        {
            refresh_help_text(root, item_help_text(*item, true));
        }
    }

    auto RuntimeOptionsMenuService::Impl::show_parent(UObject* root) -> void
    {
        screen_mode_ = RuntimeOptionsScreenMode::parent;
        for (size_t i = 0; i < choice_capacity_; ++i)
        {
            set_button_visible(choice_row(root, i), false);
        }
        paint_page_rows(root);
        // The right-side rows are a category preview until Confirm enters them.
        // Keep focus and the cyan selection treatment on the parent itself.
        for (size_t i = 0; i < page_capacity_; ++i)
        {
            set_button_selected(page_row(root, i), false);
        }
        set_active_parent_selected(root);
        if (const RuntimeOptionsMenuDefinition* const menu = active_menu())
        {
            refresh_help_text(root, menu->help_text);
        }
    }

    auto RuntimeOptionsMenuService::Impl::show_page(UObject* root) -> void
    {
        screen_mode_ = RuntimeOptionsScreenMode::page;
        set_active_parent_selected(root);
        for (size_t i = 0; i < choice_capacity_; ++i)
        {
            set_button_visible(choice_row(root, i), false);
        }
        paint_page_rows(root);
    }

    auto RuntimeOptionsMenuService::Impl::show_choice(UObject* root) -> void
    {
        const RuntimeOptionsItemDefinition* const item = selected_item();
        if (!item || item->kind != RuntimeOptionsItemKind::choice || item->choices.empty())
        {
            show_page(root);
            return;
        }
        const auto selected = current_choice_index(*item);
        if (!selected)
        {
            show_page(root);
            return;
        }
        selected_choice_index_ = *selected;
        screen_mode_ = RuntimeOptionsScreenMode::choice;
        set_active_parent_selected(root);
        paint_page_rows(root);
        paint_choice_rows(root);
    }
} // namespace kh3ap
