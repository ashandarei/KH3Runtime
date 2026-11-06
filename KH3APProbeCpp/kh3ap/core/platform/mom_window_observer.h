#pragma once

#include "kh3ap/core/platform/mom_focus_handoff.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <array>
#include <functional>
#include <deque>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

namespace kh3ap {
// OS notifications only. No Unreal calls, timer, process snapshot, or game tick.
// The worker owns all handles/window state; its subscriber only updates an atomic.
class RuntimeMoMWindowObserver {
public:
    using Activity = std::function<void(bool)>;
    using Diagnostic = std::function<void(const char*, DWORD)>;
    using Focus = std::function<bool(HWND)>;
    explicit RuntimeMoMWindowObserver(Activity activity, Diagnostic diagnostic, Focus focus = {},
            std::wstring executable = L"KINGDOM HEARTS Melody of Memory.exe")
        : activity_(std::move(activity)), diagnostic_(std::move(diagnostic)), focus_(std::move(focus)),
          executable_(std::move(executable)) {
        stop_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!stop_) throw std::runtime_error("MoM observer stop event unavailable");
        try { worker_ = std::thread([this] { run(); }); }
        catch (...) { CloseHandle(stop_); throw; }
    }
    ~RuntimeMoMWindowObserver() {
        SetEvent(stop_);
        worker_.join();
        CloseHandle(stop_);
    }
    RuntimeMoMWindowObserver(const RuntimeMoMWindowObserver&) = delete;
    auto operator=(const RuntimeMoMWindowObserver&) -> RuntimeMoMWindowObserver& = delete;

private:
    static inline thread_local RuntimeMoMWindowObserver* owner_{};
    Activity activity_;
    Diagnostic diagnostic_;
    Focus focus_;
    std::wstring executable_;
    struct WindowEvent { DWORD event; HWND window; };
    std::deque<WindowEvent> pending_windows_;
    HANDLE stop_{}, process_{};
    DWORD mom_pid_{};
    HWND mom_window_{}, kh3_window_{};
    RuntimeMoMFocusHandoff handoff_;
    std::thread worker_;

