#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    class Kh3MemoryHooks;
    class RuntimeBattleLevelSyncService;
    struct RuntimeSoraLevelMonitorState;
    class RuntimeState;

    struct RuntimeSoraStateHookConfig
    {
        RuntimeState& runtime_state;
        RuntimeSoraLevelMonitorState& monitor;
        Kh3MemoryHooks& memory_hooks;
        RuntimeBattleLevelSyncService& battle_level_sync;
        std::function<std::optional<std::filesystem::path>()> bridge_dir_resolver{};
        std::function<std::wstring()> runtime_context_resolver{};
        std::function<bool(std::wstring_view, std::wstring_view)> validated_sora_pawn_name{};
        std::function<bool(
                RC::Unreal::UObject*,
                std::wstring_view,
                std::wstring_view)> cache_validated_sora_pawn{};
        std::function<bool(RC::Unreal::UObject*)> clear_special_battle_from_level_owner{};
        std::function<void(std::wstring_view, std::optional<uint8_t>)> activate_blocking_ui{};
        std::function<void(bool)> observe_shop_session{};
        std::function<void()> observe_weapon_change{};
        std::function<void(bool)> observe_cinematic_transition{};
        std::function<void(RC::Unreal::UObject*)> observe_sora_state{};
    };

    class RuntimeSoraStateHookService
    {
    public:
        explicit RuntimeSoraStateHookService(RuntimeSoraStateHookConfig config);
        ~RuntimeSoraStateHookService();

        RuntimeSoraStateHookService(const RuntimeSoraStateHookService&) = delete;
        auto operator=(const RuntimeSoraStateHookService&)
                -> RuntimeSoraStateHookService& = delete;
        RuntimeSoraStateHookService(RuntimeSoraStateHookService&&) noexcept;
        auto operator=(RuntimeSoraStateHookService&&) noexcept
                -> RuntimeSoraStateHookService&;

        auto install_hooks(std::wstring_view reason) -> void;
        auto uninstall_hooks() -> void;
        auto flush_current_state_events() -> void;
        auto flush_vanilla_level_events() -> void;
        auto mark_sidecar_dirty() -> void;
        auto process_sidecar(
                std::chrono::steady_clock::time_point now,
                bool runtime_file_polling_ready) -> void;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
