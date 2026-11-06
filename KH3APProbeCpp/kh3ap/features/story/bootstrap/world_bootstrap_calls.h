#pragma once
#include <optional>
#include <string>
#include <string_view>
namespace RC::Unreal
{
    class UObject;
    class UClass;
    class UFunction;
} // namespace RC::Unreal
namespace kh3ap::bootstrap_native
{
    inline constexpr std::wstring_view tres_blueprint_library_default_path =
        L"/Script/TresGame.Default__TresGameBlueprintLibrary";
    inline constexpr std::wstring_view loading_screen_closed_function_path =
        L"/Script/TresGame.TresGameBlueprintLibrary:TresUI_IsClosedLoadingScreen";
    inline constexpr std::wstring_view set_shop_level_function_path =
        L"/Script/TresGame.TresGameBlueprintLibrary:BP_TresSetShopLevel";
    inline constexpr std::wstring_view widget_blueprint_library_default_path =
        L"/Script/UMG.Default__WidgetBlueprintLibrary";
    inline constexpr std::wstring_view create_widget_function_path =
        L"/Script/UMG.WidgetBlueprintLibrary:Create";

    auto find_default_object(std::wstring_view path, RC::Unreal::UFunction* function)
        -> RC::Unreal::UObject*;
    auto call_loading_screen_closed(std::wstring* failure) -> std::optional<bool>;
    auto call_set_shop_level(RC::Unreal::UObject* context, std::wstring* failure) -> bool;
    auto create_and_add_proof_ui(RC::Unreal::UObject* context, RC::Unreal::UClass* widget_class,
                                 RC::Unreal::UObject** created_widget, std::wstring* failure)
        -> bool;
    auto report_failure(std::wstring_view operation, std::wstring& previous,
                        std::wstring_view failure) -> void;
} // namespace kh3ap::bootstrap_native
