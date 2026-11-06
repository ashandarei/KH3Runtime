#include "kh3ap/diagnostics/output/diagnostic_once_set.h"

#include <utility>

namespace kh3ap
{
    auto DiagnosticOnceSet::check_and_remember(std::wstring key) -> bool
    {
        std::scoped_lock lock{mutex_};
        if (seen_.contains(key))
        {
            return false;
        }
        seen_.insert(std::move(key));
        return true;
    }

    auto DiagnosticOnceSet::contains(std::wstring_view key) const -> bool
    {
        std::scoped_lock lock{mutex_};
        return seen_.contains(std::wstring{key});
    }
}
