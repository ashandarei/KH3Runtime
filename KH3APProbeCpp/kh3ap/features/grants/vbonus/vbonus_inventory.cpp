#include "kh3ap/features/player/characters/character_swap.h"
#include "kh3ap/features/grants/vbonus/vbonus_grant_internal.h"
#include "kh3ap/features/kh02/story/kh02_story.h"
#include "kh3ap/features/kh02/rewards/kh02_inventory_profile.h"
#include "kh3ap/features/kh02/rewards/kh02_item_notice.h"
#include "kh3ap/hooks/inventory/item_catalog.h"
#include "kh3ap/core/events/keyblade_inventory_events.h"
#include "kh3ap/features/grants/grant_internal_call_depth.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include <UnrealDef.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/NameTypes.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#include <array>
#include <string>
#include <string_view>

namespace kh3ap
{
    using namespace RC;
    using namespace RC::Unreal;
    using namespace vbonus_detail;

    auto try_call_persistent_inventory_grant(std::wstring_view token, UObject* aqua, bool show_info,
                                             std::wstring* failure_reason) -> bool
    {
        if (!aqua || persistent_player_reward_context() != aqua || token.empty())
        {
            return fail_vanilla_ability_bank(L"kh02_inventory_requires_ready_aqua", failure_reason);
        }
        VanillaSoraAbilityBanks banks{};
        if (!try_resolve_vanilla_sora_ability_banks(aqua, &banks, failure_reason) ||
            !banks.persistent_only)
        {
            return false;
        }
        const bool steam = native_vbonus_module_cache().profile->storefront == L"steam";
        const auto& profile = steam ? kh02rewards::steam_inventory : kh02rewards::egs_inventory;
        const std::array addresses{profile.catalog_rva, profile.add_rva, profile.entry_rva};
        uintptr_t catalog_address = banks.module_base + profile.catalog_rva;
        for (size_t index = 0; index < addresses.size(); ++index)
        {
            if (!native_signature_matches(
                    banks.module_base + addresses[index],
                    {profile.signatures[index].data(), profile.signatures[index].size()}))
            {
                const auto original =
                    index == 0 ? original_item_catalog_lookup(catalog_address) : 0;
                // Our resolver detour replaces the first eight bytes. Validate
                // the remaining native signature as well as the hook's ownership.
                constexpr size_t resolver_detour_prefix_bytes = 8;
                if (!original ||
                    !native_signature_matches(
                        catalog_address + resolver_detour_prefix_bytes,
                        {profile.signatures[index].data() + resolver_detour_prefix_bytes,
                         profile.signatures[index].size() - resolver_detour_prefix_bytes}))
                {
                    return fail_vanilla_ability_bank(L"persistent_inventory_signature_mismatch",
                                                     failure_reason);
                }
                catalog_address = original;
            }
        }
        const auto catalog = try_read_process_value<uint64_t>(
            banks.save_owner + kh02rewards::item_catalog_owner_offset);
        if (!catalog || !is_uobject_valid_fast(reinterpret_cast<UObject*>(*catalog)))
        {
            return fail_vanilla_ability_bank(L"persistent_inventory_catalog_unavailable",
                                             failure_reason);
        }
        using CatalogId = int32_t (*)(UObject*, FName);
        using InventoryEntry = uint8_t* (*)(void*, int32_t);
        using AddInventory = int32_t (*)(void*, int32_t, int32_t, bool);
        const auto catalog_id = reinterpret_cast<CatalogId>(catalog_address);
        const auto entry = reinterpret_cast<InventoryEntry>(banks.module_base + profile.entry_rva);
        const auto add = reinterpret_cast<AddInventory>(banks.module_base + profile.add_rva);
        const auto item_id =
            catalog_id(reinterpret_cast<UObject*>(*catalog), FName(token, FNAME_Add));
        if (item_id <= 0)
        {
            return fail_vanilla_ability_bank(L"persistent_inventory_unknown_item", failure_reason);
        }
        // Shared inventory uses the same active bank as vanilla BP_TresGetItem.
        // Sora's ability/VBonus backend separately updates both character banks.
        auto* save_data = reinterpret_cast<void*>(banks.save_owner + s_active_save_data_offset);
        auto* inventory = reinterpret_cast<void*>(banks.save_owner + s_active_save_data_offset +
                                                  kh02rewards::inventory_from_save_data_offset);
        auto* item = entry(inventory, item_id);
        const auto before = try_read_process_value<uint8_t>(reinterpret_cast<uint64_t>(item));
        if (!before)
        {
            return fail_vanilla_ability_bank(L"persistent_inventory_entry_unavailable",
                                             failure_reason);
        }
        const RuntimeGrantInternalCallDepth::Scope internal_call;
        const int32_t added = add(save_data, item_id, 1, true);
        const auto after = try_read_process_value<uint8_t>(reinterpret_cast<uint64_t>(item));
        if (!after || added < 0 || added > 1 || *after != *before + added || *after == 0)
        {
            return fail_vanilla_ability_bank(L"persistent_inventory_readback_mismatch",
                                             failure_reason);
        }
        notify_runtime_keyblade_inventory_changed(token);
        if (show_info)
        {
            const auto& hud_functions = native_vbonus_hud_function_cache();
            auto* controller = try_read_reflected_object_property_value(aqua, L"Controller");
            auto* hud_owner = hud_functions.ready && is_uobject_valid_fast(controller)
                                  ? hud_functions.resolve_hud_owner(controller)
                                  : nullptr;
            const bool shown =
                kh02rewards::present_inventory_item(aqua, hud_owner, item_id, added == 0);
            Output::send<LogLevel::Verbose>(
                STR("[KH3APProbeCpp] Aqua native item popup token={} shown={}\n"), token, shown);
        }
        Output::send<LogLevel::Verbose>(
            STR("[KH3APProbeCpp] Aqua persistent inventory token={} before={} after={} added={}\n"),
            token, *before, *after, added);
        return true;
    }
} // namespace kh3ap
