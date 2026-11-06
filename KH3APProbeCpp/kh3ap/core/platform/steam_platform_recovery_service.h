#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace PLH
{
    class x64Detour;
}

namespace kh3ap
{
    struct RuntimeSteamPlatformSnapshot
    {
        bool supported_steam_image{false};
        bool steam_api_loaded{false};
        bool steam_running{false};
        bool logged_on{false};
        int32_t steam_user_handle{0};
        uint64_t steam_id{0};
        uint32_t app_id{0};
        std::string game_language{};
        std::string configured_game_language{};
        std::string ui_language{};
        uint32_t xinput_connected_mask{0};
    };

    // Keeps the supported Steam build on its normal client platform path even
    // though KH3 normally requires an unnecessary local Steam game server.
    // The retained Steam client lets KH3 select its normal per-player language
    // and controller path; this service never overrides Unreal's culture.
    class RuntimeSteamPlatformRecoveryService
    {
    public:
        explicit RuntimeSteamPlatformRecoveryService(
                bool bypass_steam_game_server_init = false) noexcept;
        RuntimeSteamPlatformRecoveryService(
                const RuntimeSteamPlatformRecoveryService&) = delete;
        auto operator=(const RuntimeSteamPlatformRecoveryService&)
                -> RuntimeSteamPlatformRecoveryService& = delete;
        ~RuntimeSteamPlatformRecoveryService();

        auto capture_early_platform_state() -> void;
        auto observe_unreal_startup_state() -> void;

    private:
        struct X64DetourDeleter
        {
            auto operator()(PLH::x64Detour* detour) const noexcept -> void;
        };

        auto install_early_steam_subsystem_init_recovery() -> void;
        auto uninstall_early_steam_subsystem_init_recovery() -> void;
        auto install_steam_game_server_init_recovery() -> void;
        auto uninstall_steam_game_server_init_recovery() -> void;
        auto handle_steam_subsystem_init(
                void* manager,
                bool relaunch_in_steam,
                uint32_t app_id) -> bool;
        static bool __fastcall hooked_steam_subsystem_init(
                void* manager,
                bool relaunch_in_steam,
                uint32_t app_id);
        static bool hooked_steam_game_server_init(
                uint32_t ip,
                uint16_t legacy_steam_port,
                uint16_t game_port,
                uint16_t query_port,
                int32_t server_mode,
                const char* version_string);
        static bool __fastcall hooked_kh3_steam_game_server_init(
                void* manager);

        RuntimeSteamPlatformSnapshot early_snapshot_{};
        std::unique_ptr<PLH::x64Detour, X64DetourDeleter>
                steam_subsystem_init_detour_{};
        uint64_t steam_subsystem_init_trampoline_{0};
        bool steam_subsystem_init_hook_attempted_{false};
        bool steam_subsystem_init_hook_installed_{false};
        std::unique_ptr<PLH::x64Detour, X64DetourDeleter>
                steam_game_server_init_detour_{};
        uint64_t steam_game_server_init_trampoline_{0};
        bool steam_game_server_init_hook_attempted_{false};
        bool steam_game_server_init_hook_installed_{false};
        void* active_steam_subsystem_manager_{nullptr};
        uint32_t active_steam_subsystem_app_id_{0};
        bool steam_client_init_succeeded_{false};
        bool bypass_steam_game_server_init_{false};
    };
}
