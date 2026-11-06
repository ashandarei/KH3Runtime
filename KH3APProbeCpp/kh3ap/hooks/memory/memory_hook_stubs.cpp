#include "kh3ap/hooks/memory/memory_hook_platform.h"
#include "kh3ap/hooks/memory/kh3_memory_hooks.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <cstring>
#include <vector>

namespace kh3ap
{
    namespace
    {
        constexpr uint8_t xmm_register_count = 16;
        constexpr uint32_t xmm_register_width = 16;
        constexpr size_t stub_reserve_capacity = 768;
    } // namespace
    auto Kh3MemoryHooks::emit_u8(std::vector<uint8_t>& code, uint8_t value) -> void
    {
        code.push_back(value);
    }

    auto Kh3MemoryHooks::emit_u32(std::vector<uint8_t>& code, uint32_t value) -> void
    {
        for (int index = 0; index < 4; ++index)
        {
            code.push_back(static_cast<uint8_t>((value >> (index * 8)) & 0xFF));
        }
    }

    auto Kh3MemoryHooks::emit_u64(std::vector<uint8_t>& code, uint64_t value) -> void
    {
        for (int index = 0; index < 8; ++index)
        {
            code.push_back(static_cast<uint8_t>((value >> (index * 8)) & 0xFF));
        }
    }

    auto Kh3MemoryHooks::emit_sub_rsp_u32(std::vector<uint8_t>& code, uint32_t value) -> void
    {
        emit_u8(code, 0x48);
        emit_u8(code, 0x81);
        emit_u8(code, 0xEC);
        emit_u32(code, value);
    }

    auto Kh3MemoryHooks::emit_add_rsp_u32(std::vector<uint8_t>& code, uint32_t value) -> void
    {
        emit_u8(code, 0x48);
        emit_u8(code, 0x81);
        emit_u8(code, 0xC4);
        emit_u32(code, value);
    }

    auto Kh3MemoryHooks::emit_movdqu_m128_rsp_disp_xmm(std::vector<uint8_t>& code, uint32_t disp,
                                                       uint8_t xmm_reg) -> void
    {
        emit_u8(code, 0xF3);
        if (xmm_reg >= 8)
        {
            emit_u8(code, 0x44);
        }
        emit_u8(code, 0x0F);
        emit_u8(code, 0x7F);
        emit_u8(code, static_cast<uint8_t>(0x84 | ((xmm_reg & 7) << 3)));
        emit_u8(code, 0x24);
        emit_u32(code, disp);
    }

    auto Kh3MemoryHooks::emit_movdqu_xmm_m128_rsp_disp(std::vector<uint8_t>& code, uint8_t xmm_reg,
                                                       uint32_t disp) -> void
    {
        emit_u8(code, 0xF3);
        if (xmm_reg >= 8)
        {
            emit_u8(code, 0x44);
        }
        emit_u8(code, 0x0F);
        emit_u8(code, 0x6F);
        emit_u8(code, static_cast<uint8_t>(0x84 | ((xmm_reg & 7) << 3)));
        emit_u8(code, 0x24);
        emit_u32(code, disp);
    }

    auto Kh3MemoryHooks::emit_save_xmm_state(std::vector<uint8_t>& code) -> void
    {
        constexpr uint32_t xmm_save_size = xmm_register_count * xmm_register_width;
        emit_sub_rsp_u32(code, xmm_save_size);
        for (uint8_t xmm_reg = 0; xmm_reg < xmm_register_count; ++xmm_reg)
        {
            emit_movdqu_m128_rsp_disp_xmm(code, static_cast<uint32_t>(xmm_reg) * xmm_register_width,
                                          xmm_reg);
        }
    }

    auto Kh3MemoryHooks::emit_restore_xmm_state(std::vector<uint8_t>& code) -> void
    {
        constexpr uint32_t xmm_save_size = xmm_register_count * xmm_register_width;
        for (uint8_t xmm_reg = 0; xmm_reg < xmm_register_count; ++xmm_reg)
        {
            emit_movdqu_xmm_m128_rsp_disp(code, xmm_reg,
                                          static_cast<uint32_t>(xmm_reg) * xmm_register_width);
        }
        emit_add_rsp_u32(code, xmm_save_size);
    }

