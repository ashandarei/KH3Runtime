#include "kh3ap/features/player/movement/character_airstep_gate_stub.h"
#ifdef KH3AP_TEST_POLYHOOK
#include <polyhook2/Detour/x64Detour.hpp>
#endif
#define NOMINMAX
#include <Windows.h>
#include <intrin.h>
#include <array>
#include <cassert>
#include <cstring>

namespace
{
    struct Pawn { uint8_t kind; bool owned; };
    struct Observation
    {
        std::array<uint64_t, 7> registers;
        uint64_t flags;
        std::array<std::array<uint8_t, 16>, 6> xmm;
        uint32_t allowed;
    };
    static_assert(offsetof(Observation, xmm) == 64 && offsetof(Observation, allowed) == 160);
    std::array<std::array<uint8_t, 16>, 6> vectors;
    constexpr std::array<uint64_t, 7> values{
        0x1122334455667700, 0x1234567812345678, 0x2345678923456789,
        0x3456789a3456789a, 0x456789ab456789ab, 0x56789abc56789abc, 0x6789abcd6789abcd};
    Pawn* expected_pawn{};
    unsigned calls{};

    __declspec(noinline) uint8_t compare_kind(Pawn* pawn, uint8_t native_kind)
    {
        assert((reinterpret_cast<uintptr_t>(_AddressOfReturnAddress()) & 15) == 8);
        assert(pawn == expected_pawn && native_kind == pawn->kind);
        // Shotlock selection and any nested native calls must still see the
        // authored identity, even when this one compare will accept Sora's ID.
        assert(pawn->kind == expected_pawn->kind);
        ++calls;
        return pawn->owned ? 1 : native_kind;
    }

