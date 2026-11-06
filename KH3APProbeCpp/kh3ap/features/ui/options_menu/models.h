#pragma once
#include "kh3ap/features/ui/options_menu_service.h"
#include <UnrealDef.hpp>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace RC::Unreal
{
    class UObject;
    class UFunction;
    class UnrealScriptFunctionCallableContext;
    struct FName;
} // namespace RC::Unreal
namespace kh3ap::options_menu_detail
{
    using RC::Unreal::UFunction;
    using RC::Unreal::UObject;
    inline constexpr std::wstring_view setting_menu_class_name = STR("TresUIP_SettingMenu");
    inline constexpr std::wstring_view focus_in_path =
        STR("/Script/TresGame.TresUIParts:OnFocusIn");
    inline constexpr std::wstring_view focus_out_path =
        STR("/Script/TresGame.TresUIParts:OnFocusOut");
    inline constexpr std::wstring_view anchor_in_path =
        STR("/Script/TresGame.TresUIParts:OnAnchorIn");
    inline constexpr std::wstring_view anchor_out_path =
        STR("/Script/TresGame.TresUIParts:OnAnchorOut");
    inline constexpr int32_t first_parent_anchor_index = 900;
    inline constexpr int32_t preferred_first_choice_anchor_index = 910;
    inline constexpr int32_t stock_first_index = 1;
    inline constexpr int32_t stock_quit_index = 8;
    inline constexpr size_t stock_row_count = stock_quit_index - stock_first_index + 1;
    inline constexpr double fallback_stock_row_span = 350.0;
    inline constexpr uint32_t max_input_diagnostic_events = 512;

    // FViewportClient's UE 4.17 secondary vtable begins with its deleting
    // destructor, followed by Precache through GetEngineShowFlags.
    inline constexpr size_t viewport_input_key_vtable_slot = 8;
    inline constexpr size_t viewport_input_axis_vtable_slot = 9;
    inline constexpr size_t viewport_lost_focus_vtable_slot = 23;
    inline constexpr int32_t input_event_pressed = 0;
    inline constexpr int32_t input_event_released = 1;
    inline constexpr uint32_t deferred_injection_tick_count = 2;
    inline constexpr uint32_t max_deferred_injection_attempts = 120;
    inline constexpr uint32_t deferred_parent_label_refresh_tick_count = 6;
    inline constexpr size_t max_visible_page_rows = 5;
    inline constexpr size_t max_visible_choice_rows = 8;
    inline constexpr int32_t enabled_text_color = 0xFFFFFF;
    // The stock setting row's disabled value transform is 115 / 256 RGB.
    inline constexpr int32_t disabled_setting_text_color = 0x737373;
    // The stock option row's disabled label transform is 153 / 256 RGB.
    inline constexpr int32_t disabled_option_text_color = 0x999999;

    enum class RuntimeOptionsScreenMode : uint8_t
    {
        inactive,
        parent,
        page,
        choice,
    };

    struct RuntimeOptionsPageFrame
    {
        const std::vector<RuntimeOptionsItemDefinition>* items{};
        size_t selected_index{};
        size_t scroll_position{};
    };

    struct DisplayTransform
    {
        double x{};
        double y{};
        double scale_x{1.0};
        double scale_y{1.0};
    };

    struct RawFKey;

    auto is_setting_menu_context(UObject* object) -> bool;

    auto read_gfx_param(UFunction* function, void* params,
                        std::initializer_list<std::wstring_view> names) -> UObject*;

    auto rounded_gfx_index(const GfxProbeFunctionPaths& paths, UObject* object)
        -> std::optional<int32_t>;
} // namespace kh3ap::options_menu_detail
