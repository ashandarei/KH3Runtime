#include "kh3ap/features/ui/options_menu/host.h"
#include "kh3ap/core/gfx/gfx_reflection_helpers.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <algorithm>
#include <array>
#include <iterator>
#include <format>
#include <optional>
#include <vector>

using namespace RC;
using namespace RC::Unreal;

namespace kh3ap
{
    using namespace options_menu_detail;
    auto RuntimeOptionsMenuService::Impl::parent_button_index(UObject* root, UObject* object)
        -> std::optional<size_t>
    {
        if (!object || !kh3ap::is_uobject_valid_fast(object))
        {
            return std::nullopt;
        }
        for (size_t i = 0; i < config_.menus.size(); ++i)
        {
            if (live_parent_button(root, i) == object)
            {
                return i;
            }
        }
        return std::nullopt;
    }

    auto RuntimeOptionsMenuService::Impl::parent_menu_index_for_anchor(int32_t index) const
        -> std::optional<size_t>
    {
        const auto found =
            std::find(parent_anchor_indices_.begin(), parent_anchor_indices_.end(), index);
        if (found == parent_anchor_indices_.end())
        {
            return std::nullopt;
        }
        return static_cast<size_t>(std::distance(parent_anchor_indices_.begin(), found));
    }

    auto RuntimeOptionsMenuService::Impl::is_parent_anchor_index(int32_t index) const -> bool
    {
        return parent_menu_index_for_anchor(index).has_value();
    }

    auto RuntimeOptionsMenuService::Impl::is_page_anchor_index(int32_t index) const -> bool
    {
        return index >= page_anchor_base_ &&
               static_cast<size_t>(index - page_anchor_base_) < page_capacity_;
    }

    auto RuntimeOptionsMenuService::Impl::is_choice_anchor_index(int32_t index) const -> bool
    {
        return index >= choice_anchor_base_ &&
               static_cast<size_t>(index - choice_anchor_base_) < choice_capacity_;
    }

    auto RuntimeOptionsMenuService::Impl::is_custom_anchor_index(int32_t index) const -> bool
    {
        return is_parent_anchor_index(index) || is_page_anchor_index(index) ||
               is_choice_anchor_index(index);
    }

    auto RuntimeOptionsMenuService::Impl::resolve_custom_anchor_index(UObject* root,
                                                                      UObject* anchor,
                                                                      int32_t callback_index)
        -> int32_t
    {
        if (is_custom_anchor_index(callback_index))
        {
            return callback_index;
        }
        const int32_t gfx_index = rounded_gfx_index(config_.gfx_paths, anchor).value_or(-1);
        if (is_custom_anchor_index(gfx_index))
        {
            return gfx_index;
        }
        if (const auto parent = parent_button_index(root, anchor))
        {
            return parent_anchor_index(*parent);
        }
        for (size_t i = 0; i < page_capacity_; ++i)
        {
            if (anchor == page_row(root, i))
            {
                return page_anchor_index(i);
            }
        }
        for (size_t i = 0; i < choice_capacity_; ++i)
        {
            if (anchor == choice_row(root, i))
            {
                return choice_anchor_index(i);
            }
        }
        return -1;
    }

    auto RuntimeOptionsMenuService::Impl::render_custom_anchor(UObject* root, int32_t anchor_index)
        -> void
    {
        if (!root || !is_custom_anchor_index(anchor_index))
        {
            return;
        }
        if (const auto parent_index = parent_menu_index_for_anchor(anchor_index))
        {
            reset_navigation(*parent_index);
            show_parent(root);
            return;
        }
        if (is_page_anchor_index(anchor_index))
        {
            RuntimeOptionsPageFrame* const frame = active_page_frame();
            const size_t selected = static_cast<size_t>(anchor_index - page_anchor_base_);
            if (frame && frame->items && selected < frame->items->size())
            {
                frame->selected_index = selected;
                show_page(root);
            }
            return;
        }
        const RuntimeOptionsItemDefinition* const item = selected_item();
        const size_t selected = static_cast<size_t>(anchor_index - choice_anchor_base_);
        if (!item || item->kind != RuntimeOptionsItemKind::choice ||
            selected >= item->choices.size())
        {
            return;
        }
        selected_choice_index_ = selected;
        screen_mode_ = RuntimeOptionsScreenMode::choice;
        set_active_parent_selected(root);
        paint_page_rows(root);
        paint_choice_rows(root);
    }

    auto RuntimeOptionsMenuService::Impl::focus_custom_anchor(UObject* root, int32_t anchor_index)
        -> bool
    {
        return root && is_custom_anchor_index(anchor_index) &&
               try_gfx_invoke_int_arg(config_.gfx_paths, root, STR("focusAnchor"), anchor_index);
    }

    auto RuntimeOptionsMenuService::Impl::selected_custom_button(UObject* root) -> UObject*
    {
        if (!root)
        {
            return nullptr;
        }
        if (screen_mode_ == RuntimeOptionsScreenMode::parent)
        {
            return active_parent_button(root);
        }
        if (screen_mode_ == RuntimeOptionsScreenMode::page)
        {
            const RuntimeOptionsPageFrame* const frame = active_page_frame();
            return frame ? page_row(root, frame->selected_index) : nullptr;
        }
        if (screen_mode_ == RuntimeOptionsScreenMode::choice)
        {
            return choice_row(root, selected_choice_index_);
        }
        return nullptr;
    }

