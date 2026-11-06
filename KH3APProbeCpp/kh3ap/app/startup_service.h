#pragma once

#include <string_view>
#include <functional>

namespace kh3ap
{
    class RuntimeGameThreadDriverService;
    class RuntimeNativeHookLifecycleService;
    class RuntimeState;

    struct RuntimeStartupConfig
    {
        RuntimeState& runtime_state;
        RuntimeGameThreadDriverService& game_thread_driver;
        RuntimeNativeHookLifecycleService& native_hook_lifecycle;
        std::wstring_view build_stamp{};
        std::function<void()> initialize_kh02;
    };

    // Owns the ordered Unreal-init sequence: mark initialization active,
    // emit configured startup diagnostics, install native hooks, then expose
    // the initialized timestamp to the game-thread driver.
    class RuntimeStartupService
    {
    public:
        explicit RuntimeStartupService(RuntimeStartupConfig config);

        auto initialize() -> void;

    private:
        RuntimeStartupConfig config_;
    };
}
