#include "kh3ap/features/grants/keyblades/progressive_keyblade_level_helpers.h"

#include "kh3ap/features/grants/abilities/ability_snapshot_helpers.h"
#include "kh3ap/features/grants/keyblades/keyblade_enhancement_profile.h"
#include "kh3ap/core/reflection/reflection_core.h"

#include <Unreal/UClass.hpp>
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
#include <optional>
#include <string>
#include <vector>

namespace kh3ap
{
    namespace
    {
        constexpr auto synthesis_save_flags_offset = 0x13898;
        constexpr auto photo_mission_save_flags_offset = 0x138f8;
        constexpr auto workshop_flag_bank_bytes = 32;
    }

    using namespace RC;
    using namespace RC::Unreal;

    namespace
    {
        constexpr std::wstring_view s_runtime_token_prefix = STR("KH3AP_PROGRESSIVE_KEYBLADE_LEVEL::");
        constexpr std::wstring_view s_individual_runtime_token_prefix = STR("KEYBLADE::");


        struct KeybladeEnhancementGroup
        {
            std::wstring_view name{};
            int32_t first_row_id{};
        };

        constexpr std::array<KeybladeEnhancementGroup, 17> s_keyblade_enhancement_groups{
                KeybladeEnhancementGroup{STR("Kingdom Key"), 0},
                KeybladeEnhancementGroup{STR("Shooting Star"), 20},
                KeybladeEnhancementGroup{STR("Hero's Origin"), 10},
                KeybladeEnhancementGroup{STR("Favorite Deputy"), 30},
                KeybladeEnhancementGroup{STR("Ever After"), 40},
                KeybladeEnhancementGroup{STR("Wheel of Fate"), 90},
                KeybladeEnhancementGroup{STR("Crystal Snow"), 60},
                KeybladeEnhancementGroup{STR("Hunny Spout"), 70},
                KeybladeEnhancementGroup{STR("Nano Gear"), 80},
                KeybladeEnhancementGroup{STR("Happy Gear"), 50},
                KeybladeEnhancementGroup{STR("Classic Tone"), 100},
                KeybladeEnhancementGroup{STR("Grand Chef"), 110},
                KeybladeEnhancementGroup{STR("Ultima Weapon"), 120},
                KeybladeEnhancementGroup{STR("Pandora's Power"), 150},
                KeybladeEnhancementGroup{STR("Starlight"), 160},
                KeybladeEnhancementGroup{STR("Oathkeeper"), 170},
                KeybladeEnhancementGroup{STR("Oblivion"), 180}};

        constexpr size_t s_save_accessor_global_instruction_size = 13;
        constexpr size_t s_save_accessor_global_displacement_offset = 9;
        constexpr size_t s_save_accessor_game_instance_offset = 0x230;
        constexpr size_t s_save_accessor_save_owner_offset = 0x50;
        constexpr size_t s_save_data_offset = 0x870;
        constexpr size_t s_enhance_flag_word_count = 8;

        struct NativeFlagBank
        {
            uint64_t module_base{};
            const NativeKeybladeEnhancementProfile* profile{};
            uint64_t save_data{};
            uint64_t setter_owner{};
            uint64_t flag_words_address{};
            std::array<uint32_t, s_enhance_flag_word_count> words{};
        };

        struct ParsedProgressiveTarget
        {
            int target_level{};
            std::optional<int32_t> first_row_id{};
            std::wstring keyblade_name{};
        };

        auto fail(std::wstring_view reason, std::wstring* failure_details) -> bool
        {
            if (failure_details)
            {
                *failure_details = reason;
            }
            return false;
        }

        template <typename T>
        auto read_value(const std::vector<std::byte>& bytes, size_t offset) -> std::optional<T>
        {
            if (offset > bytes.size() || sizeof(T) > bytes.size() - offset)
            {
                return std::nullopt;
            }
            T value{};
            std::memcpy(&value, bytes.data() + offset, sizeof(T));
            return value;
        }

        auto native_signature_matches(
                uint64_t address,
                const NativeKeybladeEnhancementSignature& expected) -> bool
        {
            if (!expected.bytes || expected.size == 0)
            {
                return false;
            }
            const auto bytes = try_read_process_memory_bytes(address, expected.size);
            if (!bytes.has_value() || bytes->size() != expected.size)
            {
                return false;
            }
            for (size_t index = 0; index < expected.size; ++index)
            {
                if (std::to_integer<uint8_t>((*bytes)[index]) != expected.bytes[index])
                {
                    return false;
                }
            }
            return true;
        }

