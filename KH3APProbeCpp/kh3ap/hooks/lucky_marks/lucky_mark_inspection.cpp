#include <optional>
#include "kh3ap/hooks/memory/memory_hook_platform.h"
#include "kh3ap/hooks/memory/kh3_memory_hooks.h"
#include "kh3ap/hooks/memory/memory_hook_detail.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/bridge/utilities.h"
#include <UnrealDef.hpp>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
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
        constexpr auto lucky_text_preview_chars = 180;
        constexpr auto lucky_native_data_probe_bytes = 0x120;
        constexpr auto lucky_native_description_chars = 7000;
        constexpr auto native_fstring_header_bytes = 0x10;
        constexpr auto lucky_item_row_key_offset = 8;
        constexpr auto lucky_item_row_category_offset = 16;
        constexpr auto lucky_item_row_comment_offset = 24;
        constexpr auto lucky_item_row_name_id_offset = 80;
        constexpr auto lucky_item_row_help_id_offset = 96;
        constexpr auto lucky_item_comment_preview_chars = 96;
        constexpr auto lucky_item_text_id_preview_chars = 160;
        constexpr auto lucky_item_row_preview_bytes = 96;
        constexpr auto lucky_ability_row_preview_bytes = 64;
        constexpr std::initializer_list<size_t> lucky_native_qword_probe_offsets{
            0x00, 0x08, 0x10, 0x18, 0x20, 0x28, 0x30, 0x38, 0x40, 0x48, 0x50,
            0x58, 0x60, 0x68, 0x70, 0x78, 0x80, 0x88, 0x90, 0x98, 0xA0, 0xA8,
            0xB0, 0xB8, 0xC0, 0xC8, 0xD0, 0xD8, 0xE0, 0xE8, 0xF0, 0xF8, 0x100};

        constexpr auto max_lucky_mark_child_samples = 12;
        constexpr auto max_inspected_lucky_mark_entries = 512;
        constexpr auto max_inspected_lucky_mark_capacity = 1024;
    } // namespace

    auto decoded_name_contains_interest(const DecodedNameValue& decoded) -> bool
    {
        constexpr std::initializer_list<std::wstring_view> interesting_tokens{
            STR("ITEM_AP"),
            STR("APUP"),
            STR("AP_"),
            STR("KEY_ITEM14"),
            STR("KEY_ITEM15"),
            STR("KEY_ITEM16"),
            STR("WEP_KEYBLADE_SO_013"),
            STR("WEP_KEYBLADE_SO_014"),
            STR("WEP_KEYBLADE_SO_015"),
            STR("WEP_KEYBLADE_SO_018")};
        return contains_any_wide(decoded.fname_first, interesting_tokens) ||
               contains_any_wide(decoded.fname_pair, interesting_tokens);
    }

    auto format_decoded_name(const DecodedNameValue& decoded) -> std::wstring
    {
        std::wostringstream stream;
        stream << STR("low=") << decoded.low32 << STR(" high=") << decoded.high32;
        if (!decoded.fname_first.empty())
        {
            stream << STR(" first=") << decoded.fname_first;
        }
        if (!decoded.fname_pair.empty())
        {
            stream << STR(" pair=") << decoded.fname_pair;
        }
        return stream.str();
    }

    auto format_hex_pointer(uintptr_t value) -> std::string
    {
        std::ostringstream stream;
        stream << "0x" << std::hex << value;
        return stream.str();
    }

    namespace
    {
        auto describe_name_qword(uintptr_t address) -> std::wstring
        {
            const auto raw = try_read_process_u64_local(address);
            if (!raw.has_value())
            {
                return STR("<unreadable>");
            }

            const auto decoded = decode_name_value(*raw);
            std::wostringstream stream;
            stream << STR("raw=0x") << std::hex << *raw << std::dec << STR(" low=") << decoded.low32
                   << STR(" high=") << decoded.high32;
            if (!decoded.fname_first.empty())
            {
                stream << STR(" first=") << decoded.fname_first;
            }
            if (!decoded.fname_pair.empty())
            {
                stream << STR(" pair=") << decoded.fname_pair;
            }
            return stream.str();
        }
    } // namespace

    auto describe_name_candidates(uintptr_t base, std::initializer_list<uintptr_t> offsets)
        -> std::wstring
    {
        if (base == 0)
        {
            return STR("<null>");
        }

        std::wostringstream stream;
        bool first = true;
        for (const uintptr_t offset : offsets)
        {
            if (!first)
            {
                stream << STR(" | ");
            }
            first = false;
            stream << STR("+0x") << std::hex << offset << std::dec << STR(" ")
                   << describe_name_qword(base + offset);
        }
        return stream.str();
    }

    auto describe_qword_candidates(uintptr_t base, std::initializer_list<uintptr_t> offsets)
        -> std::wstring
    {
        if (base == 0)
        {
            return STR("<null>");
        }

        std::wostringstream stream;
        bool first = true;
        for (const uintptr_t offset : offsets)
        {
            if (!first)
            {
                stream << STR(" | ");
            }
            first = false;
            stream << STR("+0x") << std::hex << offset << STR("=");
            const auto raw = try_read_process_u64_local(base + offset);
            if (!raw.has_value())
            {
                stream << STR("<unreadable>");
                continue;
            }

            stream << STR("0x") << *raw << std::dec;
            if (looks_like_process_pointer(*raw))
            {
                if (const auto text = try_read_utf16_string(*raw, 96); text.has_value())
                {
                    stream << STR(" utf16=") << *text;
                }
            }
        }
        return stream.str();
    }

    namespace
    {
        auto describe_lucky_mark_native_fstring_header(uintptr_t address)
            -> std::optional<std::wstring>
        {
            const auto data_ptr = try_read_process_u64_local(address);
            const auto num = try_read_process_i32_local(address + 0x08);
            const auto max = try_read_process_i32_local(address + 0x0C);
            if (!data_ptr.has_value() || !num.has_value() || !max.has_value() ||
                !looks_like_process_pointer(*data_ptr) || *num <= 0 ||
                *num > max_inspected_lucky_mark_entries || *max < *num ||
                *max > max_inspected_lucky_mark_capacity)
            {
                return std::nullopt;
            }

            const auto text =
                try_read_utf16_string(*data_ptr, static_cast<size_t>(std::min(*num, 512)));
            if (!text.has_value() || text->empty())
            {
                return std::nullopt;
            }

            std::wostringstream stream;
            stream << STR("addr=0x") << std::hex << std::uppercase << address << STR(" ptr=0x")
                   << *data_ptr << std::dec << STR(" num=") << *num << STR(" max=") << *max
                   << STR(" text=") << truncate_for_log(*text, lucky_text_preview_chars);
            return stream.str();
        }
    } // namespace

    namespace
    {
        auto describe_lucky_mark_native_fstring_candidates(uintptr_t base, size_t byte_count)
            -> std::wstring
        {
            if (!looks_like_process_pointer(base))
            {
                return STR("<not_pointer>");
            }

            std::wostringstream stream;
            bool any = false;
            for (size_t offset = 0; offset + native_fstring_header_bytes <= byte_count;
                 offset += sizeof(uint64_t))
            {
                const auto description = describe_lucky_mark_native_fstring_header(base + offset);
                if (!description.has_value())
                {
                    continue;
                }

                if (any)
                {
                    stream << STR(" | ");
                }
                any = true;
                stream << STR("+0x") << std::hex << std::uppercase << offset << std::dec << STR(" ")
                       << *description;
            }
            return any ? stream.str() : std::wstring{STR("<none>")};
        }
    } // namespace

    auto describe_lucky_mark_native_ftext_data(void* out_text) -> std::wstring
    {
        const uintptr_t out_text_address = reinterpret_cast<uintptr_t>(out_text);
        if (out_text_address == 0)
        {
            return STR("<null>");
        }

        const auto data_ptr = try_read_process_u64_local(out_text_address);
        if (!data_ptr.has_value() || !looks_like_process_pointer(*data_ptr))
        {
            std::wostringstream stream;
            stream << STR("data_ptr=")
                   << (data_ptr.has_value() ? std::format(STR("0x{:X}"), *data_ptr)
                                            : std::wstring{STR("<unreadable>")});
            return stream.str();
        }

        static constexpr std::array<uintptr_t, 33> data_offsets{
            0x00, 0x08, 0x10, 0x18, 0x20, 0x28, 0x30, 0x38, 0x40, 0x48, 0x50,
            0x58, 0x60, 0x68, 0x70, 0x78, 0x80, 0x88, 0x90, 0x98, 0xA0, 0xA8,
            0xB0, 0xB8, 0xC0, 0xC8, 0xD0, 0xD8, 0xE0, 0xE8, 0xF0, 0xF8, 0x100};

        std::wostringstream child_stream;
        int child_count = 0;
        for (const uintptr_t offset : data_offsets)
        {
            const auto child_ptr = try_read_process_u64_local(*data_ptr + offset);
            if (!child_ptr.has_value() || !looks_like_process_pointer(*child_ptr))
            {
                continue;
            }

            const std::wstring direct_text =
                try_read_utf16_string(*child_ptr, 160).value_or(std::wstring{});
            const std::wstring child_fstrings =
                describe_lucky_mark_native_fstring_candidates(*child_ptr, 0x80);
            if (direct_text.empty() && child_fstrings == STR("<none>"))
            {
                continue;
            }

            if (child_count > 0)
            {
                child_stream << STR(" | ");
            }
            child_stream << STR("data+0x") << std::hex << std::uppercase << offset << STR(" -> 0x")
                         << *child_ptr << std::dec;
            if (!direct_text.empty())
            {
                child_stream << STR(" direct_utf16=")
                             << truncate_for_log(direct_text, lucky_text_preview_chars);
            }
            if (child_fstrings != STR("<none>"))
            {
                child_stream << STR(" fstrings=[") << child_fstrings << STR("]");
            }
            ++child_count;
            if (child_count >= max_lucky_mark_child_samples)
            {
                break;
            }
        }

        const std::wstring child_summary =
            child_count > 0 ? child_stream.str() : std::wstring{STR("<none>")};

        std::wostringstream stream;
        stream << STR("out_text=0x") << std::hex << std::uppercase << out_text_address
               << STR(" data_ptr=0x") << *data_ptr << std::dec << STR(" data_head=")
               << try_probe_pointer_bytes(*data_ptr, lucky_native_data_probe_bytes)
                      .value_or(STR("<unreadable>"))
               << STR(" data_qwords=")
               << describe_qword_candidates(*data_ptr, lucky_native_qword_probe_offsets)
               << STR(" data_fstrings=")
               << describe_lucky_mark_native_fstring_candidates(*data_ptr,
                                                                lucky_native_data_probe_bytes)
               << STR(" child_text_candidates=") << child_summary;
        return truncate_for_log(stream.str(), lucky_native_description_chars);
    }

    auto format_lucky_mark_native_item_row(std::wstring_view row_name, uint64_t row_ptr,
                                           const std::vector<std::byte>& row_bytes) -> std::wstring
    {
        const auto key_name = read_lucky_mark_native_row_value<RC::Unreal::FName>(
            row_bytes, lucky_item_row_key_offset);
        const uint64_t category =
            row_bytes.size() > lucky_item_row_category_offset
                ? static_cast<uint64_t>(row_bytes[lucky_item_row_category_offset])
                : 0;
        const std::wstring comment = format_lucky_mark_native_row_fstring(
            row_bytes, lucky_item_row_comment_offset, lucky_item_comment_preview_chars);
        const std::wstring name_text_id = format_lucky_mark_native_row_fstring(
            row_bytes, lucky_item_row_name_id_offset, lucky_item_text_id_preview_chars);
        const std::wstring help_text_id = format_lucky_mark_native_row_fstring(
            row_bytes, lucky_item_row_help_id_offset, lucky_item_text_id_preview_chars);
        const auto max_num = read_lucky_mark_native_row_value<int32_t>(row_bytes, 140).value_or(-1);
        return std::format(
            STR("row={} row_ptr=0x{:X} m_KeyName={} m_Category={} m_Comment={} m_NameTextID={} "
                "m_HelpTextID={} m_MaxNum={} raw={}"),
            row_name, row_ptr,
            key_name.has_value() ? key_name->ToString() : std::wstring{STR("<missing>")}, category,
            comment, name_text_id, help_text_id, max_num,
            format_bytes_hex(row_bytes.data(),
                             std::min<size_t>(row_bytes.size(), lucky_item_row_preview_bytes)));
    }

    auto format_lucky_mark_native_milestone_row(std::wstring_view row_name, uint64_t row_ptr,
                                                const std::vector<std::byte>& row_bytes)
        -> std::wstring
    {
        const auto reward_index =
            read_lucky_mark_native_row_value<int32_t>(row_bytes, 8).value_or(-1);
        const auto mark_count =
            read_lucky_mark_native_row_value<int32_t>(row_bytes, 12).value_or(-1);
        const auto complete_flag =
            read_lucky_mark_native_row_value<uint8_t>(row_bytes, 16).value_or(0);
        const auto reward_name = read_lucky_mark_native_row_value<RC::Unreal::FName>(row_bytes, 24);
        return std::format(
            STR("row={} row_ptr=0x{:X} RewardIndex={} MarkCount={} CompleteRewardFlag={} "
                "RewardTreasureName={} raw={}"),
            row_name, row_ptr, reward_index, mark_count, complete_flag ? STR("true") : STR("false"),
            reward_name.has_value() ? reward_name->ToString() : std::wstring{STR("<missing>")},
            format_bytes_hex(row_bytes.data(),
                             std::min<size_t>(row_bytes.size(), lucky_ability_row_preview_bytes)));
    }
} // namespace kh3ap::memory_hook_detail
