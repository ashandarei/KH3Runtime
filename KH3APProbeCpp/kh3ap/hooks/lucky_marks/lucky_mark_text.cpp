#include "kh3ap/hooks/memory/memory_hook_platform.h"
#include "kh3ap/hooks/memory/kh3_memory_hooks.h"
#include "kh3ap/hooks/memory/memory_hook_detail.h"
#include "kh3ap/hooks/lucky_marks/lucky_mark_retirement.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/bridge/utilities.h"
#include <UnrealDef.hpp>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <Unreal/FString.hpp>
#include <algorithm>
#include <array>
#include <cstring>
#include <cwctype>
#include <format>
#include <mutex>
#include <sstream>
#include <utility>

namespace kh3ap::memory_hook_detail
{
    namespace
    {
        constexpr auto max_lucky_mark_text_characters = 2048;
        constexpr auto max_lucky_mark_display_characters = 160;
        constexpr auto max_lucky_mark_text_entries = 4096;
        constexpr auto max_lucky_mark_text_capacity = 8192;

        constexpr int native_item_category_stride = 256;
        constexpr int maximum_native_item_category = 19;
        constexpr int maximum_native_item_index = 255;
        constexpr int synthetic_item_category = 2;
        constexpr int first_synthetic_item_index = 200;
    } // namespace
    auto best_decoded_fname(const DecodedNameValue& decoded) -> std::wstring_view
    {
        if (!decoded.fname_pair.empty() && decoded.fname_pair != STR("None"))
        {
            return decoded.fname_pair;
        }
        return decoded.fname_first;
    }

    auto try_parse_lucky_mark_native_ap_item_id(std::wstring_view reward_token)
        -> std::optional<int32_t>
    {
        constexpr std::wstring_view prefix = STR("ITEM_AP_I");
        if (reward_token.size() < prefix.size() + 5 ||
            reward_token.substr(0, prefix.size()) != prefix)
        {
            return std::nullopt;
        }

        int category = 0;
        int index = 0;
        for (size_t offset = 0; offset < 2; ++offset)
        {
            const wchar_t ch = reward_token[prefix.size() + offset];
            if (!std::iswdigit(ch))
            {
                return std::nullopt;
            }
            category = (category * 10) + (ch - L'0');
        }
        for (size_t offset = 2; offset < 5; ++offset)
        {
            const wchar_t ch = reward_token[prefix.size() + offset];
            if (!std::iswdigit(ch))
            {
                return std::nullopt;
            }
            index = (index * 10) + (ch - L'0');
        }

        if (category <= 0 || category > maximum_native_item_category || index <= 0 ||
            index > maximum_native_item_index)
        {
            return std::nullopt;
        }
        return static_cast<int32_t>((category * native_item_category_stride) + index);
    }

    auto is_lucky_mark_native_ap_synthetic_item_id(int32_t item_id) -> bool
    {
        if (item_id <= 0)
        {
            return false;
        }

        const int category = item_id / native_item_category_stride;
        const int index = item_id % native_item_category_stride;
        return category == synthetic_item_category && index >= first_synthetic_item_index &&
               index <= maximum_native_item_index;
    }

    auto lucky_mark_native_item_row_name_from_item_id(int32_t item_id) -> std::wstring
    {
        const int category = item_id / native_item_category_stride;
        const int index = item_id % native_item_category_stride;
        return std::format(STR("I{:02}{:03}"), category, index);
    }

