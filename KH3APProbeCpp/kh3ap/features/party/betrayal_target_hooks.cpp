#include "betrayal_target_hooks.h"
#ifdef S
#pragma push_macro("S")
#undef S
#define KH3AP_BETRAYAL_RESTORE_S
#endif
#include <polyhook2/Detour/x64Detour.hpp>
#ifdef KH3AP_BETRAYAL_RESTORE_S
#pragma pop_macro("S")
#undef KH3AP_BETRAYAL_RESTORE_S
#endif
#include "kh3ap/core/objects/uobject_validity.h"
#include <Unreal/UObject.hpp>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>

namespace kh3ap::betrayal_target_hooks
{
    using RC::Unreal::UObject;
    namespace
    {
        using TargetFunction = UObject* (*)(UObject*);
        struct Site
        {
            uint32_t rva;
            std::array<uint8_t, 24> bytes;
        };
        struct Profile
        {
            uint32_t timestamp, image_size;
            std::array<Site, 3> sites;
        };
        // GetMyTarget and both native FindMyTarget branches. The reflected
        // thunks dispatch these controller vtable slots directly in both stores.
        constexpr std::array<size_t, 3> slots{0x970, 0xa38, 0xa40};
        constexpr std::array<Profile, 2> profiles{
            {{0x60547d11,
              0xaebf000,
              {{{0x6a9ff0,
                 {0x48, 0x83, 0xec, 0x28, 0x48, 0x8b, 0x81, 0xa8, 0x04, 0x00, 0x00, 0x48,
                  0x85, 0xc0, 0x74, 0x5d, 0x0f, 0xb6, 0x91, 0x47, 0x07, 0x00, 0x00, 0x48}},
                {0x6a8c10,
                 {0x48, 0x8b, 0xc4, 0x55, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
                  0x48, 0x8d, 0x68, 0x88, 0x48, 0x81, 0xec, 0x50, 0x01, 0x00, 0x00, 0x48}},
                {0x6a91b0,
                 {0x48, 0x8b, 0xc4, 0x55, 0x57, 0x41, 0x54, 0x41, 0x56, 0x41, 0x57, 0x48,
                  0x8d, 0x68, 0xa1, 0x48, 0x81, 0xec, 0x00, 0x01, 0x00, 0x00, 0x48, 0xc7}}}}},
             {0x66743f34,
              0xa969000,
              {{{0x51cf00,
                 {0x48, 0x83, 0xec, 0x28, 0x48, 0x8b, 0x81, 0xa8, 0x04, 0x00, 0x00, 0x48,
                  0x85, 0xc0, 0x74, 0x5d, 0x0f, 0xb6, 0x91, 0x47, 0x07, 0x00, 0x00, 0x48}},
                {0x51a670,
                 {0x48, 0x8b, 0xc4, 0x55, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
                  0x48, 0x8d, 0x68, 0xb8, 0x48, 0x81, 0xec, 0x20, 0x01, 0x00, 0x00, 0x48}},
                {0x51a3d0,
                 {0x48, 0x8b, 0xc4, 0x55, 0x48, 0x8d, 0x68, 0xa1, 0x48, 0x81, 0xec, 0x00,
                  0x01, 0x00, 0x00, 0x48, 0xc7, 0x45, 0xa7, 0xfe, 0xff, 0xff, 0xff, 0x48}}}}}}};
        struct State
        {
            std::array<std::unique_ptr<PLH::x64Detour>, 3> hooks;
            std::array<uint64_t, 3> originals{};
            Resolve resolve{};
            DWORD game_thread{};
            bool installed{}, attempted{};
        };
        State state;

        template <size_t Index> UObject* target(UObject* controller)
        {
            try
            {
                if (state.resolve && GetCurrentThreadId() == state.game_thread)
                {
                    if (auto* player = state.resolve(controller))
                    {
                        return player;
                    }
                }
            }
            catch (...)
            {
            }
            return reinterpret_cast<TargetFunction>(state.originals[Index])(controller);
        }
        const Profile& profile(uintptr_t base)
        {
            const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
            if (!base || dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 ||
                dos->e_lfanew > 4096)
            {
                throw std::runtime_error("betrayal_target_image");
            }
            const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
            for (const auto& candidate : profiles)
            {
                if (nt->Signature == IMAGE_NT_SIGNATURE &&
                    nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC &&
                    nt->FileHeader.TimeDateStamp == candidate.timestamp &&
                    nt->OptionalHeader.SizeOfImage == candidate.image_size)
                {
                    return candidate;
                }
            }
            throw std::runtime_error("betrayal_target_unsupported_image");
        }
    } // namespace

    void install(UObject* controller, Resolve resolve)
    {
        if (!is_uobject_valid_fast(controller))
        {
            throw std::runtime_error("betrayal_target_controller_missing");
        }
        const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        const auto& selected = profile(base);
        const auto* table = *reinterpret_cast<const uintptr_t* const*>(controller);
        for (size_t i = 0; i < slots.size(); ++i)
        {
            if (table[slots[i] / sizeof(uintptr_t)] != base + selected.sites[i].rva)
            {
                throw std::runtime_error("betrayal_target_controller_layout");
            }
        }
        if (state.installed)
        {
            return;
        }
        if (state.attempted)
        {
            throw std::runtime_error("betrayal_target_hook_unavailable");
        }
        for (const auto& site : selected.sites)
        {
            DWORD64 image{};
            const auto* unwind = RtlLookupFunctionEntry(base + site.rva, &image, nullptr);
            if (!unwind || image != base || unwind->BeginAddress != site.rva ||
                std::memcmp(reinterpret_cast<void*>(base + site.rva), site.bytes.data(),
                            site.bytes.size()))
            {
                throw std::runtime_error("betrayal_target_signature");
            }
        }
        constexpr std::array callbacks{&target<0>, &target<1>, &target<2>};
        state.attempted = true;
        state.game_thread = GetCurrentThreadId();
        try
        {
            for (size_t i = 0; i < slots.size(); ++i)
            {
                state.hooks[i] = std::make_unique<PLH::x64Detour>(
                    base + selected.sites[i].rva, reinterpret_cast<uint64_t>(callbacks[i]),
                    &state.originals[i]);
                state.hooks[i]->setIsFollowCallOnFnAddress(false);
                state.hooks[i]->setDetourScheme(PLH::x64Detour::VALLOC2);
                if (!state.hooks[i]->hook() || !state.originals[i])
                {
                    throw std::runtime_error("betrayal_target_native_hook_failed");
                }
            }
            state.resolve = resolve;
            state.installed = true;
        }
        catch (...)
        {
            for (auto& hook : state.hooks)
            {
                if (hook)
                {
                    hook->unHook();
                }
            }
            throw;
        }
    }

    UObject* stored_target(UObject* controller)
    {
        if (!state.installed || !is_uobject_valid_fast(controller))
        {
            return nullptr;
        }
        return reinterpret_cast<TargetFunction>(state.originals[0])(controller);
    }
} // namespace kh3ap::betrayal_target_hooks
