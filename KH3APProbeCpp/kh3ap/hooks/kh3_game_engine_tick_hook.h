#pragma once

#include "kh3ap/core/performance_build.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace PLH
{
    class x64Detour;
}

namespace kh3ap
{
    class Kh3GameEngineTickHook
    {
    public:
        Kh3GameEngineTickHook();
        ~Kh3GameEngineTickHook();

        Kh3GameEngineTickHook(const Kh3GameEngineTickHook&) = delete;
        auto operator=(const Kh3GameEngineTickHook&) -> Kh3GameEngineTickHook& = delete;
        Kh3GameEngineTickHook(Kh3GameEngineTickHook&&) = delete;
        auto operator=(Kh3GameEngineTickHook&&) -> Kh3GameEngineTickHook& = delete;

        auto install(std::function<void()> post_tick_callback, std::wstring_view build_stamp
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
                , std::function<std::wstring()> hitch_context = {}
#endif
                ) -> bool;
        auto uninstall() -> void;
        auto installed() const -> bool;
        auto target() const -> uintptr_t;

    private:
        using GameEngineTickFn = void (*)(void* game_engine, float delta_seconds, bool idle_mode);

        struct X64DetourDeleter
        {
            auto operator()(PLH::x64Detour* detour) const noexcept -> void;
        };

        inline static std::atomic<Kh3GameEngineTickHook*> s_owner{nullptr};
        inline static std::atomic<uintptr_t> s_original_tick{0};

        static auto game_engine_tick_detour(
                void* game_engine,
                float delta_seconds,
                bool idle_mode) -> void;

        std::function<void()> post_tick_callback_{};
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        std::function<std::wstring()> hitch_context_{};
#endif
        std::unique_ptr<PLH::x64Detour, X64DetourDeleter> detour_{};
        uint64_t trampoline_{};
        uintptr_t target_{};
        bool installed_{false};
    };
}
