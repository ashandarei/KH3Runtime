#include "kh3ap/features/ui/options_menu/host.h"
#include "kh3ap/core/input/menu_face_button.h"
#include "kh3ap/core/gfx/gfx_reflection_helpers.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/NameTypes.hpp>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <string>
#include <string_view>

using namespace RC;
using namespace RC::Unreal;

namespace kh3ap
{
    using namespace options_menu_detail;
    auto RuntimeOptionsMenuService::Impl::is_confirm_key(std::wstring_view key_name) -> bool
    {
        return key_name == STR("Enter") || key_name == STR("Virtual_Accept") ||
               key_name == STR("LeftMouseButton");
    }

    auto RuntimeOptionsMenuService::Impl::is_back_key(std::wstring_view key_name) -> bool
    {
        return key_name == STR("SpaceBar") || key_name == STR("Escape") ||
               key_name == STR("BackSpace") || key_name == STR("Virtual_Back");
    }

    auto RuntimeOptionsMenuService::Impl::is_previous_navigation_key(std::wstring_view key_name)
        -> bool
    {
        return key_name == STR("Up") || key_name == STR("UpArrow") ||
               key_name == STR("Virtual_Up") || key_name == STR("Gamepad_DPad_Up") ||
               key_name == STR("Gamepad_LeftStick_Up") || key_name == STR("MouseScrollUp");
    }

    auto RuntimeOptionsMenuService::Impl::is_next_navigation_key(std::wstring_view key_name) -> bool
    {
        return key_name == STR("Down") || key_name == STR("DownArrow") ||
               key_name == STR("Virtual_Down") || key_name == STR("Gamepad_DPad_Down") ||
               key_name == STR("Gamepad_LeftStick_Down") || key_name == STR("MouseScrollDown");
    }

    auto RuntimeOptionsMenuService::Impl::gfx_bool_member_is_true(
        UObject* object, std::wstring_view member_name) const -> bool
    {
        if (!object || !kh3ap::is_uobject_valid_fast(object))
        {
            return false;
        }
        const std::wstring value = try_gfx_get_member_value(config_.gfx_paths, object, member_name);
        return value.find(STR("ASType=2")) != std::wstring::npos &&
               value.find(STR("ASBoolean=true")) != std::wstring::npos;
    }

    auto RuntimeOptionsMenuService::Impl::recover_capture_from_selected_row(UObject* root) -> bool
    {
        if (!root || !kh3ap::is_uobject_valid_fast(root))
        {
            return false;
        }

        // Native OnAnchorIn can emit a secondary stock callback after the
        // private anchor callback. The visible Scaleform Button remains the
        // authoritative selection, so recover ownership from that state at
        // the input boundary instead of trusting the last native callback.
        int32_t recovered_index = -1;
        for (size_t i = 0; i < choice_capacity_; ++i)
        {
            UObject* const candidate = choice_row(root, i);
            if (gfx_bool_member_is_true(candidate, STR("visible")) &&
                gfx_bool_member_is_true(candidate, STR("selected")))
            {
                recovered_index = choice_anchor_index(i);
                break;
            }
        }
        for (size_t i = 0; recovered_index < 0 && i < page_capacity_; ++i)
        {
            UObject* const candidate = page_row(root, i);
            if (gfx_bool_member_is_true(candidate, STR("visible")) &&
                gfx_bool_member_is_true(candidate, STR("selected")))
            {
                recovered_index = page_anchor_index(i);
                break;
            }
        }
        for (size_t i = 0; recovered_index < 0 && i < config_.menus.size(); ++i)
        {
            UObject* const parent = live_parent_button(root, i);
            if (gfx_bool_member_is_true(parent, STR("visible")) &&
                gfx_bool_member_is_true(parent, STR("selected")))
            {
                recovered_index = parent_anchor_index(i);
            }
        }
        if (!is_custom_anchor_index(recovered_index))
        {
            return false;
        }

        capture_active_ = true;
        active_custom_anchor_index_ = recovered_index;
        render_custom_anchor(root, recovered_index);
        return true;
    }

