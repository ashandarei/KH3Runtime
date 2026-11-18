#pragma once

#include <memory>
#include <string_view>

namespace RC::Unreal
{
    class UObjectBase;
}

namespace kh3ap
{
    class RuntimeState;

    inline constexpr auto battle_portal_01_class_name = L"g_ex_battleportal_01_C";
    bool enable_battle_portal_01_default(const RC::Unreal::UObjectBase* portal);

    struct RuntimeBattlegateActivationConfig
    {
        RuntimeState& runtime_state;
        std::wstring_view get_game_flag_path;
        std::wstring_view blueprint_library_default_object_path;
    };

    class RuntimeBattlegateActivationService
    {
    public:
        explicit RuntimeBattlegateActivationService(RuntimeBattlegateActivationConfig config);
        ~RuntimeBattlegateActivationService();
        RuntimeBattlegateActivationService(const RuntimeBattlegateActivationService&) = delete;
        auto operator=(const RuntimeBattlegateActivationService&) -> RuntimeBattlegateActivationService& = delete;
        auto install_once() -> void;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
