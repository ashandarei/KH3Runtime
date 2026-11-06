#pragma once
#include <winsock2.h>
#include <Windows.h>
#include <functional>
#include <stdexcept>
#include <thread>
#include <string>

namespace kh3ap {
// Opt-in loopback lifecycle lease for a native Linux client outside Wine.
// A connected client means active; EOF (including client death) clears it.
class RuntimeMoMSocketObserver {
public:
    RuntimeMoMSocketObserver(unsigned short port, std::function<void(bool)> activity, std::string greeting = {})
        : activity_(std::move(activity)), greeting_(std::move(greeting)) {
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data)) throw std::runtime_error("MoM WSAStartup failed");
        stop_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        event_ = WSACreateEvent();
        listener_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons(port);
        if (!stop_ || event_ == WSA_INVALID_EVENT || listener_ == INVALID_SOCKET ||
            bind(listener_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) ||
            listen(listener_, 1) || WSAEventSelect(listener_, event_, FD_ACCEPT)) {
            cleanup();
            throw std::runtime_error("MoM lifecycle listener failed");
        }
        try { worker_ = std::thread([this] { run(); }); }
        catch (...) { cleanup(); throw; }
    }
    ~RuntimeMoMSocketObserver() {
        SetEvent(stop_);
        worker_.join();
        cleanup();
    }
private:
    void run() {
        HANDLE handles[]{stop_, event_};
        while (WaitForMultipleObjects(2, handles, FALSE, INFINITE) == WAIT_OBJECT_0 + 1) {
            WSANETWORKEVENTS events{};
            if (WSAEnumNetworkEvents(listener_, event_, &events)) break;
            if (!(events.lNetworkEvents & FD_ACCEPT)) continue;
            SOCKET client = accept(listener_, nullptr, nullptr);
            if (client == INVALID_SOCKET) continue;
            if (!greeting_.empty() && send(client, greeting_.data(), static_cast<int>(greeting_.size()), 0) != static_cast<int>(greeting_.size())) {
                closesocket(client);
                continue;
            }
            WSAEVENT closed = WSACreateEvent();
            if (closed == WSA_INVALID_EVENT || WSAEventSelect(client, closed, FD_CLOSE)) {
                if (closed != WSA_INVALID_EVENT) WSACloseEvent(closed);
                closesocket(client);
                continue;
            }
            activity_(true);
            HANDLE waits[]{stop_, closed};
            WaitForMultipleObjects(2, waits, FALSE, INFINITE);
            activity_(false);
            closesocket(client);
            WSACloseEvent(closed);
        }
    }
    void cleanup() {
        if (listener_ != INVALID_SOCKET) closesocket(listener_);
        if (event_ != WSA_INVALID_EVENT) WSACloseEvent(event_);
        if (stop_) CloseHandle(stop_);
        WSACleanup();
    }
    std::function<void(bool)> activity_;
    std::string greeting_;
    HANDLE stop_{};
    WSAEVENT event_{WSA_INVALID_EVENT};
    SOCKET listener_{INVALID_SOCKET};
    std::thread worker_;
};
} // namespace kh3ap