        auto parse_decimal(
                std::wstring_view value,
                int maximum,
                int* parsed_value,
                std::wstring_view invalid_reason,
                std::wstring_view range_reason,
                std::wstring* failure_details) -> bool
        {
            if (value.empty())
            {
                return fail(invalid_reason, failure_details);
            }
            int parsed = 0;
            for (const wchar_t character : value)
            {
                if (character < L'0' || character > L'9')
                {
                    return fail(invalid_reason, failure_details);
                }
                parsed = parsed * 10 + static_cast<int>(character - L'0');
                if (parsed > maximum)
                {
                    return fail(range_reason, failure_details);
                }
            }
            if (parsed_value)
            {
                *parsed_value = parsed;
            }
            return true;
        }

        auto parse_progressive_target(
                std::wstring_view runtime_token,
                ParsedProgressiveTarget* target,
                std::wstring* failure_details) -> bool
        {
            if (!runtime_token.starts_with(s_runtime_token_prefix))
            {
                return fail(STR("progressive_keyblade_level_token_prefix_invalid"), failure_details);
            }

            const std::wstring_view suffix = runtime_token.substr(s_runtime_token_prefix.size());
            if (!target)
            {
                return fail(STR("progressive_keyblade_level_target_result_missing"), failure_details);
            }
            *target = ParsedProgressiveTarget{};

            if (!suffix.starts_with(s_individual_runtime_token_prefix))
            {
                if (!parse_decimal(
                            suffix,
                            keyblade_levels::maximum,
                            &target->target_level,
                            STR("progressive_keyblade_level_token_level_invalid"),
                            STR("progressive_keyblade_level_token_level_out_of_range"),
                            failure_details) ||
                    target->target_level < 1)
                {
                    return fail(STR("progressive_keyblade_level_token_level_out_of_range"), failure_details);
                }
                return true;
            }

            const std::wstring_view individual_suffix = suffix.substr(s_individual_runtime_token_prefix.size());
            const size_t separator = individual_suffix.find(STR("::"));
            if (separator == std::wstring_view::npos ||
                individual_suffix.find(STR("::"), separator + 2) != std::wstring_view::npos)
            {
                return fail(STR("progressive_keyblade_level_individual_token_invalid"), failure_details);
            }

            int parsed_first_row_id = 0;
            if (!parse_decimal(
                        individual_suffix.substr(0, separator),
                        255,
                        &parsed_first_row_id,
                        STR("progressive_keyblade_level_keyblade_id_invalid"),
                        STR("progressive_keyblade_level_keyblade_id_out_of_range"),
                        failure_details))
            {
                return false;
            }
            if (!parse_decimal(
                        individual_suffix.substr(separator + 2),
                        keyblade_levels::maximum,
                        &target->target_level,
                        STR("progressive_keyblade_level_token_level_invalid"),
                        STR("progressive_keyblade_level_token_level_out_of_range"),
                        failure_details) ||
                target->target_level < 1)
            {
                return fail(STR("progressive_keyblade_level_token_level_out_of_range"), failure_details);
            }

            const auto group = std::find_if(
                    s_keyblade_enhancement_groups.begin(),
                    s_keyblade_enhancement_groups.end(),
                    [parsed_first_row_id](const KeybladeEnhancementGroup& candidate) {
                        return candidate.first_row_id == parsed_first_row_id;
                    });
            if (group == s_keyblade_enhancement_groups.end())
            {
                return fail(STR("progressive_keyblade_level_keyblade_id_unknown"), failure_details);
            }
            target->first_row_id = group->first_row_id;
            target->keyblade_name = group->name;
            return true;
        }

        auto find_item_manager(std::wstring_view preferred_path) -> UObject*
        {
            UObject* manager = nullptr;
            if (!preferred_path.empty())
            {
                const std::wstring owned_path{preferred_path};
                manager = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, owned_path.c_str());
                if (manager && kh3ap::is_uobject_valid_fast(manager))
                {
                    return manager;
                }
            }

