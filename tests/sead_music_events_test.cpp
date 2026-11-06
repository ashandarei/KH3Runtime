#include "kh3ap/features/music/events/sead_music_events.h"
#include "kh3ap/features/music/events/sead_music_profiles.h"
#include "kh3ap/features/music/audio/music_decode_completion.h"
#include "kh3ap/features/music/events/music_update_events.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <latch>
#include <memory>
#include <thread>
#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#endif

int main()
{
    using namespace kh3ap;
    // Exact storefront selection rejects unknown revisions and mixed identities.
    using namespace kh3ap::sead_music_detail;
    assert(select_profile(0x60547D11, 0xAEBF000) == &profiles[0]);
    assert(select_profile(0x66743F34, 0xA969000) == &profiles[1]);
    assert(!select_profile(0x60547D11, 0xA969000));
    assert(!select_profile(0x66743F34, 0xAEBF000));
    for (const auto& profile : profiles)
    {
        for (unsigned bit = 0; bit < 32; ++bit)
        {
            assert(!select_profile(profile.timestamp ^ (1u << bit), profile.image_size));
            assert(!select_profile(profile.timestamp, profile.image_size ^ (1u << bit)));
        }
        // Both finish sites must fit the shared six-byte detour entirely within
        // the nine-byte store+jump block, before the next branch target.
        const auto& finish = profile.sites[static_cast<size_t>(SeadMusicEvent::Finished)];
        const uint8_t store_jump[]{0xC6, 0x46, 0x40, 0x04, 0xE9};
        assert(std::memcmp(finish.signature.data(), store_jump, sizeof store_jump) == 0);
        assert(profile.finish_owner < finish.rva && finish.rva + 32 < profile.image_size);
    }
    // Completion, failure, and a cancelled worker finishing after its channel
    // has been replaced. Exercise real publication between threads.
    std::atomic_int notices{};
    auto completed = std::make_shared<MusicDecodeCompletion<std::optional<int>>>();
    assert(!completed->take());
    std::thread success{[completed, &notices] { completed->publish(42, [&] { ++notices; }); }};
    success.join();
    assert(completed->take().value().value() == 42 && !completed->take() && notices == 1);
    auto failure = std::make_shared<MusicDecodeCompletion<std::optional<int>>>();
    std::thread failed{[failure, &notices] { failure->publish(std::nullopt, [&] { ++notices; }); }};
    failed.join();
    const auto failed_result = failure->take();
    assert(failed_result && !*failed_result && notices == 2);
    std::latch release{1};
    auto cancelled = std::make_shared<MusicDecodeCompletion<std::optional<int>>>();
    std::weak_ptr old = cancelled;
    std::thread late{[cancelled, &release, &notices] {
        release.wait(); cancelled->publish(99, [&] { ++notices; });
    }};
    cancelled->cancel();
    cancelled.reset();
    release.count_down();
    late.join();
    assert(old.expired() && notices == 2);

    SeadMusicMailbox mailbox;
    assert(mailbox.take() == 0);
    mailbox.post(SeadMusicEvent::Loaded);
    mailbox.post(SeadMusicEvent::Loaded);
    mailbox.post(SeadMusicEvent::Play);
    assert(mailbox.take() == (sead_music_event_bit(SeadMusicEvent::Loaded) | sead_music_event_bit(SeadMusicEvent::Play)));
    assert(mailbox.take() == 0);
    mailbox.post(SeadMusicEvent::Stop);
    assert(mailbox.take() == sead_music_event_bit(SeadMusicEvent::Stop));
#if defined(_WIN32) && defined(_M_X64)
    // Execute the actual emitted stub in this standalone test process. The
    // harness sets RAX/flags, and the trampoline records them before returning.
    // Tests both production (no counter) and diagnostic machine code.
    auto* memory = static_cast<uint8_t*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    assert(memory);
    std::atomic<uint64_t> count{};
    for (bool diagnostics : {false, true})
    {
        DWORD old{};
        assert(VirtualProtect(memory, 4096, PAGE_READWRITE, &old));
        auto stub = build_sead_finish_stub(reinterpret_cast<uint64_t>(&mailbox.pending),
                                          diagnostics ? reinterpret_cast<uint64_t>(&count) : 0);
        const uint64_t trampoline = reinterpret_cast<uint64_t>(memory + 256);
        std::memcpy(stub.data() + stub.size() - 8, &trampoline, 8);
        std::memcpy(memory + 64, stub.data(), stub.size());
        const uint8_t harness[]{0x48,0x89,0xC8, 0x48,0x39,0xC0, 0xFF,0x25,0,0,0,0};
        std::memcpy(memory, harness, sizeof harness);
        const uint64_t destination = reinterpret_cast<uint64_t>(memory + 64);
        std::memcpy(memory + sizeof harness, &destination, 8);
        const uint8_t tail[]{0x48,0x89,0x02, 0x9C,0x58, 0x48,0x89,0x42,0x08, 0xC3};
        std::memcpy(memory + 256, tail, sizeof tail);
        assert(VirtualProtect(memory, 4096, PAGE_EXECUTE_READ, &old));
        FlushInstructionCache(GetCurrentProcess(), memory, 4096);
        uint64_t observed[2]{};
        constexpr uint64_t marker = 0x123456789ABCDEF0;
        mailbox.post(SeadMusicEvent::Loaded); // finish must preserve other queued reasons
        reinterpret_cast<void (*)(uint64_t, uint64_t*)>(memory)(marker, observed);
        assert(observed[0] == marker);
        assert((observed[1] & 0x8D5) == 0x44); // cmp rax,rax: ZF/PF set, CF/OF/SF/AF clear
        assert(mailbox.take() == (sead_music_event_bit(SeadMusicEvent::Loaded) | sead_music_event_bit(SeadMusicEvent::Finished)));
        assert(count.load() == (diagnostics ? 1 : 0));
    }
    VirtualFree(memory, 0, MEM_RELEASE);
#endif
    std::cout << "Music mailbox and native finish stub passed\n";
}
