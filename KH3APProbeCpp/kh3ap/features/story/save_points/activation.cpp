#include "kh3ap/features/story/save_points/activation.h"
#include "kh3ap/features/story/save_points/text.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/features/grants/grant_internal_call_depth.h"
#include <UnrealDef.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObject.hpp>
#include <vector>
#include <algorithm>
#include <chrono>
#include <format>
#include <utility>
#include "kh3ap/features/story/save_points/helpers.h"

namespace kh3ap::save_point_activation
{
    using namespace RC::Unreal;
    using save_point_native::try_resolve_cached_function;
    using save_point_text::bool_text;
    namespace
    {
        auto call_actor_function_no_params_untimed(UObject* actor, std::wstring_view function_path)
            -> ActivationCallSummary
        {
            ActivationCallSummary summary{};
            summary.function_path = std::wstring{function_path};
            if (!actor)
            {
                summary.status = STR("failed");
                summary.reason = STR("invalid_actor");
                return summary;
            }

            UFunction* const function_object = try_resolve_cached_function(function_path);
            if (!function_object)
            {
                summary.status = STR("failed");
                summary.reason = STR("missing_function");
                return summary;
            }

            std::vector<std::byte> params(static_cast<size_t>(function_object->GetParmsSize()));
            std::fill(params.begin(), params.end(), std::byte{0});
            {
                const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
                actor->ProcessEvent(function_object, params.empty() ? nullptr : params.data());
            }
            summary.status = STR("invoked");
            summary.invoked = true;
            return summary;
        }

        auto call_actor_bool_function_untimed(UObject* actor, std::wstring_view function_path,
                                              std::wstring_view param_name, bool value)
            -> ActivationCallSummary
        {
            ActivationCallSummary summary{};
            summary.function_path = std::wstring{function_path};
            summary.param_name = std::wstring{param_name};
            summary.param_value = bool_text(value);
            if (!actor)
            {
                summary.status = STR("failed");
                summary.reason = STR("invalid_actor");
                return summary;
            }

            UFunction* const function_object = try_resolve_cached_function(function_path);
            if (!function_object)
            {
                summary.status = STR("failed");
                summary.reason = STR("missing_function");
                return summary;
            }

            std::vector<std::byte> params(static_cast<size_t>(function_object->GetParmsSize()));
            std::fill(params.begin(), params.end(), std::byte{0});
            FProperty* const param_property =
                find_function_param_by_name(function_object, param_name);
            summary.param_written = write_bool_function_param(param_property, params.data(), value);
            if (!summary.param_written)
            {
                summary.status = STR("failed");
                summary.reason = param_property ? std::format(STR("param_write_failed:{}"),
                                                              param_property->GetClass().GetName())
                                                : std::wstring{STR("missing_param")};
                return summary;
            }

            {
                const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
                actor->ProcessEvent(function_object, params.data());
            }
            summary.status = STR("invoked");
            summary.invoked = true;
            return summary;
        }

        auto call_actor_two_bool_function_untimed(UObject* actor, std::wstring_view function_path,
                                                  std::wstring_view first_param_name,
                                                  bool first_value,
                                                  std::wstring_view second_param_name,
                                                  bool second_value) -> ActivationCallSummary
        {
            ActivationCallSummary summary{};
            summary.function_path = std::wstring{function_path};
            summary.param_name =
                std::wstring{first_param_name} + STR(",") + std::wstring{second_param_name};
            summary.param_value = bool_text(first_value) + STR(",") + bool_text(second_value);
            if (!actor)
            {
                summary.status = STR("failed");
                summary.reason = STR("invalid_actor");
                return summary;
            }

            UFunction* const function_object = try_resolve_cached_function(function_path);
            if (!function_object)
            {
                summary.status = STR("failed");
                summary.reason = STR("missing_function");
                return summary;
            }

            std::vector<std::byte> params(static_cast<size_t>(function_object->GetParmsSize()));
            std::fill(params.begin(), params.end(), std::byte{0});
            FProperty* const first_property =
                find_function_param_by_name(function_object, first_param_name);
            FProperty* const second_property =
                find_function_param_by_name(function_object, second_param_name);
            const bool wrote_first =
                write_bool_function_param(first_property, params.data(), first_value);
            const bool wrote_second =
                write_bool_function_param(second_property, params.data(), second_value);
            summary.param_written = wrote_first && wrote_second;
            if (!summary.param_written)
            {
                summary.status = STR("failed");
                summary.reason = std::format(
                    STR("param_write_failed first={} second={}"),
                    wrote_first
                        ? STR("ok")
                        : (first_property ? first_property->GetClass().GetName() : STR("missing")),
                    wrote_second ? STR("ok")
                                 : (second_property ? second_property->GetClass().GetName()
                                                    : STR("missing")));
                return summary;
            }

            {
                const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
                actor->ProcessEvent(function_object, params.data());
            }
            summary.status = STR("invoked");
            summary.invoked = true;
            return summary;
        }

