#include "kh3ap/features/player/characters/character_swap.h"
#include "kh3ap/features/grants/vbonus/vbonus_grant_internal.h"
#include "kh3ap/features/grants/abilities/essential_auto_equip_policy.h"
#include "kh3ap/features/world/space/space_service.h"
#include "kh3ap/features/kh02/story/kh02_story.h"
#include "kh3ap/app/enums.h"
#include "kh3ap/features/grants/grant_tokens.h"
#include "kh3ap/features/grants/delivery/item_call_helpers.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/app/state.h"
#include "kh3ap/core/types.h"
#include <UnrealDef.hpp>
#include <Unreal/UObject.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#include <chrono>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kh3ap
{
    using namespace RC;
    using namespace RC::Unreal;
    using namespace vbonus_detail;

    namespace vbonus_detail
    {
        auto try_add_direct_durable_vbonus(const VanillaSoraAbilityBanks& banks,
                                           uint64_t bonus_value, std::wstring* failure_reason)
            -> bool
        {
            const auto active_before =
                try_read_vanilla_vbonus_count(banks.active_record, bonus_value);
            const auto staging_before =
                try_read_vanilla_vbonus_count(banks.staging_record, bonus_value);
            if (!active_before.has_value() || !staging_before.has_value())
            {
                return fail_vanilla_ability_bank(
                    STR("direct_durable_vbonus_pre_grant_count_unreadable"), failure_reason);
            }
            if (*active_before != *staging_before)
            {
                return fail_vanilla_ability_bank(
                    std::format(STR("direct_durable_vbonus_pre_grant_banks_out_of_sync:bonus={}:"
                                    "active={}:staging={}"),
                                bonus_value, *active_before, *staging_before),
                    failure_reason);
            }
            if (*active_before == UINT8_MAX)
            {
                return fail_vanilla_ability_bank(
                    std::format(STR("direct_durable_vbonus_count_capacity_exhausted:bonus={}"),
                                bonus_value),
                    failure_reason);
            }
            if (!banks.add_vbonus || (!banks.persistent_only &&
                                      (!banks.add_vbonus_to_equipment ||
                                       !looks_like_process_pointer(banks.active_vbonus_target))))
            {
                return fail_vanilla_ability_bank(
                    STR("direct_durable_vbonus_native_mutator_unavailable"), failure_reason);
            }

            const uint8_t native_bonus_value = static_cast<uint8_t>(bonus_value);
            const bool active_applied =
                banks.persistent_only
                    ? banks.add_vbonus(reinterpret_cast<void*>(banks.active_record),
                                       native_bonus_value, 0, true)
                    : banks.add_vbonus_to_equipment(
                          reinterpret_cast<void*>(banks.active_vbonus_target), banks.difficulty,
                          native_bonus_value, 0, true);
            if (!active_applied)
            {
                return fail_vanilla_ability_bank(
                    std::format(STR("direct_durable_vbonus_active_grant_rejected:bonus={}"),
                                bonus_value),
                    failure_reason);
            }
            const bool staging_applied = banks.add_vbonus(
                reinterpret_cast<void*>(banks.staging_record), native_bonus_value, 0, true);

            const auto active_after =
                try_read_vanilla_vbonus_count(banks.active_record, bonus_value);
            const auto staging_after =
                try_read_vanilla_vbonus_count(banks.staging_record, bonus_value);
            const uint8_t expected_after = static_cast<uint8_t>(*active_before + 1);
            if (!staging_applied || !active_after.has_value() || !staging_after.has_value() ||
                *active_after != expected_after || *staging_after != expected_after)
            {
                return fail_vanilla_ability_bank(
                    std::format(STR("direct_durable_vbonus_post_grant_verification_failed:bonus={}:"
                                    "active={}:staging={}:expected={}"),
                                bonus_value, active_after.value_or(0), staging_after.value_or(0),
                                expected_after),
                    failure_reason);
            }

            Output::send<LogLevel::Verbose>(
                STR("[KH3APProbeCpp] Direct durable Sora VBonus grant bonus={} active_before={} "
                    "active_after={} staging_after={} difficulty={}\n"),
                bonus_value, *active_before, *active_after, *staging_after, banks.difficulty);
            return true;
        }
    } // namespace vbonus_detail

    auto try_call_durable_sora_vbonus_grant(const RuntimeItemGrantRequest& request,
                                            UObject* sora_pawn, RuntimeState& runtime_state,
                                            std::wstring* failure_reason) -> bool
    {
        const auto profile_started_at = std::chrono::steady_clock::now();
        if (!request.runtime_token.starts_with(STR("ETresVictoryBonusKind::")) ||
            request.runtime_token.ends_with(STR("::NONE")))
        {
            return fail_vanilla_ability_bank(
                STR("durable_sora_vbonus_requires_victory_bonus_token"), failure_reason);
        }
        const auto resolved_bonus = try_resolve_runtime_enum_value(request.runtime_token);
        if (!resolved_bonus.has_value() || *resolved_bonus < s_first_vbonus_kind ||
            *resolved_bonus > s_last_vbonus_kind)
        {
            return fail_vanilla_ability_bank(
                std::format(STR("durable_sora_vbonus_enum_unresolved:{}"), request.runtime_token),
                failure_reason);
        }
        // The dispatcher has verified the current delivery context. Ordinary
        // Sora must own the active equipment record; authorized persistent-only
        // contexts instead select the fixed Sora banks and defer live effects.
        if (!sora_pawn || !kh3ap::is_uobject_valid_fast(sora_pawn))
        {
            return fail_vanilla_ability_bank(
                STR("direct_durable_sora_vbonus_requires_sora_pawn_context"), failure_reason);
        }
        const auto token_resolved_at = std::chrono::steady_clock::now();

        VanillaSoraAbilityBanks vanilla_banks{};
        if (!try_resolve_vanilla_sora_ability_banks(sora_pawn, &vanilla_banks, failure_reason))
        {
            return false;
        }
        const auto banks_resolved_at = std::chrono::steady_clock::now();

        const uint8_t bonus_value = static_cast<uint8_t>(*resolved_bonus);
        const bool is_hp =
            bonus_value >= s_first_hp_vbonus_kind && bonus_value <= s_last_hp_vbonus_kind;
        const bool is_mp =
            bonus_value >= s_first_mp_vbonus_kind && bonus_value <= s_last_mp_vbonus_kind;
        const bool is_magic =
            bonus_value >= s_first_magic_vbonus_kind && bonus_value <= s_last_magic_vbonus_kind;
        const bool show_info =
            RuntimeGrantTokens::effective_show_info(request) && !vanilla_banks.persistent_only;
        const bool mirror_stats = character_swap::current_hero() == sora_pawn;

        NativeVBonusHudPresenter presenter{};
        if (show_info &&
            !try_resolve_native_vbonus_hud_presenter(runtime_state, &presenter, failure_reason))
        {
            return false;
        }
        const auto presenter_resolved_at = std::chrono::steady_clock::now();

        NativeVBonusDisplayHelpers display_helpers{};
        if (!try_resolve_native_vbonus_display_helpers(
                vanilla_banks.module_base, (show_info || mirror_stats) && (is_hp || is_mp),
                is_magic && !vanilla_banks.persistent_only, &display_helpers, failure_reason))
        {
            return false;
        }
        const auto display_helpers_resolved_at = std::chrono::steady_clock::now();

        std::optional<uint64_t> display_entry{};
        int32_t hp_amount = 0;
        int32_t mp_amount = 0;
        if ((show_info || mirror_stats) && (is_hp || is_mp))
        {
            display_helpers.resolve_stat_amounts(vanilla_banks.difficulty, bonus_value, &hp_amount,
                                                 &mp_amount);
            const int32_t amount = is_hp ? hp_amount : mp_amount;
            if (amount <= 0)
            {
                return fail_vanilla_ability_bank(
                    std::format(STR("durable_sora_vbonus_stat_display_amount_unresolved:bonus={}"),
                                static_cast<uint32_t>(bonus_value)),
                    failure_reason);
            }
            if (show_info)
            {
                display_entry = pack_native_vbonus_hud_entry(
                    is_hp ? s_vbonus_hud_hp_type : s_vbonus_hud_mp_type, static_cast<uint32_t>(amount));
            }
        }
        else if (show_info && bonus_value == s_item_slot_vbonus_kind)
        {
            display_entry = pack_native_vbonus_hud_entry(s_vbonus_hud_item_slot_type, 1);
        }
        else if (show_info && bonus_value == s_accessory_slot_vbonus_kind)
        {
            display_entry = pack_native_vbonus_hud_entry(s_vbonus_hud_accessory_slot_type, 1);
        }
        else if (show_info && bonus_value == s_armor_slot_vbonus_kind)
        {
            display_entry = pack_native_vbonus_hud_entry(s_vbonus_hud_armor_slot_type, 1);
        }
        const auto display_prepared_at = std::chrono::steady_clock::now();

        // VBonus counts are derived data: KH3 clears them in its character
        // reconstruction routine and rebuilds them from saved reward flags.
        // The AP receipt ordinal selects the flag; neither inventory contents
        // nor a missing-count comparison participates in replay selection.
        const auto bonus_name =
            request.runtime_token.substr(std::wstring_view{L"ETresVictoryBonusKind::"}.size());
        const auto flag_prefix = std::wstring{L"GIVESORA_"} + bonus_name;
        if (request.runtime_flag_name != flag_prefix &&
            !request.runtime_flag_name.starts_with(flag_prefix + L"_"))
        {
            return fail_vanilla_ability_bank(
                STR("durable_sora_vbonus_requires_matching_persistent_reward_flag"),
                failure_reason);
        }
        bool flag_owned = false;
        if (!try_call_get_game_flag_boolean(request.runtime_flag_name, flag_owned, runtime_state,
                                            s_get_bonus_flag_path, s_bonus_flag_library_path, false,
                                            failure_reason))
        {
            return false;
        }
        if (flag_owned)
        {
            // This receipt's native reward has already been recorded. A retry
            // must not add it again. The generated pool fits the finite native
            // reward catalog; extra console copies reuse an owned flag.
            // Checkpoint replay may retain the reward while rolling back the
            // separate command list. Reconcile it without incrementing counts.
            return !is_magic || vanilla_banks.persistent_only ||
                   try_refresh_runtime_sora_magic_commands(sora_pawn, failure_reason);
        }
        if (!try_call_set_game_flag_boolean(request.runtime_flag_name, true, runtime_state,
                                            s_set_bonus_flag_path, s_bonus_flag_library_path, false,
                                            failure_reason) ||
            !try_call_get_game_flag_boolean(request.runtime_flag_name, flag_owned, runtime_state,
                                            s_get_bonus_flag_path, s_bonus_flag_library_path, false,
                                            failure_reason) ||
            !flag_owned)
        {
            return fail_vanilla_ability_bank(
                STR("durable_sora_vbonus_persistent_reward_flag_not_set"), failure_reason);
        }
        if (!try_add_direct_durable_vbonus(vanilla_banks, bonus_value, failure_reason))
        {
            try_call_set_game_flag_boolean(request.runtime_flag_name, false, runtime_state,
                                           s_set_bonus_flag_path, s_bonus_flag_library_path, false,
                                           nullptr);
            return false;
        }
        const auto persistent_applied_at = std::chrono::steady_clock::now();
        if (mirror_stats)
        {
            character_swap::apply_stat_bonus(hp_amount, mp_amount);
        }

        int32_t magic_command = 0;
        auto magic_resolved_at = persistent_applied_at;
        auto shortcuts_refreshed_at = persistent_applied_at;
        if (is_magic && !vanilla_banks.persistent_only)
        {
            magic_command = display_helpers.resolve_magic_command(
                bonus_value, reinterpret_cast<void*>(vanilla_banks.active_record));
            magic_resolved_at = std::chrono::steady_clock::now();
            if (magic_command > 0)
            {
                void* const shortcut_manager = display_helpers.resolve_magic_shortcut_manager();
                if (looks_like_process_pointer(reinterpret_cast<uint64_t>(shortcut_manager)))
                {
                    display_helpers.refresh_magic_shortcuts(shortcut_manager, magic_command);
                }
                if (show_info)
                {
                    display_entry = pack_native_vbonus_hud_entry(
                        s_vbonus_hud_magic_type, static_cast<uint32_t>(magic_command));
                }
                shortcuts_refreshed_at = std::chrono::steady_clock::now();
            }
            else
            {
                // The persistent count already advanced. Do not report a
                // failed delivery and risk duplicating it on retry merely
                // because the optional display command could not be built.
                Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] Direct durable Sora magic grant applied without display "
                        "command token={} bonus={}\n"),
                    request.runtime_token, static_cast<uint32_t>(bonus_value));
                shortcuts_refreshed_at = std::chrono::steady_clock::now();
            }
        }

        if (show_info && display_entry.has_value())
        {
            present_native_vbonus_hud_entries(presenter, sora_pawn,
                                              std::vector<uint64_t>{*display_entry},
                                              STR("normal_ap_vbonus"));
        }
        const auto hud_presented_at = std::chrono::steady_clock::now();
        const auto elapsed_us = [](auto begin, auto end) -> uint64_t
        {
            return static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count());
        };
        Output::send<LogLevel::Verbose>(
            STR("[KH3APProbeCpp] Durable Sora VBonus profile token_us={} banks_us={} "
                "presenter_us={} helpers_us={} display_us={} persistent_us={} magic_resolve_us={} "
                "shortcuts_us={} hud_us={} total_us={} token={}\n"),
            elapsed_us(profile_started_at, token_resolved_at),
            elapsed_us(token_resolved_at, banks_resolved_at),
            elapsed_us(banks_resolved_at, presenter_resolved_at),
            elapsed_us(presenter_resolved_at, display_helpers_resolved_at),
            elapsed_us(display_helpers_resolved_at, display_prepared_at),
            elapsed_us(display_prepared_at, persistent_applied_at),
            elapsed_us(persistent_applied_at, magic_resolved_at),
            elapsed_us(magic_resolved_at, shortcuts_refreshed_at),
            elapsed_us(shortcuts_refreshed_at, hud_presented_at),
            elapsed_us(profile_started_at, hud_presented_at), request.runtime_token);
        Output::send<LogLevel::Verbose>(
            STR("[KH3APProbeCpp] Direct durable Sora VBonus delivery token={} bonus={} "
                "magic_command={} show_info={} hud_entry={}\n"),
            request.runtime_token, static_cast<uint32_t>(bonus_value), magic_command,
            show_info ? STR("true") : STR("false"),
            display_entry.has_value() ? STR("yes") : STR("no"));
        return true;
    }

    auto try_call_durable_sora_ability_grant(const RuntimeItemGrantRequest& request,
                                             UObject* sora_pawn, RuntimeState& runtime_state,
                                             std::wstring* failure_reason,
                                             bool auto_equip_essential) -> bool
    {
        std::wstring ability_token{};
        uint64_t ability_value = 0;
        if (!try_resolve_single_ability_value(request, &ability_token, &ability_value,
                                              failure_reason))
        {
            return false;
        }

        // Context authorization and persistent Sora record identity are checked
        // separately. The bank resolver never treats Aqua's equipment as Sora's.
        if (!sora_pawn || !kh3ap::is_uobject_valid_fast(sora_pawn))
        {
            return fail_vanilla_ability_bank(
                STR("direct_durable_sora_ability_requires_sora_pawn_context"), failure_reason);
        }

        const bool show_info = RuntimeGrantTokens::effective_show_info(request) &&
                               !RuntimeSpaceService::is_space_active() &&
                               persistent_player_reward_context() != sora_pawn;
        NativeVBonusHudPresenter presenter{};
        VanillaSoraAbilityBanks vanilla_banks{};
        bool newly_granted = false;
        if (!try_resolve_vanilla_sora_ability_banks(sora_pawn, &vanilla_banks, failure_reason))
        {
            return false;
        }
        uint32_t equip_mask = 0;
        if (auto_equip_essential && is_auto_equip_essential_ability(ability_token))
        {
            std::wstring equip_failure;
            if (!try_essential_auto_equip_mask(vanilla_banks, sora_pawn, ability_value,
                                               &equip_mask, &equip_failure))
            {
                // Optional convenience must never reject or repeat an item receipt.
                Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] Essential auto equip skipped token={} reason={}\n"),
                    ability_token, equip_failure);
            }
        }
        if (!try_add_direct_durable_ability_source(vanilla_banks, ability_value, runtime_state,
                                                   show_info ? &presenter : nullptr, newly_granted,
                                                   failure_reason, equip_mask))
        {
            return false;
        }

        if (!newly_granted)
        {
            return true;
        }

        if (show_info)
        {
            present_native_vbonus_hud_abilities(
                presenter, sora_pawn, std::vector<uint64_t>{ability_value}, STR("normal_ap"));
        }
        Output::send<LogLevel::Verbose>(STR("[KH3APProbeCpp] Direct durable Sora ability delivery "
                                            "token={} ability={} show_info={}\n"),
                                        ability_token, ability_value,
                                        show_info ? STR("true") : STR("false"));
        return true;
    }
} // namespace kh3ap
