#pragma once
#include <atomic>
#include <cstdint>
#include <string_view>

namespace kh3ap {
inline std::atomic<uint64_t> workshop_update_revision{};
inline void notify_workshop_changed() noexcept {
    workshop_update_revision.fetch_add(1,std::memory_order_release);
}
// Grant application can follow receipt ingestion in the same update. Notify
// after success: Steam's enhancement setter and EGS's direct flag writes both
// bypass the ordinary Forge transaction hook. Keyblade ownership also matters
// for Start at Max and the last Ultima shard.
inline void notify_workshop_grant_applied(std::wstring_view method,std::wstring_view token) noexcept {
    if(method==L"progressive_keyblade_level" || method==L"ultima_weapon_shard" ||
       token.starts_with(L"WEP_KEYBLADE_"))notify_workshop_changed();
}
}
