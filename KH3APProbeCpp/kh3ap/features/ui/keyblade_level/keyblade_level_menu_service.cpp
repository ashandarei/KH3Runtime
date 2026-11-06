#include "kh3ap/features/ui/keyblade_level/keyblade_level_menu_service.h"

#include "kh3ap/core/gfx/gfx_reflection_helpers.h"
#include "kh3ap/features/grants/keyblades/progressive_keyblade_level_helpers.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_validity.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/FFrame.hpp>
#include <Unreal/FWeakObjectPtr.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <UnrealDef.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cwctype>
#include <format>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

using namespace RC;
using namespace RC::Unreal;

namespace kh3ap
{
    namespace
    {
        constexpr std::wstring_view list_index_change_path =
                STR("/Script/TresGame.TresUIParts:OnListIndexChange");
        constexpr std::wstring_view focus_out_path =
                STR("/Script/TresGame.TresUIParts:OnFocusOut");
        constexpr int32_t replace_list_anchor_index = 307;
        constexpr size_t replace_list_row_capacity = 10;
        constexpr int32_t immediate_child_scan_limit = 16;
        constexpr uint8_t maximum_deferred_attempts = 4;

        constexpr std::array<std::wstring_view, 17> keyblade_names{
                STR("Kingdom Key"),
                STR("Shooting Star"),
                STR("Hero's Origin"),
                STR("Favorite Deputy"),
                STR("Ever After"),
                STR("Wheel of Fate"),
                STR("Crystal Snow"),
                STR("Hunny Spout"),
                STR("Nano Gear"),
                STR("Happy Gear"),
                STR("Classic Tone"),
                STR("Grand Chef"),
                STR("Ultima Weapon"),
                STR("Pandora's Power"),
                STR("Starlight"),
                STR("Oathkeeper"),
                STR("Oblivion"),
        };

        auto read_gfx_param(
                UFunction* function,
                void* params,
                std::initializer_list<std::wstring_view> names) -> UObject*
        {
            for (const std::wstring_view name : names)
            {
                UObject* const value =
                        try_read_function_object_param(function, params, name);
                if (value && kh3ap::is_uobject_valid_fast(value))
                {
                    return value;
                }
            }
            return nullptr;
        }

        auto proxy_as_object(UObject* proxy) -> UObject*
        {
            if (!proxy || !kh3ap::is_uobject_valid_fast(proxy))
            {
                return nullptr;
            }
            if (UClass* const object_class = proxy->GetClassPrivate();
                object_class && object_class->GetName() == STR("GFxObject"))
            {
                return proxy;
            }
            for (const std::wstring_view property_name : {
                         STR("m_ASObject"),
                         STR("m_RootMain"),
                         STR("m_Root"),
                 })
            {
                UObject* const object =
                        try_read_reflected_object_property_value(
                                proxy,
                                property_name);
                if (object && kh3ap::is_uobject_valid_fast(object))
                {
                    return object;
                }
            }
            return nullptr;
        }

        auto member_path(
                const GfxProbeFunctionPaths& paths,
                UObject* root,
                std::initializer_list<std::wstring_view> members,
                std::wstring* failed_member = nullptr) -> UObject*
        {
            if (failed_member)
            {
                failed_member->clear();
            }
            UObject* current = root;
            for (const std::wstring_view member : members)
            {
                current = current
                        ? try_gfx_get_member_value_object(
                                  paths,
                                  current,
                                  member)
                        : nullptr;
                if (!current)
                {
                    if (failed_member)
                    {
                        *failed_member = member;
                    }
                    return nullptr;
                }
            }
            return current;
        }

        auto object_class_name(UObject* object) -> std::wstring
        {
            if (!object || !kh3ap::is_uobject_valid_fast(object))
            {
                return STR("<null>");
            }
            UClass* const object_class = object->GetClassPrivate();
            return object_class ? object_class->GetName() : STR("<no-class>");
        }

