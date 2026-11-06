// Exercise the actual adapter with UE4.17's TFunction and request layout. Only
// the allocator and provider are replaced; no game process is used by this test.
#include <array>
#include <cassert>
#include <future>
#include <iostream>
#include <set>
#include <vector>
#include <malloc.h>
#include "kh3ap/features/kh02/transport/kh02_remote_abi.cpp"

namespace
{
    kh02asset::AsyncReads reads;
    std::mutex allocations_mutex;
    std::set<void*> allocations;
    std::array<std::atomic<unsigned>, 5> events{};
    std::atomic<unsigned> provider_reads{};
    std::mutex gate_mutex;
    std::condition_variable gate_changed;
    bool block_reads{}, entered{};

    void block_provider()
    {
        std::lock_guard lock(gate_mutex);
        block_reads = true;
        entered = false;
    }

    void wait_for_provider()
    {
        std::unique_lock lock(gate_mutex);
        assert(gate_changed.wait_for(lock, std::chrono::seconds(5), [] { return entered; }));
    }

    void release_provider()
    {
        {
            std::lock_guard lock(gate_mutex);
            block_reads = false;
        }
        gate_changed.notify_all();
    }

    bool& flag(MemoryRequest* request, size_t offset)
    {
        return *reinterpret_cast<bool*>(reinterpret_cast<unsigned char*>(request) + 0x70 + offset);
    }

    void cancel(MemoryRequest* request)
    {
        // UE4's nonvirtual Cancel sets these ABI fields before CancelImpl.
        flag(request, 3) = true;
        flag(request, 0) = true;
        std::atomic_thread_fence(std::memory_order_seq_cst);
        if (!flag(request, 1))
        {
            request->CancelImpl();
        }
    }

    uint8* take_results(MemoryRequest* request)
    {
        auto& memory = *reinterpret_cast<uint8**>(reinterpret_cast<unsigned char*>(request) + 0x10);
        if (flag(request, 3))
        {
            return nullptr;
        }
        auto* result = memory;
        memory = nullptr;
        return result;
    }

    auto request(unsigned file, int64 count, FAsyncFileCallBack* callback = nullptr, uint8* supplied = nullptr)
    {
        return std::make_unique<MemoryRequest>(file, 0, count, static_cast<EAsyncIOPriority>(2), callback, supplied);
    }
} // namespace

namespace kh3ap
{
    auto kh02_async_reads() -> kh02asset::AsyncReads&
    {
        return reads;
    }
} // namespace kh3ap

extern "C" long long kh02_remote_file_size(unsigned)
{
    return 4 * 1024 * 1024;
}

extern "C" bool kh02_remote_read(unsigned file, unsigned long long offset, unsigned count, void* output)
{
    ++provider_reads;
    {
        std::unique_lock lock(gate_mutex);
        entered = true;
        gate_changed.notify_all();
        gate_changed.wait(lock, [] { return !block_reads; });
    }
    if (file == 1)
    {
        return false;
    }
    auto* bytes = static_cast<uint8*>(output);
    for (unsigned i = 0; i < count; ++i)
    {
        bytes[i] = static_cast<uint8>((offset + i) % 251);
    }
    return true;
}

extern "C" void* kh02_unreal_malloc(size_t count, unsigned alignment)
{
    auto* memory = _aligned_malloc(count, std::max<size_t>(alignment, 16));
    std::lock_guard lock(allocations_mutex);
    if (memory)
    {
        assert(allocations.insert(memory).second);
    }
    return memory;
}

extern "C" void kh02_unreal_free(void* memory)
{
    if (memory)
    {
        std::lock_guard lock(allocations_mutex);
        assert(allocations.erase(memory) == 1);
        _aligned_free(memory);
    }
}

extern "C" void* kh02_unreal_realloc(void* memory, size_t count, unsigned alignment)
{
    std::lock_guard lock(allocations_mutex);
    assert(!memory || allocations.contains(memory));
    auto* replacement = _aligned_realloc(memory, count, std::max<size_t>(alignment, 16));
    if (replacement || !count)
    {
        allocations.erase(memory);
    }
    if (replacement)
    {
        allocations.insert(replacement);
    }
    return replacement;
}

extern "C" size_t kh02_unreal_quantize(size_t count, unsigned)
{
    return count;
}

extern "C" void kh02_async_event(unsigned kind, unsigned long long, unsigned long long)
{
    ++events.at(kind);
}