    void diagnose(const char* event, DWORD value) noexcept {
        try { if (diagnostic_) diagnostic_(event, value); } catch (...) {}
    }
    static void CALLBACK window_event(HWINEVENTHOOK, DWORD event, HWND window,
                                     LONG object, LONG child, DWORD, DWORD) noexcept {
        if (!owner_ || !window ||
            (event != EVENT_SYSTEM_FOREGROUND && (object != OBJID_WINDOW || child != CHILDID_SELF))) return;
        try {
            // Do not close/change wait handles inside a reentrant OS callback.
            owner_->pending_windows_.push_back({event, window});
        } catch (...) { owner_->diagnose("callback_failed", GetLastError()); }
    }
    void drain_window_events() {
        while (!pending_windows_.empty()) {
            const auto [event, window] = pending_windows_.front();
            pending_windows_.pop_front();
            if (event == EVENT_OBJECT_DESTROY) {
                if (window == mom_window_) mom_window_ = nullptr;
                if (window == kh3_window_) kh3_window_ = nullptr;
                // Window destruction is not process exit. Keep the input mask
                // until the process handle signals, including crashes.
            } else {
                observe_window(window);
            }
            update_focus();
        }
    }
    void observe_window(HWND window) {
        if (!IsWindowVisible(window) || GetWindow(window, GW_OWNER)) return;
        wchar_t name[64]{};
        if (!GetClassNameW(window, name, 64)) return;
        DWORD pid{};
        GetWindowThreadProcessId(window, &pid);
        if (pid == GetCurrentProcessId() && _wcsicmp(name, L"UnrealWindow") == 0) {
            kh3_window_ = window;
            return;
        }
        if (_wcsicmp(name, L"UnityWndClass") != 0) return;
        if (process_) {
            if (pid == mom_pid_) mom_window_ = window;
            return;
        }
        HANDLE candidate = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!candidate) return;
        wchar_t path[32768]{};
        DWORD length = static_cast<DWORD>(std::size(path));
        const bool readable = QueryFullProcessImageNameW(candidate, 0, path, &length) != FALSE;
        const wchar_t* base = wcsrchr(path, L'\\');
        if (!readable || _wcsicmp(base ? base + 1 : path, executable_.c_str()) != 0) {
            CloseHandle(candidate);
            return;
        }
        process_ = candidate;
        mom_pid_ = pid;
        mom_window_ = window;
        activity_(true);
        diagnose("active", pid);
    }
    void update_focus() {
        const HWND foreground = GetForegroundWindow();
        const auto target = handoff_.observe(mom_pid_, IsWindow(mom_window_) != FALSE,
                foreground == mom_window_, IsWindow(kh3_window_) != FALSE,
                foreground == kh3_window_, RuntimeMoMFocusHandoff::Clock::now());
        if (target == RuntimeMoMFocusHandoff::Target::none) return;
        const bool to_mom = target == RuntimeMoMFocusHandoff::Target::mom;
        const HWND window = to_mom ? mom_window_ : kh3_window_;
        DWORD pid{};
        GetWindowThreadProcessId(window, &pid);
        if (pid != (to_mom ? mom_pid_ : GetCurrentProcessId())) return;
        // Respect foreground policy; never attach input queues or synthesize input.
        bool accepted{};
        if (focus_) accepted = focus_(window);
        else {
            if (IsIconic(window)) ShowWindowAsync(window, SW_RESTORE);
            accepted = SetForegroundWindow(window) != FALSE;
        }
        diagnose(to_mom ? "focus_mom" : "focus_kh3", accepted);
        // Observe synchronous success too, so an immediate Alt-Tab cannot restart
        // a handoff before its queued foreground notification reaches this thread.
        const HWND after = GetForegroundWindow();
        (void)handoff_.observe(mom_pid_, IsWindow(mom_window_) != FALSE,
                after == mom_window_, IsWindow(kh3_window_) != FALSE,
                after == kh3_window_, RuntimeMoMFocusHandoff::Clock::now());
    }
    void run() noexcept {
        owner_ = this;
        std::array<HWINEVENTHOOK, 3> hooks{};
        try {
            const std::array<DWORD, 3> events{EVENT_OBJECT_SHOW, EVENT_OBJECT_DESTROY, EVENT_SYSTEM_FOREGROUND};
            for (size_t i = 0; i < events.size(); ++i) {
                hooks[i] = SetWinEventHook(events[i], events[i], nullptr, window_event,
                                          0, 0, WINEVENT_OUTOFCONTEXT);
                if (!hooks[i]) throw std::runtime_error("MoM window notification unavailable");
            }
            // One startup snapshot covers MoM/KH3 windows already present before
            // subscription. Subscribe first so creation during the snapshot is covered.
            snapshot_windows();
            update_focus();
            diagnose("subscribed_no_polling", 0);
            for (;;) {
                drain_window_events();
                const HANDLE handles[]{stop_, process_};
                const DWORD count = process_ ? 2 : 1;
                const DWORD result = MsgWaitForMultipleObjects(count, handles, FALSE, INFINITE, QS_ALLINPUT);
                if (result == WAIT_OBJECT_0) break;
                if (process_ && result == WAIT_OBJECT_0 + 1) {
                    CloseHandle(process_);
                    process_ = nullptr;
                    mom_pid_ = 0;
                    mom_window_ = nullptr;
                    activity_(false);
                    diagnose("exited", 0);
                    // An immediate restart can show the new window before the
                    // old process signals. Reconcile once on this exit event.
                    snapshot_windows();
                    update_focus();
                } else if (result == WAIT_OBJECT_0 + count) {
                    MSG message{};
                    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                        TranslateMessage(&message);
                        DispatchMessageW(&message);
                    }
                } else {
                    throw std::runtime_error("MoM event wait failed");
                }
            }
        } catch (...) { diagnose("observer_failed", GetLastError()); }
        for (auto hook : hooks) if (hook) UnhookWinEvent(hook);
        if (process_) { CloseHandle(process_); process_ = nullptr; }
        try { activity_(false); } catch (...) {}
        owner_ = nullptr;
    }
    void snapshot_windows() {
        EnumWindows([](HWND window, LPARAM context) -> BOOL {
            try { reinterpret_cast<RuntimeMoMWindowObserver*>(context)->observe_window(window); }
            catch (...) { return FALSE; }
            return TRUE;
        }, reinterpret_cast<LPARAM>(this));
    }
};
} // namespace kh3ap
