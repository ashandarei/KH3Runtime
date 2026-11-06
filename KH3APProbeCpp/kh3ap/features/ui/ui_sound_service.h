#pragma once

#include <Unreal/FWeakObjectPtr.hpp>

#include <cstdint>
#include <string_view>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    enum class RuntimeUiSound : uint8_t
    {
        cursor,
        decide,
        cancel,
    };

    // Plays KH3's stock UI feedback through the active UTresUIManager. The
    // service owns storefront-specific native dispatch and sound resolution so
    // menu scaffolds only express which feedback they need.
    class RuntimeUiSoundService
    {
    public:
        auto play(RC::Unreal::UObject* ui_part, RuntimeUiSound sound) -> bool;

    private:
        auto try_load_asset_from_registry(std::wstring_view object_path)
                -> RC::Unreal::UObject*;
        auto try_resolve_common_sound(RuntimeUiSound sound)
                -> RC::Unreal::UObject*;
        static auto try_resolve_active_sound(
                RC::Unreal::UObject* ui_part,
                std::wstring_view property_name) -> RC::Unreal::UObject*;

        RC::Unreal::FWeakObjectPtr cursor_sound_{};
        RC::Unreal::FWeakObjectPtr decide_sound_{};
        RC::Unreal::FWeakObjectPtr cancel_sound_{};
        bool route_logged_[3]{};
        bool failure_logged_{};
    };
}
