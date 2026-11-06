#pragma once

// Private decoding/table interfaces shared by the native hook components.
// This does not change hook ownership or the public Kh3MemoryHooks API.
#include "kh3ap/core/types.h"
#include <Unreal/NameTypes.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace RC::Unreal
{
    class UObject;
}
namespace kh3ap::memory_hook_detail
{
    struct LuckyMarkNativeRowMapEntryRaw
    {
        RC::Unreal::FName row_name{};
        uint64_t row_ptr{};
        int64_t padding{};
    };

    struct LuckyMarkNativeRowMapCandidate
    {
        size_t offset{};
        MapHeader header{};
        int readable_prefix_entries{};
        std::wstring entry_bytes{};
    };

    struct LuckyMarkNativeMilestoneRow
    {
        int32_t milestone{};
        uint64_t row_ptr{};
        int32_t reward_index{};
        uint64_t reward_token_name{};
    };
    static_assert(sizeof(LuckyMarkNativeRowMapEntryRaw) == 24,
                  "Unexpected raw DataTable RowMap entry size");

    template <typename T>
    auto read_lucky_mark_native_row_value(const std::vector<std::byte>& row_bytes, size_t offset)
        -> std::optional<T>
    {
        if (offset > row_bytes.size() || row_bytes.size() - offset < sizeof(T))
        {
            return std::nullopt;
        }
        T value{};
        std::memcpy(&value, row_bytes.data() + offset, sizeof(T));
        return value;
    }

    auto try_read_process_u64_local(uintptr_t address) -> std::optional<uint64_t>;

    auto try_read_process_i32_local(uintptr_t address) -> std::optional<int32_t>;

    auto read_lucky_mark_native_row_map_entry(const MapHeader& header, int index)
        -> std::optional<LuckyMarkNativeRowMapEntryRaw>;

    auto collect_lucky_mark_native_row_map_candidates(RC::Unreal::UObject* table_object)
        -> std::vector<LuckyMarkNativeRowMapCandidate>;

    auto select_lucky_mark_native_row_map_candidate(
        const std::vector<LuckyMarkNativeRowMapCandidate>& candidates)
        -> std::optional<LuckyMarkNativeRowMapCandidate>;

    auto summarize_lucky_mark_native_row_map_candidates(
        const std::vector<LuckyMarkNativeRowMapCandidate>& candidates) -> std::wstring;

    auto read_lucky_mark_native_row_bytes(uint64_t row_ptr, size_t size)
        -> std::optional<std::vector<std::byte>>;

    auto collect_lucky_mark_native_milestone_rows()
        -> std::optional<std::vector<LuckyMarkNativeMilestoneRow>>;

    auto write_lucky_mark_native_retired_alias(uint64_t row_ptr) -> bool;

    auto try_find_lucky_mark_native_row_bytes(std::wstring_view table_path,
                                              std::wstring_view row_name, size_t row_read_size)
        -> std::optional<std::vector<std::byte>>;

    auto best_decoded_fname(const DecodedNameValue& decoded) -> std::wstring_view;

    auto try_parse_lucky_mark_native_ap_item_id(std::wstring_view reward_token)
        -> std::optional<int32_t>;

    auto is_lucky_mark_native_ap_synthetic_item_id(int32_t item_id) -> bool;

    auto lucky_mark_native_item_row_name_from_item_id(int32_t item_id) -> std::wstring;

    auto format_lucky_mark_native_row_fstring(const std::vector<std::byte>& row_bytes,
                                              size_t offset, size_t max_wchars = 160)
        -> std::wstring;

    auto try_resolve_lucky_mark_native_ap_display_name(int32_t item_id)
        -> std::optional<std::wstring>;

    auto apply_lucky_mark_native_fstring_assignment_display_text(
        void* out_text, std::wstring_view display_name, std::wstring& formatter_display_text)
        -> bool;

    auto decoded_name_contains_interest(const DecodedNameValue& decoded) -> bool;

    auto format_decoded_name(const DecodedNameValue& decoded) -> std::wstring;

    auto format_hex_pointer(uintptr_t value) -> std::string;

    auto describe_name_candidates(uintptr_t base, std::initializer_list<uintptr_t> offsets)
        -> std::wstring;

    auto describe_qword_candidates(uintptr_t base, std::initializer_list<uintptr_t> offsets)
        -> std::wstring;

    auto describe_lucky_mark_native_ftext_data(void* out_text) -> std::wstring;

    auto format_lucky_mark_native_item_row(std::wstring_view row_name, uint64_t row_ptr,
                                           const std::vector<std::byte>& row_bytes) -> std::wstring;

    auto format_lucky_mark_native_milestone_row(std::wstring_view row_name, uint64_t row_ptr,
                                                const std::vector<std::byte>& row_bytes)
        -> std::wstring;
} // namespace kh3ap::memory_hook_detail
