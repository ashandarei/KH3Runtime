#define NOMINMAX
#include <Windows.h>
#include "kh3ap/features/kh02/ui/character_record_lookup_stub.h"
#include <array>
#include <cassert>
#include <cstring>
#include <iostream>

namespace
{
    struct Executable
    {
        void* address{};
        explicit Executable(const std::vector<uint8_t>& bytes)
        {
            address = VirtualAlloc(nullptr, bytes.size(), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
            assert(address);
            std::memcpy(address, bytes.data(), bytes.size());
            DWORD previous{};
            assert(VirtualProtect(address, bytes.size(), PAGE_EXECUTE_READ, &previous));
            FlushInstructionCache(GetCurrentProcess(), address, bytes.size());
        }
        ~Executable() { VirtualFree(address, 0, MEM_RELEASE); }
    };

    void immediate(std::vector<uint8_t>& code, uint64_t value)
    {
        for (unsigned shift = 0; shift < 64; shift += 8)
        {
            code.push_back(static_cast<uint8_t>(value >> shift));
        }
    }

    struct Observation
    {
        std::array<std::array<uint8_t, 16>, 6> input;
        uint64_t result{};
        uint64_t r10{};
        uint64_t r11{};
        uint64_t padding{};
        std::array<std::array<uint8_t, 16>, 6> output;
        uint64_t r8{};
        uint64_t r9{};
    };
    static_assert(offsetof(Observation, result) == 0x60);
    static_assert(offsetof(Observation, output) == 0x80);

    auto observe(void* callback) -> Observation
    {
        // A retail-style caller keeps values in r10/r11 and xmm0..5 across
        // the lookup. Seed them, call the real generated thunk, then read back.
        std::vector<uint8_t> driver{0x57, 0x48, 0x83, 0xec, 0x20, 0x48, 0x89, 0xcf};
        driver.insert(driver.end(), {0x49, 0xba});
        immediate(driver, 0x123456789abcdef0);
        driver.insert(driver.end(), {0x49, 0xbb});
        immediate(driver, 0xfedcba9876543210);
        for (uint8_t index = 0; index < 6; ++index)
        {
            driver.insert(driver.end(), {0x0f, 0x10, static_cast<uint8_t>(0x47 + index * 8),
                                         static_cast<uint8_t>(index * 16)});
        }
        driver.insert(driver.end(), {0x48, 0xc7, 0xc1, 0x34, 0x12, 0, 0, // rcx = save bank stand-in
                                     0xba, 7, 0, 0, 0, 0x48, 0xb8});  // edx = Roxas character ID
        immediate(driver, reinterpret_cast<uint64_t>(callback));
        driver.insert(driver.end(), {0xff, 0xd0,
                                     0x48, 0x89, 0x47, 0x60,
                                     0x4c, 0x89, 0x57, 0x68,
                                     0x4c, 0x89, 0x5f, 0x70});
        for (uint8_t index = 0; index < 6; ++index)
        {
            driver.insert(driver.end(), {0x0f, 0x11, static_cast<uint8_t>(0x87 + index * 8),
                                         static_cast<uint8_t>(0x80 + index * 16), 0, 0, 0});
        }
        driver.insert(driver.end(), {0x4c, 0x89, 0x87, 0xe0, 0, 0, 0,
                                     0x4c, 0x89, 0x8f, 0xe8, 0, 0, 0});
        driver.insert(driver.end(), {0x48, 0x83, 0xc4, 0x20, 0x5f, 0xc3});
        Executable caller(driver);
        Observation result;
        for (size_t index = 0; index < result.input.size(); ++index)
        {
            result.input[index].fill(static_cast<uint8_t>(index + 1));
        }
        reinterpret_cast<void (*)(Observation*)>(caller.address)(&result);
        return result;
    }
}

int main()
{
    // Legal public Win64 callback: returns native_record+16, destroys volatile registers.
    std::vector<uint8_t> code{0x49, 0x8d, 0x40, 0x10,
                              0x31, 0xc9, 0x31, 0xd2, 0x45, 0x31, 0xc0, 0x45, 0x31, 0xc9,
                              0x45, 0x31, 0xd2, 0x45, 0x31, 0xdb};
    for (uint8_t index = 0; index < 6; ++index)
    {
        code.insert(code.end(), {0x66, 0x0f, 0xef, static_cast<uint8_t>(0xc0 + index * 9)});
    }
    code.push_back(0xc3);
    Executable clobber(code);
    const auto baseline = observe(clobber.address);
    assert(baseline.r10 == 0 && baseline.r11 == 0);
    assert(baseline.input != baseline.output);

    // Native lookup returns bank+character and has observable r8/r9 outputs.
    Executable native({0x48, 0x8d, 0x04, 0x11, 0x49, 0x89, 0xc8, 0x41, 0x89, 0xd1, 0xc3});
    const auto original = reinterpret_cast<uint64_t>(native.address);
    Executable thunk(kh3ap::build_character_record_lookup_stub(
        reinterpret_cast<uint64_t>(clobber.address), reinterpret_cast<uint64_t>(&original)));
    const auto protected_call = observe(thunk.address);
    assert(protected_call.result == 0x124b);
    assert(protected_call.r8 == 0x1234 && protected_call.r9 == 7);
    assert(protected_call.r10 == 0x123456789abcdef0);
    assert(protected_call.r11 == 0xfedcba9876543210);
    assert(protected_call.input == protected_call.output);
    std::cout << "Character record lookup: arguments, result and retail live registers preserved\n";
}
