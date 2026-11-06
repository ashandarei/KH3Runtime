#include "kh3ap/hooks/blocking_ui/blocking_ui_focus_service.h"

#include "kh3ap/features/grants/messages/ap_message_gate.h"
#include "kh3ap/hooks/blocking_ui/blocking_ui_hook_service.h"
#include "kh3ap/diagnostics/output/diagnostic_probe_output_helpers.h"
#include "kh3ap/diagnostics/reflection/reflection_diagnostic_models.h"
#include "kh3ap/app/state.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <UnrealDef.hpp>

#include <format>
#include <optional>

using namespace RC;

namespace kh3ap
{
    RuntimeBlockingUiFocusService::RuntimeBlockingUiFocusService(
            const RuntimeBlockingUiHookConfig& config)
        : config_(config)
    {
    }

    auto RuntimeBlockingUiFocusService::gameplay_event_allowed() const -> bool
    {
        return !(config_.ignored_map_active &&
                 config_.ignored_map_active()) &&
                !(config_.gameplay_controller_active &&
                  !config_.gameplay_controller_active());
    }

    auto RuntimeBlockingUiFocusService::publish_transition(
            bool active,
            std::wstring_view context_name,
            std::wstring_view event_name) -> void
    {
        const RuntimeBlockingUiProbeUpdateData update_data{
                .reason = std::wstring{STR("pause_menu")},
                .context_name = std::wstring{context_name},
                .event_name = std::wstring{event_name},
                .callback_id = active ? 1 : 0,
                .next_active = active,
        };

        RuntimeBlockingUiState& blocking_ui_state =
                config_.runtime_state.ui_diagnostic_state()
                        .blocking_ui_state();
        const auto previous_blocking_ui = blocking_ui_state.snapshot();
        update_runtime_blocking_ui_state_from_probe_data(
                make_runtime_diagnostic_probe_output_deps(
                        config_.bridge_dir_resolver
                                ? config_.bridge_dir_resolver()
                                : std::nullopt,
                        config_.build_stamp,
                        config_.runtime_context_resolver
                                ? config_.runtime_context_resolver()
                                : std::wstring{}),
                blocking_ui_state,
                update_data);
        const auto updated_blocking_ui = blocking_ui_state.snapshot();
        const bool active_changed =
                previous_blocking_ui.active != updated_blocking_ui.active;
        const bool changed =
                active_changed ||
                previous_blocking_ui.context != updated_blocking_ui.context ||
                previous_blocking_ui.event != updated_blocking_ui.event ||
                previous_blocking_ui.callback_id !=
                        updated_blocking_ui.callback_id;
        if (changed)
        {
            extend_runtime_ap_message_quiet_window(
                    config_.ap_message_gate_state,
                    std::format(
                            STR("blocking_ui:{}:{}"),
                            updated_blocking_ui.active
                                    ? STR("active")
                                    : STR("cleared"),
                            update_data.reason),
                    config_.quiet_window);
        }
        if (active_changed && !updated_blocking_ui.active && config_.on_menu_closed)
        {
            config_.on_menu_closed();
        }
        if (config_.shoot_flow_restore_enabled &&
            active_changed &&
            config_.observe_pause_ui)
        {
            config_.observe_pause_ui(updated_blocking_ui.active);
        }
    }

    auto RuntimeBlockingUiFocusService::reset_locked() -> void
    {
        ++lifecycle_generation_;
        handoff_state_.reset();
        pending_release_generation_ = 0;
        pending_release_context_.clear();
        pending_release_event_.clear();
        release_pending_.store(false, std::memory_order_release);
    }

    auto RuntimeBlockingUiFocusService::reset() -> void
    {
        std::lock_guard lock{mutex_};
        reset_locked();
    }

    auto RuntimeBlockingUiFocusService::observe_engine_pause(
            bool active,
            std::wstring_view context_name,
            std::wstring_view event_name) -> void
    {
        if (!gameplay_event_allowed())
        {
            return;
        }
        std::lock_guard lock{mutex_};
        reset_locked();
        publish_transition(active, context_name, event_name);
    }

