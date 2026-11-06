#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace kh3ap
{
    // Shared loaded-entity boundary for runtime encounter configuration.
    auto ensure_runtime_level_entity_serialize_hook() -> bool;
    class RuntimeGoaSettingsService;

    struct RuntimeEnemyRemapConfig
    {
        bool enabled{};
        RuntimeGoaSettingsService& goa_settings;
        std::function<std::optional<std::filesystem::path>()>
                bridge_dir_resolver{};
    };

    struct RuntimeEnemyRemapStatus
    {
        bool enemy_available{};
        bool boss_available{};
        bool enemy_enabled{};
        bool boss_enabled{};
        uint32_t boss_pending_class_loads{};
        bool boss_class_load_failed{};
    };

    class RuntimeEnemyRemapService
    {
    public:
        explicit RuntimeEnemyRemapService(RuntimeEnemyRemapConfig config);

        auto install_once() -> void;
        auto status() const -> RuntimeEnemyRemapStatus;
        auto request_loaded_toggle_class_prewarm() -> uint32_t;
        auto process_pending_class_prewarm() -> void;
        auto set_enemy_enabled(bool enabled) -> bool;
        auto set_boss_enabled(bool enabled) -> bool;

    private:
        RuntimeEnemyRemapConfig config_;
        bool installed_{};
    };
}