    auto Kh3MemoryHooks::allocate_vanilla_level_write_stub() -> bool
    {
        if (state_.vanilla_level_write_stub)
        {
            return true;
        }

        std::vector<uint8_t> code{};
        code.reserve(stub_reserve_capacity);
        emit_u8(code, 0x9C);
        for (uint8_t opcode : {0x50, 0x51, 0x52, 0x53, 0x55, 0x56, 0x57})
        {
            emit_u8(code, opcode);
        }
        for (uint8_t opcode : {0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57})
        {
            emit_u8(code, 0x41);
            emit_u8(code, opcode);
        }
        emit_save_xmm_state(code);

        emit_u8(code, 0x48);
        emit_u8(code, 0x83);
        emit_u8(code, 0xEC);
        emit_u8(code, 0x30);
        emit_u8(code, 0x48);
        emit_u8(code, 0x89);
        emit_u8(code, 0x04);
        emit_u8(code, 0x24);
        emit_u8(code, 0x48);
        emit_u8(code, 0x8B);
        emit_u8(code, 0xC4);
        emit_u8(code, 0x48);
        emit_u8(code, 0x83);
        emit_u8(code, 0xE4);
        emit_u8(code, 0xF0);
        emit_u8(code, 0x48);
        emit_u8(code, 0x83);
        emit_u8(code, 0xEC);
        emit_u8(code, 0x30);
        emit_u8(code, 0x48);
        emit_u8(code, 0x89);
        emit_u8(code, 0x44);
        emit_u8(code, 0x24);
        emit_u8(code, 0x20);
        emit_u8(code, 0x44);
        emit_u8(code, 0x8B);
        emit_u8(code, 0x43);
        emit_u8(code, 0x40);
        // mov r9, [rbx+0x28] -- capture the level owner while rbx is known-live.
        emit_u8(code, 0x4C);
        emit_u8(code, 0x8B);
        emit_u8(code, 0x4B);
        emit_u8(code, 0x28);
        emit_u8(code, 0x48);
        emit_u8(code, 0x8B);
        emit_u8(code, 0xCB);
        emit_u8(code, 0x48);
        emit_u8(code, 0xB8);
        emit_u64(code,
                 reinterpret_cast<uint64_t>(&Kh3MemoryHooks::vanilla_level_write_polyhook_logger));
        emit_u8(code, 0xFF);
        emit_u8(code, 0xD0);
        emit_u8(code, 0x48);
        emit_u8(code, 0x8B);
        emit_u8(code, 0x64);
        emit_u8(code, 0x24);
        emit_u8(code, 0x20);
        emit_u8(code, 0x48);
        emit_u8(code, 0x8B);
        emit_u8(code, 0x04);
        emit_u8(code, 0x24);
        emit_u8(code, 0x48);
        emit_u8(code, 0x83);
        emit_u8(code, 0xC4);
        emit_u8(code, 0x30);
        emit_restore_xmm_state(code);

        for (uint8_t opcode : {0x5F, 0x5E, 0x5D, 0x5C, 0x5B, 0x5A, 0x59, 0x58})
        {
            emit_u8(code, 0x41);
            emit_u8(code, opcode);
        }
        for (uint8_t opcode : {0x5F, 0x5E, 0x5D, 0x5B, 0x5A, 0x59, 0x58})
        {
            emit_u8(code, opcode);
        }
        emit_u8(code, 0x9D);

        emit_u8(code, 0xFF);
        emit_u8(code, 0x25);
        emit_u32(code, 0);
        const size_t trampoline_slot_offset = code.size();
        emit_u64(code, 0);

        void* const stub =
            VirtualAlloc(nullptr, code.size(), MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (!stub)
        {
            return false;
        }
        std::memcpy(stub, code.data(), code.size());
        state_.vanilla_level_write_stub = stub;
        state_.vanilla_level_write_stub_size = code.size();
        *reinterpret_cast<uint64_t*>(reinterpret_cast<std::byte*>(stub) + trampoline_slot_offset) =
            state_.vanilla_level_write_trampoline;
        FlushInstructionCache(GetCurrentProcess(), stub, code.size());
        return true;
    }

    auto Kh3MemoryHooks::update_vanilla_level_write_stub_trampoline() -> bool
    {
        if (!state_.vanilla_level_write_stub || state_.vanilla_level_write_stub_size < 8)
        {
            return false;
        }
        auto* const slot = reinterpret_cast<uint64_t*>(
            reinterpret_cast<std::byte*>(state_.vanilla_level_write_stub) +
            state_.vanilla_level_write_stub_size - sizeof(uint64_t));
        *slot = state_.vanilla_level_write_trampoline;
        DWORD previous{};
        return VirtualProtect(state_.vanilla_level_write_stub, state_.vanilla_level_write_stub_size,
                              PAGE_EXECUTE_READ, &previous) &&
               FlushInstructionCache(GetCurrentProcess(), state_.vanilla_level_write_stub,
                                     state_.vanilla_level_write_stub_size);
    }

    auto Kh3MemoryHooks::allocate_current_state_write_stub() -> bool
    {
        if (state_.current_state_write_stub)
        {
            return true;
        }

        std::vector<uint8_t> code{};
        code.reserve(stub_reserve_capacity);
        emit_u8(code, 0x9C);
        for (uint8_t opcode : {0x50, 0x51, 0x52, 0x53, 0x55, 0x56, 0x57})
        {
            emit_u8(code, opcode);
        }
        for (uint8_t opcode : {0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57})
        {
            emit_u8(code, 0x41);
            emit_u8(code, opcode);
        }
        emit_save_xmm_state(code);

        emit_u8(code, 0x48);
        emit_u8(code, 0x83);
        emit_u8(code, 0xEC);
        emit_u8(code, 0x30);
        emit_u8(code, 0x48);
        emit_u8(code, 0x89);
        emit_u8(code, 0x04);
        emit_u8(code, 0x24);
        emit_u8(code, 0x48);
        emit_u8(code, 0x8B);
        emit_u8(code, 0xC4);
        emit_u8(code, 0x48);
        emit_u8(code, 0x83);
        emit_u8(code, 0xE4);
        emit_u8(code, 0xF0);
        emit_u8(code, 0x48);
        emit_u8(code, 0x83);
        emit_u8(code, 0xEC);
        emit_u8(code, 0x30);
        emit_u8(code, 0x48);
        emit_u8(code, 0x89);
        emit_u8(code, 0x44);
        emit_u8(code, 0x24);
        emit_u8(code, 0x20);
        emit_u8(code, 0x48);
        emit_u8(code, 0x8B);
        emit_u8(code, 0xD3);
        emit_u8(code, 0x48);
        emit_u8(code, 0x8B);
        emit_u8(code, 0xCE);
        emit_u8(code, 0x48);
        emit_u8(code, 0xB8);
        emit_u64(code,
                 reinterpret_cast<uint64_t>(&Kh3MemoryHooks::current_state_write_polyhook_logger));
        emit_u8(code, 0xFF);
        emit_u8(code, 0xD0);
        emit_u8(code, 0x48);
        emit_u8(code, 0x8B);
        emit_u8(code, 0x64);
        emit_u8(code, 0x24);
        emit_u8(code, 0x20);
        emit_u8(code, 0x48);
        emit_u8(code, 0x89);
        emit_u8(code, 0x84);
        emit_u8(code, 0x24);
        emit_u32(code, 0x188);
        emit_u8(code, 0x48);
        emit_u8(code, 0x8B);
        emit_u8(code, 0x04);
        emit_u8(code, 0x24);
        emit_u8(code, 0x48);
        emit_u8(code, 0x83);
        emit_u8(code, 0xC4);
        emit_u8(code, 0x30);
        emit_restore_xmm_state(code);

        for (uint8_t opcode : {0x5F, 0x5E, 0x5D, 0x5C, 0x5B, 0x5A, 0x59, 0x58})
        {
            emit_u8(code, 0x41);
            emit_u8(code, opcode);
        }
        for (uint8_t opcode : {0x5F, 0x5E, 0x5D, 0x5B, 0x5A, 0x59, 0x58})
        {
            emit_u8(code, opcode);
        }
        emit_u8(code, 0x9D);

        emit_u8(code, 0xFF);
        emit_u8(code, 0x25);
        emit_u32(code, 0);
        const size_t trampoline_slot_offset = code.size();
        emit_u64(code, 0);

        void* const stub =
            VirtualAlloc(nullptr, code.size(), MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (!stub)
        {
            return false;
        }
        std::memcpy(stub, code.data(), code.size());
        state_.current_state_write_stub = stub;
        state_.current_state_write_stub_size = code.size();
        *reinterpret_cast<uint64_t*>(reinterpret_cast<std::byte*>(stub) + trampoline_slot_offset) =
            state_.current_state_write_trampoline;
        FlushInstructionCache(GetCurrentProcess(), stub, code.size());
        return true;
    }

    auto Kh3MemoryHooks::update_current_state_write_stub_trampoline() -> bool
    {
        if (!state_.current_state_write_stub || state_.current_state_write_stub_size < 8)
        {
            return false;
        }
        auto* const slot = reinterpret_cast<uint64_t*>(
            reinterpret_cast<std::byte*>(state_.current_state_write_stub) +
            state_.current_state_write_stub_size - sizeof(uint64_t));
        *slot = state_.current_state_write_trampoline;
        DWORD previous{};
        return VirtualProtect(state_.current_state_write_stub, state_.current_state_write_stub_size,
                              PAGE_EXECUTE_READ, &previous) &&
               FlushInstructionCache(GetCurrentProcess(), state_.current_state_write_stub,
                                     state_.current_state_write_stub_size);
    }
} // namespace kh3ap