    auto RuntimeOptionsMenuService::Impl::trigger_selected_button_decide(UObject* root) -> void
    {
        // The viewport hook consumes custom confirms before the stock Config
        // controller can run this timeline. This restores the selected Button's
        // authored confirm animation; KH3 dispatches its sound separately.
        (void)try_gfx_invoke_no_args(config_.gfx_paths, selected_custom_button(root),
                                     STR("decide"));
    }

    auto RuntimeOptionsMenuService::Impl::ensure_injected(UObject* root) -> bool
    {
        if (!root || !kh3ap::is_uobject_valid_fast(root) || config_.menus.empty())
        {
            return false;
        }
        UObject* const body = member_path(root, {STR("menuMain"), STR("body")});
        if (!body)
        {
            return false;
        }

        std::vector<UObject*> parents(config_.menus.size());
        bool parent_clone_required = false;
        for (size_t i = 0; i < parents.size(); ++i)
        {
            parents[i] = display_child_named(body, parent_instances_[i]);
            if (!parents[i])
            {
                parents[i] =
                    try_gfx_get_member_value_object(config_.gfx_paths, body, parent_members_[i]);
            }
            parent_clone_required = parent_clone_required || !parents[i];
        }

        if (parent_clone_required)
        {
            std::array<UObject*, stock_row_count> stock_rows{};
            for (size_t i = 0; i < stock_rows.size(); ++i)
            {
                stock_rows[i] = try_gfx_get_member_value_object(config_.gfx_paths, body,
                                                                std::format(STR("coBtn{}"), i + 1));
                if (!stock_rows[i])
                {
                    return false;
                }
            }
            UObject* const quit_row = stock_rows.back();
            for (size_t i = 0; i < parents.size(); ++i)
            {
                if (!parents[i])
                {
                    parents[i] = clone_parent_button(body, quit_row, i);
                }
                if (!parents[i])
                {
                    Output::send<LogLevel::Error>(
                        STR("[KH3APProbeCpp] Runtime options injection failed "
                            "stage=parent_clone menu={}\n"),
                        i);
                    return false;
                }
            }

            const double y_first =
                try_gfx_get_member_value_number(config_.gfx_paths, stock_rows.front(), STR("y"))
                    .value_or(0.0);
            const double y_last =
                try_gfx_get_member_value_number(config_.gfx_paths, quit_row, STR("y"))
                    .value_or(y_first + fallback_stock_row_span);
            const double stock_spacing =
                std::max(1.0, (y_last - y_first) / static_cast<double>(stock_row_count - 1));
            std::vector<UObject*> visible_rows{};
            visible_rows.reserve(stock_rows.size() + parents.size());
            visible_rows.insert(visible_rows.end(), stock_rows.begin(), stock_rows.end());
            visible_rows.insert(visible_rows.end(), parents.begin(), parents.end());
            // Preserve stock row height and spacing when appending custom menus.
            for (size_t i = 0; i < visible_rows.size(); ++i)
            {
                (void)try_gfx_set_member_value_number(config_.gfx_paths, visible_rows[i],
                                                      STR("scaleY"), 1.0);
                (void)try_gfx_set_member_value_number(config_.gfx_paths, visible_rows[i], STR("y"),
                                                      y_first +
                                                          stock_spacing * static_cast<double>(i));
            }
            configure_anchor(stock_rows.front(), parent_anchor_index(parents.size() - 1),
                             stock_first_index + 1);
            configure_anchor(quit_row, stock_quit_index - 1, parent_anchor_index(0));
            for (size_t i = 0; i < parents.size(); ++i)
            {
                configure_anchor(parents[i], i == 0 ? stock_quit_index : parent_anchor_index(i - 1),
                                 i + 1 < parents.size() ? parent_anchor_index(i + 1)
                                                        : stock_first_index);
            }
        }

        for (size_t i = 0; i < parents.size(); ++i)
        {
            parent_buttons_[i] = parents[i];
        }
        if (!ensure_detached_items(root))
        {
            Output::send<LogLevel::Error>(STR("[KH3APProbeCpp] Runtime options injection failed "
                                              "stage=custom_rows\n"));
            return false;
        }

        // Finish every first-frame display mutation before publishing the menu as
        // ready. These operations used to be repeated after the success log,
        // which made the log an ambiguous crash boundary and needlessly doubled
        // the Scaleform calls made on the first Settings open.
        hide_detached_items(root);
        for (UObject* const parent : parents)
        {
            normalize_parent_button(parent);
            set_button_selected(parent, false);
        }
        refresh_parent_labels(root);

        if (!injection_logged_)
        {
            injection_logged_ = true;
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] Runtime options injected "
                    "movie=/Game/UI/Scaleform/85_settingmenu/"
                    "SettingMenu menus={} page_capacity={} page_window={} "
                    "choice_capacity={} "
                    "private_anchors={} "
                    "page_scroll=stock_colist "
                    "native_page_reuse=false "
                    "native_option_callbacks=false "
                    "asset_patch=false\n"),
                config_.menus.size(), page_capacity_, max_visible_page_rows, choice_capacity_,
                config_.menus.size() + page_capacity_ + choice_capacity_);
        }
        return true;
    }
} // namespace kh3ap
