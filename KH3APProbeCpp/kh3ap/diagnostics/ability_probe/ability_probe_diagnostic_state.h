#pragma once

#include <mutex>
#include <string>
#include <string_view>

namespace kh3ap
{
    class AbilityProbeDiagnosticState
    {
    public:
        [[nodiscard]] auto acquire_log_lock() -> std::unique_lock<std::mutex>;

        [[nodiscard]] auto live_owner_force_enabled() const -> bool;
        [[nodiscard]] auto ability_array_needle_force_enabled() const -> bool;
        auto set_live_owner_force_enabled(bool enabled) -> void;
        auto set_ability_array_needle_force_enabled(bool enabled) -> void;

        auto reset_live_owner_probe_key() -> void;
        auto reset_ability_array_needle_probe_key() -> void;
        auto mark_live_owner_probe_key_if_new(std::wstring_view probe_key) -> bool;
        auto mark_ability_array_needle_probe_key_if_new(std::wstring_view probe_key) -> bool;

    private:
        mutable std::mutex mutex_{};
        std::wstring last_live_owner_probe_key_{};
        std::wstring last_ability_array_needle_probe_key_{};
        bool live_owner_force_enabled_{false};
        bool ability_array_needle_force_enabled_{false};
    };
}
