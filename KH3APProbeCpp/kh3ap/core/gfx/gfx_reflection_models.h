#pragma once

#include <string_view>

namespace kh3ap
{
    struct GfxProbeFunctionPaths
    {
        std::wstring_view get_member_value_path{};
        std::wstring_view invoke_path{};
        std::wstring_view set_member_value_path{};
    };
}
