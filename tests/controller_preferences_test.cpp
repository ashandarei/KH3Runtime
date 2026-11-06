#include "kh3ap/core/input/controller_preferences_service.h"
#include <cassert>
#include <iostream>
#include <optional>

using kh3ap::RuntimeControllerPreferencesService;
struct Store {
    std::optional<bool> saved;
    int reads{}, writes{};
    bool writable{true};
    auto config() -> kh3ap::RuntimeControllerPreferencesConfig {
        return {
            [this]() { ++reads; return saved; },
            [this](bool allowed) {
                ++writes;
                if (!writable) return false;
                saved = allowed;
                return true;
            }
        };
    }
};
int main() {
    Store fresh;
    RuntimeControllerPreferencesService defaults{fresh.config()};
    defaults.install_once();
    assert(!defaults.background_input_allowed());
    assert(fresh.saved == false && fresh.writes == 1);
    defaults.install_once(false);
    assert(fresh.reads == 1 && fresh.writes == 1); // no reinitialization or polling

    for (bool old_focus : {false, true}) {
        Store migrated;
        RuntimeControllerPreferencesService first{migrated.config()};
        first.install_once(old_focus);
        assert(first.background_input_allowed() == !old_focus);
        assert(migrated.saved == !old_focus);
        RuntimeControllerPreferencesService next_seed{migrated.config()};
        next_seed.install_once(!old_focus); // different seed cannot override global preference
        assert(next_seed.background_input_allowed() == !old_focus);
        assert(migrated.writes == 1);
    }

    Store saved{true};
    RuntimeControllerPreferencesService current{saved.config()};
    current.install_once(true);
    assert(current.background_input_allowed() && saved.writes == 0);
    assert(current.set_background_input_allowed(false));
    RuntimeControllerPreferencesService restart{saved.config()};
    restart.install_once(false);
    assert(!restart.background_input_allowed());
    assert(restart.set_background_input_allowed(true));
    assert(saved.saved == true);
    // MoM temporarily masks an enabled preference, with no persistence writes.
    std::function<void(bool)> notify_mom;
    int subscriptions = 0, unsubscriptions = 0;
    auto handoff_config = saved.config();
    handoff_config.observe_melody_of_memory = [&](std::function<void(bool)> callback) {
        ++subscriptions;
        notify_mom = std::move(callback);
        return std::shared_ptr<void>(new int{}, [&](void* value) {
            // Simulate a final callback while shutdown joins the observer.
            notify_mom(false);
            notify_mom = {};
            delete static_cast<int*>(value);
            ++unsubscriptions;
        });
    };
    {
        RuntimeControllerPreferencesService handoff{handoff_config};
        handoff.install_once();
        handoff.install_once();
        assert(subscriptions == 1);
        assert(handoff.background_input_allowed());
        const auto writes_before_handoff = saved.writes;
        notify_mom(true);
        assert(!handoff.background_input_allowed());
        assert(saved.saved == true && saved.writes == writes_before_handoff);
        for (int i = 0; i < 10000; ++i) assert(!handoff.background_input_allowed());
        assert(subscriptions == 1); // input queries do not perform discovery
        notify_mom(false); // process exit, including a crash
        assert(handoff.background_input_allowed());
        assert(saved.saved == true && saved.writes == writes_before_handoff);
        assert(handoff.set_background_input_allowed(false));
        notify_mom(true);
        notify_mom(false);
        assert(!handoff.background_input_allowed()); // OFF remains OFF across a song
        assert(handoff.set_background_input_allowed(true));
    }
    assert(unsubscriptions == 1 && !notify_mom);
    saved.writable = false;
    assert(!restart.set_background_input_allowed(false));
    assert(!restart.background_input_allowed()); // immediate behavior, explicit persistence failure
    assert(saved.saved == true); // existing disk value is untouched
    const auto reads = saved.reads, writes = saved.writes;
    for (int i = 0; i < 10000; ++i) assert(!restart.background_input_allowed());
    assert(saved.reads == reads && saved.writes == writes);
    std::cout << "Controller preference tests passed\n";
}
