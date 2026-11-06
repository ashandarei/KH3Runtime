#pragma once

#include "kh3ap/core/performance_build.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

namespace kh3ap
{
    auto record_runtime_profile_sample(
            std::wstring_view scope_name,
            uint64_t elapsed_us) -> void;

    auto record_runtime_reflection_profile_sample(
            std::wstring_view scope_name,
            uint64_t elapsed_us) -> void;

    // Called after the update callback's profiling scopes have closed.
    auto flush_runtime_profiles() -> void;

    template <bool Enabled>
    class RuntimeReflectionProfileScope;

    template <>
    class RuntimeReflectionProfileScope<false>
    {
    public:
        explicit RuntimeReflectionProfileScope(std::wstring_view)
        {
        }
    };

    template <>
    class RuntimeReflectionProfileScope<true>
    {
    public:
        explicit RuntimeReflectionProfileScope(std::wstring_view scope_name)
            : scope_name_(scope_name),
              started_at_(std::chrono::steady_clock::now())
        {
        }

        RuntimeReflectionProfileScope(const RuntimeReflectionProfileScope&) = delete;
        auto operator=(const RuntimeReflectionProfileScope&)
                -> RuntimeReflectionProfileScope& = delete;

        ~RuntimeReflectionProfileScope()
        {
            const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - started_at_);
            record_runtime_reflection_profile_sample(
                    scope_name_,
                    static_cast<uint64_t>(elapsed.count()));
        }

    private:
        std::wstring scope_name_;
        std::chrono::steady_clock::time_point started_at_{};
    };
}
