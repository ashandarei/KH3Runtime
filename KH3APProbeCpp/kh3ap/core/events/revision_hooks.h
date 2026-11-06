#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <polyhook2/Detour/x64Detour.hpp>
#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <vector>
#include <DynamicOutput/DynamicOutput.hpp>
#include "kh3ap/core/events/revision_stub.h"

namespace kh3ap
{
    // A signature-guarded notification at an instruction boundary. The stub
    // preserves flags/RAX and tail-jumps through the original instructions, so it
    // neither guesses the owning function's ABI nor touches gameplay objects.
    class RuntimeNativeRevisionHooks
    {
        struct ReleaseStub
        {
            auto operator()(void* memory) const noexcept -> void
            {
                VirtualFree(memory, 0, MEM_RELEASE);
            }
        };
        struct Hook
        {
            std::unique_ptr<void, ReleaseStub> stub;
            std::unique_ptr<PLH::x64Detour> detour;
            uint64_t trampoline{};
        };
        struct Storage
        {
            std::atomic<uint64_t> revision{};
            std::vector<std::unique_ptr<Hook>> hooks;
        };
        Storage* state_ = new Storage;

    public:
        RuntimeNativeRevisionHooks() = default;
        RuntimeNativeRevisionHooks(const RuntimeNativeRevisionHooks&) = delete;
        auto operator=(const RuntimeNativeRevisionHooks&) -> RuntimeNativeRevisionHooks& = delete;
        RuntimeNativeRevisionHooks(RuntimeNativeRevisionHooks&&) = delete;
        auto operator=(RuntimeNativeRevisionHooks&&) -> RuntimeNativeRevisionHooks& = delete;
        ~RuntimeNativeRevisionHooks()
        {
            bool retained = false;
            for (auto& hook : state_->hooks)
            {
                if (!hook->detour->unHook())
                {
                    retained = true;
                    continue;
                }
                hook->stub.reset();
            }
            // Failed unhooks must retain both their trampoline and signal storage.
            if (!retained)
            {
                delete state_;
            }
        }
        uint64_t revision() const
        {
            return state_->revision.load(std::memory_order_acquire);
        }
        bool install(uintptr_t address, std::span<const uint8_t> expected)
        {
            if (!address || expected.empty() ||
                std::memcmp(reinterpret_cast<void*>(address), expected.data(), expected.size()))
            {
                return false;
            }
            // Reserve before publishing a native branch: retaining its owner after
            // installation must not depend on another allocation succeeding.
            state_->hooks.reserve(state_->hooks.size() + 1);
            auto hook = std::make_unique<Hook>();
            // pushfq; push rax; mov rax,revision; lock inc qword [rax]; pop rax;
            // popfq; jmp [rip+0]; trampoline address.
            auto code =
                build_native_revision_stub(reinterpret_cast<uint64_t>(&state_->revision), 0);
            hook->stub.reset(VirtualAlloc(nullptr, code.size(), MEM_COMMIT | MEM_RESERVE,
                                          PAGE_EXECUTE_READWRITE));
            if (!hook->stub)
            {
                return false;
            }
            std::memcpy(hook->stub.get(), code.data(), code.size());
            hook->detour = std::make_unique<PLH::x64Detour>(
                address, reinterpret_cast<uint64_t>(hook->stub.get()), &hook->trampoline);
            hook->detour->setIsFollowCallOnFnAddress(false);
            // Only a six-byte indirect jump fits an inline store or leaf setter.
            // Never permit a register-spoiling or fourteen-byte fallback.
            hook->detour->setDetourScheme(PLH::x64Detour::VALLOC2);
            if (!hook->detour->hook())
            {
                RC::Output::send<RC::LogLevel::Error>(L"[KH3APProbeCpp] Native revision hook "
                                                      L"failed address=0x{:X} guarded_bytes={}\n",
                                                      address, expected.size());
                return false;
            }
            std::memcpy(static_cast<uint8_t*>(hook->stub.get()) + code.size() -
                            sizeof(hook->trampoline),
                        &hook->trampoline, sizeof(hook->trampoline));
            DWORD previous{};
            if (!VirtualProtect(hook->stub.get(), code.size(), PAGE_EXECUTE_READ, &previous) ||
                !FlushInstructionCache(GetCurrentProcess(), hook->stub.get(), code.size()))
            {
                if (hook->detour->unHook())
                {
                    hook->stub.reset();
                }
                else
                {
                    // A live detour still owns the stub and revision counter.
                    state_->hooks.push_back(std::move(hook));
                }
                return false;
            }
            state_->hooks.push_back(std::move(hook));
            return true;
        }
    };
} // namespace kh3ap
