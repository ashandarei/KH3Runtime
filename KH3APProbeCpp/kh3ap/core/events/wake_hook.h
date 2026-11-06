#pragma once
#include "kh3ap/core/objects/uobject_validity.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

namespace kh3ap
{
    // A narrow reflected native hook; the callback owns only a signal, never
    // its service or gameplay UObjects. Rejection does not enable polling.
    class RuntimeNativeWakeHook
    {
    public:
        RuntimeNativeWakeHook() = default;
        RuntimeNativeWakeHook(const RuntimeNativeWakeHook&) = delete;
        auto operator=(const RuntimeNativeWakeHook&) -> RuntimeNativeWakeHook& = delete;
        RuntimeNativeWakeHook(RuntimeNativeWakeHook&&) = delete;
        auto operator=(RuntimeNativeWakeHook&&) -> RuntimeNativeWakeHook& = delete;

        ~RuntimeNativeWakeHook() noexcept
        {
            if (!installed_)
            {
                return;
            }
            try
            {
                RC::Unreal::UObjectGlobals::UnregisterHook(path_, ids_);
            }
            catch (...)
            {
                log_failure(L"unregister_failed");
            }
        }

        auto failure_reason() const noexcept -> std::wstring_view { return failure_reason_; }

        auto install(const wchar_t* path, int parameter_size, std::function<void()> notify) -> bool
        {
            if (attempted_)
            {
                return installed_;
            }
            attempted_ = true;
            if (!path || !*path || parameter_size < 0 || !notify)
            {
                return fail(L"invalid_request");
            }
            using namespace RC::Unreal;
            try
            {
                path_ = path;
                auto* function = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, path);
                if (!is_uobject_valid_fast(function))
                {
                    return fail(L"function_missing");
                }
                if (!(function->GetFunctionFlags() & FUNC_Native))
                {
                    return fail(L"function_not_native");
                }
                if (function->GetParmsSize() != parameter_size)
                {
                    return fail(L"parameter_size_mismatch");
                }
                ids_ = UObjectGlobals::RegisterHook(function, [](auto&, void*) {},
                    [notify = std::move(notify)](auto&, void*) { notify(); }, nullptr);
                installed_ = true;
            }
            catch (...)
            {
                return fail(L"lookup_or_registration_failed");
            }
            return installed_;
        }

    private:
        auto log_failure(std::wstring_view reason) const noexcept -> void
        {
            try
            {
                RC::Output::send<RC::LogLevel::Warning>(
                    L"[KH3APProbeCpp] Native wake hook failed path={} reason={}\n", path_, reason);
            }
            catch (...)
            {
                // Operational logging cannot abort hook installation or teardown.
            }
        }

        auto fail(std::wstring_view reason) noexcept -> bool
        {
            failure_reason_ = reason;
            log_failure(reason);
            return false;
        }

        bool attempted_{}, installed_{};
        std::wstring path_;
        std::wstring_view failure_reason_;
        std::pair<int, int> ids_{};
    };
}
