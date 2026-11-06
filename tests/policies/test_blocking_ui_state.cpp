#include "kh3ap/hooks/blocking_ui/blocking_ui_state.h"

#include <cassert>
#include <chrono>

int main()
{
    using namespace kh3ap;

    constexpr auto handoff_grace = std::chrono::milliseconds{250};
    const auto handoff_started_at = std::chrono::steady_clock::time_point{
            std::chrono::milliseconds{1000}};
    RuntimeBlockingUiFocusHandoffState focus_handoff{};

    const auto pause_focus_in = focus_handoff.observe_focus_in(0x100);
    assert(pause_focus_in.decision ==
           RuntimeBlockingUiFocusDecision::publish_active);
    assert(pause_focus_in.focused_context_count == 1);
    assert(!pause_focus_in.release_pending);

    const auto pause_to_phone_focus_out = focus_handoff.observe_focus_out(
            0x100,
            handoff_started_at,
            handoff_grace);
    assert(pause_to_phone_focus_out.decision ==
           RuntimeBlockingUiFocusDecision::defer_inactive);
    assert(pause_to_phone_focus_out.focused_context_count == 0);
    assert(pause_to_phone_focus_out.release_pending);

    const auto before_phone_focus = focus_handoff.settle(
            handoff_started_at + std::chrono::milliseconds{100});
    assert(before_phone_focus.decision ==
           RuntimeBlockingUiFocusDecision::none);
    assert(before_phone_focus.release_pending);

    // The replacement panel claims focus inside the grace window, so there is
    // no false release edge between pause and the Gummiphone.
    const auto phone_focus_in = focus_handoff.observe_focus_in(0x200);
    assert(phone_focus_in.decision ==
           RuntimeBlockingUiFocusDecision::publish_active);
    assert(phone_focus_in.focused_context_count == 1);
    assert(!phone_focus_in.release_pending);
    assert(focus_handoff.settle(
                   handoff_started_at + handoff_grace)
                   .decision == RuntimeBlockingUiFocusDecision::none);

    // KH3 focuses CampMenu before MobilePortal focuses out on the reverse
    // handoff. Ownership keeps the state active regardless of event order.
    const auto pause_refocus = focus_handoff.observe_focus_in(0x100);
    assert(pause_refocus.focused_context_count == 2);
    const auto phone_focus_out = focus_handoff.observe_focus_out(
            0x200,
            handoff_started_at + std::chrono::milliseconds{500},
            handoff_grace);
    assert(phone_focus_out.decision ==
           RuntimeBlockingUiFocusDecision::publish_active);
    assert(phone_focus_out.focused_context_count == 1);
    assert(!phone_focus_out.release_pending);

    const auto final_pause_focus_out = focus_handoff.observe_focus_out(
            0x100,
            handoff_started_at + std::chrono::milliseconds{1000},
            handoff_grace);
    assert(final_pause_focus_out.decision ==
           RuntimeBlockingUiFocusDecision::defer_inactive);
    assert(focus_handoff.settle(
                   handoff_started_at + std::chrono::milliseconds{1249})
                   .decision == RuntimeBlockingUiFocusDecision::none);
    const auto final_release = focus_handoff.settle(
            handoff_started_at + std::chrono::milliseconds{1250});
    assert(final_release.decision ==
           RuntimeBlockingUiFocusDecision::publish_inactive);
    assert(!final_release.release_pending);

    RuntimeBlockingUiState state{};
    const auto initial = state.snapshot();
    assert(!initial.active);
    assert(initial.reason.empty());

    const auto opened_at = std::chrono::steady_clock::time_point{
            std::chrono::milliseconds{10}};
    assert(state.update(
            true,
            L"pause_menu",
            L"/Script/Engine.PlayerController:SetPause",
            L"SetPause",
            1,
            opened_at));
    const auto opened = state.snapshot();
    assert(opened.active);
    assert(opened.reason == L"pause_menu");
    assert(opened.context == L"/Script/Engine.PlayerController:SetPause");
    assert(opened.event == L"SetPause");
    assert(opened.callback_id == 1);
    assert(opened.updated_at == opened_at);

    const auto refreshed_at = std::chrono::steady_clock::time_point{
            std::chrono::milliseconds{20}};
    assert(!state.update(
            true,
            L"pause_menu",
            L"/Script/Engine.PlayerController:SetPause",
            L"SetPause",
            1,
            refreshed_at));
    assert(state.snapshot().updated_at == refreshed_at);

    assert(state.update(
            false,
            L"pause_menu",
            L"/Script/Engine.PlayerController:SetPause",
            L"SetPause",
            0));
    assert(!state.snapshot().active);

    return 0;
}
