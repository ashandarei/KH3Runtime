#pragma once

#include <string>
#include <string_view>

namespace kh3ap
{
    auto zero_pad_number(int value, int width) -> std::wstring;
    auto uppercase_ascii(std::wstring_view value) -> std::wstring;
}
