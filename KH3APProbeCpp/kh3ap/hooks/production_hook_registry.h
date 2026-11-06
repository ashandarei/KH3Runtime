#pragma once

#include <functional>
#include <memory>
#include <string_view>

namespace RC::Unreal
{
    class UnrealScriptFunctionCallableContext;
}

namespace kh3ap
{
    enum class HookPhase;
    struct HookTargetInfo;

    using RuntimeProductionHookHandler = std::function<void(
            RC::Unreal::UnrealScriptFunctionCallableContext&,
            HookTargetInfo&,
            HookPhase)>;

    struct RuntimeProductionHookRegistryConfig
    {
        RuntimeProductionHookHandler apply_map_name{};
        RuntimeProductionHookHandler uip_map_name_callback{};
        RuntimeProductionHookHandler tres_map_jump{};
        RuntimeProductionHookHandler handle_starting_new_player{};
        RuntimeProductionHookHandler find_player_start{};
        RuntimeProductionHookHandler set_pause_menu_type{};
        RuntimeProductionHookHandler set_special_battle_mode{};
        std::function<void(std::wstring_view)> note_game_thread{};
    };

    class RuntimeProductionHookRegistry
    {
    public:
        explicit RuntimeProductionHookRegistry(RuntimeProductionHookRegistryConfig config);
        ~RuntimeProductionHookRegistry();

        RuntimeProductionHookRegistry(const RuntimeProductionHookRegistry&) = delete;
        auto operator=(const RuntimeProductionHookRegistry&)
                -> RuntimeProductionHookRegistry& = delete;
        RuntimeProductionHookRegistry(RuntimeProductionHookRegistry&&) noexcept;
        auto operator=(RuntimeProductionHookRegistry&&) noexcept
                -> RuntimeProductionHookRegistry&;

        auto install() -> void;
        auto uninstall() noexcept -> void;
        auto installed() const -> bool;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
