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
#include "kh3ap/hooks/lucky_marks/lucky_mark_retirement.h"
#include <Unreal/UObjectGlobals.hpp>
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
        constexpr auto max_lucky_mark_row_map_offset = 0x400;
        constexpr auto first_lucky_mark_row_map_candidate = 0x20;
        constexpr auto last_lucky_mark_row_map_candidate = 0x180;
        constexpr auto lucky_mark_row_map_candidate_stride = 0x8;

        constexpr int maximum_table_rows = 12000;
        constexpr int maximum_table_capacity = 20000;
        constexpr size_t maximum_row_read_bytes = 512;
    } // namespace
    namespace
    {
        auto is_plausible_lucky_mark_native_row_map_header(const MapHeader& header) -> bool
        {
            return header.data_ptr != 0 && looks_like_process_pointer(header.data_ptr) &&
                   header.num > 0 && header.num <= maximum_table_rows && header.max >= header.num &&
                   header.max <= maximum_table_capacity;
        }
    } // namespace

    namespace
    {
        auto read_lucky_mark_native_row_map_header_at(RC::Unreal::UObject* table_object,
                                                      size_t offset) -> std::optional<MapHeader>
        {
            if (!table_object || !kh3ap::is_uobject_valid_fast(table_object) || offset > max_lucky_mark_row_map_offset)
            {
                return std::nullopt;
            }

            MapHeader header{};
            SIZE_T bytes_read = 0;
            const auto address = reinterpret_cast<uintptr_t>(table_object) + offset;
            if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address),
                                   &header, sizeof(header), &bytes_read) ||
                bytes_read != sizeof(header) ||
                !is_plausible_lucky_mark_native_row_map_header(header) ||
                !try_probe_pointer_bytes(header.data_ptr, sizeof(LuckyMarkNativeRowMapEntryRaw))
                     .has_value())
            {
                return std::nullopt;
            }
            return header;
        }
    } // namespace

    auto read_lucky_mark_native_row_map_entry(const MapHeader& header, int index)
        -> std::optional<LuckyMarkNativeRowMapEntryRaw>
    {
        if (index < 0 || index >= header.num || !looks_like_process_pointer(header.data_ptr))
        {
            return std::nullopt;
        }

        LuckyMarkNativeRowMapEntryRaw entry{};
        SIZE_T bytes_read = 0;
        const uint64_t address =
            header.data_ptr + static_cast<uint64_t>(index) * sizeof(LuckyMarkNativeRowMapEntryRaw);
        if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address), &entry,
                               sizeof(entry), &bytes_read) ||
            bytes_read != sizeof(entry) || !looks_like_process_pointer(entry.row_ptr))
        {
            return std::nullopt;
        }
        return entry;
    }

    namespace
    {
        auto count_lucky_mark_native_readable_prefix_entries(const MapHeader& header) -> int
        {
            if (!is_plausible_lucky_mark_native_row_map_header(header))
            {
                return 0;
            }

            int readable = 0;
            const int limit = std::min(header.num, 12);
            for (int index = 0; index < limit; ++index)
            {
                const auto entry = read_lucky_mark_native_row_map_entry(header, index);
                if (entry.has_value() && looks_like_process_pointer(entry->row_ptr) &&
                    try_probe_pointer_bytes(entry->row_ptr, 8).has_value())
                {
                    ++readable;
                }
            }
            return readable;
        }
    } // namespace

    auto collect_lucky_mark_native_row_map_candidates(RC::Unreal::UObject* table_object)
        -> std::vector<LuckyMarkNativeRowMapCandidate>
    {
        std::vector<LuckyMarkNativeRowMapCandidate> candidates{};
        if (!table_object || !kh3ap::is_uobject_valid_fast(table_object))
        {
            return candidates;
        }

        for (size_t offset = first_lucky_mark_row_map_candidate; offset <= last_lucky_mark_row_map_candidate; offset += lucky_mark_row_map_candidate_stride)
        {
            const auto header = read_lucky_mark_native_row_map_header_at(table_object, offset);
            if (!header.has_value())
            {
                continue;
            }

            candidates.push_back(LuckyMarkNativeRowMapCandidate{
                .offset = offset,
                .header = *header,
                .readable_prefix_entries = count_lucky_mark_native_readable_prefix_entries(*header),
                .entry_bytes =
                    try_probe_pointer_bytes(
                        header->data_ptr,
                        std::min<size_t>(96, static_cast<size_t>(std::max(header->num, 1)) *
                                                 sizeof(LuckyMarkNativeRowMapEntryRaw)))
                        .value_or(STR("<unreadable>"))});
        }
        return candidates;
    }

    auto select_lucky_mark_native_row_map_candidate(
        const std::vector<LuckyMarkNativeRowMapCandidate>& candidates)
        -> std::optional<LuckyMarkNativeRowMapCandidate>
    {
        if (candidates.empty())
        {
            return std::nullopt;
        }

        const auto preferred = std::find_if(candidates.begin(), candidates.end(),
                                            [](const LuckyMarkNativeRowMapCandidate& candidate)
                                            {
                                                return candidate.offset == 0x48 &&
                                                       candidate.readable_prefix_entries > 0;
                                            });
        if (preferred != candidates.end())
        {
            return *preferred;
        }

        const auto best = std::max_element(
            candidates.begin(), candidates.end(),
            [](const LuckyMarkNativeRowMapCandidate& left,
               const LuckyMarkNativeRowMapCandidate& right)
            {
                if (left.readable_prefix_entries != right.readable_prefix_entries)
                {
                    return left.readable_prefix_entries < right.readable_prefix_entries;
                }
                return left.offset > right.offset;
            });
        if (best != candidates.end() && best->readable_prefix_entries > 0)
        {
            return *best;
        }
        return std::nullopt;
    }

    auto summarize_lucky_mark_native_row_map_candidates(
        const std::vector<LuckyMarkNativeRowMapCandidate>& candidates) -> std::wstring
    {
        if (candidates.empty())
        {
            return STR("<none>");
        }

        std::wostringstream stream;
        bool first = true;
        for (const auto& candidate : candidates)
        {
            if (!first)
            {
                stream << STR(" ; ");
            }
            first = false;
            stream << STR("offset=0x") << std::hex << std::uppercase << candidate.offset << std::dec
                   << STR(" ptr=0x") << std::hex << std::uppercase << candidate.header.data_ptr
                   << std::dec << STR(" num=") << candidate.header.num << STR(" max=")
                   << candidate.header.max << STR(" readable_prefix_entries=")
                   << candidate.readable_prefix_entries << STR(" entry_bytes=")
                   << candidate.entry_bytes;
        }
        return stream.str();
    }

    auto read_lucky_mark_native_row_bytes(uint64_t row_ptr, size_t size)
        -> std::optional<std::vector<std::byte>>
    {
        if (!looks_like_process_pointer(row_ptr) || size == 0 || size > maximum_row_read_bytes)
        {
            return std::nullopt;
        }

        std::vector<std::byte> bytes(size);
        SIZE_T bytes_read = 0;
        if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(row_ptr),
                               bytes.data(), bytes.size(), &bytes_read) ||
            bytes_read < std::min<size_t>(bytes.size(), 8))
        {
            return std::nullopt;
        }
        if (bytes_read < bytes.size())
        {
            bytes.resize(bytes_read);
        }
        return bytes;
    }

    auto collect_lucky_mark_native_milestone_rows()
        -> std::optional<std::vector<LuckyMarkNativeMilestoneRow>>
    {
        constexpr std::wstring_view table_path = STR(
            "/Game/Load/Tres/TresLuckyMarkMilestoneRewardData.TresLuckyMarkMilestoneRewardData");
        constexpr size_t row_map_offset = 0x48;
        constexpr int max_lucky_mark_rows = 90;

        RC::Unreal::UObject* const table_object =
            RC::Unreal::UObjectGlobals::StaticFindObject<RC::Unreal::UObject*>(
                nullptr, nullptr, std::wstring{table_path});
        const auto header = read_lucky_mark_native_row_map_header_at(table_object, row_map_offset);
        if (!header.has_value() || header->num <= 0 || header->num > max_lucky_mark_rows)
        {
            return std::nullopt;
        }

        std::vector<LuckyMarkNativeMilestoneRow> rows{};
        rows.reserve(static_cast<size_t>(header->num));
        for (int index = 0; index < header->num; ++index)
        {
            const auto entry = read_lucky_mark_native_row_map_entry(*header, index);
            if (!entry.has_value())
            {
                return std::nullopt;
            }

            const std::wstring row_name = entry->row_name.ToString();
            const auto milestone = try_parse_lucky_mark_milestone_row_name(row_name);
            const auto row_bytes = read_lucky_mark_native_row_bytes(entry->row_ptr, 32);
            if (!milestone.has_value() || !row_bytes.has_value())
            {
                return std::nullopt;
            }

            const auto reward_index = read_lucky_mark_native_row_value<int32_t>(*row_bytes, 8);
            const auto mark_count = read_lucky_mark_native_row_value<int32_t>(*row_bytes, 12);
            const auto reward_token_name =
                read_lucky_mark_native_row_value<uint64_t>(*row_bytes, 24);
            if (!reward_index.has_value() || !mark_count.has_value() ||
                !reward_token_name.has_value() || *mark_count != *milestone || *reward_index < 0 ||
                *reward_index > max_lucky_mark_rows)
            {
                return std::nullopt;
            }

            rows.push_back(LuckyMarkNativeMilestoneRow{.milestone = *milestone,
                                                       .row_ptr = entry->row_ptr,
                                                       .reward_index = *reward_index,
                                                       .reward_token_name = *reward_token_name});
        }
        return rows;
    }

    auto write_lucky_mark_native_retired_alias(uint64_t row_ptr) -> bool
    {
        if (!looks_like_process_pointer(row_ptr))
        {
            return false;
        }

        constexpr int32_t alias_index = lucky_mark_native_retired_alias_index;
        SIZE_T bytes_written = 0;
        const uintptr_t reward_index_address = row_ptr + 8;
        if (!WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(reward_index_address),
                                &alias_index, sizeof(alias_index), &bytes_written) ||
            bytes_written != sizeof(alias_index))
        {
            return false;
        }
        return try_read_process_i32_local(reward_index_address) == alias_index;
    }

    auto try_find_lucky_mark_native_row_bytes(std::wstring_view table_path,
                                              std::wstring_view row_name, size_t row_read_size)
        -> std::optional<std::vector<std::byte>>
    {
        RC::Unreal::UObject* const object =
            RC::Unreal::UObjectGlobals::StaticFindObject<RC::Unreal::UObject*>(
                nullptr, nullptr, std::wstring{table_path});
        const auto candidates = collect_lucky_mark_native_row_map_candidates(object);
        const auto selected_header = select_lucky_mark_native_row_map_candidate(candidates);
        if (!object || !kh3ap::is_uobject_valid_fast(object) || !selected_header.has_value())
        {
            return std::nullopt;
        }

        const MapHeader& header = selected_header->header;
        const int limit = std::min(header.num, maximum_table_rows);
        for (int index = 0; index < limit; ++index)
        {
            const auto entry = read_lucky_mark_native_row_map_entry(header, index);
            if (!entry.has_value())
            {
                continue;
            }
            if (entry->row_name.ToString() != row_name)
            {
                continue;
            }
            return read_lucky_mark_native_row_bytes(entry->row_ptr, row_read_size);
        }
        return std::nullopt;
    }
} // namespace kh3ap::memory_hook_detail
