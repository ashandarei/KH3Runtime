#include "kh3ap/diagnostics/ability_probe/ability_probe_diagnostic_state.h"

namespace kh3ap
{
    auto AbilityProbeDiagnosticState::acquire_log_lock() -> std::unique_lock<std::mutex>
    {
        return std::unique_lock<std::mutex>{mutex_};
    }

    auto AbilityProbeDiagnosticState::live_owner_force_enabled() const -> bool
    {
        std::scoped_lock lock{mutex_};
        return live_owner_force_enabled_;
    }

    auto AbilityProbeDiagnosticState::ability_array_needle_force_enabled() const -> bool
    {
        std::scoped_lock lock{mutex_};
        return ability_array_needle_force_enabled_;
    }

    auto AbilityProbeDiagnosticState::set_live_owner_force_enabled(bool enabled) -> void
    {
        std::scoped_lock lock{mutex_};
        live_owner_force_enabled_ = enabled;
    }

    auto AbilityProbeDiagnosticState::set_ability_array_needle_force_enabled(bool enabled) -> void
    {
        std::scoped_lock lock{mutex_};
        ability_array_needle_force_enabled_ = enabled;
    }

    auto AbilityProbeDiagnosticState::reset_live_owner_probe_key() -> void
    {
        std::scoped_lock lock{mutex_};
        last_live_owner_probe_key_.clear();
    }

    auto AbilityProbeDiagnosticState::reset_ability_array_needle_probe_key() -> void
    {
        std::scoped_lock lock{mutex_};
        last_ability_array_needle_probe_key_.clear();
    }

    auto AbilityProbeDiagnosticState::mark_live_owner_probe_key_if_new(std::wstring_view probe_key) -> bool
    {
        std::scoped_lock lock{mutex_};
        if (probe_key == last_live_owner_probe_key_)
        {
            return false;
        }
        last_live_owner_probe_key_ = std::wstring{probe_key};
        return true;
    }

    auto AbilityProbeDiagnosticState::mark_ability_array_needle_probe_key_if_new(std::wstring_view probe_key) -> bool
    {
        std::scoped_lock lock{mutex_};
        if (probe_key == last_ability_array_needle_probe_key_)
        {
            return false;
        }
        last_ability_array_needle_probe_key_ = std::wstring{probe_key};
        return true;
    }
}
