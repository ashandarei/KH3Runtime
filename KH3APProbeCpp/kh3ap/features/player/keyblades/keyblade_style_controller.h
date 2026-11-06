#pragma once
#include <string>
#include <string_view>
namespace RC::Unreal
{
    class UObject;
}
namespace kh3ap::keyblade_options
{
    using RC::Unreal::UObject;
    auto fail(std::wstring* failure_details, std::wstring message) -> bool;
    auto try_call_controller_bool(UObject* controller, std::wstring_view function_path,
                                  std::wstring_view parameter_name, bool value,
                                  std::wstring* failure_details) -> bool;
    auto try_call_controller_no_params(UObject* controller, std::wstring_view function_path,
                                       std::wstring* failure_details) -> bool;
} // namespace kh3ap::keyblade_options