int main()
{
    const auto caller = std::this_thread::get_id();
    {
        unsigned callbacks = 0;
        FAsyncFileCallBack callback = [&](bool canceled, IAsyncReadRequest*)
        {
            assert(!canceled);
            assert(std::this_thread::get_id() != caller);
            ++callbacks;
        };
        auto read = request(0, 2 * 1024 * 1024 + 7, &callback);
        read->WaitCompletionImpl(0);
        assert(callbacks == 1 && flag(read.get(), 1));
        auto* result = take_results(read.get());
        assert(result);
        for (unsigned i = 0; i < 2 * 1024 * 1024 + 7; ++i)
        {
            assert(result[i] == i % 251);
        }
        read.reset();
        kh02_unreal_free(result); // Ownership transferred to the engine.
    }
    {
        block_provider();
        unsigned callbacks = 0;
        FAsyncFileCallBack callback = [&](bool canceled, IAsyncReadRequest*)
        {
            assert(canceled);
            ++callbacks;
        };
        const auto before = provider_reads.load();
        auto active = request(0, 2 * 1024 * 1024, &callback);
        wait_for_provider();
        auto queued = request(0, 64, &callback);
        cancel(queued.get());
        cancel(active.get());
        assert(!reads.configure(false));
        active->WaitCompletionImpl(0.01f);
        assert(!flag(active.get(), 1)); // A timed wait does not complete a read.
        release_provider();
        active->WaitCompletionImpl(0);
        queued->WaitCompletionImpl(0);
        assert(callbacks == 2 && provider_reads == before + 1);
        assert(!take_results(active.get()) && !take_results(queued.get()));
    }
    {
        std::array<uint8, 64> supplied{};
        auto read = request(0, supplied.size(), nullptr, supplied.data());
        read->WaitCompletionImpl(0);
        assert(take_results(read.get()) == supplied.data());
        read.reset(); // Never free user memory.
        for (unsigned i = 0; i < supplied.size(); ++i)
        {
            assert(supplied[i] == i % 251);
        }
        auto canceled = request(1, supplied.size(), nullptr, supplied.data());
        canceled->WaitCompletionImpl(0);
        assert(!take_results(canceled.get()));
    }
    for (unsigned file : {1})
    {
        unsigned callbacks = 0;
        FAsyncFileCallBack callback = [&](bool canceled, IAsyncReadRequest*)
        {
            assert(canceled);
            ++callbacks;
        };
        auto failed = request(file, 64, &callback);
        failed->WaitCompletionImpl(0);
        assert(callbacks == 1 && !take_results(failed.get()));
    }
    {
        unsigned callbacks = 0;
        FAsyncFileCallBack callback = [&](bool canceled, IAsyncReadRequest*)
        {
            assert(!canceled && std::this_thread::get_id() == caller);
            ++callbacks;
        };
        assert(reads.configure(false));
        auto read = request(0, 64, &callback);
        assert(flag(read.get(), 1) && callbacks == 1);
        assert(reads.configure(true));
    }
    {
        unsigned callbacks = 0;
        FAsyncFileCallBack callback = [&](bool, IAsyncReadRequest*) { ++callbacks; };
        auto empty = request(0, 0, &callback);
        auto invalid = request(0, -1, &callback);
        auto oversized = request(0, 4 * 1024 * 1024 + 1, &callback);
        MemoryRequest hint(0, 0, MAX_int64, AIOP_Precache, &callback, nullptr);
        MemoryRequest size(0, &callback);
        assert(callbacks == 5);
        assert(flag(invalid.get(), 3) && flag(oversized.get(), 3));
        assert(!take_results(&hint));
        assert(*reinterpret_cast<int64*>(reinterpret_cast<uint8*>(&size) + 0x10) == 4 * 1024 * 1024);
    }
    {
        FAsyncFileCallBack callback = [](bool, IAsyncReadRequest*)
        {
            auto nested = request(0, 64);
            nested->WaitCompletionImpl(0);
            assert(flag(nested.get(), 1));
        };
        auto outer = request(0, 64, &callback);
        outer->WaitCompletionImpl(0);
    }
    {
        kh02asset::AsyncReads bounded(1);
        std::promise<void> release, started;
        auto ready = release.get_future().share();
        auto active = std::make_shared<kh02asset::ReadTask>(
            [&](auto&)
            {
                started.set_value();
                ready.wait();
            });
        bounded.submit(active);
        started.get_future().wait();
        auto queued = std::make_shared<kh02asset::ReadTask>([](auto&) {});
        bounded.submit(queued);
        bool inline_done = false;
        auto overflow = std::make_shared<kh02asset::ReadTask>(
            [&](auto&)
            {
                assert(std::this_thread::get_id() == caller);
                inline_done = true;
            });
        bounded.submit(overflow);
        assert(inline_done && bounded.stats().inline_reads == 1);
        release.set_value();
        bounded.shutdown();
        assert(bounded.stats().completed == 3 && bounded.stats().pending == 0);
    }
    {
        FAsyncFileCallBack callback = [](bool, IAsyncReadRequest*) { throw std::runtime_error("Callback failed"); };
        auto failed = request(0, 64, &callback);
        failed->WaitCompletionImpl(0);
        assert(flag(failed.get(), 1) && flag(failed.get(), 3) && events[4] == 1);
    }
    // Mimic the engine deleting immediately after PollCompletion. The derived
    // destructor must fence the worker's remaining bookkeeping on every run.
    for (unsigned i = 0; i < 1000; ++i)
    {
        auto read = request(0, 64);
        while (!*reinterpret_cast<volatile bool*>(reinterpret_cast<uint8*>(read.get()) + 0x71))
        {
            std::this_thread::yield();
        }
    }
    {
        block_provider();
        auto active = request(0, 64);
        wait_for_provider();
        auto queued = request(0, 64);
        auto shutdown = std::async(std::launch::async, [] { reads.shutdown(); });
        assert(shutdown.wait_for(std::chrono::milliseconds(10)) == std::future_status::timeout);
        release_provider();
        shutdown.get();
        queued->WaitCompletionImpl(0);
        assert(flag(queued.get(), 3));
    }
    {
        auto late = request(0, 64);
        assert(flag(late.get(), 1) && flag(late.get(), 3));
    }
    assert(reads.stats().pending == 0);
    assert(events[2] == events[3]);
    assert(allocations.empty());
    std::cout << "Async ABI: reads, wait, cancellation, callbacks, ownership and shutdown passed\n";
}