    namespace
    {
        auto try_read_lucky_mark_native_row_fstring_text(uint64_t address, size_t max_wchars)
            -> std::optional<std::wstring>
        {
            if (address == 0 || max_wchars == 0)
            {
                return std::nullopt;
            }

            std::vector<wchar_t> buffer(max_wchars + 1, L'\0');
            SIZE_T bytes_read = 0;
            const size_t bytes_to_read = max_wchars * sizeof(wchar_t);
            if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address),
                                   buffer.data(), bytes_to_read, &bytes_read) ||
                bytes_read < sizeof(wchar_t))
            {
                return std::nullopt;
            }

            const size_t wchar_count = bytes_read / sizeof(wchar_t);
            size_t end = 0;
            while (end < wchar_count && buffer[end] != L'\0')
            {
                ++end;
            }
            std::wstring value(buffer.data(), end);
            const bool printable =
                std::all_of(value.begin(), value.end(),
                            [](wchar_t ch)
                            {
                                return ch == L'\t' || ch == L'\r' || ch == L'\n' ||
                                       (ch >= L' ' && (ch < 0xD800 || ch > 0xDFFF));
                            });
            if (!printable)
            {
                return std::nullopt;
            }

            return value;
        }
    } // namespace

    auto format_lucky_mark_native_row_fstring(const std::vector<std::byte>& row_bytes,
                                              size_t offset, size_t max_wchars) -> std::wstring
    {
        const auto header = read_lucky_mark_native_row_value<ArrayHeader>(row_bytes, offset);
        if (!header.has_value() || header->data_ptr == 0 || header->num <= 0 ||
            header->max < header->num || header->num > max_lucky_mark_text_characters)
        {
            return STR("");
        }

        const size_t read_wchars = std::min<size_t>(max_wchars, static_cast<size_t>(header->num));
        if (const auto value =
                try_read_lucky_mark_native_row_fstring_text(header->data_ptr, read_wchars);
            value.has_value())
        {
            return *value;
        }

        return std::format(STR("<unreadable ptr=0x{:X} num={} max={}>"), header->data_ptr,
                           header->num, header->max);
    }

    auto try_resolve_lucky_mark_native_ap_display_name(int32_t item_id)
        -> std::optional<std::wstring>
    {
        if (!lucky_mark_native_ap_display_candidate(item_id))
        {
            return std::nullopt;
        }

        const std::wstring row_name = lucky_mark_native_item_row_name_from_item_id(item_id);
        const auto row_bytes = try_find_lucky_mark_native_row_bytes(
            STR("/Game/Load/Tres/TresItemData.TresItemData"), row_name, 160);
        if (!row_bytes.has_value())
        {
            return std::nullopt;
        }

        const auto key_name = read_lucky_mark_native_row_value<RC::Unreal::FName>(*row_bytes, 8);
        if (!key_name.has_value() ||
            !lucky_mark_native_ap_display_row_matches(
                item_id, try_parse_lucky_mark_native_ap_item_id(key_name->ToString())))
        {
            return std::nullopt;
        }

        std::wstring display_name = format_lucky_mark_native_row_fstring(*row_bytes, 24, 128);
        if (display_name.empty() || display_name.starts_with(STR("<unreadable")))
        {
            return std::nullopt;
        }
        if (display_name.size() > max_lucky_mark_display_characters)
        {
            display_name.resize(160);
        }
        return display_name;
    }

    namespace
    {
        auto build_lucky_mark_native_formatter_display_text(std::wstring_view display_name)
            -> std::wstring
        {
            std::wstring formatter_display_text{STR("<icon(CampItem)>")};
            formatter_display_text.append(display_name);
            return formatter_display_text;
        }
    } // namespace

    namespace
    {
        auto try_read_lucky_mark_native_output_fstring_text(void* out_text)
            -> std::optional<std::wstring>
        {
            if (!out_text)
            {
                return std::nullopt;
            }

            const uintptr_t out_text_address = reinterpret_cast<uintptr_t>(out_text);
            const auto data_ptr = try_read_process_u64_local(out_text_address);
            const auto num = try_read_process_i32_local(out_text_address + 0x08);
            const auto max = try_read_process_i32_local(out_text_address + 0x0C);
            if (!data_ptr.has_value() || !num.has_value() || !max.has_value() ||
                !looks_like_process_pointer(*data_ptr) || *num <= 0 || *num > max_lucky_mark_text_entries || *max < *num ||
                *max > max_lucky_mark_text_capacity)
            {
                return std::nullopt;
            }

            return try_read_lucky_mark_native_row_fstring_text(
                *data_ptr, static_cast<size_t>(std::min(*num, 4096)));
        }
    } // namespace

    auto apply_lucky_mark_native_fstring_assignment_display_text(
        void* out_text, std::wstring_view display_name, std::wstring& formatter_display_text)
        -> bool
    {
        formatter_display_text.clear();
        if (!out_text || display_name.empty())
        {
            return false;
        }

        formatter_display_text = build_lucky_mark_native_formatter_display_text(display_name);
        const std::wstring owned_display_text{formatter_display_text};
        auto* const string_value = static_cast<RC::Unreal::FString*>(out_text);
        *string_value = RC::Unreal::FString(owned_display_text.c_str());
        return try_read_lucky_mark_native_output_fstring_text(out_text).value_or(std::wstring{}) ==
               formatter_display_text;
    }
} // namespace kh3ap::memory_hook_detail
