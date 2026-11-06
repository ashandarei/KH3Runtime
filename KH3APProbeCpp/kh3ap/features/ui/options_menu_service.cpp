#include "kh3ap/features/ui/options_menu/host.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <memory>
#include <string>
#include <utility>

using namespace RC;
using namespace RC::Unreal;

namespace kh3ap
{
    using namespace options_menu_detail;
    auto RuntimeOptionsMenuService::Impl::install_once() -> void
    {
        if (installed_ || !config_.enabled || config_.menus.empty())
        {
            return;
        }
        installed_ = true;
        focus_in_hook_ids_ = UObjectGlobals::RegisterHook(
            std::wstring{focus_in_path},
            [](UnrealScriptFunctionCallableContext&, void*)
            {
            },
            [this](UnrealScriptFunctionCallableContext& context, void*)
            {
                handle_focus_in(context);
            },
            nullptr);
        focus_out_hook_ids_ = UObjectGlobals::RegisterHook(
            std::wstring{focus_out_path},
            [](UnrealScriptFunctionCallableContext&, void*)
            {
            },
            [this](UnrealScriptFunctionCallableContext& context, void*)
            {
                handle_focus_out(context);
            },
            nullptr);
        anchor_in_hook_ids_ = UObjectGlobals::RegisterHook(
            std::wstring{anchor_in_path},
            [this](UnrealScriptFunctionCallableContext& context, void*)
            {
                handle_anchor_in_pre(context);
            },
            [this](UnrealScriptFunctionCallableContext& context, void*)
            {
                handle_anchor_in_post(context);
            },
            nullptr);
        anchor_out_hook_ids_ = UObjectGlobals::RegisterHook(
            std::wstring{anchor_out_path},
            [this](UnrealScriptFunctionCallableContext& context, void*)
            {
                handle_anchor_out_pre(context);
            },
            [](UnrealScriptFunctionCallableContext&, void*)
            {
            },
            nullptr);

        Output::send<LogLevel::Warning>(
            STR("[KH3APProbeCpp] Runtime options private-anchor GFx hooks "
                "installed focus_in=({}, {}) focus_out=({}, {}) "
                "anchor_in=({}, {}) anchor_out=({}, {}) "
                "anchor_bases=parent:{} page:{} choice:{} "
                "asset_patch=false native_page_reuse=false "
                "native_option_callbacks=false input=viewport_consume "
                "choice_apply=definition_callback\n"),
            focus_in_hook_ids_.first, focus_in_hook_ids_.second, focus_out_hook_ids_.first,
            focus_out_hook_ids_.second, anchor_in_hook_ids_.first, anchor_in_hook_ids_.second,
            anchor_out_hook_ids_.first, anchor_out_hook_ids_.second, parent_anchor_index(0),
            page_anchor_base_, choice_anchor_base_);
        (void)try_install_viewport_input_hook();
    }

    auto RuntimeOptionsMenuService::Impl::tick() -> void
    {
        (void)try_install_viewport_input_hook();
        bool injection_completed_this_tick = false;
        if (injection_pending_)
        {
            UObject* const context = menu_context_.Get();
            UObject* const root = live_menu_root();
            if (!context || !root || !is_setting_menu_context(context) ||
                !kh3ap::is_uobject_valid_fast(root))
            {
                injection_pending_ = false;
                injection_ready_ = false;
                injection_delay_ticks_ = 0;
                injection_attempts_ = 0;
            }
            else if (injection_delay_ticks_ > 0)
            {
                --injection_delay_ticks_;
            }
            else
            {
                ++injection_attempts_;
                if (ensure_injected(root))
                {
                    injection_pending_ = false;
                    injection_ready_ = true;
                    injection_completed_this_tick = true;
                    injection_attempts_ = 0;
                    parent_label_refresh_ticks_ = deferred_parent_label_refresh_tick_count;
                    for (const RuntimeOptionsMenuDefinition& menu : config_.menus)
                    {
                        if (!menu.on_host_ready)
                        {
                            continue;
                        }
                        try
                        {
                            menu.on_host_ready();
                        }
                        catch (...)
                        {
                            // One menu's lifecycle callback must not disable the shared host.
                        }
                    }
                }
                else if (injection_attempts_ >= max_deferred_injection_attempts)
                {
                    injection_pending_ = false;
                    injection_ready_ = false;
                    Output::send<LogLevel::Error>(
                        STR("[KH3APProbeCpp] Runtime options injection "
                            "abandoned reason=deferred_attempt_limit attempts={}\n"),
                        injection_attempts_);
                }
            }
        }
        if (injection_ready_ && !injection_completed_this_tick && parent_label_refresh_ticks_ > 0)
        {
            UObject* const root = live_menu_root();
            if (!root || !kh3ap::is_uobject_valid_fast(root))
            {
                parent_label_refresh_ticks_ = 0;
            }
            else
            {
                // clone() initially exposes the symbol's authored placeholder text.
                // Its nested text field is constructed on a later Scaleform advance,
                // so stabilize the label for a few ordinary game ticks instead of
                // repainting it from inside OnAnchorIn.
                refresh_parent_labels(root);
                --parent_label_refresh_ticks_;
            }
        }
        if (!capture_active_)
        {
            return;
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
            injection_ready_ = false;
            injection_pending_ = false;
            injection_delay_ticks_ = 0;
            injection_attempts_ = 0;
            parent_label_refresh_ticks_ = 0;
            navigation_direction_latched_ = 0;
            menu_context_ = nullptr;
            menu_root_ = nullptr;
            for (FWeakObjectPtr& parent : parent_buttons_)
            {
                parent = nullptr;
            }
        }
    }

    RuntimeOptionsMenuService::RuntimeOptionsMenuService(RuntimeOptionsMenuConfig config)
        : impl_(std::make_unique<Impl>(std::move(config)))
    {
    }

    RuntimeOptionsMenuService::~RuntimeOptionsMenuService() = default;
    RuntimeOptionsMenuService::RuntimeOptionsMenuService(RuntimeOptionsMenuService&&) noexcept =
        default;
    auto RuntimeOptionsMenuService::operator=(RuntimeOptionsMenuService&&) noexcept
        -> RuntimeOptionsMenuService& = default;

    auto RuntimeOptionsMenuService::install_once() -> void
    {
        impl_->install_once();
    }

    auto RuntimeOptionsMenuService::tick() -> void
    {
        impl_->tick();
    }
} // namespace kh3ap
