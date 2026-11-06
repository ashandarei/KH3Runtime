#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace kh3ap::party_runtime
{
    inline constexpr std::wstring_view party_unlock_token_prefix = L"KH3AP_PARTY_MEMBER_UNLOCK::";
    inline constexpr std::wstring_view set_friend_all_path =
        L"/Script/TresGame.TresFriendComponent:SetFriendAll";
    inline constexpr std::wstring_view set_friend_path =
        L"/Script/TresGame.TresFriendComponent:SetFriend";
    inline constexpr std::wstring_view delete_friend_path =
        L"/Script/TresGame.TresFriendComponent:DeleteFriend";
    inline constexpr auto party_preload_timeout = std::chrono::seconds{30};
    inline constexpr auto party_asset_request_interval = std::chrono::seconds{1};
    inline constexpr auto party_reconcile_retry_delay = std::chrono::milliseconds{100};
    inline constexpr std::size_t native_friend_slot_count = 6;
    inline constexpr uint32_t friend_settle_ticks = 2;
    inline constexpr uint32_t custom_party_asset_settle_ticks = 30;
    // Distinct latent-action identities keep retries independent of prior requests.
    inline constexpr uint32_t pawn_class_load_uuid_prefix = 0x40000000;
    inline constexpr uint32_t pawn_class_load_sequence_mask = 0x0fffffff;
    inline constexpr uint32_t party_tree_load_uuid_base = 0x4b530001;
    inline constexpr std::wstring_view teleport_function_path =
        L"/Script/Engine.Actor:K2_TeleportTo";
    inline constexpr int teleport_parameter_size = 25;
    inline constexpr uint8_t state_id_idle = 1;
    inline constexpr uint8_t state_id_turn = 44;
    inline constexpr std::wstring_view controller_possess_path =
        L"/Script/Engine.Controller:Possess";
    inline constexpr std::wstring_view can_battle_path =
        L"/Script/TresGame.TresNpcPawnBase:CanBattle";
    inline constexpr std::wstring_view is_battle_mode_function_name = L"IsBattleMode";
    inline constexpr std::wstring_view set_battle_mode_function_name = L"SetBattleMode";
    inline constexpr std::wstring_view on_change_battle_mode_function_name = L"OnChangeBattleMode";

} // namespace kh3ap::party_runtime
