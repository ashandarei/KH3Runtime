#include "kh3ap/features/ui/options_menu/host.h"
#include <algorithm>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace RC;
using namespace RC::Unreal;

namespace kh3ap
{
    using namespace options_menu_detail;
    namespace
    {
        auto report_callback_failure(CallbackKind kind, std::wstring_view label, std::string_view reason) -> void
        {
            constexpr std::wstring_view operations[]{L"availability", L"choice_availability", L"help",
                L"choice_help", L"selected_choice", L"value_text", L"apply_choice"};
            const std::wstring details(reason.begin(), reason.end());
            Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] Options callback failed operation={} option={} reason={}\n"),
                operations[static_cast<size_t>(kind)], label, details);
        }

        auto scaleform_identity_token(std::wstring_view id, std::wstring fallback) -> std::wstring
        {
            std::wstring token{};
            bool capitalize_next = true;
            for (const wchar_t character : id)
            {
                const bool ascii_lower = character >= L'a' && character <= L'z';
                const bool ascii_upper = character >= L'A' && character <= L'Z';
                const bool ascii_digit = character >= L'0' && character <= L'9';
                if (!ascii_lower && !ascii_upper && !ascii_digit)
                {
                    capitalize_next = true;
                    continue;
                }
                token.push_back(capitalize_next && ascii_lower
                                    ? static_cast<wchar_t>(character - L'a' + L'A')
                                    : character);
                capitalize_next = false;
            }
            return token.empty() ? std::move(fallback) : token;
        }

        auto measure_menu_capacity(const std::vector<RuntimeOptionsItemDefinition>& items,
                                   size_t& page_capacity, size_t& choice_capacity) -> void
        {
            page_capacity = std::max(page_capacity, items.size());
            for (const RuntimeOptionsItemDefinition& item : items)
            {
                choice_capacity = std::max(choice_capacity, item.choices.size());
                measure_menu_capacity(item.children, page_capacity, choice_capacity);
            }
        }
    } // namespace
    RuntimeOptionsMenuService::Impl::Impl(RuntimeOptionsMenuConfig config)
        : config_(std::move(config)), callback_state_(report_callback_failure)
    {
        for (const RuntimeOptionsMenuDefinition& menu : config_.menus)
        {
            measure_menu_capacity(menu.items, page_capacity_, choice_capacity_);
        }

        // The host owns one compact, stable page/choice pool. Parent anchors are
        // allocated around those ranges, so adding another top-level menu cannot
        // renumber existing page or choice rows.
        page_anchor_base_ = first_parent_anchor_index + 1;
        choice_anchor_base_ = std::max(preferred_first_choice_anchor_index,
                                       page_anchor_base_ + static_cast<int32_t>(page_capacity_));

        const auto anchor_reserved_for_rows = [this](int32_t anchor)
        {
            const bool page_anchor =
                anchor >= page_anchor_base_ &&
                anchor < page_anchor_base_ + static_cast<int32_t>(page_capacity_);
            const bool choice_anchor =
                anchor >= choice_anchor_base_ &&
                anchor < choice_anchor_base_ + static_cast<int32_t>(choice_capacity_);
            return page_anchor || choice_anchor;
        };
        parent_anchor_indices_.assign(config_.menus.size(), -1);
        int32_t generated_parent_anchor = first_parent_anchor_index;
        for (int32_t& anchor : parent_anchor_indices_)
        {
            while (anchor_reserved_for_rows(generated_parent_anchor) ||
                   std::find(parent_anchor_indices_.begin(), parent_anchor_indices_.end(),
                             generated_parent_anchor) != parent_anchor_indices_.end())
            {
                ++generated_parent_anchor;
            }
            anchor = generated_parent_anchor++;
        }

        const std::wstring host_token =
            config_.menus.empty()
                ? std::wstring{STR("Options")}
                : scaleform_identity_token(config_.menus.front().id, std::wstring{STR("Options")});
        page_scroll_host_member_ = std::format(STR("__kh3ap{}PageScrollHost"), host_token);
        page_scroll_host_instance_ = std::format(STR("kh3ap{}PageScrollHost"), host_token);
        parent_buttons_.resize(config_.menus.size());
        for (size_t i = 0; i < config_.menus.size(); ++i)
        {
            const std::wstring menu_token =
                scaleform_identity_token(config_.menus[i].id, std::format(STR("Menu{}"), i));
            parent_members_.push_back(std::format(STR("__kh3ap{}Options"), menu_token));
            parent_instances_.push_back(std::format(STR("kh3ap{}Options"), menu_token));
        }
        for (size_t i = 0; i < page_capacity_; ++i)
        {
            const std::wstring row_token =
                !config_.menus.empty() && i < config_.menus.front().items.size()
                    ? scaleform_identity_token(config_.menus.front().items[i].id,
                                               std::format(STR("Row{}"), i))
                    : std::format(STR("Row{}"), i);
            page_members_.push_back(std::format(STR("__kh3ap{}{}"), host_token, row_token));
            page_instances_.push_back(std::format(STR("kh3ap{}{}"), host_token, row_token));
        }
        for (size_t i = 0; i < choice_capacity_; ++i)
        {
            const std::wstring choice_token = i == 0   ? std::wstring{STR("On")}
                                              : i == 1 ? std::wstring{STR("Off")}
                                                       : std::format(STR("Choice{}"), i);
            choice_members_.push_back(std::format(STR("__kh3ap{}{}"), host_token, choice_token));
            choice_instances_.push_back(std::format(STR("kh3ap{}{}"), host_token, choice_token));
        }
        if (!config_.menus.empty())
        {
            reset_navigation(0);
        }
    }

    auto RuntimeOptionsMenuService::Impl::parent_anchor_index(size_t index) const -> int32_t
    {
        return index < parent_anchor_indices_.size() ? parent_anchor_indices_[index] : -1;
    }

    auto RuntimeOptionsMenuService::Impl::page_anchor_index(size_t index) const -> int32_t
    {
        return page_anchor_base_ + static_cast<int32_t>(index);
    }

    auto RuntimeOptionsMenuService::Impl::choice_anchor_index(size_t index) const -> int32_t
    {
        return choice_anchor_base_ + static_cast<int32_t>(index);
    }

    auto RuntimeOptionsMenuService::Impl::active_menu() const -> const RuntimeOptionsMenuDefinition*
    {
        return active_menu_index_ < config_.menus.size() ? &config_.menus[active_menu_index_]
                                                         : nullptr;
    }

    auto RuntimeOptionsMenuService::Impl::active_page_frame() -> RuntimeOptionsPageFrame*
    {
        return page_stack_.empty() ? nullptr : &page_stack_.back();
    }

    auto RuntimeOptionsMenuService::Impl::active_page_frame() const
        -> const RuntimeOptionsPageFrame*
    {
        return page_stack_.empty() ? nullptr : &page_stack_.back();
    }

    auto RuntimeOptionsMenuService::Impl::selected_item() const
        -> const RuntimeOptionsItemDefinition*
    {
        const RuntimeOptionsPageFrame* const frame = active_page_frame();
        return frame && frame->items && frame->selected_index < frame->items->size()
                   ? &(*frame->items)[frame->selected_index]
                   : nullptr;
    }

    auto RuntimeOptionsMenuService::Impl::reset_navigation(size_t menu_index) -> void
    {
        if (menu_index >= config_.menus.size())
        {
            return;
        }
        active_menu_index_ = menu_index;
        page_stack_.clear();
        page_stack_.push_back(RuntimeOptionsPageFrame{
            .items = &config_.menus[menu_index].items,
            .selected_index = 0,
            .scroll_position = 0,
        });
        selected_choice_index_ = 0;
    }

    auto RuntimeOptionsMenuService::Impl::item_available(const RuntimeOptionsItemDefinition& item) const
        -> bool
    {
        if (item.available && !callback_state_.read<bool>(&item, item.label, CallbackKind::ItemAvailability,
                item.available).value_or(false))
        {
            return false;
        }
        return item.kind != RuntimeOptionsItemKind::choice || item.choices.empty() ||
               current_choice_index(item).has_value();
    }

    auto RuntimeOptionsMenuService::Impl::choice_available(const RuntimeOptionsChoice& choice) const
        -> bool
    {
        return !choice.available || callback_state_.read<bool>(&choice, choice.label,
            CallbackKind::ChoiceAvailability, choice.available).value_or(false);
    }

    auto RuntimeOptionsMenuService::Impl::item_help_text(const RuntimeOptionsItemDefinition& item,
                                                        bool choosing) const -> std::wstring
    {
        const bool choice_help = choosing && item.choice_help_text;
        const RuntimeOptionsHelpText& provider = choice_help ? item.choice_help_text : item.help_text;
        if (!provider)
        {
            return {};
        }
        return callback_state_.read<std::wstring>(&item, item.label,
            choice_help ? CallbackKind::ChoiceHelp : CallbackKind::Help, provider)
                .value_or(STR("Help is temporarily unavailable."));
    }

    auto RuntimeOptionsMenuService::Impl::current_choice_index(const RuntimeOptionsItemDefinition& item) const
        -> std::optional<size_t>
    {
        return callback_state_.selection(item);
    }

    auto RuntimeOptionsMenuService::Impl::item_value_text(const RuntimeOptionsItemDefinition& item) const
        -> std::wstring
    {
        if (!item_available(item))
        {
            return STR("UNAVAILABLE");
        }
        if (item.kind == RuntimeOptionsItemKind::submenu)
        {
            return STR(">");
        }
        if (item.kind == RuntimeOptionsItemKind::information)
        {
            if (!item.value_text)
            {
                return item.value;
            }
            return callback_state_.read<std::wstring>(&item, item.label, CallbackKind::Value, item.value_text)
                .value_or(STR("UNAVAILABLE"));
        }
        if (item.choices.empty())
        {
            return {};
        }
        const auto selected = current_choice_index(item);
        return selected ? item.choices[*selected].label : STR("UNAVAILABLE");
    }

    auto RuntimeOptionsMenuService::Impl::selected_item_available() const -> bool
    {
        const RuntimeOptionsItemDefinition* const item = selected_item();
        return item && item_available(*item);
    }

    auto RuntimeOptionsMenuService::Impl::apply_selected_choice() -> void
    {
        const RuntimeOptionsItemDefinition* const item = selected_item();
        if (!item || item->kind != RuntimeOptionsItemKind::choice ||
            selected_choice_index_ >= item->choices.size() || !item_available(*item) ||
            !choice_available(item->choices[selected_choice_index_]) || !item->apply_choice)
        {
            return;
        }
        (void)callback_state_.run(item, item->label, CallbackKind::Apply,
            [&] { item->apply_choice(selected_choice_index_); });
    }
} // namespace kh3ap