    void append(std::vector<uint8_t>& code, uint64_t value)
    {
        for (unsigned shift = 0; shift < 64; shift += 8)
        {
            code.push_back(static_cast<uint8_t>(value >> shift));
        }
    }
    void* executable(const std::vector<uint8_t>& code)
    {
        auto* memory = VirtualAlloc(nullptr, code.size(), MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        assert(memory);
        std::memcpy(memory, code.data(), code.size());
        assert(FlushInstructionCache(GetCurrentProcess(), memory, code.size()));
        return memory;
    }

    void run(bool hooked, uint32_t rejection_distance = 0)
    {
        Pawn foreign{99, false};
        // Wrap the observer in a native callback that deliberately destroys
        // every volatile register except its AL result, plus xmm0-xmm5.
        std::vector<uint8_t> callback{0x48, 0x83, 0xec, 0x28, 0x48, 0xb8};
        append(callback, reinterpret_cast<uint64_t>(&compare_kind));
        callback.insert(callback.end(), {0xff, 0xd0, 0x48, 0x83, 0xc4, 0x28});
        for (unsigned reg = 0; reg < 6; ++reg)
        {
            callback.insert(callback.end(), {0x66, 0x0f, 0xef, static_cast<uint8_t>(0xc0 + 9 * reg)});
        }
        for (const auto prefix : {uint16_t{0xb948}, uint16_t{0xba48}, uint16_t{0xb849},
                                  uint16_t{0xb949}, uint16_t{0xba49}, uint16_t{0xbb49}})
        {
            callback.push_back(static_cast<uint8_t>(prefix));
            callback.push_back(static_cast<uint8_t>(prefix >> 8));
            append(callback, 0xffffffffffffffff);
        }
        callback.push_back(0xc3);
        void* observer = executable(callback);

        std::vector<uint8_t> driver{
            0x53, 0x55, 0x41, 0x55, 0x41, 0x57, 0x48, 0x83, 0xec, 0x28, // aligned native body
            0x4c, 0x89, 0xc5, // rbp=output
            0x48, 0x89, 0xcb, // rbx=native pawn on EGS and Steam
            0x49, 0xbd}; // old register R13 must not be used for this helper
        append(driver, reinterpret_cast<uint64_t>(&foreign));
        driver.insert(driver.end(), {0x49, 0xbf}); // old register R15 must not be used either
        append(driver, reinterpret_cast<uint64_t>(&foreign));
        driver.insert(driver.end(), {0x48, 0xb8});
        append(driver, reinterpret_cast<uint64_t>(vectors.data()));
        for (uint8_t index = 0; index < 6; ++index)
        {
            driver.insert(driver.end(), {0xf3, 0x0f, 0x6f, static_cast<uint8_t>(0x40 + 8 * index),
                static_cast<uint8_t>(16 * index)});
        }
        driver.insert(driver.end(), {0x48, 0xb8});
        append(driver, values[0]);
        driver.insert(driver.end(), {0x88, 0xd0}); // al=native kind argument
        const std::array<uint16_t, 6> registers{0xb948, 0xba48, 0xb849, 0xb949, 0xba49, 0xbb49};
        for (size_t index = 0; index < registers.size(); ++index)
        {
            driver.push_back(static_cast<uint8_t>(registers[index]));
            driver.push_back(static_cast<uint8_t>(registers[index] >> 8));
            append(driver, values[index + 1]);
        }
        driver.push_back(0xf9); // CF must survive the direct branch shim
        const size_t site = driver.size();
        size_t stub_literal{};
        if (hooked)
        {
            assert(rejection_distance >= 15);
            driver.insert(driver.end(), {0x3c, 1, 0x0f, 0x85});
            for (unsigned shift = 0; shift < 32; shift += 8)
            {
                driver.push_back(static_cast<uint8_t>(rejection_distance >> shift));
            }
            driver.insert(driver.end(), {0xc7, 0x85, 0xa0, 0, 0, 0, 1, 0, 0, 0, 0xe9});
            for (unsigned shift = 0; shift < 32; shift += 8)
            {
                driver.push_back(static_cast<uint8_t>((rejection_distance - 5) >> shift));
            }
            driver.resize(site + 8 + rejection_distance, 0x90);
            driver.insert(driver.end(), {0xc7, 0x85, 0xa0, 0, 0, 0, 0, 0, 0, 0});
        }
        else
        {
            driver.insert(driver.end(), {0xff, 0x25, 0, 0, 0, 0});
            stub_literal = driver.size();
            append(driver, 0);
        }
        const size_t continuation = driver.size();
        driver.insert(driver.end(), {0x48, 0x89, 0x45, 0, 0x48, 0x89, 0x4d, 8,
            0x48, 0x89, 0x55, 16, 0x4c, 0x89, 0x45, 24, 0x4c, 0x89, 0x4d, 32,
            0x4c, 0x89, 0x55, 40, 0x4c, 0x89, 0x5d, 48,
            0x9c, 0x58, 0x48, 0x89, 0x45, 56});
        for (uint8_t index = 0; index < 6; ++index)
        {
            driver.insert(driver.end(), {0xf3, 0x0f, 0x7f, static_cast<uint8_t>(0x85 + 8 * index),
                static_cast<uint8_t>(64 + 16 * index), 0, 0, 0});
        }
        driver.insert(driver.end(), {0x48, 0x83, 0xc4, 0x28, 0x41, 0x5f, 0x41, 0x5d, 0x5d, 0x5b, 0xc3});
        void* entry = executable(driver);
        uint64_t trampoline = reinterpret_cast<uint64_t>(entry) + continuation;
        const auto code = kh3ap::character_airstep::build_aim_kind_gate_stub(
            reinterpret_cast<uint64_t>(observer), trampoline);
        void* stub = executable(code);
#ifdef KH3AP_TEST_POLYHOOK
        PLH::x64Detour hook(reinterpret_cast<uint64_t>(entry) + site, reinterpret_cast<uint64_t>(stub), &trampoline);
        if (hooked)
        {
            kh3ap::character_airstep::configure_aim_kind_gate_detour(hook);
            assert(hook.hook());
            assert(*reinterpret_cast<uint16_t*>(static_cast<uint8_t*>(entry) + site) == 0x25ff);
            std::memcpy(static_cast<uint8_t*>(stub) + code.size() - 8, &trampoline, 8);
        }
#else
        assert(!hooked);
#endif
        if (!hooked) { std::memcpy(static_cast<uint8_t*>(entry) + stub_literal, &stub, sizeof(stub)); }
        FlushInstructionCache(GetCurrentProcess(), stub, code.size());
        FlushInstructionCache(GetCurrentProcess(), entry, driver.size());
        auto invoke = reinterpret_cast<void (*)(Pawn*, uint8_t, Observation*)>(entry);
        for (const uint8_t kind : {1, 2, 3, 5, 6})
        {
            for (bool owned : {false, true})
            {
                Pawn pawn{kind, owned};
                expected_pawn = &pawn;
                const auto before = calls;
                Observation observed{};
                invoke(&pawn, kind, &observed);
                const auto expected_kind = owned ? 1 : kind;
                auto expected_registers = values;
                expected_registers[0] |= expected_kind;
                assert(calls == before + 1 && pawn.kind == kind);
                assert(observed.registers == expected_registers && observed.xmm == vectors);
                if (hooked) { assert(observed.allowed == (expected_kind == 1)); }
                else { assert((observed.flags & 1) == 1); }
            }
        }
#ifdef KH3AP_TEST_POLYHOOK
        if (hooked)
        {
            assert(hook.unHook());
            assert(std::memcmp(static_cast<uint8_t*>(entry) + site, driver.data() + site, 8) == 0);
        }
#endif
        VirtualFree(entry, 0, MEM_RELEASE);
        VirtualFree(stub, 0, MEM_RELEASE);
        VirtualFree(observer, 0, MEM_RELEASE);
    }
}

int main()
{
    for (size_t index = 0; index < vectors.size(); ++index)
    {
        for (size_t byte = 0; byte < vectors[index].size(); ++byte)
        {
            vectors[index][byte] = static_cast<uint8_t>(index * 16 + byte + 1);
        }
    }
    run(false);
#ifdef KH3AP_TEST_POLYHOOK
    // Exact EGS/Steam forward rejection distances from the discovery helper.
    for (uint32_t distance : {0xd95u, 0xe53u})
    {
        run(true, distance);
    }
#endif
}