            manager = nullptr;
            UObjectGlobals::ForEachUObject([&manager](UObject* object, int32_t, int32_t) {
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

        auto resolve_native_flag_bank(NativeFlagBank* bank, std::wstring* failure_details) -> bool
        {
            if (!bank)
            {
                return fail(STR("progressive_keyblade_level_flag_bank_result_missing"), failure_details);
            }
            *bank = NativeFlagBank{};

            HMODULE const module = GetModuleHandleW(nullptr);
            if (!module)
            {
                return fail(STR("progressive_keyblade_level_game_module_unavailable"), failure_details);
            }
            bank->module_base = reinterpret_cast<uint64_t>(module);
            bank->profile = native_keyblade_enhancement_profile_for_module(module);
            if (!bank->profile)
            {
                return fail(STR("progressive_keyblade_level_unsupported_game_image"), failure_details);
            }

            const uint64_t get_save_owner_address = bank->module_base + bank->profile->save_accessor_rva;
            const uint64_t flag_write_address =
                    bank->module_base + bank->profile->enhancement_flag_write_rva;
            const uint64_t refresh_address = bank->module_base + bank->profile->refresh_item_manager_rva;
            if (!native_signature_matches(get_save_owner_address, bank->profile->save_accessor_signature))
            {
                return fail(STR("progressive_keyblade_level_get_save_owner_signature_mismatch"), failure_details);
            }
            if (!native_signature_matches(
                        flag_write_address,
                        bank->profile->enhancement_flag_write_signature))
            {
                return fail(STR("progressive_keyblade_level_flag_write_signature_mismatch"), failure_details);
            }
            if (!native_signature_matches(
                        refresh_address,
                        bank->profile->refresh_item_manager_signature))
            {
                return fail(STR("progressive_keyblade_level_refresh_item_manager_signature_mismatch"), failure_details);
            }

            const auto accessor_bytes = try_read_process_memory_bytes(
                    get_save_owner_address,
                    s_save_accessor_global_instruction_size);
            const auto global_displacement = accessor_bytes.has_value()
                                                     ? read_value<int32_t>(
                                                               *accessor_bytes,
                                                               s_save_accessor_global_displacement_offset)
                                                     : std::nullopt;
            if (!global_displacement.has_value())
            {
                return fail(STR("progressive_keyblade_level_save_accessor_unreadable"), failure_details);
            }

            const uint64_t global_slot = static_cast<uint64_t>(
                    static_cast<int64_t>(get_save_owner_address + s_save_accessor_global_instruction_size) +
                    static_cast<int64_t>(*global_displacement));
            const auto global_ptr_bytes = try_read_process_memory_bytes(global_slot, sizeof(uint64_t));
            const uint64_t global_ptr = global_ptr_bytes.has_value()
                                                ? read_value<uint64_t>(*global_ptr_bytes, 0).value_or(0)
                                                : 0;
            const auto game_instance_bytes = looks_like_process_pointer(global_ptr)
                                                       ? try_read_process_memory_bytes(
                                                                 global_ptr + s_save_accessor_game_instance_offset,
                                                                 sizeof(uint64_t))
                                                       : std::nullopt;
            const uint64_t game_instance = game_instance_bytes.has_value()
                                                   ? read_value<uint64_t>(*game_instance_bytes, 0).value_or(0)
                                                   : 0;
            const auto save_owner_bytes = looks_like_process_pointer(game_instance)
                                                    ? try_read_process_memory_bytes(
                                                              game_instance + s_save_accessor_save_owner_offset,
                                                              sizeof(uint64_t))
                                                    : std::nullopt;
            const uint64_t save_owner = save_owner_bytes.has_value()
                                                ? read_value<uint64_t>(*save_owner_bytes, 0).value_or(0)
                                                : 0;
            if (!looks_like_process_pointer(save_owner))
            {
                return fail(STR("progressive_keyblade_level_save_owner_unavailable"), failure_details);
            }

            bank->save_data = save_owner + s_save_data_offset;
            bank->setter_owner =
                    bank->save_data +
                    bank->profile->enhancement_flag_setter_owner_offset_from_save_data;
            bank->flag_words_address =
                    bank->save_data +
                    bank->profile->enhancement_flag_words_offset_from_save_data;
            const auto flag_bytes = try_read_process_memory_bytes(
                    bank->flag_words_address,
                    bank->words.size() * sizeof(bank->words[0]));
            if (!flag_bytes.has_value())
            {
                return fail(STR("progressive_keyblade_level_flag_words_unreadable"), failure_details);
            }
            std::memcpy(bank->words.data(), flag_bytes->data(), flag_bytes->size());
            return true;
        }

        auto flag_is_set(const NativeFlagBank& bank, int32_t flag_index) -> bool
        {
            if (flag_index < 0 || flag_index >= static_cast<int32_t>(bank.words.size() * 32))
            {
                return false;
            }
            const size_t word_index = static_cast<size_t>(flag_index / 32);
            const uint32_t bit_mask = uint32_t{1} << static_cast<uint32_t>(flag_index % 32);
            return (bank.words[word_index] & bit_mask) != 0;
        }
    }

    auto try_query_native_workshop(NativeWorkshopSnapshot& result, std::wstring* failure_details) -> bool
    {
        result = {};
        NativeFlagBank bank;
        if (!resolve_native_flag_bank(&bank, failure_details)) return false;
        // These are adjacent members of the same native synthesis-save block.
        // EGS ShopItemSynthesis reads +0x13898, ShopPhotoMission reads
        // save_owner+0x14168 (=save_data+0x138f8). Enhancements use +0x13938.
        const auto synthesis = try_read_process_memory_bytes(bank.save_data + synthesis_save_flags_offset, workshop_flag_bank_bytes);
        const auto photos = try_read_process_memory_bytes(bank.save_data + photo_mission_save_flags_offset, workshop_flag_bank_bytes);
        if (!synthesis || !photos) return fail(L"workshop_flag_banks_unreadable", failure_details);
        result.save_data = bank.save_data;
        std::memcpy(result.synthesis.data(), synthesis->data(), workshop_flag_bank_bytes);
        std::memcpy(result.photos.data(), photos->data(), workshop_flag_bank_bytes);
        result.enhancements = bank.words;
        return true;
    }

    auto try_query_keyblade_level(
            std::wstring_view keyblade_name,
            bool levels_start_at_max,
            KeybladeLevelQueryResult* result,
            std::wstring* failure_details) -> bool
    {
        if (!result)
        {
            return fail(STR("keyblade_level_query_result_missing"), failure_details);
        }
        *result = KeybladeLevelQueryResult{};
        if (failure_details)
        {
            failure_details->clear();
        }

        const auto group_it = std::find_if(
                s_keyblade_enhancement_groups.begin(),
                s_keyblade_enhancement_groups.end(),
                [keyblade_name](const KeybladeEnhancementGroup& group) {
                    return group.name == keyblade_name;
                });
        if (group_it == s_keyblade_enhancement_groups.end())
        {
            return fail(STR("keyblade_level_query_keyblade_unknown"), failure_details);
        }

        KeybladeLevelQueryResult query{};
        query.keyblade_name = group_it->name;
        query.maximum_level = keyblade_levels::maximum;
        if (levels_start_at_max)
        {
            query.current_level = keyblade_levels::maximum;
            query.achieved_flag_count = keyblade_levels::maximum;
            *result = query;
            return true;
        }

        NativeFlagBank bank{};
        if (!resolve_native_flag_bank(&bank, failure_details))
        {
            return false;
        }
        for (int level_offset = 0;
             level_offset < keyblade_levels::maximum;
             ++level_offset)
        {
            if (flag_is_set(bank, group_it->first_row_id + level_offset))
            {
                ++query.achieved_flag_count;
            }
        }
        query.current_level = std::clamp(
                query.achieved_flag_count,
                0,
                query.maximum_level);
        *result = query;
        return true;
    }

    auto try_query_ultima_weapon_shop_prerequisites(
            const std::vector<std::wstring>& required_keyblade_names,
            UltimaWeaponShopPrerequisiteQueryResult* result,
            std::wstring* failure_details) -> bool
    {
        if (!result)
        {
            return fail(STR("keyblade_max_level_query_result_missing"), failure_details);
        }
        *result = UltimaWeaponShopPrerequisiteQueryResult{};
        if (failure_details)
        {
            failure_details->clear();
        }

        NativeFlagBank bank{};
        if (!resolve_native_flag_bank(&bank, failure_details))
        {
            return false;
        }

        UltimaWeaponShopPrerequisiteQueryResult query{};
        for (const KeybladeEnhancementGroup& group : s_keyblade_enhancement_groups)
        {
            // Ultima cannot be a prerequisite for obtaining itself. Pandora's
            // Power is a static GoA replacement rather than an AP inventory
            // award, so it is not an acquisition prerequisite even though
            // Keyblade Upgrade Sanity creates its individual level items.
            if (group.name == STR("Ultima Weapon") || group.name == STR("Pandora's Power"))
            {
                continue;
            }
            if (!required_keyblade_names.empty() &&
                std::find(
                        required_keyblade_names.begin(),
                        required_keyblade_names.end(),
                        group.name) == required_keyblade_names.end())
            {
                continue;
            }
            for (int level_offset = 0; level_offset < keyblade_levels::maximum; ++level_offset)
            {
                ++query.required_flag_count;
                if (flag_is_set(bank, group.first_row_id + level_offset))
                {
                    ++query.maxed_flag_count;
                }
            }
        }
        query.all_required_keyblades_maxed =
                query.required_flag_count > 0 &&
                query.maxed_flag_count == query.required_flag_count;
        *result = query;
        return true;
    }

    auto try_apply_progressive_keyblade_level(
            std::wstring_view runtime_token,
            std::wstring_view preferred_item_manager_path,
            bool force_item_manager_refresh,
            ProgressiveKeybladeLevelApplyResult* result,
            std::wstring* failure_details) -> bool
    {
        if (result)
        {
            *result = ProgressiveKeybladeLevelApplyResult{};
        }
        if (failure_details)
        {
            failure_details->clear();
        }

        ParsedProgressiveTarget target{};
        if (!parse_progressive_target(runtime_token, &target, failure_details))
        {
            return false;
        }

        UObject* const item_manager = find_item_manager(preferred_item_manager_path);
        if (!item_manager || !kh3ap::is_uobject_valid_fast(item_manager))
        {
            return fail(STR("progressive_keyblade_level_item_manager_unavailable"), failure_details);
        }

        NativeFlagBank before{};
        if (!resolve_native_flag_bank(&before, failure_details))
        {
            return false;
        }

        ProgressiveKeybladeLevelApplyResult applied{};
        applied.target_level = target.target_level;
        applied.applies_to_all_keyblades = !target.first_row_id.has_value();
        applied.keyblade_name = target.keyblade_name;
        applied.expected_flag_count = (
                target.first_row_id.has_value()
                        ? 1
                        : static_cast<int>(s_keyblade_enhancement_groups.size())) * target.target_level;

        for (const KeybladeEnhancementGroup& group : s_keyblade_enhancement_groups)
        {
            if (target.first_row_id.has_value() && group.first_row_id != *target.first_row_id)
            {
                continue;
            }
            for (int level_offset = 0; level_offset < target.target_level; ++level_offset)
            {
                const int32_t flag_index = group.first_row_id + level_offset;
                if (flag_is_set(before, flag_index))
                {
                    ++applied.already_set_count;
                    continue;
                }
                if (!try_write_native_keyblade_enhancement_flag(
                            *before.profile,
                            before.module_base,
                            before.setter_owner,
                            before.flag_words_address,
                            flag_index))
                {
                    return fail(STR("progressive_keyblade_level_flag_write_failed"), failure_details);
                }
                ++applied.newly_set_count;
            }
        }

        const bool should_refresh_item_manager =
                applied.newly_set_count > 0 || force_item_manager_refresh;
        if (should_refresh_item_manager)
        {
            using RefreshItemManager = void (*)(UObject*);
            auto* const refresh_item_manager = reinterpret_cast<RefreshItemManager>(
                    before.module_base + before.profile->refresh_item_manager_rva);
            refresh_item_manager(item_manager);
            applied.item_manager_refreshed = true;
        }

        if (applied.newly_set_count > 0)
        {
            // Refresh before final readback: if resolving the bank transiently fails,
            // a retry will see the durable flags and may take the no-op path.
            NativeFlagBank after{};
            if (!resolve_native_flag_bank(&after, failure_details))
            {
                return false;
            }
            for (const KeybladeEnhancementGroup& group : s_keyblade_enhancement_groups)
            {
                if (target.first_row_id.has_value() && group.first_row_id != *target.first_row_id)
                {
                    continue;
                }
                for (int level_offset = 0; level_offset < target.target_level; ++level_offset)
                {
                    if (!flag_is_set(after, group.first_row_id + level_offset))
                    {
                        return fail(STR("progressive_keyblade_level_flag_readback_failed"), failure_details);
                    }
                }
            }
        }

        if (result)
        {
            *result = applied;
        }
        return true;
    }
}
