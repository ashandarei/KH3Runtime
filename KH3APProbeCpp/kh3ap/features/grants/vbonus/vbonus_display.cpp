#include "kh3ap/features/grants/vbonus/vbonus_grant_internal.h"
#include "kh3ap/features/grants/keyblades/keyblade_enhancement_profile.h"
#include <UnrealDef.hpp>
#include <Unreal/UObject.hpp>
#include <optional>

namespace kh3ap
{
    using namespace RC;
    using namespace RC::Unreal;
    using namespace vbonus_detail;

    namespace vbonus_detail
    {
        auto resolve_egs_vbonus_stat_display_amounts(int32_t difficulty, uint8_t bonus,
                                                     int32_t* hp_amount, int32_t* mp_amount) -> void
        {
            if (!hp_amount || !mp_amount)
            {
                return;
            }
            *hp_amount = 0;
            *mp_amount = 0;
            if (bonus < s_first_hp_vbonus_kind || bonus > s_last_mp_vbonus_kind)
            {
                return;
            }

            const auto& module = native_vbonus_module_cache();
            if (!module.ready || module.profile->storefront != STR("egs"))
            {
                return;
            }

            // The EGS leaf accepts an already-range-checked zero-based switch
            // index and does not initialize its output pointers. Adapt it to
            // the Steam-shaped interface used by the shared grant path.
            const auto resolve_stat_amounts = reinterpret_cast<ResolveEgsVBonusStatDisplayAmounts>(
                module.module_base + s_egs_resolve_vbonus_stat_display_amounts_rva);
            resolve_stat_amounts(difficulty, hp_amount, mp_amount, static_cast<int32_t>(bonus - 1));
        }

        auto resolve_egs_magic_shortcut_manager() -> void*
        {
            const auto& module = native_vbonus_module_cache();
            if (!module.ready || module.profile->storefront != STR("egs"))
            {
                return nullptr;
            }

            // EGS inlines this chain in its native victory-bonus delivery
            // routine. Do not retain any of these raw pointers beyond this
            // immediate lookup.
            const auto root = try_read_process_value<uint64_t>(
                module.module_base + s_egs_magic_shortcut_root_global_rva);
            const auto root_object =
                root.has_value()
                    ? try_read_process_value<uint64_t>(*root + s_magic_shortcut_root_object_offset)
                    : std::nullopt;
            if (!root_object.has_value() ||
                !kh3ap::is_uobject_valid_fast(reinterpret_cast<UObject*>(*root_object)))
            {
                return nullptr;
            }
            const auto owner =
                try_read_process_value<uint64_t>(*root_object + s_magic_shortcut_owner_offset);
            const auto manager =
                owner.has_value()
                    ? try_read_process_value<uint64_t>(*owner + s_magic_shortcut_manager_offset)
                    : std::nullopt;
            return manager.has_value() ? reinterpret_cast<void*>(*manager) : nullptr;
        }

        auto native_vbonus_hud_function_cache() -> const NativeVBonusHudFunctionCache&
        {
            static const NativeVBonusHudFunctionCache cache = []()
            {
                NativeVBonusHudFunctionCache result{};
                const auto& module = native_vbonus_module_cache();
                if (!module.ready)
                {
                    result.failure_reason = STR("native_vbonus_hud_requires_supported_image");
                    return result;
                }

                const bool is_steam = module.profile->storefront == STR("steam");
                const bool is_egs = module.profile->storefront == STR("egs");
                if (!is_steam && !is_egs)
                {
                    result.failure_reason = STR("native_vbonus_hud_storefront_unsupported");
                    return result;
                }

                const uint64_t resolve_hud_owner_address =
                    module.module_base + (is_steam ? s_steam_resolve_vbonus_hud_owner_rva
                                                   : s_egs_resolve_vbonus_hud_owner_rva);
                const uint64_t present_hud_address =
                    module.module_base +
                    (is_steam ? s_steam_present_vbonus_hud_rva : s_egs_present_vbonus_hud_rva);
                const NativeKeybladeEnhancementSignature resolve_hud_owner_signature{
                    is_steam ? s_steam_resolve_vbonus_hud_owner_signature.data()
                             : s_egs_resolve_vbonus_hud_owner_signature.data(),
                    is_steam ? s_steam_resolve_vbonus_hud_owner_signature.size()
                             : s_egs_resolve_vbonus_hud_owner_signature.size(),
                };
                const NativeKeybladeEnhancementSignature present_hud_signature{
                    is_steam ? s_steam_present_vbonus_hud_signature.data()
                             : s_egs_present_vbonus_hud_signature.data(),
                    is_steam ? s_steam_present_vbonus_hud_signature.size()
                             : s_egs_present_vbonus_hud_signature.size(),
                };
                if (!native_signature_matches(resolve_hud_owner_address,
                                              resolve_hud_owner_signature))
                {
                    result.failure_reason =
                        STR("native_vbonus_hud_owner_resolver_signature_mismatch");
                    return result;
                }
                if (!native_signature_matches(present_hud_address, present_hud_signature))
                {
                    result.failure_reason = STR("native_vbonus_hud_presenter_signature_mismatch");
                    return result;
                }

                result.resolve_hud_owner =
                    reinterpret_cast<ResolveVBonusHudOwner>(resolve_hud_owner_address);
                result.present = reinterpret_cast<PresentVBonusHud>(present_hud_address);
                result.ready = true;
                return result;
            }();
            return cache;
        }

