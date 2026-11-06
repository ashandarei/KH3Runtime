#include "kh3ap/diagnostics/grants/manual_keyblade_enhancement_probe_helpers.h"

#include "kh3ap/bridge/serialization/json_fields.h"
#include "kh3ap/features/grants/abilities/ability_snapshot_helpers.h"
#include "kh3ap/bridge/transport/bridge_io_helpers.h"
#include "kh3ap/features/grants/keyblades/keyblade_enhancement_profile.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/types.h"
#include "kh3ap/bridge/utilities.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <UnrealDef.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace kh3ap
{
    using namespace RC;
    using namespace RC::Unreal;

    auto make_manual_keyblade_enhancement_probe_deps(
        std::optional<std::filesystem::path> bridge_dir, std::wstring_view request_filename,
        std::wstring_view result_filename, std::wstring_view events_filename,
        std::wstring_view protocol_name, int64_t protocol_version,
        std::wstring_view runtime_context_summary, std::wstring_view build_stamp,
        std::wstring_view data_table_path, std::wstring_view item_manager_path,
        std::wstring_view checker_default_object_path, std::wstring_view enhance_test_function_path)
        -> ManualKeybladeEnhancementProbeDeps
    {
        return {std::move(bridge_dir),
                request_filename,
                result_filename,
                events_filename,
                protocol_name,
                protocol_version,
                runtime_context_summary,
                build_stamp,
                data_table_path,
                item_manager_path,
                checker_default_object_path,
                enhance_test_function_path};
    }

    namespace
    {
        constexpr auto keyblade_probe_hash_initial_state = 1469598103934665603ull;
        constexpr auto keyblade_probe_hash_prime = 1099511628211ull;
        constexpr size_t s_weapon_enhance_row_read_size = 0x88;
        constexpr size_t s_flag_index_offset = 0x68;
        constexpr size_t s_weapon_id_offset = 0x6C;
        constexpr size_t s_init_achieved_offset = 0x70;
        constexpr size_t s_weapon_level_offset = 0x74;
        constexpr size_t s_attack_plus_offset = 0x78;
        constexpr size_t s_magic_plus_offset = 0x7C;
        constexpr size_t s_append_ability_offset = 0x80;
        constexpr size_t s_enhanced_equip_ap_offset = 0x20;
        constexpr size_t s_enhanced_equip_attack_offset = 0x24;
        constexpr size_t s_enhanced_equip_magic_offset = 0x28;
        constexpr size_t s_enhanced_equip_defense_offset = 0x2C;
        constexpr size_t s_save_accessor_global_instruction_size = 13;
        constexpr size_t s_save_accessor_global_displacement_offset = 9;
        constexpr size_t s_save_accessor_game_instance_offset = 0x230;
        constexpr size_t s_save_accessor_save_owner_offset = 0x50;
        constexpr size_t s_save_data_offset = 0x870;
        constexpr size_t s_enhance_flag_word_count = 8;
        constexpr int s_enhance_flag_bits_per_word = 32;
        constexpr int s_max_enhance_flag_index = 255;
        constexpr size_t s_row_map_entry_size = 24;
        constexpr int s_max_row_map_entries = 12000;
        constexpr int s_max_row_map_capacity = 20000;
        constexpr int s_row_map_readability_sample_entries = 12;
        constexpr size_t s_row_map_readability_probe_bytes = 8;
        constexpr size_t s_row_map_scan_begin = 0x20;
        constexpr size_t s_row_map_scan_end = 0x180;
        constexpr size_t s_row_map_scan_stride = 0x8;
        constexpr size_t s_preferred_row_map_offset = 0x48;
        constexpr int s_primary_weapon_row_end_exclusive = 130;
        constexpr int s_secondary_weapon_row_begin = 150;
        constexpr int s_secondary_weapon_row_end_exclusive = 190;
        constexpr uint8_t s_unreadable_weapon_row_enum = 0xFF;
        constexpr int s_max_enhanced_buffer_entries = 512;
        constexpr int s_max_enhanced_buffer_element_bytes = 1024;
        constexpr size_t s_max_enhanced_buffer_sample_bytes = 4096;
        constexpr size_t s_enhanced_buffer_log_prefix_bytes = 160;
        constexpr size_t s_enhanced_record_log_prefix_bytes = 0x60;

        struct RowMapEntryRaw
        {
            FName row_name{};
            uint64_t row_ptr{};
            int64_t padding{};
        };

        static_assert(sizeof(RowMapEntryRaw) == s_row_map_entry_size,
                      "Unexpected raw DataTable RowMap entry size");

        struct RowMapCandidate
        {
            size_t offset{};
            MapHeader header{};
            int readable_prefix_entries{};
        };

        struct WeaponEnhanceRowSnapshot
        {
            bool table_found{false};
            bool row_map_found{false};
            bool row_found{false};
            std::wstring table_object{};
            size_t row_map_offset{};
            int32_t row_map_num{};
            int32_t row_map_max{};
            uint64_t row_ptr{};
            int32_t flag_index{};
            uint8_t weapon_id{};
            bool init_achieved{false};
            int32_t weapon_level{};
            int32_t attack_plus{};
            int32_t magic_plus{};
            uint8_t append_ability{};
            std::wstring raw_bytes{};
        };

        struct EnhancedBufferSnapshot
        {
            bool manager_found{false};
            bool property_found{false};
            std::wstring manager_object{};
            int32_t inner_element_size{};
            ArrayHeader header{};
            size_t sampled_byte_count{};
            uint64_t sampled_hash{};
            std::wstring sampled_prefix{};
            struct Record
            {
                int32_t index{};
                std::wstring key_name{};
                int32_t ap{};
                int32_t attack{};
                int32_t magic{};
                int32_t defense{};
                std::wstring raw_prefix{};
            };
            std::vector<Record> records{};
        };

        struct NativeFlagBankSnapshot
        {
            bool module_found{false};
            bool get_save_owner_signature_valid{false};
            bool flag_write_signature_valid{false};
            bool save_owner_found{false};
            bool flag_storage_readable{false};
            bool requested_flag_valid{false};
            bool requested_flag_set{false};
            uint64_t module_base{};
            const NativeKeybladeEnhancementProfile* profile{};
            uint64_t save_owner{};
            uint64_t save_data{};
            uint64_t setter_owner{};
            uint64_t flag_words{};
            int32_t requested_flag_index{-1};
            std::array<uint32_t, s_enhance_flag_word_count> words{};
            std::wstring get_save_owner_prefix{};
            std::wstring flag_write_prefix{};
            std::wstring error{};
        };

        struct ForgeUiRecordSnapshot
        {
            int32_t index{};
            int32_t weapon_item_id{};
            int32_t current_level{};
            int32_t maximum_level{};
            int32_t field_0c{};
            int32_t attack_at_40{};
            int32_t magic_at_44{};
            uint64_t value_at_70{};
            std::wstring raw_bytes{};
        };

        struct ForgeUiObjectSnapshot
        {
            std::wstring object_name{};
            uint64_t object_address{};
            ArrayHeader records_header{};
            bool records_readable{false};
            std::wstring object_native_prefix{};
            std::vector<ForgeUiRecordSnapshot> records{};
        };

        struct ForgeUiSnapshot
        {
            std::vector<ForgeUiObjectSnapshot> objects{};
        };

        struct InvocationSnapshot
        {
            bool requested{false};
            bool confirmed{false};
            bool checker_found{false};
            bool function_found{false};
            bool row_param_found{false};
            bool row_param_written{false};
            bool invoked{false};
            bool flag_write_signature_valid{false};
            bool used_direct_word_or{false};
            bool refresh_signature_valid{false};
            bool flag_was_set_before{false};
            bool flag_is_set_after{false};
            bool item_manager_refreshed{false};
            std::wstring checker_object{};
            std::wstring function_object{};
            std::wstring error{};
        };

        template <typename T>
        auto read_value(const std::vector<std::byte>& bytes, size_t offset) -> std::optional<T>
        {
            if (offset > bytes.size() || bytes.size() - offset < sizeof(T))
            {
                return std::nullopt;
            }
            T value{};
            std::memcpy(&value, bytes.data() + offset, sizeof(T));
            return value;
        }

        auto is_supported_row_id(std::wstring_view row_id) -> bool
        {
            if (!row_id.starts_with(STR("IW_")) ||
                row_id.size() <= std::wstring_view{STR("IW_")}.size())
            {
                return false;
            }
            int value = 0;
            for (wchar_t character : row_id.substr(std::wstring_view{STR("IW_")}.size()))
            {
                if (character < L'0' || character > L'9')
                {
                    return false;
                }
                if (value > (std::numeric_limits<int>::max() - 9) / 10)
                {
                    return false;
                }
                value = (value * 10) + static_cast<int>(character - L'0');
            }
            return (value >= 0 && value < s_primary_weapon_row_end_exclusive) ||
                   (value >= s_secondary_weapon_row_begin &&
                    value < s_secondary_weapon_row_end_exclusive);
        }

        auto is_plausible_row_map_header(const MapHeader& header) -> bool
        {
            return header.data_ptr != 0 && looks_like_process_pointer(header.data_ptr) &&
                   header.num > 0 && header.num <= s_max_row_map_entries &&
                   header.max >= header.num && header.max <= s_max_row_map_capacity;
        }

        auto read_row_map_entry(const MapHeader& header, int index) -> std::optional<RowMapEntryRaw>
        {
            if (index < 0 || index >= header.num || !looks_like_process_pointer(header.data_ptr))
            {
                return std::nullopt;
            }
            const uint64_t address =
                header.data_ptr + static_cast<uint64_t>(index) * sizeof(RowMapEntryRaw);
            const auto bytes = try_read_process_memory_bytes(address, sizeof(RowMapEntryRaw));
            if (!bytes.has_value())
            {
                return std::nullopt;
            }
            RowMapEntryRaw entry{};
            std::memcpy(&entry, bytes->data(), sizeof(entry));
            if (!looks_like_process_pointer(entry.row_ptr))
            {
                return std::nullopt;
            }
            return entry;
        }

        auto count_readable_prefix_entries(const MapHeader& header) -> int
        {
            int readable = 0;
            const int limit = std::min(header.num, s_row_map_readability_sample_entries);
            for (int index = 0; index < limit; ++index)
            {
                const auto entry = read_row_map_entry(header, index);
                if (entry.has_value() &&
                    try_read_process_memory_bytes(entry->row_ptr, s_row_map_readability_probe_bytes)
                        .has_value())
                {
                    ++readable;
                }
            }
            return readable;
        }

        auto find_row_map(UObject* table_object) -> std::optional<RowMapCandidate>
        {
            if (!table_object || !kh3ap::is_uobject_valid_fast(table_object))
            {
                return std::nullopt;
            }

            std::vector<RowMapCandidate> candidates{};
            const uint64_t object_address = reinterpret_cast<uint64_t>(table_object);
            for (size_t offset = s_row_map_scan_begin; offset <= s_row_map_scan_end;
                 offset += s_row_map_scan_stride)
            {
                const auto bytes =
                    try_read_process_memory_bytes(object_address + offset, sizeof(MapHeader));
                if (!bytes.has_value())
                {
                    continue;
                }
                MapHeader header{};
                std::memcpy(&header, bytes->data(), sizeof(header));
                if (!is_plausible_row_map_header(header) ||
                    !try_read_process_memory_bytes(header.data_ptr, sizeof(RowMapEntryRaw))
                         .has_value())
                {
                    continue;
                }
                candidates.push_back({offset, header, count_readable_prefix_entries(header)});
            }

            if (const auto preferred =
                    std::find_if(candidates.begin(), candidates.end(),
                                 [](const RowMapCandidate& candidate)
                                 {
                                     return candidate.offset == s_preferred_row_map_offset &&
                                            candidate.readable_prefix_entries > 0;
                                 });
                preferred != candidates.end())
            {
                return *preferred;
            }

            const auto best = std::max_element(
                candidates.begin(), candidates.end(),
                [](const RowMapCandidate& left, const RowMapCandidate& right)
                {
                    if (left.readable_prefix_entries != right.readable_prefix_entries)
                    {
                        return left.readable_prefix_entries < right.readable_prefix_entries;
                    }
                    return left.offset > right.offset;
                });
            if (best == candidates.end() || best->readable_prefix_entries <= 0)
            {
                return std::nullopt;
            }
            return *best;
        }

        auto snapshot_weapon_enhance_row(UObject* table_object, std::wstring_view row_id)
            -> WeaponEnhanceRowSnapshot
        {
            WeaponEnhanceRowSnapshot snapshot{};
            snapshot.table_found = table_object && kh3ap::is_uobject_valid_fast(table_object);
            snapshot.table_object = snapshot.table_found ? format_object_debug_name(table_object)
                                                         : std::wstring{STR("<null>")};
            if (!snapshot.table_found)
            {
                return snapshot;
            }

            const auto row_map = find_row_map(table_object);
            if (!row_map.has_value())
            {
                return snapshot;
            }
            snapshot.row_map_found = true;
            snapshot.row_map_offset = row_map->offset;
            snapshot.row_map_num = row_map->header.num;
            snapshot.row_map_max = row_map->header.max;

            const int limit = std::min(row_map->header.num, s_max_row_map_entries);
            for (int index = 0; index < limit; ++index)
            {
                const auto entry = read_row_map_entry(row_map->header, index);
                if (!entry.has_value() || entry->row_name.ToString() != row_id)
                {
                    continue;
                }

                const auto row_bytes =
                    try_read_process_memory_bytes(entry->row_ptr, s_weapon_enhance_row_read_size);
                if (!row_bytes.has_value())
                {
                    return snapshot;
                }
                snapshot.row_found = true;
                snapshot.row_ptr = entry->row_ptr;
                snapshot.flag_index =
                    read_value<int32_t>(*row_bytes, s_flag_index_offset).value_or(-1);
                snapshot.weapon_id = read_value<uint8_t>(*row_bytes, s_weapon_id_offset)
                                         .value_or(s_unreadable_weapon_row_enum);
                snapshot.init_achieved =
                    read_value<uint8_t>(*row_bytes, s_init_achieved_offset).value_or(0) != 0;
                snapshot.weapon_level =
                    read_value<int32_t>(*row_bytes, s_weapon_level_offset).value_or(-1);
                snapshot.attack_plus =
                    read_value<int32_t>(*row_bytes, s_attack_plus_offset).value_or(0);
                snapshot.magic_plus =
                    read_value<int32_t>(*row_bytes, s_magic_plus_offset).value_or(0);
                snapshot.append_ability = read_value<uint8_t>(*row_bytes, s_append_ability_offset)
                                              .value_or(s_unreadable_weapon_row_enum);
                snapshot.raw_bytes = format_bytes_hex(row_bytes->data(), row_bytes->size());
                return snapshot;
            }
            return snapshot;
        }

        auto find_item_manager(std::wstring_view preferred_path) -> UObject*
        {
            UObject* manager = UObjectGlobals::StaticFindObject<UObject*>(
                nullptr, nullptr, std::wstring{preferred_path}.c_str());
            if (manager && kh3ap::is_uobject_valid_fast(manager))
            {
                return manager;
            }

            UObjectGlobals::ForEachUObject(
                [&manager](UObject* object, int32_t, int32_t)
                {
                    if (manager || !object || !kh3ap::is_uobject_valid_fast(object))
                    {
                        return manager ? RC::LoopAction::Break : RC::LoopAction::Continue;
                    }
                    UClass* const object_class = object->GetClassPrivate();
                    if (object_class && object_class->GetName() == STR("TresItemMan"))
                    {
                        manager = object;
                        return RC::LoopAction::Break;
                    }
                    return RC::LoopAction::Continue;
                });
            return manager;
        }

        auto hash_bytes(const std::vector<std::byte>& bytes) -> uint64_t
        {
            uint64_t hash = keyblade_probe_hash_initial_state;
            for (const std::byte value : bytes)
            {
                hash ^= static_cast<uint64_t>(std::to_integer<uint8_t>(value));
                hash *= keyblade_probe_hash_prime;
            }
            return hash;
        }

        auto native_signature_matches(uint64_t address,
                                      const NativeKeybladeEnhancementSignature& expected,
                                      std::wstring& observed) -> bool
        {
            if (!expected.bytes || expected.size == 0)
            {
                observed = STR("<missing-signature>");
                return false;
            }
            const auto bytes = try_read_process_memory_bytes(address, expected.size);
            if (!bytes.has_value() || bytes->size() != expected.size)
            {
                observed = STR("<unreadable>");
                return false;
            }
            observed = format_bytes_hex(bytes->data(), bytes->size());
            for (size_t index = 0; index < expected.size; ++index)
            {
                if (std::to_integer<uint8_t>((*bytes)[index]) != expected.bytes[index])
                {
                    return false;
                }
            }
            return true;
        }

        auto snapshot_native_flag_bank(int32_t flag_index) -> NativeFlagBankSnapshot
        {
            NativeFlagBankSnapshot snapshot{};
            snapshot.requested_flag_index = flag_index;
            HMODULE const module = GetModuleHandleW(nullptr);
            snapshot.module_found = module != nullptr;
            snapshot.module_base = reinterpret_cast<uint64_t>(module);
            if (!snapshot.module_found)
            {
                snapshot.error = STR("game_module_unavailable");
                return snapshot;
            }
            snapshot.profile = native_keyblade_enhancement_profile_for_module(module);
            if (!snapshot.profile)
            {
                snapshot.error = STR("unsupported_game_image");
                return snapshot;
            }

            const uint64_t get_save_owner_address =
                snapshot.module_base + snapshot.profile->save_accessor_rva;
            const uint64_t flag_write_address =
                snapshot.module_base + snapshot.profile->enhancement_flag_write_rva;
            snapshot.get_save_owner_signature_valid = native_signature_matches(
                get_save_owner_address, snapshot.profile->save_accessor_signature,
                snapshot.get_save_owner_prefix);
            snapshot.flag_write_signature_valid = native_signature_matches(
                flag_write_address, snapshot.profile->enhancement_flag_write_signature,
                snapshot.flag_write_prefix);
            if (!snapshot.get_save_owner_signature_valid)
            {
                snapshot.error = STR("native_get_save_owner_signature_mismatch");
                return snapshot;
            }

            // Do not execute the accessor here. Its validity check can contend with
            // UE object iteration while this manual probe is running on the game
            // thread. Decode the signature-guarded RIP-relative global chain that
            // the accessor itself reads instead.
            const auto accessor_bytes = try_read_process_memory_bytes(
                get_save_owner_address, s_save_accessor_global_instruction_size);
            const auto global_displacement =
                accessor_bytes.has_value()
                    ? read_value<int32_t>(*accessor_bytes,
                                          s_save_accessor_global_displacement_offset)
                    : std::nullopt;
            if (!global_displacement.has_value())
            {
                snapshot.error = STR("native_save_accessor_unreadable");
                return snapshot;
            }
            const uint64_t global_slot = static_cast<uint64_t>(
                static_cast<int64_t>(get_save_owner_address +
                                     s_save_accessor_global_instruction_size) +
                static_cast<int64_t>(*global_displacement));
            const auto global_ptr_bytes =
                try_read_process_memory_bytes(global_slot, sizeof(uint64_t));
            const uint64_t global_ptr = global_ptr_bytes.has_value()
                                            ? read_value<uint64_t>(*global_ptr_bytes, 0).value_or(0)
                                            : 0;
            const auto game_instance_bytes =
                looks_like_process_pointer(global_ptr)
                    ? try_read_process_memory_bytes(
                          global_ptr + s_save_accessor_game_instance_offset, sizeof(uint64_t))
                    : std::nullopt;
            const uint64_t game_instance =
                game_instance_bytes.has_value()
                    ? read_value<uint64_t>(*game_instance_bytes, 0).value_or(0)
                    : 0;
            const auto save_owner_bytes =
                looks_like_process_pointer(game_instance)
                    ? try_read_process_memory_bytes(
                          game_instance + s_save_accessor_save_owner_offset, sizeof(uint64_t))
                    : std::nullopt;
            snapshot.save_owner = save_owner_bytes.has_value()
                                      ? read_value<uint64_t>(*save_owner_bytes, 0).value_or(0)
                                      : 0;
            snapshot.save_owner_found = looks_like_process_pointer(snapshot.save_owner);
            if (!snapshot.save_owner_found)
            {
                snapshot.error = STR("save_owner_unavailable");
                return snapshot;
            }

            snapshot.save_data = snapshot.save_owner + s_save_data_offset;
            snapshot.setter_owner =
                snapshot.save_data +
                snapshot.profile->enhancement_flag_setter_owner_offset_from_save_data;
            snapshot.flag_words =
                snapshot.save_data + snapshot.profile->enhancement_flag_words_offset_from_save_data;
            const auto bytes = try_read_process_memory_bytes(
                snapshot.flag_words, snapshot.words.size() * sizeof(snapshot.words[0]));
            if (!bytes.has_value())
            {
                snapshot.error = STR("enhancement_flag_words_unreadable");
                return snapshot;
            }
            std::memcpy(snapshot.words.data(), bytes->data(), bytes->size());
            snapshot.flag_storage_readable = true;
            snapshot.requested_flag_valid =
                flag_index >= 0 && flag_index <= s_max_enhance_flag_index;
            if (snapshot.requested_flag_valid)
            {
                const size_t word_index =
                    static_cast<size_t>(flag_index / s_enhance_flag_bits_per_word);
                const uint32_t bit_mask =
                    uint32_t{1} << static_cast<uint32_t>(flag_index % s_enhance_flag_bits_per_word);
                snapshot.requested_flag_set = (snapshot.words[word_index] & bit_mask) != 0;
            }
            return snapshot;
        }

        auto set_native_enhancement_flag(int32_t flag_index, UObject* item_manager,
                                         const NativeFlagBankSnapshot& before,
                                         InvocationSnapshot& invocation) -> NativeFlagBankSnapshot
        {
            invocation.requested = true;
            invocation.confirmed = true;
            invocation.flag_write_signature_valid = before.flag_write_signature_valid;
            invocation.used_direct_word_or =
                before.profile && before.profile->flag_write_kind ==
                                      NativeKeybladeEnhancementFlagWriteKind::DirectWordOr;
            invocation.flag_was_set_before = before.requested_flag_set;
            NativeFlagBankSnapshot after = before;

            if (!before.flag_storage_readable || !before.requested_flag_valid)
            {
                invocation.error = STR("enhancement_flag_storage_unavailable");
                return after;
            }
            if (!before.profile)
            {
                invocation.error = STR("unsupported_game_image");
                return after;
            }
            if (!before.flag_write_signature_valid)
            {
                invocation.error = STR("native_flag_write_signature_mismatch");
                return after;
            }
            if (!item_manager || !kh3ap::is_uobject_valid_fast(item_manager))
            {
                invocation.error = STR("item_manager_unavailable");
                return after;
            }

            std::wstring refresh_prefix{};
            const uint64_t refresh_address =
                before.module_base + before.profile->refresh_item_manager_rva;
            invocation.refresh_signature_valid = native_signature_matches(
                refresh_address, before.profile->refresh_item_manager_signature, refresh_prefix);
            if (!invocation.refresh_signature_valid)
            {
                invocation.error = STR("native_refresh_item_manager_signature_mismatch");
                return after;
            }

            if (!before.requested_flag_set)
            {
                if (!try_write_native_keyblade_enhancement_flag(*before.profile, before.module_base,
                                                                before.setter_owner,
                                                                before.flag_words, flag_index))
                {
                    invocation.error = STR("native_flag_write_failed");
                    return after;
                }
                invocation.invoked = true;
            }

            after = snapshot_native_flag_bank(flag_index);
            invocation.flag_is_set_after = after.requested_flag_set;
            if (!after.flag_storage_readable || !after.requested_flag_set)
            {
                invocation.error = STR("native_set_flag_did_not_persist_in_memory");
                return after;
            }

            using RefreshItemManager = void (*)(UObject*);
            auto* const refresh_item_manager =
                reinterpret_cast<RefreshItemManager>(refresh_address);
            refresh_item_manager(item_manager);
            invocation.item_manager_refreshed = true;
            return after;
        }

        auto snapshot_enhanced_buffer(UObject* item_manager) -> EnhancedBufferSnapshot
        {
            EnhancedBufferSnapshot snapshot{};
            snapshot.manager_found = item_manager && kh3ap::is_uobject_valid_fast(item_manager);
            snapshot.manager_object = snapshot.manager_found
                                          ? format_object_debug_name(item_manager)
                                          : std::wstring{STR("<null>")};
            if (!snapshot.manager_found)
            {
                return snapshot;
            }

            FProperty* const property =
                item_manager->GetPropertyByNameInChain(STR("m_EnhancedEquipDataBuf"));
            void* const value_ptr =
                item_manager->GetValuePtrByPropertyNameInChain(STR("m_EnhancedEquipDataBuf"));
            if (!property || !value_ptr || property->GetClass().GetName() != STR("ArrayProperty"))
            {
                return snapshot;
            }

            auto* const array_property = static_cast<FArrayProperty*>(property);
            FProperty* const inner_property = const_cast<FProperty*>(array_property->GetInner());
            if (!inner_property)
            {
                return snapshot;
            }
            snapshot.property_found = true;
            snapshot.inner_element_size = inner_property->GetElementSize();
            std::memcpy(&snapshot.header, value_ptr, sizeof(snapshot.header));

            if (!looks_like_process_pointer(snapshot.header.data_ptr) || snapshot.header.num <= 0 ||
                snapshot.header.max < snapshot.header.num ||
                snapshot.header.num > s_max_enhanced_buffer_entries ||
                snapshot.inner_element_size <= 0 ||
                snapshot.inner_element_size > s_max_enhanced_buffer_element_bytes)
            {
                return snapshot;
            }

            const size_t full_size = static_cast<size_t>(snapshot.header.num) *
                                     static_cast<size_t>(snapshot.inner_element_size);
            const size_t sample_size =
                std::min<size_t>(full_size, s_max_enhanced_buffer_sample_bytes);
            const auto bytes = try_read_process_memory_bytes(snapshot.header.data_ptr, sample_size);
            if (!bytes.has_value())
            {
                return snapshot;
            }
            snapshot.sampled_byte_count = bytes->size();
            snapshot.sampled_hash = hash_bytes(*bytes);
            const size_t prefix_size =
                std::min<size_t>(bytes->size(), s_enhanced_buffer_log_prefix_bytes);
            snapshot.sampled_prefix = format_bytes_hex(bytes->data(), prefix_size);
            for (int32_t index = 0; index < snapshot.header.num; ++index)
            {
                const size_t base =
                    static_cast<size_t>(index) * static_cast<size_t>(snapshot.inner_element_size);
                if (base > bytes->size() ||
                    bytes->size() - base < static_cast<size_t>(snapshot.inner_element_size))
                {
                    break;
                }
                EnhancedBufferSnapshot::Record record{};
                record.index = index;
                // The first fields in this native struct are not reflection-safe
                // FNames. Keep them in raw_prefix instead of passing arbitrary
                // integers into the global name pool.
                record.ap =
                    read_value<int32_t>(*bytes, base + s_enhanced_equip_ap_offset).value_or(0);
                record.attack =
                    read_value<int32_t>(*bytes, base + s_enhanced_equip_attack_offset).value_or(0);
                record.magic =
                    read_value<int32_t>(*bytes, base + s_enhanced_equip_magic_offset).value_or(0);
                record.defense =
                    read_value<int32_t>(*bytes, base + s_enhanced_equip_defense_offset).value_or(0);
                const size_t raw_size =
                    std::min<size_t>(static_cast<size_t>(snapshot.inner_element_size),
                                     s_enhanced_record_log_prefix_bytes);
                record.raw_prefix = format_bytes_hex(bytes->data() + base, raw_size);
                snapshot.records.push_back(std::move(record));
            }
            return snapshot;
        }

        auto append_bool(std::ostringstream& json, std::string_view name, bool value) -> void
        {
            json << ",\"" << name << "\":" << (value ? "true" : "false");
        }

        auto append_row_snapshot(std::ostringstream& json, std::string_view name,
                                 const WeaponEnhanceRowSnapshot& row) -> void
        {
            json << ",\"" << name << "\":{";
            json << "\"table_found\":" << (row.table_found ? "true" : "false");
            append_bool(json, "row_map_found", row.row_map_found);
            append_bool(json, "row_found", row.row_found);
            json << ",\"table_object\":\"" << json_escape(row.table_object) << "\"";
            json << ",\"row_map_offset\":\"0x" << std::hex << std::uppercase << row.row_map_offset
                 << std::dec << "\"";
            json << ",\"row_map_num\":" << row.row_map_num;
            json << ",\"row_map_max\":" << row.row_map_max;
            json << ",\"row_ptr\":\"0x" << std::hex << std::uppercase << row.row_ptr << std::dec
                 << "\"";
            json << ",\"flag_index\":" << row.flag_index;
            json << ",\"weapon_id\":" << static_cast<int>(row.weapon_id);
            append_bool(json, "init_achieved", row.init_achieved);
            json << ",\"weapon_level\":" << row.weapon_level;
            json << ",\"attack_plus\":" << row.attack_plus;
            json << ",\"magic_plus\":" << row.magic_plus;
            json << ",\"append_ability\":" << static_cast<int>(row.append_ability);
            json << ",\"raw_bytes\":\"" << json_escape(row.raw_bytes) << "\"";
            json << "}";
        }

        auto append_buffer_snapshot(std::ostringstream& json, std::string_view name,
                                    const EnhancedBufferSnapshot& buffer) -> void
        {
            json << ",\"" << name << "\":{";
            json << "\"manager_found\":" << (buffer.manager_found ? "true" : "false");
            append_bool(json, "property_found", buffer.property_found);
            json << ",\"manager_object\":\"" << json_escape(buffer.manager_object) << "\"";
            json << ",\"inner_element_size\":" << buffer.inner_element_size;
            json << ",\"data_ptr\":\"0x" << std::hex << std::uppercase << buffer.header.data_ptr
                 << std::dec << "\"";
            json << ",\"num\":" << buffer.header.num;
            json << ",\"max\":" << buffer.header.max;
            json << ",\"sampled_byte_count\":" << buffer.sampled_byte_count;
            json << ",\"sampled_hash\":\"0x" << std::hex << std::uppercase << buffer.sampled_hash
                 << std::dec << "\"";
            json << ",\"sampled_prefix\":\"" << json_escape(buffer.sampled_prefix) << "\"";
            json << ",\"records\":[";
            for (size_t index = 0; index < buffer.records.size(); ++index)
            {
                if (index > 0)
                {
                    json << ",";
                }
                const auto& record = buffer.records[index];
                json << "{\"index\":" << record.index;
                json << ",\"key_name\":\"" << json_escape(record.key_name) << "\"";
                json << ",\"ap\":" << record.ap;
                json << ",\"attack\":" << record.attack;
                json << ",\"magic\":" << record.magic;
                json << ",\"defense\":" << record.defense;
                json << ",\"raw_prefix\":\"" << json_escape(record.raw_prefix) << "\"}";
            }
            json << "]";
            json << "}";
        }

        auto append_native_flag_bank_snapshot(std::ostringstream& json, std::string_view name,
                                              const NativeFlagBankSnapshot& snapshot) -> void
        {
            json << ",\"" << name << "\":{";
            json << "\"module_found\":" << (snapshot.module_found ? "true" : "false");
            json << ",\"storefront\":\""
                 << json_escape(snapshot.profile ? snapshot.profile->storefront
                                                 : std::wstring_view{})
                 << "\"";
            json << ",\"image_size\":\"0x" << std::hex << std::uppercase
                 << (snapshot.profile ? snapshot.profile->image_size : 0) << std::dec << "\"";
            json << ",\"flag_write_kind\":\""
                 << (snapshot.profile && snapshot.profile->flag_write_kind ==
                                             NativeKeybladeEnhancementFlagWriteKind::DirectWordOr
                         ? "direct_word_or"
                         : "native_setter")
                 << "\"";
            append_bool(json, "get_save_owner_signature_valid",
                        snapshot.get_save_owner_signature_valid);
            append_bool(json, "flag_write_signature_valid", snapshot.flag_write_signature_valid);
            append_bool(json, "save_owner_found", snapshot.save_owner_found);
            append_bool(json, "flag_storage_readable", snapshot.flag_storage_readable);
            append_bool(json, "requested_flag_valid", snapshot.requested_flag_valid);
            append_bool(json, "requested_flag_set", snapshot.requested_flag_set);
            json << ",\"module_base\":\"0x" << std::hex << std::uppercase << snapshot.module_base
                 << std::dec << "\"";
            json << ",\"save_owner\":\"0x" << std::hex << std::uppercase << snapshot.save_owner
                 << std::dec << "\"";
            json << ",\"save_data\":\"0x" << std::hex << std::uppercase << snapshot.save_data
                 << std::dec << "\"";
            json << ",\"setter_owner\":\"0x" << std::hex << std::uppercase << snapshot.setter_owner
                 << std::dec << "\"";
            json << ",\"flag_words_address\":\"0x" << std::hex << std::uppercase
                 << snapshot.flag_words << std::dec << "\"";
            json << ",\"requested_flag_index\":" << snapshot.requested_flag_index;
            json << ",\"words\":[";
            for (size_t index = 0; index < snapshot.words.size(); ++index)
            {
                if (index > 0)
                {
                    json << ",";
                }
                json << "\"0x" << std::hex << std::uppercase << snapshot.words[index] << std::dec
                     << "\"";
            }
            json << "]";
            json << ",\"get_save_owner_prefix\":\"" << json_escape(snapshot.get_save_owner_prefix)
                 << "\"";
            json << ",\"flag_write_prefix\":\"" << json_escape(snapshot.flag_write_prefix) << "\"";
            json << ",\"error\":\"" << json_escape(snapshot.error) << "\"";
            json << "}";
        }

        auto append_forge_ui_snapshot(std::ostringstream& json, std::string_view name,
                                      const ForgeUiSnapshot& snapshot) -> void
        {
            json << ",\"" << name << "\":{\"objects\":[";
            for (size_t object_index = 0; object_index < snapshot.objects.size(); ++object_index)
            {
                if (object_index > 0)
                {
                    json << ",";
                }
                const auto& object = snapshot.objects[object_index];
                json << "{\"object_name\":\"" << json_escape(object.object_name) << "\"";
                json << ",\"object_address\":\"0x" << std::hex << std::uppercase
                     << object.object_address << std::dec << "\"";
                json << ",\"records_data_ptr\":\"0x" << std::hex << std::uppercase
                     << object.records_header.data_ptr << std::dec << "\"";
                json << ",\"records_num\":" << object.records_header.num;
                json << ",\"records_max\":" << object.records_header.max;
                append_bool(json, "records_readable", object.records_readable);
                json << ",\"object_native_prefix\":\"" << json_escape(object.object_native_prefix)
                     << "\"";
                json << ",\"records\":[";
                for (size_t record_index = 0; record_index < object.records.size(); ++record_index)
                {
                    if (record_index > 0)
                    {
                        json << ",";
                    }
                    const auto& record = object.records[record_index];
                    json << "{\"index\":" << record.index;
                    json << ",\"weapon_item_id\":" << record.weapon_item_id;
                    json << ",\"current_level\":" << record.current_level;
                    json << ",\"maximum_level\":" << record.maximum_level;
                    json << ",\"field_0c\":" << record.field_0c;
                    json << ",\"attack_at_40\":" << record.attack_at_40;
                    json << ",\"magic_at_44\":" << record.magic_at_44;
                    json << ",\"value_at_70\":\"0x" << std::hex << std::uppercase
                         << record.value_at_70 << std::dec << "\"";
                    json << ",\"raw_bytes\":\"" << json_escape(record.raw_bytes) << "\"}";
                }
                json << "]}";
            }
            json << "]}";
        }

        auto serialize_result(const ManualKeybladeEnhancementProbeDeps& deps,
                              std::wstring_view request_id, std::wstring_view row_id,
                              std::wstring_view action, std::wstring_view status,
                              const WeaponEnhanceRowSnapshot& row_before,
                              const NativeFlagBankSnapshot& flag_bank_before,
                              const NativeFlagBankSnapshot& flag_bank_after,
                              const ForgeUiSnapshot& forge_ui,
                              const EnhancedBufferSnapshot& buffer_before,
                              const EnhancedBufferSnapshot& buffer_after,
                              const InvocationSnapshot& invocation) -> std::string
        {
            std::ostringstream json;
            json << "{";
            json << "\"schema\":\"kh3ap.manual_keyblade_enhancement_probe_result.v1\"";
            json << ",\"protocol_name\":\"" << json_escape(deps.protocol_name) << "\"";
            json << ",\"protocol_version\":" << deps.protocol_version;
            json << ",\"recorded_at_utc\":\"" << now_iso8601_utc() << "\"";
            json << ",\"build_stamp\":\"" << json_escape(deps.build_stamp) << "\"";
            json << ",\"request_id\":\"" << json_escape(request_id) << "\"";
            json << ",\"row_id\":\"" << json_escape(row_id) << "\"";
            json << ",\"action\":\"" << json_escape(action) << "\"";
            json << ",\"status\":\"" << json_escape(status) << "\"";
            json << ",\"runtime_context\":\"" << json_escape(deps.runtime_context_summary) << "\"";
            append_row_snapshot(json, "row_before", row_before);
            append_native_flag_bank_snapshot(json, "native_flag_bank_before", flag_bank_before);
            append_native_flag_bank_snapshot(json, "native_flag_bank_after", flag_bank_after);
            append_forge_ui_snapshot(json, "forge_ui", forge_ui);
            append_buffer_snapshot(json, "enhanced_buffer_before", buffer_before);
            append_buffer_snapshot(json, "enhanced_buffer_after", buffer_after);
            json << ",\"invocation\":{";
            json << "\"requested\":" << (invocation.requested ? "true" : "false");
            append_bool(json, "confirmed", invocation.confirmed);
            append_bool(json, "checker_found", invocation.checker_found);
            append_bool(json, "function_found", invocation.function_found);
            append_bool(json, "row_param_found", invocation.row_param_found);
            append_bool(json, "row_param_written", invocation.row_param_written);
            append_bool(json, "invoked", invocation.invoked);
            append_bool(json, "flag_write_signature_valid", invocation.flag_write_signature_valid);
            append_bool(json, "used_direct_word_or", invocation.used_direct_word_or);
            append_bool(json, "refresh_signature_valid", invocation.refresh_signature_valid);
            append_bool(json, "flag_was_set_before", invocation.flag_was_set_before);
            append_bool(json, "flag_is_set_after", invocation.flag_is_set_after);
            append_bool(json, "item_manager_refreshed", invocation.item_manager_refreshed);
            json << ",\"checker_object\":\"" << json_escape(invocation.checker_object) << "\"";
            json << ",\"function_object\":\"" << json_escape(invocation.function_object) << "\"";
            json << ",\"error\":\"" << json_escape(invocation.error) << "\"";
            json << "}";
            json << "}\n";
            return json.str();
        }
    } // namespace

    auto process_pending_manual_keyblade_enhancement_probe(
        const ManualKeybladeEnhancementProbeDeps& deps) -> void
    {
        if (!deps.bridge_dir.has_value())
        {
            return;
        }

        const auto request_path = *deps.bridge_dir / std::filesystem::path{deps.request_filename};
        if (!std::filesystem::exists(request_path))
        {
            return;
        }

        const auto request_text = read_text_file(request_path);
        const std::wstring request_id =
            request_text.has_value() ? JsonFieldReader::string_field(*request_text, "request_id")
                                           .value_or(std::wstring{})
                                     : std::wstring{};
        const std::wstring row_id =
            request_text.has_value()
                ? JsonFieldReader::string_field(*request_text, "row_id").value_or(std::wstring{})
                : std::wstring{};
        const std::wstring action = request_text.has_value()
                                        ? JsonFieldReader::string_field(*request_text, "action")
                                              .value_or(std::wstring{STR("inspect")})
                                        : std::wstring{STR("inspect")};
        const bool confirm_mutation =
            request_text.has_value() &&
            JsonFieldReader::bool_field(*request_text, "confirm_mutation").value_or(false);
        const std::wstring effective_request_id =
            request_id.empty() ? std::wstring{STR("manual-keyblade-enhancement-probe")}
                               : request_id;
        try_remove_file(request_path);

        Output::send<LogLevel::Warning>(
            STR("[KH3APProbeCpp] ManualKeybladeEnhancementProbe request_id={} row_id={} action={} "
                "stage=request_consumed\n"),
            effective_request_id, row_id, action);

        WeaponEnhanceRowSnapshot row_before{};
        NativeFlagBankSnapshot flag_bank_before{};
        NativeFlagBankSnapshot flag_bank_after{};
        ForgeUiSnapshot forge_ui{};
        EnhancedBufferSnapshot buffer_before{};
        EnhancedBufferSnapshot buffer_after{};
        InvocationSnapshot invocation{};
        invocation.requested = action == STR("invoke_test") || action == STR("set_native_flag");
        invocation.confirmed = confirm_mutation;
        std::wstring status = STR("completed");

        if (!request_text.has_value())
        {
            status = STR("rejected");
            invocation.error = STR("request_unreadable");
        }
        else if (!is_supported_row_id(row_id))
        {
            status = STR("rejected");
            invocation.error = STR("unsupported_row_id");
        }
        else if (action != STR("inspect") && action != STR("invoke_test") &&
                 action != STR("set_native_flag"))
        {
            status = STR("rejected");
            invocation.error = STR("unsupported_action");
        }
        else
        {
            UObject* const table_object = UObjectGlobals::StaticFindObject<UObject*>(
                nullptr, nullptr, std::wstring{deps.data_table_path}.c_str());
            UObject* const item_manager = find_item_manager(deps.item_manager_path);
            Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] ManualKeybladeEnhancementProbe "
                                                "request_id={} stage=objects_resolved\n"),
                                            effective_request_id);
            row_before = snapshot_weapon_enhance_row(table_object, row_id);
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] ManualKeybladeEnhancementProbe request_id={} "
                    "stage=row_snapshotted found={} flag_index={}\n"),
                effective_request_id, row_before.row_found, row_before.flag_index);
            flag_bank_before = snapshot_native_flag_bank(row_before.flag_index);
            flag_bank_after = flag_bank_before;
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] ManualKeybladeEnhancementProbe request_id={} "
                    "stage=flag_bank_snapshotted readable={} flag_set={} error={}\n"),
                effective_request_id, flag_bank_before.flag_storage_readable,
                flag_bank_before.requested_flag_set, flag_bank_before.error);
            // The live Forge object scan is useful only while that UI is open and
            // is not required to prove or set the persistent enhancement flag.
            // Leave it out of this guarded setter path.
            buffer_before = snapshot_enhanced_buffer(item_manager);
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] ManualKeybladeEnhancementProbe request_id={} "
                    "stage=buffer_before_snapshotted records={}\n"),
                effective_request_id, buffer_before.records.size());

            if (action == STR("set_native_flag"))
            {
                if (!confirm_mutation)
                {
                    status = STR("rejected");
                    invocation.error = STR("set_native_flag_requires_confirm_mutation");
                }
                else if (!row_before.row_found)
                {
                    status = STR("failed");
                    invocation.error = STR("enhancement_row_not_available");
                }
                else
                {
                    flag_bank_after = set_native_enhancement_flag(
                        row_before.flag_index, item_manager, flag_bank_before, invocation);
                    Output::send<LogLevel::Warning>(
                        STR("[KH3APProbeCpp] ManualKeybladeEnhancementProbe request_id={} "
                            "stage=native_setter_finished invoked={} flag_set={} refreshed={} "
                            "error={}\n"),
                        effective_request_id, invocation.invoked, invocation.flag_is_set_after,
                        invocation.item_manager_refreshed, invocation.error);
                    if (!invocation.error.empty())
                    {
                        status = STR("failed");
                    }
                }
            }
            else if (action == STR("invoke_test"))
            {
                if (!confirm_mutation)
                {
                    status = STR("rejected");
                    invocation.error = STR("invoke_test_requires_confirm_mutation");
                }
                else if (!row_before.row_found)
                {
                    status = STR("failed");
                    invocation.error = STR("enhancement_row_not_available");
                }
                else
                {
                    UObject* const checker_object = UObjectGlobals::StaticFindObject<UObject*>(
                        nullptr, nullptr, std::wstring{deps.checker_default_object_path}.c_str());
                    UFunction* const function_object = UObjectGlobals::StaticFindObject<UFunction*>(
                        nullptr, nullptr, std::wstring{deps.enhance_test_function_path}.c_str());
                    invocation.checker_found =
                        checker_object && kh3ap::is_uobject_valid_fast(checker_object);
                    invocation.function_found = function_object != nullptr;
                    invocation.checker_object = invocation.checker_found
                                                    ? format_object_debug_name(checker_object)
                                                    : std::wstring{STR("<null>")};
                    invocation.function_object = function_object ? function_object->GetFullName()
                                                                 : std::wstring{STR("<null>")};
                    if (!invocation.checker_found || !invocation.function_found)
                    {
                        status = STR("failed");
                        invocation.error = STR("enhance_test_function_unavailable");
                    }
                    else
                    {
                        std::vector<std::byte> params(
                            static_cast<size_t>(function_object->GetParmsSize()));
                        std::fill(params.begin(), params.end(), std::byte{0});
                        FProperty* const row_property =
                            find_function_param_by_name(function_object, STR("rowId"));
                        invocation.row_param_found = row_property != nullptr;
                        invocation.row_param_written = write_name_or_string_function_param(
                            row_property, params.data(), row_id);
                        if (!invocation.row_param_written)
                        {
                            status = STR("failed");
                            invocation.error = STR("enhance_test_row_param_write_failed");
                        }
                        else
                        {
                            checker_object->ProcessEvent(function_object,
                                                         params.empty() ? nullptr : params.data());
                            invocation.invoked = true;
                        }
                    }
                }
            }
            buffer_after =
                action == STR("inspect") ? buffer_before : snapshot_enhanced_buffer(item_manager);
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] ManualKeybladeEnhancementProbe request_id={} "
                    "stage=buffer_after_snapshotted records={}\n"),
                effective_request_id, buffer_after.records.size());
        }

        const std::string result = serialize_result(
            deps, effective_request_id, row_id, action, status, row_before, flag_bank_before,
            flag_bank_after, forge_ui, buffer_before, buffer_after, invocation);
        write_text_file(*deps.bridge_dir / std::filesystem::path{deps.result_filename}, result);
        append_text_file(*deps.bridge_dir / std::filesystem::path{deps.events_filename}, result);
        Output::send<LogLevel::Warning>(
            STR("[KH3APProbeCpp] ManualKeybladeEnhancementProbe request_id={} row_id={} action={} "
                "status={} invoked={} error={}\n"),
            effective_request_id, row_id, action, status, invocation.invoked, invocation.error);
    }
} // namespace kh3ap
