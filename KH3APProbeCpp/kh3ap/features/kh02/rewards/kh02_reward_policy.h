#pragma once
#include <string_view>

namespace kh3ap::kh02rewards
{
    constexpr auto uses_native_ability_popup(std::wstring_view method) -> bool
    {
        return method == L"controller_unlock" || method == L"durable_sora_ability";
    }

    // Ownership-only operations. Traps, DeathLink and live Sora actions stay queued.
    constexpr auto supports_persistent_grant(std::wstring_view method) -> bool
    {
        return method == L"bp_tres_get_item" || method == L"bp_tres_get_item_add_munny" ||
               method == L"durable_sora_ability" || method == L"durable_sora_vbonus" ||
               method == L"controller_unlock" || method == L"party_member_unlock" || method == L"character_unlock" ||
               method == L"ultima_weapon_shard";
    }
} // namespace kh3ap::kh02rewards
