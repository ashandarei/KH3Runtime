#include "shared/kh02/kh02_control_pipe.h"
#define NOMINMAX
#include <Windows.h>
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>

int main()
{
    using namespace kh02control;
    Identity identity{GetCurrentProcessId(), GetCurrentProcessId(), 123, 456};
    identity.nonce.fill('b');
    std::atomic<bool> ready{};
    std::atomic<unsigned> accepted{};
    Server server(identity,
                  [&](const Request&)
                  {
                      if (!ready.load())
                      {
                          return Status::not_ready;
                      }
                      ++accepted;
                      return Status::accepted;
                  });
    Request request;
    request.identity = identity;
    request.generation = 1;
    request.client_pid = GetCurrentProcessId();
    assert(exchange(request).status == Status::not_ready);
    auto wrong = request;
    wrong.identity.catalog_tag++;
    assert(exchange(wrong).status == Status::wrong_session);
    wrong = request;
    wrong.client_pid++;
    assert(exchange(wrong).status == Status::invalid);

    Client client;
    const auto started = std::chrono::steady_clock::now();
    const auto ticket = client.submit(request);
    assert(std::chrono::steady_clock::now() - started < std::chrono::milliseconds(100));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    assert(!ticket->complete.load());
    ready = true;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!ticket->complete.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    assert(ticket->complete.load());
    assert(ticket->reply.status == Status::accepted);
    assert(accepted == 1);

    ready = false;
    request.generation++;
    const auto canceled = client.submit(request);
    canceled->canceled = true;
    while (!canceled->complete.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    assert(canceled->complete.load());
    assert(canceled->reply.status == Status::canceled);
    assert(accepted == 1);
    std::cout << "KH02 pipe identity, asynchronous readiness, cancellation and teardown passed\n";
}
