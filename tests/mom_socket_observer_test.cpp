#include "kh3ap/core/platform/mom_socket_observer.h"
#include <cassert>
#include <condition_variable>
#include <mutex>
#include <chrono>

int main() {
    WSADATA data{};
    assert(WSAStartup(MAKEWORD(2, 2), &data) == 0);
    // Reserve an ephemeral test port, then release it for the observer.
    SOCKET reservation = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(bind(reservation, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
    int length = sizeof(address);
    assert(getsockname(reservation, reinterpret_cast<sockaddr*>(&address), &length) == 0);
    closesocket(reservation);
    std::mutex mutex;
    std::condition_variable changed;
    bool active{};
    int transitions{};
    auto wait = [&](bool expected, int count) {
        std::unique_lock lock(mutex);
        assert(changed.wait_for(lock, std::chrono::seconds(5), [&] { return active == expected && transitions >= count; }));
    };
    SOCKET client = INVALID_SOCKET;
    {
        kh3ap::RuntimeMoMSocketObserver observer(ntohs(address.sin_port), [&](bool value) {
            std::lock_guard lock(mutex);
            active = value;
            ++transitions;
            changed.notify_all();
        }, "fixture-greeting\n");
        for (int round = 0; round < 2; ++round) {
            client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            assert(connect(client, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
            DWORD timeout = 5000;
            setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
            std::string greeting;
            char value{};
            while (greeting.size() < 17 && recv(client, &value, 1, 0) == 1) greeting += value;
            assert(greeting == "fixture-greeting\n");
            wait(true, round * 2 + 1);
            if (round == 0) {
                closesocket(client);
                wait(false, 2);
            }
        }
        // Destruction must wake the blocking active-client wait.
    }
    wait(false, 4);
    closesocket(client);
    WSACleanup();
}
