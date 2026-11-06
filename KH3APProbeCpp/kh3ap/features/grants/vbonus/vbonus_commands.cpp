#include <algorithm>
#include <cstring>
#include <span>
#include "kh3ap/features/player/combat/magic_command_ids.h"
#include "kh3ap/features/grants/vbonus/vbonus_grant_internal.h"
#include "kh3ap/features/grants/command_refresh_plan.h"
#include "kh3ap/features/world/space/space_service.h"
#include "kh3ap/features/kh02/story/kh02_story.h"
#include "kh3ap/features/grants/grant_internal_call_depth.h"
#include "kh3ap/features/grants/delivery/item_call_helpers.h"
#include "kh3ap/features/grants/keyblades/keyblade_enhancement_profile.h"
#include "kh3ap/features/player/objects/object_resolver.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/app/state.h"
#include <UnrealDef.hpp>
#include <Unreal/UObject.hpp>
#include <array>
#include <string>

namespace kh3ap
{
    namespace
    {
        constexpr auto steam_register_heartbinder_rva = 0xEA0A70;
        constexpr auto egs_register_heartbinder_rva = 0x1137520;
        constexpr auto first_heartbinder_command_id = 0x906;
    } // namespace

    using namespace RC;
    using namespace RC::Unreal;
    using namespace vbonus_detail;

    auto try_refresh_runtime_sora_link_commands(UObject* sora_pawn, RuntimeState& runtime_state,
                                                std::wstring* failure_reason) -> bool
    {
        const RuntimeHotPathProfileScope profile{L"commands:links_refresh"};
        if (RuntimeSpaceService::is_space_active() || !is_uobject_valid_fast(sora_pawn) ||
            !RuntimeObjectResolver::is_sora_ability_pawn_candidate(sora_pawn))
        {
            return false;
        }
        VanillaSoraAbilityBanks banks{};
        using RegisterHeartbinderCommand = void (*)(void*, int32_t, int32_t);
        RegisterHeartbinderCommand register_command{};
        constexpr std::array tokens{L"KEY_ITEM06", L"KEY_ITEM07", L"KEY_ITEM08", L"KEY_ITEM09",
                                    L"KEY_ITEM10"};
        return refresh_owned_commands<tokens.size()>(
            [&](size_t index, bool& owned)
            {
                return try_call_bp_tres_have_item(
                    tokens[index], sora_pawn, owned, runtime_state,
                    STR("/Script/TresGame.TresGameBlueprintLibrary:BP_TresHaveItem"),
                    STR("/Script/TresGame.Default__TresGameBlueprintLibrary"), false,
                    failure_reason);
            },
            [&]()
            {
                NativeVBonusDisplayHelpers helpers{};
                if (!try_resolve_vanilla_sora_ability_banks(sora_pawn, &banks, failure_reason) ||
                    !try_resolve_native_vbonus_display_helpers(banks.module_base, false, true,
                                                               &helpers, failure_reason))
                {
                    return false;
                }
                if (!looks_like_process_pointer(
                        reinterpret_cast<uint64_t>(helpers.resolve_magic_shortcut_manager())))
                {
                    return fail_vanilla_ability_bank(L"link_command_manager_not_ready",
                                                     failure_reason);
                }

                // BP_TresGetItem's post-inventory routine registers Heartbinder commands
                // independently of adding inventory. It checks the five existing slots,
                // inserts only missing commands, and refreshes the native command UI.
                // Both executable profiles are verified before resolving these RVAs.
                constexpr std::array<uint8_t, 64> egs_signature{
                    0x45, 0x85, 0xC0, 0x0F, 0x8E, 0x59, 0x02, 0x00, 0x00, 0x48, 0x8B, 0xC4, 0x57,
                    0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x40, 0x48,
                    0xC7, 0x40, 0xC0, 0xFE, 0xFF, 0xFF, 0xFF, 0x48, 0x89, 0x58, 0x08, 0x48, 0x89,
                    0x68, 0x10, 0x48, 0x89, 0x70, 0x18, 0x44, 0x8B, 0xEA, 0x4C, 0x8B, 0xE1, 0x8D,
                    0x82, 0xFA, 0xF6, 0xFF, 0xFF, 0x33, 0xF6, 0x83, 0xF8, 0x04, 0x0F, 0x87,
                };
                constexpr std::array<uint8_t, 64> steam_signature{
                    0x45, 0x85, 0xC0, 0x0F, 0x8E, 0x9B, 0x00, 0x00, 0x00, 0x48, 0x89, 0x5C, 0x24,
                    0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x8D, 0x82, 0xFA, 0xF6, 0xFF, 0xFF, 0x8B,
                    0xFA, 0x48, 0x8B, 0xD9, 0x83, 0xF8, 0x04, 0x77, 0x28, 0x48, 0x89, 0x74, 0x24,
                    0x30, 0xE8, 0x83, 0x21, 0x68, 0x00, 0x48, 0x8B, 0xF0, 0x48, 0x85, 0xC0, 0x74,
                    0x11, 0x8B, 0xCF, 0xE8, 0xA4, 0x67, 0x28, 0x00, 0x8B, 0xD0, 0x48, 0x8B,
                };
                const bool steam = native_vbonus_module_cache().profile->storefront == STR("steam");
                const auto address = banks.module_base + (steam ? steam_register_heartbinder_rva
                                                                : egs_register_heartbinder_rva);
                const NativeKeybladeEnhancementSignature signature{
                    steam ? steam_signature.data() : egs_signature.data(), egs_signature.size()};
                if (!native_signature_matches(address, signature))
                {
                    return fail_vanilla_ability_bank(L"heartbinder_command_signature_mismatch",
                                                     failure_reason);
                }
                register_command = reinterpret_cast<RegisterHeartbinderCommand>(address);
                return true;
            },
            [&](size_t index)
            {
                const RuntimeGrantInternalCallDepth::Scope internal_call;
                register_command(reinterpret_cast<void*>(banks.save_owner),
                                 first_heartbinder_command_id + static_cast<int32_t>(index), 1);
                return true;
            });
    }

