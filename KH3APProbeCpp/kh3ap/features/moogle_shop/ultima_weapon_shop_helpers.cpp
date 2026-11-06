#include "kh3ap/features/moogle_shop/ultima_weapon_shop_helpers.h"

#include "kh3ap/features/grants/abilities/ability_snapshot_helpers.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/types.h"

#include <Unreal/FProperty.hpp>
#include <Unreal/NameTypes.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UScriptStruct.hpp>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kh3ap
{
    namespace
    {
        constexpr size_t data_table_preferred_row_map_offset = 0x48;
        constexpr size_t data_table_row_map_scan_begin = 0x20;
        constexpr size_t data_table_row_map_scan_end = 0x180;
        constexpr auto data_table_row_map_candidate_count =
            (data_table_row_map_scan_end - data_table_row_map_scan_begin) / sizeof(uintptr_t) + 1;

        constexpr auto max_ultima_row_map_entries = 12000;
        constexpr auto native_shop_row_map_entry_bytes = 24;
        constexpr auto max_shop_table_rows = 12000;
        constexpr auto max_shop_table_capacity = 20000;
        constexpr auto max_shop_level_property_offset = 256;
        constexpr auto max_ultima_shop_level = 99;
    } // namespace

    using namespace RC;
    using namespace RC::Unreal;

    namespace
    {
        constexpr std::wstring_view s_item_data_table_path =
            STR("/Game/Load/Tres/TresItemData.TresItemData");
        constexpr std::wstring_view s_item_data_row_struct_path =
            STR("/Script/TresGame.TresItemDataTable");
        constexpr std::wstring_view s_ultima_weapon_row_name = STR("I03015");

        struct RowMapEntryRaw
        {
            FName row_name{};
            uint64_t row_ptr{};
            int64_t padding{};
        };

        static_assert(sizeof(RowMapEntryRaw) == native_shop_row_map_entry_bytes,
                      "Unexpected raw DataTable RowMap entry size");

        auto fail(std::wstring_view reason, std::wstring* failure_details) -> bool
        {
            if (failure_details)
            {
                *failure_details = reason;
            }
            return false;
        }

        template <typename T> auto read_process_value(uint64_t address) -> std::optional<T>
        {
            const auto bytes = try_read_process_memory_bytes(address, sizeof(T));
            if (!bytes.has_value() || bytes->size() != sizeof(T))
            {
                return std::nullopt;
            }
            T value{};
            std::memcpy(&value, bytes->data(), sizeof(value));
            return value;
        }

        auto plausible_row_map(const MapHeader& header) -> bool
        {
            return looks_like_process_pointer(header.data_ptr) && header.num > 0 &&
                   header.num <= max_shop_table_rows && header.max >= header.num &&
                   header.max <= max_shop_table_capacity;
        }

        auto find_row_in_header(const MapHeader& header, std::wstring_view row_name) -> uint64_t
        {
            if (!plausible_row_map(header))
            {
                return 0;
            }
            const int limit = std::min(header.num, max_ultima_row_map_entries);
            for (int index = 0; index < limit; ++index)
            {
                const auto entry = read_process_value<RowMapEntryRaw>(
                    header.data_ptr + static_cast<uint64_t>(index) * sizeof(RowMapEntryRaw));
                if (!entry.has_value() || !looks_like_process_pointer(entry->row_ptr))
                {
                    continue;
                }
                if (entry->row_name.ToString() == row_name)
                {
                    return entry->row_ptr;
                }
            }
            return 0;
        }

        auto find_ultima_weapon_row(UObject* table) -> uint64_t
        {
            if (!table || !kh3ap::is_uobject_valid_fast(table))
            {
                return 0;
            }

            constexpr std::array<size_t, data_table_row_map_candidate_count> offsets = []
            {
                std::array<size_t, data_table_row_map_candidate_count> values{};
                values[0] = data_table_preferred_row_map_offset;
                size_t output_index = 1;
                for (size_t offset = data_table_row_map_scan_begin;
                     offset <= data_table_row_map_scan_end; offset += sizeof(uintptr_t))
                {
                    if (offset != data_table_preferred_row_map_offset)
                    {
                        values[output_index++] = offset;
                    }
                }
                return values;
            }();

            for (const size_t offset : offsets)
            {
                const auto header =
                    read_process_value<MapHeader>(reinterpret_cast<uint64_t>(table) + offset);
                if (!header.has_value())
                {
                    continue;
                }
                if (const uint64_t row = find_row_in_header(*header, s_ultima_weapon_row_name);
                    row != 0)
                {
                    return row;
                }
            }
            return 0;
        }

        auto write_int32(uint64_t address, int32_t value) -> bool
        {
            auto* const destination = reinterpret_cast<void*>(address);
            DWORD old_protection = 0;
            const bool changed_protection =
                VirtualProtect(destination, sizeof(value), PAGE_READWRITE, &old_protection) != 0;
            if (!changed_protection)
            {
                return false;
            }
            std::memcpy(destination, &value, sizeof(value));
            DWORD ignored = 0;
            VirtualProtect(destination, sizeof(value), old_protection, &ignored);
            return read_process_value<int32_t>(address).value_or(-1) == value;
        }

        auto resolve_shop_level_address(UObject* table, UltimaWeaponShopRowCache& cache,
                                        std::wstring* failure_details) -> uint64_t
        {
            if (cache.item_data_table == table && looks_like_process_pointer(cache.row_address) &&
                cache.shop_level_offset >= 0 &&
                cache.shop_level_offset <= max_shop_level_property_offset)
            {
                return cache.row_address + static_cast<uint64_t>(cache.shop_level_offset);
            }

            cache.reset();
            const uint64_t row = find_ultima_weapon_row(table);
            if (!looks_like_process_pointer(row))
            {
                fail(STR("ultima_weapon_item_data_row_unavailable"), failure_details);
                return 0;
            }

            UObject* const row_struct_object = UObjectGlobals::StaticFindObject<UObject*>(
                nullptr, nullptr, std::wstring{s_item_data_row_struct_path});
            if (!row_struct_object || !kh3ap::is_uobject_valid_fast(row_struct_object) ||
                !row_struct_object->GetClassPrivate() ||
                row_struct_object->GetClassPrivate()->GetName() != STR("ScriptStruct"))
            {
                fail(STR("ultima_weapon_item_data_row_struct_unavailable"), failure_details);
                return 0;
            }
            auto* const row_struct = static_cast<UScriptStruct*>(row_struct_object);
            FProperty* const shop_level_property =
                row_struct->GetPropertyByNameInChain(STR("m_ShopLevel"));
            if (!shop_level_property ||
                shop_level_property->GetClass().GetName() != STR("IntProperty") ||
                shop_level_property->GetOffset_Internal() < 0 ||
                shop_level_property->GetOffset_Internal() > max_shop_level_property_offset)
            {
                fail(STR("ultima_weapon_shop_level_property_unavailable"), failure_details);
                return 0;
            }

            cache.item_data_table = table;
            cache.row_address = row;
            cache.shop_level_offset = shop_level_property->GetOffset_Internal();
            return row + static_cast<uint64_t>(cache.shop_level_offset);
        }
    } // namespace

    auto try_update_ultima_weapon_shop_level(int desired_shop_level,
                                             UltimaWeaponShopRowCache& cache,
                                             UltimaWeaponShopLevelUpdateResult* result,
                                             std::wstring* failure_details) -> bool
    {
        if (!result)
        {
            return fail(STR("ultima_weapon_shop_update_result_missing"), failure_details);
        }
        *result = UltimaWeaponShopLevelUpdateResult{};
        if (failure_details)
        {
            failure_details->clear();
        }
        if (desired_shop_level < 0 || desired_shop_level > max_ultima_shop_level)
        {
            return fail(STR("ultima_weapon_shop_level_out_of_range"), failure_details);
        }

        UObject* const table = UObjectGlobals::StaticFindObject<UObject*>(
            nullptr, nullptr, std::wstring{s_item_data_table_path});
        if (!table || !kh3ap::is_uobject_valid_fast(table))
        {
            cache.reset();
            return fail(STR("ultima_weapon_item_data_table_unavailable"), failure_details);
        }
        uint64_t shop_level_address = resolve_shop_level_address(table, cache, failure_details);
        if (shop_level_address == 0)
        {
            return false;
        }
        auto current_shop_level = read_process_value<int32_t>(shop_level_address);
        if (!current_shop_level.has_value() && cache.item_data_table == table)
        {
            // A streamed/reloaded DataTable may invalidate the cached row while
            // retaining the same UObject address. Resolve it once more before
            // deferring the event for a later retry.
            cache.reset();
            shop_level_address = resolve_shop_level_address(table, cache, failure_details);
            current_shop_level = shop_level_address != 0
                                     ? read_process_value<int32_t>(shop_level_address)
                                     : std::nullopt;
        }
        if (!current_shop_level.has_value())
        {
            return fail(STR("ultima_weapon_shop_level_unreadable"), failure_details);
        }

        UltimaWeaponShopLevelUpdateResult update{};
        update.previous_shop_level = *current_shop_level;
        update.desired_shop_level = desired_shop_level;
        update.changed = *current_shop_level != desired_shop_level;
        if (update.changed && !write_int32(shop_level_address, desired_shop_level))
        {
            return fail(STR("ultima_weapon_shop_level_write_failed"), failure_details);
        }
        *result = update;
        return true;
    }
} // namespace kh3ap