    auto RuntimeBlockingUiFocusService::observe_ui_lifecycle(
            uintptr_t context_id,
            std::wstring_view context_name,
            std::wstring_view event_name,
            bool focus_in,
            std::chrono::steady_clock::time_point now) -> void
    {
        if (!gameplay_event_allowed())
        {
            return;
        }

        std::lock_guard lock{mutex_};
        const auto previous = config_.runtime_state.ui_diagnostic_state()
                                      .blocking_ui_state()
                                      .snapshot();
        RuntimeBlockingUiFocusUpdate focus_update{};
        bool handoff_cancelled = false;
        ++lifecycle_generation_;

        if (focus_in)
        {
            // A false shared snapshot means a map/title lifecycle already
            // retired any identities left by destroyed UI.
            if (!previous.active)
            {
                handoff_state_.reset();
            }
            handoff_cancelled = handoff_state_.release_pending();
            focus_update = handoff_state_.observe_focus_in(context_id);
            pending_release_generation_ = 0;
            pending_release_context_.clear();
            pending_release_event_.clear();
            release_pending_.store(false, std::memory_order_release);
        }
        else
        {
            if (!previous.active)
            {
                handoff_state_.reset();
                pending_release_generation_ = 0;
                pending_release_context_.clear();
                pending_release_event_.clear();
                release_pending_.store(false, std::memory_order_release);
                return;
            }
            focus_update = handoff_state_.observe_focus_out(
                    context_id,
                    now,
                    config_.focus_handoff_grace);
            if (focus_update.decision ==
                RuntimeBlockingUiFocusDecision::defer_inactive)
            {
                pending_release_generation_ = lifecycle_generation_;
                pending_release_context_ = context_name;
                pending_release_event_ = event_name;
                release_pending_.store(true, std::memory_order_release);
            }
            else
            {
                pending_release_generation_ = 0;
                pending_release_context_.clear();
                pending_release_event_.clear();
                release_pending_.store(false, std::memory_order_release);
            }
        }

        if (handoff_cancelled)
        {
            Output::send<LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] RuntimeBlockingUI focus handoff claimed context={} event={} focused_contexts={} generation={}\n"),
                    context_name,
                    event_name,
                    static_cast<uint64_t>(
                            focus_update.focused_context_count),
                    lifecycle_generation_);
        }

        switch (focus_update.decision)
        {
        case RuntimeBlockingUiFocusDecision::publish_active:
            publish_transition(true, context_name, event_name);
            break;
        case RuntimeBlockingUiFocusDecision::defer_inactive:
            Output::send<LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] RuntimeBlockingUI focus release deferred context={} event={} grace_ms={} focused_contexts=0 generation={} object_scans=false game_state_polling=false\n"),
                    context_name,
                    event_name,
                    static_cast<uint64_t>(
                            config_.focus_handoff_grace.count()),
                    lifecycle_generation_);
            break;
        case RuntimeBlockingUiFocusDecision::publish_inactive:
        case RuntimeBlockingUiFocusDecision::none:
            break;
        }
    }

    auto RuntimeBlockingUiFocusService::process_pending_release(
            std::chrono::steady_clock::time_point now) -> void
    {
        // The game-thread update calls this method frequently, but the normal
        // path is one atomic load. The mutex and deadline are touched only
        // while a final focus-out is awaiting handoff confirmation.
        if (!release_pending_.load(std::memory_order_acquire))
        {
            return;
        }

        uint64_t settled_generation = 0;
        std::wstring context_name{};
        std::wstring event_name{};
        {
            std::lock_guard lock{mutex_};
            if (!release_pending_.load(std::memory_order_relaxed))
            {
                return;
            }
            const auto focus_update = handoff_state_.settle(now);
            if (focus_update.decision !=
                RuntimeBlockingUiFocusDecision::publish_inactive)
            {
                if (!focus_update.release_pending)
                {
                    release_pending_.store(
                            false,
                            std::memory_order_release);
                }
                return;
            }

            settled_generation = pending_release_generation_;
            context_name = pending_release_context_;
            event_name = pending_release_event_;
            pending_release_generation_ = 0;
            pending_release_context_.clear();
            pending_release_event_.clear();
            release_pending_.store(false, std::memory_order_release);
        }

        if (context_name.empty())
        {
            context_name = STR("<focus_handoff>");
        }
        if (event_name.empty())
        {
            event_name = STR("OnFocusOut");
        }
        // A focus event can arrive between deadline settlement and bridge
        // publication on a non-game dispatch thread. Reacquire and reject the
        // stale token before emitting the false edge.
        std::lock_guard lock{mutex_};
        if (settled_generation == 0 ||
            settled_generation != lifecycle_generation_ ||
            release_pending_.load(std::memory_order_relaxed))
        {
            Output::send<LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] RuntimeBlockingUI stale focus release discarded context={} event={} settled_generation={} current_generation={}\n"),
                    context_name,
                    event_name,
                    settled_generation,
                    lifecycle_generation_);
            return;
        }

        const bool gameplay_active = gameplay_event_allowed();
        Output::send<LogLevel::Verbose>(
                STR("[KH3APProbeCpp] RuntimeBlockingUI focus release settled context={} event={} gameplay_active={} focused_contexts=0 generation={}\n"),
                context_name,
                event_name,
                gameplay_active,
                settled_generation);
        publish_transition(false, context_name, event_name);
    }
}