        auto call_actor_integral_function_untimed(UObject* actor, std::wstring_view function_path,
                                                  std::wstring_view param_name, uint64_t value)
            -> ActivationCallSummary
        {
            ActivationCallSummary summary{};
            summary.function_path = std::wstring{function_path};
            summary.param_name = std::wstring{param_name};
            summary.param_value = std::to_wstring(value);
            if (!actor)
            {
                summary.status = STR("failed");
                summary.reason = STR("invalid_actor");
                return summary;
            }

            UFunction* const function_object = try_resolve_cached_function(function_path);
            if (!function_object)
            {
                summary.status = STR("failed");
                summary.reason = STR("missing_function");
                return summary;
            }

            std::vector<std::byte> params(static_cast<size_t>(function_object->GetParmsSize()));
            std::fill(params.begin(), params.end(), std::byte{0});
            FProperty* const param_property =
                find_function_param_by_name(function_object, param_name);
            summary.param_written =
                write_integral_function_param(param_property, params.data(), value);
            if (!summary.param_written)
            {
                summary.status = STR("failed");
                summary.reason = param_property ? std::format(STR("param_write_failed:{}"),
                                                              param_property->GetClass().GetName())
                                                : std::wstring{STR("missing_param")};
                return summary;
            }

            {
                const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
                actor->ProcessEvent(function_object, params.data());
            }
            summary.status = STR("invoked");
            summary.invoked = true;
            return summary;
        }

        auto call_actor_two_integral_function_untimed(
            UObject* actor, std::wstring_view function_path, std::wstring_view first_param_name,
            uint64_t first_value, std::wstring_view second_param_name, uint64_t second_value)
            -> ActivationCallSummary
        {
            ActivationCallSummary summary{};
            summary.function_path = std::wstring{function_path};
            summary.param_name =
                std::wstring{first_param_name} + STR(",") + std::wstring{second_param_name};
            summary.param_value =
                std::to_wstring(first_value) + STR(",") + std::to_wstring(second_value);
            if (!actor)
            {
                summary.status = STR("failed");
                summary.reason = STR("invalid_actor");
                return summary;
            }

            UFunction* const function_object = try_resolve_cached_function(function_path);
            if (!function_object)
            {
                summary.status = STR("failed");
                summary.reason = STR("missing_function");
                return summary;
            }

            std::vector<std::byte> params(static_cast<size_t>(function_object->GetParmsSize()));
            std::fill(params.begin(), params.end(), std::byte{0});
            FProperty* const first_property =
                find_function_param_by_name(function_object, first_param_name);
            FProperty* const second_property =
                find_function_param_by_name(function_object, second_param_name);
            const bool wrote_first =
                write_integral_function_param(first_property, params.data(), first_value);
            const bool wrote_second =
                write_integral_function_param(second_property, params.data(), second_value);
            summary.param_written = wrote_first && wrote_second;
            if (!summary.param_written)
            {
                summary.status = STR("failed");
                summary.reason = std::format(
                    STR("param_write_failed first={} second={}"),
                    wrote_first
                        ? STR("ok")
                        : (first_property ? first_property->GetClass().GetName() : STR("missing")),
                    wrote_second ? STR("ok")
                                 : (second_property ? second_property->GetClass().GetName()
                                                    : STR("missing")));
                return summary;
            }

            {
                const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
                actor->ProcessEvent(function_object, params.data());
            }
            summary.status = STR("invoked");
            summary.invoked = true;
            return summary;
        }

        template <typename Callable>
        auto measure_activation_call(Callable&& callable) -> ActivationCallSummary
        {
            const auto started_at = std::chrono::steady_clock::now();
            ActivationCallSummary summary = std::forward<Callable>(callable)();
            summary.elapsed_us =
                static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                          std::chrono::steady_clock::now() - started_at)
                                          .count());
            return summary;
        }
    } // namespace

    auto call_actor_function_no_params(UObject* actor, std::wstring_view function_path)
        -> ActivationCallSummary
    {
        return measure_activation_call(
            [&]()
            {
                return call_actor_function_no_params_untimed(actor, function_path);
            });
    }

    auto call_actor_bool_function(UObject* actor, std::wstring_view function_path,
                                  std::wstring_view param_name, bool value) -> ActivationCallSummary
    {
        return measure_activation_call(
            [&]()
            {
                return call_actor_bool_function_untimed(actor, function_path, param_name, value);
            });
    }

    auto call_actor_two_bool_function(UObject* actor, std::wstring_view function_path,
                                      std::wstring_view first_param_name, bool first_value,
                                      std::wstring_view second_param_name, bool second_value)
        -> ActivationCallSummary
    {
        return measure_activation_call(
            [&]()
            {
                return call_actor_two_bool_function_untimed(actor, function_path, first_param_name,
                                                            first_value, second_param_name,
                                                            second_value);
            });
    }

    auto call_actor_integral_function(UObject* actor, std::wstring_view function_path,
                                      std::wstring_view param_name, uint64_t value)
        -> ActivationCallSummary
    {
        return measure_activation_call(
            [&]()
            {
                return call_actor_integral_function_untimed(actor, function_path, param_name,
                                                            value);
            });
    }

    auto call_actor_two_integral_function(UObject* actor, std::wstring_view function_path,
                                          std::wstring_view first_param_name, uint64_t first_value,
                                          std::wstring_view second_param_name,
                                          uint64_t second_value) -> ActivationCallSummary
    {
        return measure_activation_call(
            [&]()
            {
                return call_actor_two_integral_function_untimed(actor, function_path,
                                                                first_param_name, first_value,
                                                                second_param_name, second_value);
            });
    }
} // namespace kh3ap::save_point_activation
