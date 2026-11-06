#pragma once

#include <mutex>
#include <string>
#include <string_view>
#include <unordered_set>

namespace kh3ap
{
    class DiagnosticOnceSet
    {
    public:
        auto check_and_remember(std::wstring key) -> bool;
        [[nodiscard]] auto contains(std::wstring_view key) const -> bool;

    private:
        mutable std::mutex mutex_{};
        std::unordered_set<std::wstring> seen_{};
    };
}