        auto object_name(UObject* object) -> std::wstring
        {
            return object && kh3ap::is_uobject_valid_fast(object)
                    ? object->GetName()
                    : STR("<null>");
        }

        auto normalized_label(std::wstring_view value) -> std::wstring
        {
            std::wstring normalized{};
            normalized.reserve(value.size());
            for (const wchar_t ch : value)
            {
                if (std::iswalnum(ch))
                {
                    normalized.push_back(
                            static_cast<wchar_t>(std::towlower(ch)));
                }
            }
            return normalized;
        }

        auto canonical_keyblade_name(std::wstring_view candidate)
                -> std::optional<std::wstring>
        {
            const std::wstring normalized_candidate = normalized_label(candidate);
            if (normalized_candidate.empty())
            {
                return std::nullopt;
            }
            for (const std::wstring_view keyblade_name : keyblade_names)
            {
                if (normalized_candidate.find(normalized_label(keyblade_name)) !=
                    std::wstring::npos)
                {
                    return std::wstring{keyblade_name};
                }
            }
            return std::nullopt;
        }

        auto live_object(const FWeakObjectPtr& weak) -> UObject*
        {
            UObject* const object = weak.Get();
            return object && kh3ap::is_uobject_valid_fast(object)
                    ? object
                    : nullptr;
        }
    }

    struct RuntimeKeybladeLevelMenuService::Impl
    {
        explicit Impl(RuntimeKeybladeLevelMenuConfig config)
            : config_(std::move(config))
        {
        }

