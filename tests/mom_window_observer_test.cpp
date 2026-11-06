#include "kh3ap/core/platform/mom_window_observer.h"
#include <condition_variable>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <vector>

static void require(bool ok) { if (!ok) throw std::runtime_error("MoM observer assertion failed"); }
static HWND window(const wchar_t* name) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = name;
    RegisterClassW(&wc);
    // An off-screen fixture, never activated or used for desktop input.
    HWND result = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, name, L"KH3AP observer fixture",
            WS_POPUP, -32000, -32000, 1, 1, nullptr, nullptr, wc.hInstance, nullptr);
    require(result != nullptr);
    ShowWindow(result, SW_SHOWNOACTIVATE);
    return result;
}
static int fixture(HANDLE stop, HANDLE hide, HANDLE hidden) {
    HWND hwnd = window(L"UnityWndClass");
    for (;;) {
        HANDLE handles[]{stop, hide};
        DWORD result = MsgWaitForMultipleObjects(2, handles, FALSE, INFINITE, QS_ALLINPUT);
        if (result == WAIT_OBJECT_0) break;
        if (result == WAIT_OBJECT_0 + 1) {
            DestroyWindow(hwnd);
            hwnd = nullptr;
            ResetEvent(hide);
            SetEvent(hidden);
        } else if (result == WAIT_OBJECT_0 + 2) {
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&message);
        } else return 2;
    }
    if (hwnd) DestroyWindow(hwnd);
    return 0;
}
struct Child {
    HANDLE stop{}, hide{}, hidden{}, process{};
    explicit Child(const std::wstring& executable) {
        SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
        stop = CreateEventW(&security, TRUE, FALSE, nullptr);
        hide = CreateEventW(&security, TRUE, FALSE, nullptr);
        hidden = CreateEventW(&security, TRUE, FALSE, nullptr);
        std::wstring command = L"\"" + executable + L"\" --fixture " +
                std::to_wstring(reinterpret_cast<uintptr_t>(stop)) + L" " +
                std::to_wstring(reinterpret_cast<uintptr_t>(hide)) + L" " +
                std::to_wstring(reinterpret_cast<uintptr_t>(hidden));
        STARTUPINFOW startup{sizeof(startup)};
        PROCESS_INFORMATION info{};
        const bool started = CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr,
                TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &info) != FALSE;
        if (!started) {
            CloseHandle(stop); CloseHandle(hide); CloseHandle(hidden);
            throw std::runtime_error("fixture launch failed");
        }
        process = info.hProcess;
        CloseHandle(info.hThread);
    }
    ~Child() {
        SetEvent(stop);
        if (WaitForSingleObject(process, 5000) != WAIT_OBJECT_0) TerminateProcess(process, 2);
        CloseHandle(process); CloseHandle(stop); CloseHandle(hide); CloseHandle(hidden);
    }
};
int wmain(int argc, wchar_t** argv) {
    try {
        if (argc == 5 && std::wstring_view(argv[1]) == L"--fixture")
            return fixture(reinterpret_cast<HANDLE>(std::stoull(argv[2])),
                    reinterpret_cast<HANDLE>(std::stoull(argv[3])),
                    reinterpret_cast<HANDLE>(std::stoull(argv[4])));
        wchar_t executable[32768]{};
        require(GetModuleFileNameW(nullptr, executable, 32768) != 0);
        const auto name = std::filesystem::path(executable).filename().wstring();
        std::mutex mutex;
        std::condition_variable cv;
        std::vector<bool> activity;
        bool subscribed = false, failed = false;
        auto wait_for = [&](auto predicate) {
            std::unique_lock lock(mutex);
            require(cv.wait_for(lock, std::chrono::seconds(8), predicate));
            require(!failed);
        };
        auto subscribe = [&] {
            return std::make_unique<kh3ap::RuntimeMoMWindowObserver>(
                    [&](bool active) { std::lock_guard lock(mutex); activity.push_back(active); cv.notify_all(); },
                    [&](const char* event, DWORD) {
                        std::lock_guard lock(mutex);
                        subscribed |= std::string_view(event) == "subscribed_no_polling";
                        failed |= std::string_view(event) == "observer_failed";
                        cv.notify_all();
                    }, [](HWND) { return false; }, name);
        };
        auto observer = subscribe();
        wait_for([&] { return subscribed || failed; });
        {
            Child child(executable);
            wait_for([&] { return !activity.empty() && activity.back(); });
            SetEvent(child.hide);
            require(WaitForSingleObject(child.hidden, 5000) == WAIT_OBJECT_0);
            { // Window destruction must not unmask background input before process exit.
                std::unique_lock lock(mutex);
                require(!cv.wait_for(lock, std::chrono::milliseconds(200), [&] { return !activity.back(); }));
            }
            SetEvent(child.stop);
            wait_for([&] { return !activity.back(); });
        }
        {
            Child child(executable);
            wait_for([&] { return activity.back(); });
            require(TerminateProcess(child.process, 3) != FALSE);
            wait_for([&] { return !activity.back(); });
        }
        observer.reset(); // stopped worker cannot callback into destroyed state
        {
            Child child(executable);
            // Use a temporary subscription to know its window has appeared, then
            // verify a new subscription discovers the already-running window.
            observer = subscribe();
            wait_for([&] { return activity.back(); });
            observer.reset();
            observer = subscribe();
            wait_for([&] { return activity.back(); });
            observer.reset(); // shutdown while the observed process is still alive
        }
        std::cout << "MoM window/process event tests passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
