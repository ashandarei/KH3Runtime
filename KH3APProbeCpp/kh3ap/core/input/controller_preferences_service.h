#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <utility>

namespace kh3ap {
struct RuntimeControllerPreferencesConfig {
    std::function<std::optional<bool>()> read_background_input;
    std::function<bool(bool)> write_background_input;
    // Destroying the subscription stops callbacks before the service is destroyed.
    std::function<std::shared_ptr<void>(std::function<void(bool)>)> observe_melody_of_memory;
};

// Disk access is confined to startup and explicit changes. The input hooks
// read the atomic value only. The injected store also permits isolated tests.
class RuntimeControllerPreferencesService {
public:
    explicit RuntimeControllerPreferencesService(RuntimeControllerPreferencesConfig config)
        : config_(std::move(config)) {}

    auto install_once(std::optional<bool> legacy_focus_enabled = {}) -> void {
        if (installed_) return;
        installed_ = true;
        const auto saved = config_.read_background_input ? config_.read_background_input() : std::nullopt;
        background_input_allowed_.store(saved.value_or(legacy_focus_enabled ? !*legacy_focus_enabled : false));
        if (!saved && config_.write_background_input)
            (void)config_.write_background_input(background_input_allowed());
        if (config_.observe_melody_of_memory)
            mom_subscription_ = config_.observe_melody_of_memory(
                    [this](bool active) { mom_active_.store(active); });
    }

    [[nodiscard]] auto background_input_allowed() const -> bool {
        return background_input_allowed_.load() && !mom_active_.load();
    }

    auto set_background_input_allowed(bool allowed) -> bool {
        background_input_allowed_.store(allowed);
        return config_.write_background_input && config_.write_background_input(allowed);
    }

private:
    RuntimeControllerPreferencesConfig config_;
    std::atomic_bool background_input_allowed_{false};
    std::atomic_bool mom_active_{false};
    bool installed_{};
    // Last member: unsubscribe/join before destroying the callback's atomics.
    std::shared_ptr<void> mom_subscription_;
};

// Windows and Proton/Wine resolve this through their own LOCALAPPDATA.
auto make_controller_preferences_config() -> RuntimeControllerPreferencesConfig;
} // namespace kh3ap
