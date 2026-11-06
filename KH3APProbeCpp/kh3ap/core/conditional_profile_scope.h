#pragma once

#include "kh3ap/core/reflection/reflection_core.h"

#include <string_view>

namespace kh3ap
{
    template <bool Enabled>
    class RuntimeConditionalProfileScope;

    template <>
    class RuntimeConditionalProfileScope<false>
    {
    public:
        explicit RuntimeConditionalProfileScope(std::wstring_view)
        {
        }
    };

    template <>
    class RuntimeConditionalProfileScope<true>
    {
    public:
        explicit RuntimeConditionalProfileScope(std::wstring_view scope_name)
            : scope_(scope_name)
        {
        }

    private:
        RuntimeHotPathProfileScope scope_;
    };
}
