#pragma once
#include <cstdint>
#include <string>
#include <string_view>
namespace RC::Unreal
{
    class UObject;
}
namespace kh3ap::save_point_activation
{
    using RC::Unreal::UObject;
    struct ActivationCallSummary
    {
        std::wstring function_path{};
        std::wstring param_name{};
        std::wstring param_value{};
        std::wstring status{};
        std::wstring reason{};
        uint64_t elapsed_us{};
        bool param_written{};
        bool invoked{};
    };

    // Game-thread only: actors must be validated in the current frame. These
    // wrappers borrow them for an immediate ProcessEvent and retain no UObject.
    auto call_actor_function_no_params(UObject* actor, std::wstring_view function_path)
        -> ActivationCallSummary;

    auto call_actor_bool_function(UObject* actor, std::wstring_view function_path,
                                  std::wstring_view param_name, bool value)
        -> ActivationCallSummary;

    auto call_actor_two_bool_function(UObject* actor, std::wstring_view function_path,
                                      std::wstring_view first_param_name, bool first_value,
                                      std::wstring_view second_param_name, bool second_value)
        -> ActivationCallSummary;

    auto call_actor_integral_function(UObject* actor, std::wstring_view function_path,
                                      std::wstring_view param_name, uint64_t value)
        -> ActivationCallSummary;

    auto call_actor_two_integral_function(UObject* actor, std::wstring_view function_path,
                                          std::wstring_view first_param_name, uint64_t first_value,
                                          std::wstring_view second_param_name,
                                          uint64_t second_value) -> ActivationCallSummary;
} // namespace kh3ap::save_point_activation