        auto native_vbonus_stat_function_cache() -> const NativeVBonusStatFunctionCache&
        {
            static const NativeVBonusStatFunctionCache cache = []()
            {
                NativeVBonusStatFunctionCache result{};
                const auto& module = native_vbonus_module_cache();
                if (!module.ready)
                {
                    result.failure_reason =
                        STR("native_vbonus_display_helper_requires_supported_image");
                    return result;
                }

                const bool is_steam = module.profile->storefront == STR("steam");
                const bool is_egs = module.profile->storefront == STR("egs");
                if (!is_steam && !is_egs)
                {
                    result.failure_reason =
                        STR("native_vbonus_display_helper_storefront_unsupported");
                    return result;
                }

                const uint64_t address =
                    module.module_base + (is_steam ? s_steam_resolve_vbonus_stat_display_amounts_rva
                                                   : s_egs_resolve_vbonus_stat_display_amounts_rva);
                const NativeKeybladeEnhancementSignature signature{
                    is_steam ? s_steam_resolve_vbonus_stat_display_amounts_signature.data()
                             : s_egs_resolve_vbonus_stat_display_amounts_signature.data(),
                    is_steam ? s_steam_resolve_vbonus_stat_display_amounts_signature.size()
                             : s_egs_resolve_vbonus_stat_display_amounts_signature.size(),
                };
                if (!native_signature_matches(address, signature))
                {
                    result.failure_reason =
                        STR("native_vbonus_stat_display_resolver_signature_mismatch");
                    return result;
                }

                result.resolve_stat_amounts =
                    is_steam ? reinterpret_cast<ResolveVBonusStatDisplayAmounts>(address)
                             : &resolve_egs_vbonus_stat_display_amounts;
                result.ready = true;
                return result;
            }();
            return cache;
        }

        auto native_vbonus_magic_function_cache() -> const NativeVBonusMagicFunctionCache&
        {
            static const NativeVBonusMagicFunctionCache cache = []()
            {
                NativeVBonusMagicFunctionCache result{};
                const auto& module = native_vbonus_module_cache();
                if (!module.ready)
                {
                    result.failure_reason =
                        STR("native_vbonus_display_helper_requires_supported_image");
                    return result;
                }

                const bool is_steam = module.profile->storefront == STR("steam");
                const bool is_egs = module.profile->storefront == STR("egs");
                if (!is_steam && !is_egs)
                {
                    result.failure_reason =
                        STR("native_vbonus_display_helper_storefront_unsupported");
                    return result;
                }

                const uint64_t command_address =
                    module.module_base + (is_steam ? s_steam_resolve_magic_display_command_rva
                                                   : s_egs_resolve_magic_display_command_rva);
                const uint64_t manager_address =
                    module.module_base + (is_steam
                                              ? s_steam_resolve_magic_shortcut_manager_rva
                                              : s_egs_resolve_magic_shortcut_manager_inline_rva);
                const uint64_t refresh_address =
                    module.module_base + (is_steam ? s_steam_refresh_magic_shortcuts_rva
                                                   : s_egs_refresh_magic_shortcuts_rva);
                const NativeKeybladeEnhancementSignature command_signature{
                    is_steam ? s_steam_resolve_magic_display_command_signature.data()
                             : s_egs_resolve_magic_display_command_signature.data(),
                    is_steam ? s_steam_resolve_magic_display_command_signature.size()
                             : s_egs_resolve_magic_display_command_signature.size(),
                };
                const NativeKeybladeEnhancementSignature manager_signature{
                    is_steam ? s_steam_resolve_magic_shortcut_manager_signature.data()
                             : s_egs_resolve_magic_shortcut_manager_inline_signature.data(),
                    is_steam ? s_steam_resolve_magic_shortcut_manager_signature.size()
                             : s_egs_resolve_magic_shortcut_manager_inline_signature.size(),
                };
                const NativeKeybladeEnhancementSignature refresh_signature{
                    is_steam ? s_steam_refresh_magic_shortcuts_signature.data()
                             : s_egs_refresh_magic_shortcuts_signature.data(),
                    is_steam ? s_steam_refresh_magic_shortcuts_signature.size()
                             : s_egs_refresh_magic_shortcuts_signature.size(),
                };
                if (!native_signature_matches(command_address, command_signature))
                {
                    result.failure_reason =
                        STR("native_vbonus_magic_display_resolver_signature_mismatch");
                    return result;
                }
                if (!native_signature_matches(manager_address, manager_signature))
                {
                    result.failure_reason =
                        STR("native_vbonus_magic_shortcut_manager_signature_mismatch");
                    return result;
                }
                if (!native_signature_matches(refresh_address, refresh_signature))
                {
                    result.failure_reason =
                        STR("native_vbonus_magic_shortcut_refresh_signature_mismatch");
                    return result;
                }

                result.resolve_magic_command =
                    reinterpret_cast<ResolveMagicDisplayCommand>(command_address);
                result.resolve_magic_shortcut_manager =
                    is_steam ? reinterpret_cast<ResolveMagicShortcutManager>(manager_address)
                             : &resolve_egs_magic_shortcut_manager;
                result.refresh_magic_shortcuts =
                    reinterpret_cast<RefreshMagicShortcuts>(refresh_address);
                result.ready = true;
                return result;
            }();
            return cache;
        }
    } // namespace vbonus_detail

} // namespace kh3ap