        auto install_once() -> void
        {
            if (installed_ || !config_.enabled)
            {
                return;
            }
            installed_ = true;
            list_index_hook_ids_ = UObjectGlobals::RegisterHook(
                    std::wstring{list_index_change_path},
                    [this](UnrealScriptFunctionCallableContext& context, void*) {
                        handle_list_index_change(context);
                    },
                    [](UnrealScriptFunctionCallableContext&, void*) {},
                    nullptr);
            focus_out_hook_ids_ = UObjectGlobals::RegisterHook(
                    std::wstring{focus_out_path},
                    [this](UnrealScriptFunctionCallableContext& context, void*) {
                        handle_focus_out(context);
                    },
                    [](UnrealScriptFunctionCallableContext&, void*) {},
                    nullptr);
            Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] Keyblade level bottom-help hooks "
                        "installed list_index=({}, {}) focus_out=({}, {}) "
                        "movie=/Game/UI/Scaleform/15_campmenu/CampMenu "
                        "anchor_index={} polling=false\n"),
                    list_index_hook_ids_.first,
                    list_index_hook_ids_.second,
                    focus_out_hook_ids_.first,
                    focus_out_hook_ids_.second,
                    replace_list_anchor_index);
        }

        auto root_from_context(UObject* context) -> UObject*
        {
            if (!context || !kh3ap::is_uobject_valid_fast(context))
            {
                return nullptr;
            }
            UObject* const proxy = try_read_reflected_object_property_value(
                    context,
                    STR("m_ASProxyUIComponent"));
            return proxy_as_object(proxy);
        }

        auto anchor_index(UObject* anchor) -> std::optional<int32_t>
        {
            UObject* const object = proxy_as_object(anchor);
            if (!object)
            {
                return std::nullopt;
            }
            const auto value = try_gfx_get_member_value_number(
                    config_.gfx_paths,
                    object,
                    STR("index"));
            return value.has_value()
                    ? std::optional<int32_t>{static_cast<int32_t>(
                              std::llround(*value))}
                    : std::nullopt;
        }

        auto try_read_selected_label(
                UObject* list,
                int32_t selected_index,
                std::wstring* label,
                std::wstring* failure_stage) -> bool
        {
            if (label)
            {
                label->clear();
            }
            if (failure_stage)
            {
                failure_stage->clear();
            }
            const auto fail = [failure_stage](std::wstring_view stage) {
                if (failure_stage)
                {
                    *failure_stage = stage;
                }
                return false;
            };
            if (!list || !kh3ap::is_uobject_valid_fast(list) ||
                selected_index < 0)
            {
                return fail(STR("invalid_list_or_index"));
            }
            const auto scroll_position_value =
                    try_gfx_get_member_value_number(
                            config_.gfx_paths,
                            list,
                            STR("scrollPosition"));
            const auto column_count_value = try_gfx_get_member_value_number(
                    config_.gfx_paths,
                    list,
                    STR("columnNum"));
            if (!scroll_position_value.has_value() ||
                !column_count_value.has_value())
            {
                return fail(STR("list_geometry"));
            }
            const int32_t scroll_position = static_cast<int32_t>(
                    std::llround(*scroll_position_value));
            const int32_t column_count = std::max(
                    1,
                    static_cast<int32_t>(
                            std::llround(*column_count_value)));
            const int32_t visible_index =
                    selected_index - scroll_position * column_count;
            if (visible_index < 0 ||
                visible_index >=
                        static_cast<int32_t>(replace_list_row_capacity))
            {
                return fail(STR("visible_index"));
            }
            UObject* const container = try_gfx_get_member_value_object(
                    config_.gfx_paths,
                    list,
                    STR("container"));
            if (!container)
            {
                return fail(STR("container"));
            }
            const auto child_count_value = try_gfx_get_member_value_number(
                    config_.gfx_paths,
                    container,
                    STR("numChildren"));
            if (!child_count_value.has_value())
            {
                return fail(STR("child_count"));
            }
            const int32_t child_count = std::clamp(
                    static_cast<int32_t>(
                            std::llround(*child_count_value)),
                    0,
                    immediate_child_scan_limit);
            int32_t row_ordinal = 0;
            for (int32_t child_index = 0;
                 child_index < child_count;
                 ++child_index)
            {
                UObject* const child = try_gfx_invoke_get_child_at(
                        config_.gfx_paths,
                        container,
                        child_index);
                const auto candidate = child
                        ? try_gfx_get_member_value_string(
                                  config_.gfx_paths,
                                  child,
                                  STR("label"))
                        : std::nullopt;
                if (!candidate.has_value())
                {
                    continue;
                }
                if (row_ordinal == visible_index)
                {
                    if (label)
                    {
                        *label = *candidate;
                    }
                    return !candidate->empty() ||
                            fail(STR("selected_label_empty"));
                }
                ++row_ordinal;
            }
            return fail(STR("selected_row"));
        }

        auto handle_list_index_change(
                UnrealScriptFunctionCallableContext& context) -> void
        {
            UFunction* const function = get_frame_node(context.TheStack);
            void* const params = get_frame_locals(context.TheStack);
            UObject* const anchor = read_gfx_param(
                    function,
                    params,
                    {STR("AnchorComponent")});
            const auto observed_anchor_index = anchor_index(anchor);
            if (observed_anchor_index.value_or(-1) !=
                replace_list_anchor_index)
            {
                return;
            }
            UObject* const list = proxy_as_object(anchor);
            UObject* const ui_component = read_gfx_param(
                    function,
                    params,
                    {STR("UIComponent")});
            UObject* root = root_from_context(context.Context);
            if (!root)
            {
                root = proxy_as_object(ui_component);
            }
            const auto list_index = read_function_int_param(
                    function,
                    params,
                    STR("ListIndex"));
            const auto prev_list_index = read_function_int_param(
                    function,
                    params,
                    STR("PrevListIndex"));
            if (callback_diagnostics_logged_ < maximum_callback_diagnostics)
            {
                ++callback_diagnostics_logged_;
                Output::send<LogLevel::Warning>(
                        STR("[KH3APProbeCpp] Keyblade list callback "
                            "phase=pre context_class={} context_name={} "
                            "anchor_param_class={} anchor_as_class={} "
                            "anchor_index={} "
                            "ui_param_class={} root_as_class={} "
                            "list_index={} prev_index={}\n"),
                        object_class_name(context.Context),
                        object_name(context.Context),
                        object_class_name(anchor),
                        object_class_name(list),
                        observed_anchor_index.value_or(-1),
                        object_class_name(ui_component),
                        object_class_name(root),
                        list_index.value_or(-1),
                        prev_list_index.value_or(-1));
            }
            if (!root || !list || !list_index.has_value())
            {
                Output::send<LogLevel::Warning>(
                        STR("[KH3APProbeCpp] Keyblade level bottom-help "
                            "callback matched but required object missing "
                            "root={} list={} list_index={}\n"),
                        root != nullptr,
                        list != nullptr,
                        list_index.has_value());
                return;
            }
            if (menu_context_.Get() != context.Context)
            {
                restore_stock_help();
                stock_help_text_.clear();
            }
            menu_context_ = context.Context;
            selected_list_index_ = static_cast<int32_t>(*list_index);
            std::wstring selected_label{};
            std::wstring selection_failure{};
            if (try_read_selected_label(
                        list,
                        selected_list_index_,
                        &selected_label,
                        &selection_failure))
            {
                const auto keyblade_name =
                        canonical_keyblade_name(selected_label);
                selected_keyblade_name_ =
                        keyblade_name.value_or(std::wstring{});
                selection_capture_failed_ = false;
            }
            else
            {
                selected_keyblade_name_.clear();
                selection_capture_failed_ = true;
                if (selection_diagnostics_logged_ <
                    maximum_callback_diagnostics)
                {
                    ++selection_diagnostics_logged_;
                    Output::send<LogLevel::Warning>(
                            STR("[KH3APProbeCpp] Keyblade selection capture "
                                "deferred index={} stage={}\n"),
                            selected_list_index_,
                            selection_failure.empty()
                                    ? STR("unknown")
                                    : selection_failure);
                }
            }
            // Callback GFxObject arguments are transient wrappers. Do not
            // retain them across the callback boundary; reacquire the root
            // and list from the stable native CampMenu context during the
            // deferred tick.
            pending_render_ = true;
            deferred_attempts_ = 0;
        }

        auto handle_focus_out(UnrealScriptFunctionCallableContext& context)
                -> void
        {
            UFunction* const function = get_frame_node(context.TheStack);
            void* const params = get_frame_locals(context.TheStack);
            UObject* const anchor = read_gfx_param(
                    function,
                    params,
                    {STR("AnchorComponent")});
            if (anchor_index(anchor).value_or(-1) != replace_list_anchor_index)
            {
                return;
            }
            restore_stock_help();
            menu_context_ = nullptr;
            selected_list_index_ = -1;
            pending_render_ = false;
            deferred_attempts_ = 0;
            last_keyblade_name_.clear();
            last_help_text_.clear();
            selected_keyblade_name_.clear();
            selection_capture_failed_ = false;
            stock_help_text_.clear();
        }

        auto replace_list_from_root(
                UObject* root,
                std::wstring* failed_member = nullptr) -> UObject*
        {
            return member_path(
                    config_.gfx_paths,
                    root,
                    {
                            STR("equip"),
                            STR("equipReplaceAnm"),
                            STR("equipReplace"),
                            STR("replaceListVisibleAnm"),
                            STR("replaceListVisible"),
                            STR("replaceList"),
                    },
                    failed_member);
        }

        auto help_text_from_root(
                UObject* root,
                std::wstring* failed_member = nullptr) -> UObject*
        {
            return member_path(
                    config_.gfx_paths,
                    root,
                    {
                            STR("equip"),
                            STR("equipReplaceAnm"),
                            STR("helpText"),
                            STR("txHelpText"),
                    },
                    failed_member);
        }

        auto log_render_failure(
                std::wstring_view stage,
                UObject* root,
                const std::wstring& detail = std::wstring{}) -> void
        {
            if (last_render_failure_stage_ == stage ||
                render_diagnostics_logged_ >= maximum_callback_diagnostics)
            {
                return;
            }
            last_render_failure_stage_ = stage;
            ++render_diagnostics_logged_;
            Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] Keyblade bottom-help render failure "
                        "stage={} detail={} index={} root_class={} "
                        "root_name={}\n"),
                    stage,
                    detail.empty() ? STR("<none>") : detail,
                    selected_list_index_,
                    object_class_name(root),
                    object_name(root));
        }

        auto restore_stock_help() -> void
        {
            if (stock_help_text_.empty())
            {
                return;
            }
            UObject* const menu_context = live_object(menu_context_);
            UObject* const root = menu_context
                    ? root_from_context(menu_context)
                    : nullptr;
            UObject* const help = root
                    ? help_text_from_root(root)
                    : nullptr;
            if (!help)
            {
                return;
            }
            (void)try_gfx_set_member_value_string(
                    config_.gfx_paths,
                    help,
                    STR("text"),
                    stock_help_text_);
        }

        auto render_current_selection() -> bool
        {
            UObject* const menu_context = live_object(menu_context_);
            if (!menu_context)
            {
                pending_render_ = false;
                return true;
            }
            UObject* const root = root_from_context(menu_context);
            if (!root)
            {
                log_render_failure(STR("root"), nullptr);
                return false;
            }
            if (selection_capture_failed_)
            {
                std::wstring list_failed_member{};
                UObject* const list = replace_list_from_root(
                        root,
                        &list_failed_member);
                if (!list)
                {
                    log_render_failure(
                            STR("list_path"),
                            root,
                            list_failed_member);
                    return false;
                }
                std::wstring label{};
                std::wstring selection_failure{};
                if (!try_read_selected_label(
                            list,
                            selected_list_index_,
                            &label,
                            &selection_failure))
                {
                    log_render_failure(
                            STR("selected_label"),
                            root,
                            selection_failure);
                    return false;
                }
                selected_keyblade_name_ = canonical_keyblade_name(label)
                        .value_or(std::wstring{});
                selection_capture_failed_ = false;
            }

            std::wstring help_failed_member{};
            UObject* const help = help_text_from_root(
                    root,
                    &help_failed_member);
            if (!help)
            {
                log_render_failure(
                        STR("help_path"),
                        root,
                        help_failed_member);
                return false;
            }
            if (stock_help_text_.empty())
            {
                stock_help_text_ = try_gfx_get_member_value_string(
                        config_.gfx_paths,
                        help,
                        STR("text")).value_or(std::wstring{});
            }
            if (!help_route_logged_)
            {
                help_route_logged_ = true;
                Output::send<LogLevel::Warning>(
                        STR("[KH3APProbeCpp] Keyblade level bottom-help "
                            "path resolved route=equip.equipReplaceAnm.helpText "
                            "object_lifetime=same_render_call\n"));
            }
            if (selected_keyblade_name_.empty())
            {
                if (!stock_help_text_.empty())
                {
                    (void)try_gfx_set_member_value_string(
                            config_.gfx_paths,
                            help,
                            STR("text"),
                            stock_help_text_);
                }
                last_keyblade_name_.clear();
                last_help_text_.clear();
                return true;
            }

            KeybladeLevelQueryResult level{};
            std::wstring failure{};
            const bool starts_at_max = config_.levels_start_at_max
                    ? config_.levels_start_at_max()
                    : false;
            if (!try_query_keyblade_level(
                        selected_keyblade_name_,
                        starts_at_max,
                        &level,
                        &failure))
            {
                Output::send<LogLevel::Error>(
                        STR("[KH3APProbeCpp] Keyblade level query failed "
                            "keyblade={} error={}\n"),
                        selected_keyblade_name_,
                        failure);
                return true;
            }

            const std::wstring help_text = std::format(
                    STR("{} - KEYBLADE LEVEL {} / {}"),
                    selected_keyblade_name_,
                    level.current_level,
                    level.maximum_level);
            if (!try_gfx_set_member_value_string(
                        config_.gfx_paths,
                        help,
                        STR("text"),
                        help_text))
            {
                log_render_failure(STR("write_help"), root);
                return false;
            }
            if (last_keyblade_name_ != selected_keyblade_name_ ||
                last_help_text_ != help_text)
            {
                Output::send<LogLevel::Normal>(
                        STR("[KH3APProbeCpp] Keyblade level bottom-help updated "
                            "route=list_index_event index={} keyblade={} "
                            "level={}/{} achieved_flags={}\n"),
                        selected_list_index_,
                        selected_keyblade_name_,
                        level.current_level,
                        level.maximum_level,
                        level.achieved_flag_count);
            }
            last_keyblade_name_ = selected_keyblade_name_;
            last_help_text_ = help_text;
            last_render_failure_stage_.clear();
            return true;
        }

        auto tick() -> void
        {
            if (!config_.enabled || !pending_render_)
            {
                return;
            }
            if (render_current_selection())
            {
                pending_render_ = false;
                deferred_attempts_ = 0;
                return;
            }
            ++deferred_attempts_;
            if (deferred_attempts_ >= maximum_deferred_attempts)
            {
                pending_render_ = false;
                Output::send<LogLevel::Warning>(
                        STR("[KH3APProbeCpp] Keyblade level bottom-help skipped "
                            "after bounded deferred attempts index={}\n"),
                        selected_list_index_);
            }
        }

        auto observe_progressive_keyblade_level_applied() -> void
        {
            if (config_.enabled && live_object(menu_context_) &&
                selected_list_index_ >= 0)
            {
                pending_render_ = true;
                deferred_attempts_ = 0;
            }
        }

        RuntimeKeybladeLevelMenuConfig config_{};
        FWeakObjectPtr menu_context_{};
        bool installed_{};
        bool pending_render_{};
        bool help_route_logged_{};
        bool selection_capture_failed_{};
        static constexpr uint32_t maximum_callback_diagnostics = 8;
        uint32_t callback_diagnostics_logged_{};
        uint32_t selection_diagnostics_logged_{};
        uint32_t render_diagnostics_logged_{};
        int32_t selected_list_index_{-1};
        uint8_t deferred_attempts_{};
        std::wstring stock_help_text_{};
        std::wstring selected_keyblade_name_{};
        std::wstring last_render_failure_stage_{};
        std::wstring last_keyblade_name_{};
        std::wstring last_help_text_{};
        std::pair<int, int> list_index_hook_ids_{};
        std::pair<int, int> focus_out_hook_ids_{};
    };

    RuntimeKeybladeLevelMenuService::RuntimeKeybladeLevelMenuService(
            RuntimeKeybladeLevelMenuConfig config)
        : impl_(std::make_unique<Impl>(std::move(config)))
    {
    }

    RuntimeKeybladeLevelMenuService::~RuntimeKeybladeLevelMenuService() = default;
    RuntimeKeybladeLevelMenuService::RuntimeKeybladeLevelMenuService(
            RuntimeKeybladeLevelMenuService&&) noexcept = default;
    auto RuntimeKeybladeLevelMenuService::operator=(
            RuntimeKeybladeLevelMenuService&&) noexcept
            -> RuntimeKeybladeLevelMenuService& = default;

    auto RuntimeKeybladeLevelMenuService::install_once() -> void
    {
        impl_->install_once();
    }

    auto RuntimeKeybladeLevelMenuService::tick() -> void
    {
        impl_->tick();
    }

    auto RuntimeKeybladeLevelMenuService::observe_progressive_keyblade_level_applied()
            -> void
    {
        impl_->observe_progressive_keyblade_level_applied();
    }
}
