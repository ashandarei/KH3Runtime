#pragma once
#define NOMINMAX
#include <Windows.h>
#include <atomic>
#include <future>
#include <thread>
#include <stdexcept>

namespace kh3ap
{
    // Wine does not reliably pump startup messages on an inactive desktop.
    // Observe only the owned child; idle supervision blocks on the message queue.
    class Kh02CompanionWindows
    {
        DWORD child_pid_{};
        HWND host_window_{};
        std::thread worker_;
        std::atomic<DWORD> worker_id_{};
        inline static thread_local Kh02CompanionWindows* current_{};

        static void CALLBACK changed(HWINEVENTHOOK, DWORD event, HWND window, LONG object, LONG child,
                                     DWORD, DWORD)
        {
            auto* self = current_;
            DWORD owner{};
            if (!self || !window || !GetWindowThreadProcessId(window, &owner) || owner != self->child_pid_)
            {
                return;
            }
            if (event != EVENT_SYSTEM_FOREGROUND && (object != OBJID_WINDOW || child != CHILDID_SELF))
            {
                return;
            }
            // Never synchronously message the source: its engine thread parks.
            ShowWindowAsync(window, SW_HIDE);
            if (GetForegroundWindow() == window && IsWindow(self->host_window_))
            {
                DWORD host_pid{};
                GetWindowThreadProcessId(self->host_window_, &host_pid);
                if (host_pid == GetCurrentProcessId())
                {
                    SetForegroundWindow(self->host_window_);
                }
            }
        }

      public:
        Kh02CompanionWindows(DWORD child_pid, HWND host_window) : child_pid_(child_pid), host_window_(host_window)
        {
            std::promise<bool> started;
            auto ready = started.get_future();
            worker_ = std::thread([this, started = std::move(started)]() mutable
            {
                current_ = this;
                MSG message{};
                PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE);
                const auto windows = SetWinEventHook(EVENT_OBJECT_CREATE, EVENT_OBJECT_SHOW, nullptr, changed,
                                                    child_pid_, 0, WINEVENT_OUTOFCONTEXT);
                const auto foreground = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
                                                       nullptr, changed, child_pid_, 0, WINEVENT_OUTOFCONTEXT);
                worker_id_.store(GetCurrentThreadId());
                started.set_value(windows && foreground);
                if (windows && foreground)
                {
                    while (GetMessageW(&message, nullptr, 0, 0) > 0)
                    {
                        TranslateMessage(&message);
                        DispatchMessageW(&message);
                    }
                }
                if (foreground)
                {
                    UnhookWinEvent(foreground);
                }
                if (windows)
                {
                    UnhookWinEvent(windows);
                }
                current_ = nullptr;
            });
            if (!ready.get())
            {
                worker_.join();
                throw std::runtime_error("companion_window_events");
            }
        }

        ~Kh02CompanionWindows()
        {
            PostThreadMessageW(worker_id_.load(), WM_QUIT, 0, 0);
            worker_.join();
        }
    };
}