    auto RuntimeOptionsMenuService::Impl::trace_input_focus(std::wstring_view event, int32_t anchor)
        -> void
    {
        if (input_diagnostic_count_ >= max_input_diagnostic_events)
        {
            return;
        }
        ++input_diagnostic_count_;
        Output::send<LogLevel::Warning>(
            STR("[KH3APProbeCpp] OptionsInput t={} event={} mode={} anchor={} "
                "capture={} direction={}\n"),
            GetTickCount64(), event, static_cast<int32_t>(screen_mode_), anchor, capture_active_,
            navigation_direction_latched_);
    }

    auto RuntimeOptionsMenuService::Impl::route_viewport_key(const FName& key_name,
                                                             int32_t controller_id,
                                                             int32_t event_type, bool gamepad)
        -> bool
    {
        const std::wstring key = key_name.ToString();
        if (!is_menu_face_button(key) && !is_confirm_key(key) && !is_back_key(key) &&
            !is_previous_navigation_key(key) && !is_next_navigation_key(key))
        {
            return false;
        }
        const auto mode_before = screen_mode_;
        const int32_t anchor_before = active_custom_anchor_index_;
        const bool menu_active = injection_ready_ || injection_pending_;
        // Clear menu-local latches even if focus returned to the parent/native
        // page, where handle_viewport_key would reject navigation before cleanup.
        if (event_type == input_event_released)
        {
            if (consumed_key_name_ == key)
            {
                consumed_key_name_.clear();
            }
            if (is_previous_navigation_key(key) || is_next_navigation_key(key))
            {
                navigation_direction_latched_ = 0;
            }
        }
        bool handler_called = false;
        // Do not clear this ownership table on focus/anchor changes. Several keys
        // (and physical/virtual aliases) can remain held across those callbacks.
        const bool consumed =
            input_ownership_.route(controller_id, key, event_type,
                                   [&]()
                                   {
                                       handler_called = true;
                                       return handle_viewport_key(key_name, event_type);
                                   });
        if ((menu_active || consumed) && input_diagnostic_count_ < max_input_diagnostic_events)
        {
            ++input_diagnostic_count_;
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] OptionsInput t={} controller={} gamepad={} "
                    "key={} event={} consumed={} dispatched={} mode={}->{} "
                    "anchor={}->{} direction={}\n"),
                GetTickCount64(), controller_id, gamepad, key, event_type, consumed, handler_called,
                static_cast<int32_t>(mode_before), static_cast<int32_t>(screen_mode_),
                anchor_before, active_custom_anchor_index_, navigation_direction_latched_);
        }
        return consumed;
    }

    auto RuntimeOptionsMenuService::Impl::handle_viewport_key(const FName& key_name,
                                                              int32_t event_type) -> bool
    {
        if (!injection_ready_)
        {
            return false;
        }

        const std::wstring key = key_name.ToString();
        const auto face_button = runtime_menu_face_button(key);
        const bool confirm = is_confirm_key(key) || face_button == RuntimeMenuFaceButton::confirm;
        const bool back = is_back_key(key) || face_button == RuntimeMenuFaceButton::back;
        const bool previous_row = is_previous_navigation_key(key);
        const bool next_row = is_next_navigation_key(key);
        const int32_t navigation_direction = previous_row ? -1 : next_row ? 1 : 0;
        const RuntimeOptionsPageFrame* const navigation_frame =
            screen_mode_ == RuntimeOptionsScreenMode::page ? active_page_frame() : nullptr;
        const bool page_navigation = navigation_frame && navigation_frame->items &&
                                     !navigation_frame->items->empty() &&
                                     (previous_row || next_row);
        const RuntimeOptionsItemDefinition* const navigation_item =
            screen_mode_ == RuntimeOptionsScreenMode::choice ? selected_item() : nullptr;
        const bool choice_navigation =
            navigation_item && !navigation_item->choices.empty() && (previous_row || next_row);
        if (!confirm && !back && !page_navigation && !choice_navigation)
        {
            return false;
        }

        UObject* const root = live_menu_root();
        UObject* const context = menu_context_.Get();
        if (!root || !context || !kh3ap::is_uobject_valid_fast(root) ||
            !is_setting_menu_context(context))
        {
            capture_active_ = false;
            active_custom_anchor_index_ = -1;
            screen_mode_ = RuntimeOptionsScreenMode::inactive;
            consumed_key_name_.clear();
            return false;
        }
        if ((!capture_active_ || !is_custom_anchor_index(active_custom_anchor_index_)) &&
            !recover_capture_from_selected_row(root))
        {
            return false;
        }

        // Scaleform's Button activates on release. Once a custom press is
        // claimed, retain ownership through repeats and the matching
        // release even if that press moved from a nested page back to the
        // parent. Letting only the release through would execute whatever
        // harmless stock page index backs the custom visual row (Quit in
        // this movie), which is the exact coupling this route avoids.
        if (event_type != input_event_pressed)
        {
            const bool belongs_to_claimed_press = consumed_key_name_ == key;
            const bool custom_page_owns_key =
                confirm || (back && screen_mode_ != RuntimeOptionsScreenMode::parent) ||
                page_navigation || choice_navigation;
            const bool consume_event = belongs_to_claimed_press || custom_page_owns_key;
            if (event_type == input_event_released && belongs_to_claimed_press)
            {
                consumed_key_name_.clear();
            }
            if (event_type == input_event_released && navigation_direction != 0)
            {
                navigation_direction_latched_ = 0;
            }
            return consume_event;
        }

        // A single controller direction can arrive under more than one FKey name
        // (for example Gamepad_LeftStick_Down plus Virtual_Down). Latch the logical
        // direction rather than the raw key so aliases and repeated Pressed events
        // produce exactly one move until a direction release arrives.
        if (navigation_direction != 0 && navigation_direction_latched_ == navigation_direction)
        {
            return true;
        }
        if (consumed_key_name_ == key)
        {
            return true;
        }
        if (navigation_direction != 0)
        {
            navigation_direction_latched_ = navigation_direction;
        }

        const auto claim_press = [&]() -> bool
        {
            consumed_key_name_ = key;
            return true;
        };

        if (page_navigation)
        {
            RuntimeOptionsPageFrame* const frame = active_page_frame();
            if (frame && frame->items && !frame->items->empty())
            {
                const size_t item_count = frame->items->size();
                const size_t previous_index = frame->selected_index;
                frame->selected_index = previous_row
                                            ? (frame->selected_index + item_count - 1) % item_count
                                            : (frame->selected_index + 1) % item_count;
                paint_page_rows(root);
                (void)focus_custom_anchor(root, page_anchor_index(frame->selected_index));
                if (frame->selected_index != previous_index)
                {
                    (void)ui_sound_service_.play(context, RuntimeUiSound::cursor);
                }
            }
            return claim_press();
        }

        if (choice_navigation)
        {
            if (navigation_item->kind == RuntimeOptionsItemKind::choice &&
                !navigation_item->choices.empty())
            {
                const size_t previous_index = selected_choice_index_;
                selected_choice_index_ =
                    previous_row ? (selected_choice_index_ + navigation_item->choices.size() - 1) %
                                       navigation_item->choices.size()
                                 : (selected_choice_index_ + 1) % navigation_item->choices.size();
                paint_choice_rows(root);
                (void)focus_custom_anchor(root, choice_anchor_index(selected_choice_index_));
                if (selected_choice_index_ != previous_index)
                {
                    (void)ui_sound_service_.play(context, RuntimeUiSound::cursor);
                }
            }
            return claim_press();
        }

        if (back)
        {
            if (screen_mode_ == RuntimeOptionsScreenMode::choice)
            {
                (void)ui_sound_service_.play(context, RuntimeUiSound::cancel);
                show_page(root);
                if (const RuntimeOptionsPageFrame* const frame = active_page_frame())
                {
                    (void)focus_custom_anchor(root, page_anchor_index(frame->selected_index));
                }
                return claim_press();
            }
            if (screen_mode_ == RuntimeOptionsScreenMode::page)
            {
                (void)ui_sound_service_.play(context, RuntimeUiSound::cancel);
                if (page_stack_.size() > 1)
                {
                    page_stack_.pop_back();
                    show_page(root);
                    if (const RuntimeOptionsPageFrame* const frame = active_page_frame())
                    {
                        (void)focus_custom_anchor(root, page_anchor_index(frame->selected_index));
                    }
                }
                else
                {
                    show_parent(root);
                    (void)focus_custom_anchor(root, parent_anchor_index(active_menu_index_));
                }
                return claim_press();
            }

            // On the parent row, Back retains the game's native Config
            // close behavior. It cannot mutate Gamepad settings because
            // no custom action is dispatched and no Gamepad object is
            // part of this route.
            return false;
        }

        if (screen_mode_ == RuntimeOptionsScreenMode::parent)
        {
            trigger_selected_button_decide(root);
            (void)ui_sound_service_.play(context, RuntimeUiSound::decide);
            reset_navigation(active_menu_index_);
            show_page(root);
            if (const RuntimeOptionsPageFrame* const frame = active_page_frame();
                frame && frame->items && !frame->items->empty())
            {
                (void)focus_custom_anchor(root, page_anchor_index(frame->selected_index));
            }
            return claim_press();
        }
        if (screen_mode_ == RuntimeOptionsScreenMode::page)
        {
            const RuntimeOptionsItemDefinition* const item = selected_item();
            if (item && selected_item_available())
            {
                const bool has_action = item->kind == RuntimeOptionsItemKind::submenu
                                            ? !item->children.empty()
                                            : !item->choices.empty();
                if (!has_action)
                {
                    return claim_press();
                }
                trigger_selected_button_decide(root);
                (void)ui_sound_service_.play(context, RuntimeUiSound::decide);
                if (item->kind == RuntimeOptionsItemKind::submenu)
                {
                    page_stack_.push_back(RuntimeOptionsPageFrame{
                        .items = &item->children,
                        .selected_index = 0,
                        .scroll_position = 0,
                    });
                    show_page(root);
                    (void)focus_custom_anchor(root, page_anchor_index(0));
                }
                else
                {
                    show_choice(root);
                    if (screen_mode_ == RuntimeOptionsScreenMode::choice)
                    {
                        (void)focus_custom_anchor(root, choice_anchor_index(selected_choice_index_));
                    }
                    else if (const auto* frame = active_page_frame())
                    {
                        (void)focus_custom_anchor(root, page_anchor_index(frame->selected_index));
                    }
                }
            }
            return claim_press();
        }
        if (screen_mode_ == RuntimeOptionsScreenMode::choice)
        {
            const RuntimeOptionsItemDefinition* const item = selected_item();
            const bool selected_choice_available =
                item && item->kind == RuntimeOptionsItemKind::choice &&
                selected_choice_index_ < item->choices.size() && item_available(*item) &&
                choice_available(item->choices[selected_choice_index_]);
            if (selected_choice_available)
            {
                trigger_selected_button_decide(root);
                (void)ui_sound_service_.play(context, RuntimeUiSound::decide);
                apply_selected_choice();
            }
            else
            {
                (void)ui_sound_service_.play(context, RuntimeUiSound::cancel);
                paint_choice_rows(root);
                (void)focus_custom_anchor(root, choice_anchor_index(selected_choice_index_));
                return claim_press();
            }
            show_page(root);
            if (const RuntimeOptionsPageFrame* const frame = active_page_frame())
            {
                (void)focus_custom_anchor(root, page_anchor_index(frame->selected_index));
            }
            return claim_press();
        }
        return false;
    }
} // namespace kh3ap
