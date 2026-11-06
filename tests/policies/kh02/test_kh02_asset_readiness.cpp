#include "shared/kh02/kh02_asset_readiness.h"
#include <cassert>
#include <chrono>
#include <future>
#include <latch>

namespace
{
    auto expect_failure(const kh02control::AssetReadinessSignal& signal, HANDLE source, std::stop_token token,
                        DWORD timeout) -> std::string
    {
        try
        {
            signal.wait(source, token, timeout);
        }
        catch (const std::runtime_error& error)
        {
            return error.what();
        }
        assert(false);
        return {};
    }
} // namespace

int main()
{
    using namespace std::chrono_literals;
    kh02control::Identity identity;
    identity.owner_pid = GetCurrentProcessId();
    identity.source_pid = identity.owner_pid;
    identity.session_generation = GetTickCount64();
    identity.catalog_tag = 42;
    identity.nonce.fill('a');
    kh02control::AssetReadinessSignal signal(identity);
    const auto publisher = kh02control::open_asset_readiness_publisher(identity);
    const auto source = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    assert(source);

    // A signal before the waiter starts must remain visible to every waiter.
    assert(SetEvent(publisher));
    signal.wait(source, {}, 0);
    signal.wait(source, {}, 0);
    std::stop_source canceled;
    canceled.request_stop();
    assert(expect_failure(signal, source, canceled.get_token(), 0) == "kh02_asset_readiness_canceled");
    assert(SetEvent(source));
    assert(expect_failure(signal, source, {}, 0) == "kh02_provider_source_exited");
    ResetEvent(source);

    // A different session cannot consume the previous session's signal.
    identity.nonce.fill('b');
    kh02control::AssetReadinessSignal next_signal(identity);
    assert(expect_failure(next_signal, source, {}, 0) == "kh02_provider_readiness_timeout");
    const auto next_publisher = kh02control::open_asset_readiness_publisher(identity);
    std::latch started(2);
    const auto wait = [&]
    {
        started.count_down();
        next_signal.wait(source, {}, 2'000);
    };
    auto first = std::async(std::launch::async, wait);
    auto second = std::async(std::launch::async, wait);
    started.wait();
    assert(SetEvent(next_publisher));
    assert(first.wait_for(2s) == std::future_status::ready);
    assert(second.wait_for(2s) == std::future_status::ready);
    first.get();
    second.get();

    identity.nonce.fill('c');
    kh02control::AssetReadinessSignal canceled_signal(identity);
    std::stop_source stop;
    std::latch waiting(1);
    auto interrupted = std::async(std::launch::async,
                                  [&]
                                  {
                                      waiting.count_down();
                                      return expect_failure(canceled_signal, source, stop.get_token(), 2'000);
                                  });
    waiting.wait();
    stop.request_stop();
    assert(interrupted.wait_for(2s) == std::future_status::ready);
    assert(interrupted.get() == "kh02_asset_readiness_canceled");
    CloseHandle(next_publisher);
    CloseHandle(publisher);
    CloseHandle(source);
}