    auto
    try_read_runtime_sora_magic_commands(UObject* context,
                                         std::array<uint8_t, magic_command::family_count>& commands,
                                         std::wstring* failure_reason) -> bool
    {
        commands = {};
        VanillaSoraAbilityBanks banks{};
        if (!try_resolve_vanilla_sora_ability_banks(context, &banks, failure_reason))
        {
            return false;
        }
        const auto counts = try_read_process_bytes(
            banks.active_record + s_sora_vbonus_count_table_offset + s_first_magic_vbonus_kind,
            commands.size());
        if (!counts || counts->size() != commands.size())
        {
            return fail_vanilla_ability_bank(L"magic_command_counts_unreadable", failure_reason);
        }
        NativeVBonusDisplayHelpers helpers{};
        if (!try_resolve_native_vbonus_display_helpers(banks.module_base, false, true, &helpers,
                                                       failure_reason))
        {
            return false;
        }
        for (size_t family = 0; family < commands.size(); ++family)
        {
            if ((*counts)[family] == std::byte{})
            {
                continue;
            }
            const auto command = helpers.resolve_magic_command(
                static_cast<uint8_t>(s_first_magic_vbonus_kind + family),
                reinterpret_cast<void*>(banks.active_record));
            // The shipping normal spell groups are Fire29..Curaga51, stride4.
            const int first = magic_command::first_normal +
                              static_cast<int>(family) * magic_command::normal_family_stride;
            if (command < first || command > first + magic_command::last_normal_tier_offset)
            {
                return fail_vanilla_ability_bank(L"magic_command_tier_invalid", failure_reason);
            }
            commands[family] = static_cast<uint8_t>(command);
        }
        return true;
    }

    auto try_refresh_runtime_sora_magic_commands(UObject* sora_pawn, std::wstring* failure_reason)
        -> bool
    {
        const RuntimeHotPathProfileScope profile{L"commands:magic_refresh"};
        if (RuntimeSpaceService::is_space_active() || !is_uobject_valid_fast(sora_pawn) ||
            !RuntimeObjectResolver::is_sora_ability_pawn_candidate(sora_pawn))
        {
            return false;
        }
        VanillaSoraAbilityBanks banks{};
        if (!try_resolve_vanilla_sora_ability_banks(sora_pawn, &banks, failure_reason))
        {
            return false;
        }
        constexpr size_t magic_count = s_last_magic_vbonus_kind - s_first_magic_vbonus_kind + 1;
        const auto counts = read_command_bonus_counts<magic_count>(
            s_first_magic_vbonus_kind, s_last_vbonus_kind,
            [&](uint8_t first, std::span<uint8_t> values)
            {
                const RuntimeHotPathProfileScope count_profile{L"commands:bonus_counts_snapshot"};
                if (!looks_like_process_pointer(banks.active_record))
                {
                    return false;
                }
                const auto bytes = try_read_process_bytes(
                    banks.active_record + s_sora_vbonus_count_table_offset + first, values.size());
                if (!bytes || bytes->size() != values.size())
                {
                    return false;
                }
                std::memcpy(values.data(), bytes->data(), values.size());
                return true;
            });
        if (!counts)
        {
            return fail_vanilla_ability_bank(L"magic_command_counts_unreadable", failure_reason);
        }
        if (std::none_of(counts->begin(), counts->end(),
                         [](uint8_t value)
                         {
                             return value != 0;
                         }))
        {
            return true;
        }
        NativeVBonusDisplayHelpers helpers{};
        if (!try_resolve_native_vbonus_display_helpers(banks.module_base, false, true, &helpers,
                                                       failure_reason))
        {
            return false;
        }
        void* const manager = helpers.resolve_magic_shortcut_manager();
        if (!looks_like_process_pointer(reinterpret_cast<uint64_t>(manager)))
        {
            return fail_vanilla_ability_bank(L"magic_command_manager_not_ready", failure_reason);
        }
        for (uint8_t bonus = s_first_magic_vbonus_kind; bonus <= s_last_magic_vbonus_kind; ++bonus)
        {
            if ((*counts)[bonus - s_first_magic_vbonus_kind] == 0)
            {
                continue;
            }
            const int32_t command =
                helpers.resolve_magic_command(bonus, reinterpret_cast<void*>(banks.active_record));
            if (command <= 0)
            {
                return false;
            }
            // The native VBonus UI path also reconciles the configured battle
            // command list, upgrading an unlocked entry in place. Space has no
            // Sora/UI owner to run this step. Retry may restore an older list
            // even when the persistent reward flag and bonus count survive.
            helpers.refresh_magic_shortcuts(manager, command);
        }
        return true;
    }
} // namespace kh3ap
