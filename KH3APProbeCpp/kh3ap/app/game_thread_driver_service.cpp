#include "kh3ap/app/game_thread_driver_service.h"

#include "kh3ap/hooks/kh3_game_engine_tick_hook.h"
#include "kh3ap/app/game_thread_driver_state.h"
#include "kh3ap/diagnostics/profiling/reflection_profile.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <UnrealDef.hpp>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <chrono>
#include <cstdint>
#include <exception>
#include <string>
#include <utility>

using namespace RC;

namespace kh3ap
{
    namespace
    {
        class UpdateCompletionGuard
        {
        public:
            explicit UpdateCompletionGuard(
                    RuntimeGameThreadDriverState& state) noexcept
                : state_(state)
            {
            }

            ~UpdateCompletionGuard()
            {
                state_.finish_update();
            }

        private:
            RuntimeGameThreadDriverState& state_;
        };
    }

    struct RuntimeGameThreadDriverService::Impl
    {
        explicit Impl(RuntimeGameThreadDriverConfig value)
            : config(std::move(value))
        {
        }

        ~Impl()
        {
            state.mark_unreal_uninitialized();
            tick_hook.uninstall();
        }

        auto note_game_thread(std::wstring_view source) -> void
        {
            const uint32_t current_thread_id =
                    static_cast<uint32_t>(GetCurrentThreadId());
            if (!state.note_game_thread(current_thread_id))
            {
                return;
            }

            Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] Runtime game thread identified source={} thread_id={} build_stamp={}\n"),
                    source,
                    static_cast<uint64_t>(current_thread_id),
                    config.build_stamp);
        }

        auto maybe_run_update() -> void
        {
            if (!config.update_loop_enabled)
            {
                return;
            }

            const auto now = std::chrono::steady_clock::now();
            const auto decision = state.try_begin_update(
                    static_cast<uint32_t>(GetCurrentThreadId()),
                    now,
                    config.update_interval);
            if (!decision.should_run)
            {
                return;
            }

            const UpdateCompletionGuard finish_update{state};
            try
            {
                if (decision.log_first_update)
                {
                    Output::send<LogLevel::Warning>(
                            STR("[KH3APProbeCpp] Runtime update executing on identified game thread thread_id={} build_stamp={}\n"),
                            static_cast<uint64_t>(decision.game_thread_id),
                            config.build_stamp);
                }
                if (config.run_update)
                {
                    config.run_update(
                            state.unreal_initialized(),
                            state.unreal_init_time());
                }
            }
            catch (const std::exception& exception)
            {
                const std::string message = exception.what()
                        ? exception.what()
                        : "<empty std::exception::what()>";
                Output::send<LogLevel::Error>(
                        STR("[KH3APProbeCpp] Runtime game-thread update exception={}\n"),
                        std::wstring{message.begin(), message.end()});
            }
            catch (...)
            {
                Output::send<LogLevel::Error>(
                        STR("[KH3APProbeCpp] Runtime game-thread update unknown exception\n"));
            }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            // Callback scopes have recorded their samples, including on unwinding.
            try
            {
                flush_runtime_profiles();
            }
            catch (...)
            {
                Output::send<LogLevel::Error>(
                        STR("[KH3APProbeCpp] Runtime profile flush exception\n"));
            }
#endif
        }

        auto install_tick_hook() -> bool
        {
            if (!config.tick_hook_enabled)
            {
                return false;
            }
            return tick_hook.install(
                    [this]() {
                        note_game_thread(STR("UGameEngine.Tick"));
                        maybe_run_update();
                    },
                    config.build_stamp
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
                    , config.hitch_context
#endif
                    );
        }

        RuntimeGameThreadDriverConfig config;
        RuntimeGameThreadDriverState state{};
        Kh3GameEngineTickHook tick_hook{};
    };

    RuntimeGameThreadDriverService::RuntimeGameThreadDriverService(
            RuntimeGameThreadDriverConfig config)
        : impl_(std::make_unique<Impl>(std::move(config)))
    {
    }

    RuntimeGameThreadDriverService::~RuntimeGameThreadDriverService() = default;
    RuntimeGameThreadDriverService::RuntimeGameThreadDriverService(
            RuntimeGameThreadDriverService&&) noexcept = default;
    auto RuntimeGameThreadDriverService::operator=(
            RuntimeGameThreadDriverService&&) noexcept
            -> RuntimeGameThreadDriverService& = default;

    auto RuntimeGameThreadDriverService::begin_unreal_init() -> void
    {
        impl_->state.begin_unreal_init(std::chrono::steady_clock::now());
    }

    auto RuntimeGameThreadDriverService::complete_unreal_init() -> void
    {
        impl_->state.complete_unreal_init();
    }

    auto RuntimeGameThreadDriverService::mark_unreal_uninitialized() -> void
    {
        impl_->state.mark_unreal_uninitialized();
    }

    auto RuntimeGameThreadDriverService::note_game_thread(
            std::wstring_view source) -> void
    {
        impl_->note_game_thread(source);
    }

    auto RuntimeGameThreadDriverService::install_tick_hook() -> bool
    {
        return impl_->install_tick_hook();
    }

    auto RuntimeGameThreadDriverService::uninstall_tick_hook() -> void
    {
        impl_->tick_hook.uninstall();
    }

    auto RuntimeGameThreadDriverService::unreal_init_time() const
            -> std::chrono::steady_clock::time_point
    {
        return impl_->state.unreal_init_time();
    }
}
