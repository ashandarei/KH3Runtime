#include "kh3ap/core/text_format.h"

#include <algorithm>

namespace kh3ap
{
    auto zero_pad_number(int value, int width) -> std::wstring
    {
        std::wstring text = std::to_wstring(value);
        while (static_cast<int>(text.size()) < width)
        {
            text.insert(text.begin(), L'0');
        }
        return text;
    }

    auto uppercase_ascii(std::wstring_view value) -> std::wstring
    {
        std::wstring result{value};
        std::transform(result.begin(), result.end(), result.begin(), [](wchar_t ch) {
            return (ch >= L'a' && ch <= L'z') ? static_cast<wchar_t>(ch - L'a' + L'A') : ch;
        });
        return result;
    }

}
