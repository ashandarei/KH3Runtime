#include "kh3ap/features/ui/options_menu/host.h"
#include "kh3ap/core/gfx/gfx_reflection_helpers.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include <Unreal/UFunctionStructs.hpp>
#include <string_view>

using namespace RC;
using namespace RC::Unreal;

namespace kh3ap
{
    using namespace options_menu_detail;
    namespace options_menu_detail
    {
        auto read_gfx_param(UFunction* function, void* params,
                            std::initializer_list<std::wstring_view> names) -> UObject*
        {
            for (const std::wstring_view name : names)
            {
                UObject* const value = try_read_function_object_param(function, params, name);
                if (value && kh3ap::is_uobject_valid_fast(value))
                {
                    return value;
                }
            }
            return nullptr;
        }
    } // namespace options_menu_detail
    auto RuntimeOptionsMenuService::Impl::write_callback_index(UFunction* function, void* params,
                                                               int32_t value) -> bool
    {
        return write_integral_function_param(find_function_param_by_name(function, STR("Index")),
                                             params,
                                             static_cast<uint64_t>(static_cast<uint32_t>(value)));
    }

    auto
    RuntimeOptionsMenuService::Impl::handle_focus_in(UnrealScriptFunctionCallableContext& context)
        -> void
    {
        if (!is_setting_menu_context(context.Context))
        {
            return;
        }
        UFunction* const function = get_frame_node(context.TheStack);
        void* const params = get_frame_locals(context.TheStack);
        UObject* const root = read_gfx_param(function, params, {STR("UIComponent"), STR("Target")});
        if (!root)
        {
            return;
        }
        input_diagnostic_count_ = 0;
        trace_input_focus(STR("focus_in"), -1);
        menu_context_ = context.Context;
        menu_root_ = root;
        capture_active_ = false;
        screen_mode_ = RuntimeOptionsScreenMode::inactive;
        active_custom_anchor_index_ = -1;
        pending_custom_anchor_index_ = -1;
        consumed_key_name_.clear();
        if (!config_.menus.empty())
        {
            reset_navigation(0);
        }
        for (FWeakObjectPtr& parent : parent_buttons_)
        {
            parent = nullptr;
        }
        injection_logged_ = false;
        injection_ready_ = false;
        injection_pending_ = true;
        injection_delay_ticks_ = deferred_injection_tick_count;
        injection_attempts_ = 0;
        parent_label_refresh_ticks_ = 0;
        navigation_direction_latched_ = 0;
    }

    auto
    RuntimeOptionsMenuService::Impl::handle_focus_out(UnrealScriptFunctionCallableContext& context)
        -> void
    {
        if (!is_setting_menu_context(context.Context))
        {
            return;
        }
        UObject* const active_context = menu_context_.Get();
        if (active_context && active_context != context.Context)
        {
            return;
        }
        trace_input_focus(STR("focus_out"), active_custom_anchor_index_);
        // Do not invoke Scaleform while its native OnFocusOut traversal is active.
        // A later open will normalize visibility after its deferred injection.
        injection_ready_ = false;
        injection_pending_ = false;
        injection_delay_ticks_ = 0;
        injection_attempts_ = 0;
        parent_label_refresh_ticks_ = 0;
        navigation_direction_latched_ = 0;
        capture_active_ = false;
        screen_mode_ = RuntimeOptionsScreenMode::inactive;
        active_custom_anchor_index_ = -1;
        pending_custom_anchor_index_ = -1;
        consumed_key_name_.clear();
        menu_context_ = nullptr;
        menu_root_ = nullptr;
        for (FWeakObjectPtr& parent : parent_buttons_)
        {
            parent = nullptr;
        }
    }

    auto RuntimeOptionsMenuService::Impl::handle_anchor_in_pre(
        UnrealScriptFunctionCallableContext& context) -> void
    {
        if (!injection_ready_ || !is_setting_menu_context(context.Context) ||
            menu_context_.Get() != context.Context)
        {
            return;
        }
        UFunction* const function = get_frame_node(context.TheStack);
        void* const params = get_frame_locals(context.TheStack);
        UObject* const root = read_gfx_param(function, params, {STR("UIComponent"), STR("Target")});
        UObject* const anchor = read_gfx_param(
            function, params, {STR("AnchorComponent"), STR("Anchor"), STR("FocusObject")});
        const auto callback_index = read_function_int_param(function, params, STR("Index"));
        if (!root || !anchor || !callback_index.has_value())
        {
            return;
        }
        menu_context_ = context.Context;
        menu_root_ = root;
        const int32_t custom_index =
            resolve_custom_anchor_index(root, anchor, static_cast<int32_t>(*callback_index));
        trace_input_focus(STR("anchor_in"), custom_index);
        pending_custom_anchor_index_ = -1;
        if (!is_custom_anchor_index(custom_index))
        {
            capture_active_ = false;
            active_custom_anchor_index_ = -1;
            screen_mode_ = RuntimeOptionsScreenMode::inactive;
            consumed_key_name_.clear();
            hide_detached_items(root);
            for (size_t i = 0; i < config_.menus.size(); ++i)
            {
                set_button_selected(live_parent_button(root, i), false);
            }
            return;
        }

        // Preserve the actual private anchor in Scaleform, but give the
        // stock native handler its harmless blank Quit-page index. This
        // keeps native page state away from Gamepad while every confirm
        // and nested action is consumed by the viewport hook below.
        (void)write_callback_index(function, params, stock_quit_index);
        capture_active_ = true;
        active_custom_anchor_index_ = custom_index;
        pending_custom_anchor_index_ = custom_index;
    }

    auto RuntimeOptionsMenuService::Impl::handle_anchor_in_post(
        UnrealScriptFunctionCallableContext& context) -> void
    {
        if (!injection_ready_ || !is_setting_menu_context(context.Context) ||
            menu_context_.Get() != context.Context ||
            !is_custom_anchor_index(pending_custom_anchor_index_))
        {
            return;
        }
        const int32_t custom_index = pending_custom_anchor_index_;
        pending_custom_anchor_index_ = -1;
        UFunction* const function = get_frame_node(context.TheStack);
        void* const params = get_frame_locals(context.TheStack);
        UObject* root = read_gfx_param(function, params, {STR("UIComponent"), STR("Target")});
        if (!root)
        {
            root = live_menu_root();
        }
        render_custom_anchor(root, custom_index);
    }

    auto RuntimeOptionsMenuService::Impl::handle_anchor_out_pre(
        UnrealScriptFunctionCallableContext& context) -> void
    {
        if (!injection_ready_ || !is_setting_menu_context(context.Context) ||
            menu_context_.Get() != context.Context)
        {
            return;
        }
        UFunction* const function = get_frame_node(context.TheStack);
        void* const params = get_frame_locals(context.TheStack);
        UObject* const root = read_gfx_param(function, params, {STR("UIComponent"), STR("Target")});
        UObject* const anchor = read_gfx_param(
            function, params, {STR("AnchorComponent"), STR("Anchor"), STR("FocusObject")});
        const auto callback_index = read_function_int_param(function, params, STR("Index"));
        if (!root || !anchor || !callback_index.has_value())
        {
            return;
        }
        const int32_t custom_index =
            resolve_custom_anchor_index(root, anchor, static_cast<int32_t>(*callback_index));
        if (is_custom_anchor_index(custom_index))
        {
            (void)write_callback_index(function, params, stock_quit_index);
        }
    }
} // namespace kh3ap
