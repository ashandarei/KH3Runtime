#include "kh3ap/hooks/hook_integrity_diagnostic.h"
#include "kh3ap/hooks/frame_wait/kh3_frame_wait.h"
#include "kh3ap/core/performance_build.h"

#ifdef S
#pragma push_macro("S")
#undef S
#define KH3AP_RESTORE_S_MACRO_AFTER_POLYHOOK
#endif
#include <polyhook2/Detour/x64Detour.hpp>
#ifdef KH3AP_RESTORE_S_MACRO_AFTER_POLYHOOK
#pragma pop_macro("S")
#undef KH3AP_RESTORE_S_MACRO_AFTER_POLYHOOK
#endif

#include "kh3ap/bridge/transport/bridge_paths.h"
#include "kh3ap/bridge/serialization/json_fields.h"
#include "kh3ap/bridge/transport/bridge_io_helpers.h"
#include "kh3ap/bridge/utilities.h"

#include <UnrealDef.hpp>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace kh3ap
{
    namespace
    {
        constexpr size_t k_xmm_register_count = 16;
        constexpr size_t k_volatile_xmm_register_count = 6;

        using XmmBytes = std::array<uint8_t, 16>;

        enum class GprIndex : size_t
        {
            Rax,
            Rcx,
            Rdx,
            Rbx,
            Rbp,
            Rsi,
            Rdi,
            R8,
            R9,
            R10,
            R11,
            R12,
            R13,
            R14,
            R15,
            Count,
        };

        constexpr size_t k_gpr_count = static_cast<size_t>(GprIndex::Count);

        constexpr std::array<const char*, k_gpr_count> k_gpr_names{{
                "rax",
                "rcx",
                "rdx",
                "rbx",
                "rbp",
                "rsi",
                "rdi",
                "r8",
                "r9",
                "r10",
                "r11",
                "r12",
                "r13",
                "r14",
                "r15",
        }};

        constexpr std::array<uint8_t, k_gpr_count> k_gpr_registers{{
                0,
                1,
                2,
                3,
                5,
                6,
                7,
                8,
                9,
                10,
                11,
                12,
                13,
                14,
                15,
        }};

        constexpr std::array<uint64_t, k_gpr_count> k_gpr_sentinels{{
                0x1111222233334444ULL,
                0x2222333344445555ULL,
                0,
                0,
                0x5555666677778888ULL,
                0x6666777788889999ULL,
                0x777788889999AAAAULL,
                0x88889999AAAABBBBULL,
                0x9999AAAABBBBCCCCULL,
                0xAAAABBBBCCCCDDDDULL,
                0xBBBBCCCCDDDDEEEEULL,
                0xCCCCDDDDEEEEFFFFULL,
                0xDDDDEEEEFFFF1111ULL,
                0xEEEEFFFF11112222ULL,
                0xFFFF111122223333ULL,
        }};

        struct alignas(16) HookIntegrityFrame
        {
            std::array<uint8_t, 0x40> prefix{};
            uint32_t level_slot{0};
            uint32_t new_level{0};
            uint64_t flags_after{0};
            std::array<uint64_t, k_gpr_count> gpr_after{};
            std::array<XmmBytes, k_xmm_register_count> xmm_expected{};
            std::array<XmmBytes, k_volatile_xmm_register_count> xmm_clobber{};
            std::array<XmmBytes, k_xmm_register_count> xmm_after{};
        };

        static_assert(offsetof(HookIntegrityFrame, level_slot) == 0x40);

        struct ExecutableBlock
        {
            void* memory{};
            size_t size{};

            ExecutableBlock() = default;
            ExecutableBlock(void* block_memory, size_t block_size)
                    : memory(block_memory), size(block_size)
            {
            }
            ExecutableBlock(const ExecutableBlock&) = delete;
            auto operator=(const ExecutableBlock&) -> ExecutableBlock& = delete;

            ExecutableBlock(ExecutableBlock&& other) noexcept
                    : memory(other.memory), size(other.size)
            {
                other.memory = nullptr;
                other.size = 0;
            }

            auto operator=(ExecutableBlock&& other) noexcept -> ExecutableBlock&
            {
                if (this != &other)
                {
                    release();
                    memory = other.memory;
                    size = other.size;
                    other.memory = nullptr;
                    other.size = 0;
                }
                return *this;
            }

            ~ExecutableBlock()
            {
                release();
            }

            auto release() -> void
            {
                if (memory)
                {
                    VirtualFree(memory, 0, MEM_RELEASE);
                    memory = nullptr;
                    size = 0;
                }
            }
        };

        struct GeneratedTarget
        {
            ExecutableBlock block{};
            uint64_t hook_address{};
        };

        struct GeneratedStub
        {
            ExecutableBlock block{};
            size_t trampoline_slot_offset{};
        };

        struct HookScenarioResult
        {
            std::wstring name{};
            bool hook_installed{false};
            bool completed{false};
            uint32_t calls{0};
            bool level_pass{false};
            bool flags_pass{false};
            bool gpr_pass{false};
            bool volatile_xmm_pass{false};
            bool nonvolatile_xmm_pass{false};
            std::wstring first_mismatch{};
            std::wstring error{};
        };

        auto emit_u8(std::vector<uint8_t>& code, uint8_t value) -> void
        {
            code.push_back(value);
        }

        auto emit_u32(std::vector<uint8_t>& code, uint32_t value) -> void
        {
            for (int shift = 0; shift < 32; shift += 8)
            {
                code.push_back(static_cast<uint8_t>((value >> shift) & 0xFF));
            }
        }

        auto emit_u64(std::vector<uint8_t>& code, uint64_t value) -> void
        {
            for (int shift = 0; shift < 64; shift += 8)
            {
                code.push_back(static_cast<uint8_t>((value >> shift) & 0xFF));
            }
        }

        auto emit_rex_if_needed(std::vector<uint8_t>& code, bool w, bool r, bool x, bool b) -> void
        {
            const uint8_t rex = static_cast<uint8_t>(
                    0x40 |
                    (w ? 0x08 : 0) |
                    (r ? 0x04 : 0) |
                    (x ? 0x02 : 0) |
                    (b ? 0x01 : 0));
            if (rex != 0x40)
            {
                emit_u8(code, rex);
            }
        }

        auto emit_mov_r64_imm64(std::vector<uint8_t>& code, uint8_t reg, uint64_t value) -> void
        {
            emit_rex_if_needed(code, true, false, false, reg >= 8);
            emit_u8(code, static_cast<uint8_t>(0xB8 + (reg & 7)));
            emit_u64(code, value);
        }

        auto emit_mov_m64_base_disp_r64(std::vector<uint8_t>& code, uint8_t base_reg, int32_t disp, uint8_t source_reg) -> void
        {
            emit_rex_if_needed(code, true, source_reg >= 8, false, base_reg >= 8);
            emit_u8(code, 0x89);
            emit_u8(code, static_cast<uint8_t>(0x80 | ((source_reg & 7) << 3) | (base_reg & 7)));
            emit_u32(code, static_cast<uint32_t>(disp));
        }

        auto emit_mov_edx_m32_base_disp(std::vector<uint8_t>& code, uint8_t base_reg, int32_t disp) -> void
        {
            emit_rex_if_needed(code, false, false, false, base_reg >= 8);
            emit_u8(code, 0x8B);
            emit_u8(code, static_cast<uint8_t>(0x80 | (2 << 3) | (base_reg & 7)));
            emit_u32(code, static_cast<uint32_t>(disp));
        }

        auto emit_mov_eax_m32_base_disp(std::vector<uint8_t>& code, uint8_t base_reg, int32_t disp) -> void
        {
            emit_rex_if_needed(code, false, false, false, base_reg >= 8);
            emit_u8(code, 0x8B);
            emit_u8(code, static_cast<uint8_t>(0x80 | (0 << 3) | (base_reg & 7)));
            emit_u32(code, static_cast<uint32_t>(disp));
        }

        auto emit_movdqu_xmm_m128_base_disp(std::vector<uint8_t>& code, uint8_t xmm_reg, uint8_t base_reg, int32_t disp) -> void
        {
            emit_u8(code, 0xF3);
            emit_rex_if_needed(code, false, xmm_reg >= 8, false, base_reg >= 8);
            emit_u8(code, 0x0F);
            emit_u8(code, 0x6F);
            emit_u8(code, static_cast<uint8_t>(0x80 | ((xmm_reg & 7) << 3) | (base_reg & 7)));
            emit_u32(code, static_cast<uint32_t>(disp));
        }

        auto emit_movdqu_m128_base_disp_xmm(std::vector<uint8_t>& code, uint8_t base_reg, int32_t disp, uint8_t xmm_reg) -> void
        {
            emit_u8(code, 0xF3);
            emit_rex_if_needed(code, false, xmm_reg >= 8, false, base_reg >= 8);
            emit_u8(code, 0x0F);
            emit_u8(code, 0x7F);
            emit_u8(code, static_cast<uint8_t>(0x80 | ((xmm_reg & 7) << 3) | (base_reg & 7)));
            emit_u32(code, static_cast<uint32_t>(disp));
        }

        auto emit_movdqu_m128_rsp_disp_xmm(std::vector<uint8_t>& code, int32_t disp, uint8_t xmm_reg) -> void
        {
            emit_u8(code, 0xF3);
            emit_rex_if_needed(code, false, xmm_reg >= 8, false, false);
            emit_u8(code, 0x0F);
            emit_u8(code, 0x7F);
            emit_u8(code, static_cast<uint8_t>(0x84 | ((xmm_reg & 7) << 3)));
            emit_u8(code, 0x24);
            emit_u32(code, static_cast<uint32_t>(disp));
        }

        auto emit_movdqu_xmm_m128_rsp_disp(std::vector<uint8_t>& code, uint8_t xmm_reg, int32_t disp) -> void
        {
            emit_u8(code, 0xF3);
            emit_rex_if_needed(code, false, xmm_reg >= 8, false, false);
            emit_u8(code, 0x0F);
            emit_u8(code, 0x6F);
            emit_u8(code, static_cast<uint8_t>(0x84 | ((xmm_reg & 7) << 3)));
            emit_u8(code, 0x24);
            emit_u32(code, static_cast<uint32_t>(disp));
        }

        auto emit_push_r64(std::vector<uint8_t>& code, uint8_t reg) -> void
        {
            if (reg >= 8)
            {
                emit_u8(code, 0x41);
            }
            emit_u8(code, static_cast<uint8_t>(0x50 + (reg & 7)));
        }

        auto emit_pop_r64(std::vector<uint8_t>& code, uint8_t reg) -> void
        {
            if (reg >= 8)
            {
                emit_u8(code, 0x41);
            }
            emit_u8(code, static_cast<uint8_t>(0x58 + (reg & 7)));
        }

        auto allocate_executable_block(const std::vector<uint8_t>& code) -> ExecutableBlock
        {
            if (code.empty())
            {
                return {};
            }

            void* const memory = VirtualAlloc(nullptr, code.size(), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            if (!memory)
            {
                return {};
            }

            std::memcpy(memory, code.data(), code.size());
            DWORD previous{};
            if (!VirtualProtect(memory, code.size(), PAGE_EXECUTE_READ, &previous) ||
                !FlushInstructionCache(GetCurrentProcess(), memory, code.size()))
            {
                VirtualFree(memory, 0, MEM_RELEASE);
                return {};
            }
            return {memory, code.size()};
        }

        auto fill_xmm_patterns(HookIntegrityFrame& frame) -> void
        {
            for (size_t reg = 0; reg < frame.xmm_expected.size(); ++reg)
            {
                for (size_t byte = 0; byte < frame.xmm_expected[reg].size(); ++byte)
                {
                    frame.xmm_expected[reg][byte] = static_cast<uint8_t>(0x31 + (reg * 17) + (byte * 7));
                }
            }

            for (size_t reg = 0; reg < frame.xmm_clobber.size(); ++reg)
            {
                for (size_t byte = 0; byte < frame.xmm_clobber[reg].size(); ++byte)
                {
                    frame.xmm_clobber[reg][byte] = static_cast<uint8_t>(0xA0 + (reg * 19) + (byte * 5));
                }
            }
        }

        auto reset_observed_outputs(HookIntegrityFrame& frame) -> void
        {
            frame.flags_after = 0;
            frame.gpr_after.fill(0);
            for (auto& value : frame.xmm_after)
            {
                value.fill(0);
            }
            frame.level_slot = 11;
            frame.new_level = 23;
        }

        auto make_generated_logger() -> ExecutableBlock
        {
            std::vector<uint8_t> code{};
            code.reserve(160);

            for (uint8_t xmm = 0; xmm < k_volatile_xmm_register_count; ++xmm)
            {
                const auto disp = static_cast<int32_t>(
                        offsetof(HookIntegrityFrame, xmm_clobber) + (static_cast<size_t>(xmm) * sizeof(XmmBytes)));
                emit_movdqu_xmm_m128_base_disp(code, xmm, 1, disp);
            }

            emit_mov_r64_imm64(code, 0, 0x0BADF00D11112222ULL);
            emit_mov_r64_imm64(code, 1, 0x0BADF00D22223333ULL);
            emit_mov_r64_imm64(code, 2, 0x0BADF00D33334444ULL);
            emit_mov_r64_imm64(code, 8, 0x0BADF00D44445555ULL);
            emit_mov_r64_imm64(code, 9, 0x0BADF00D55556666ULL);
            emit_mov_r64_imm64(code, 10, 0x0BADF00D66667777ULL);
            emit_mov_r64_imm64(code, 11, 0x0BADF00D77778888ULL);
            emit_u8(code, 0xC3);

            return allocate_executable_block(code);
        }

        auto make_generated_target() -> GeneratedTarget
        {
            std::vector<uint8_t> code{};
            code.reserve(512);

            for (uint8_t reg : {3, 5, 6, 7, 12, 13, 14, 15})
            {
                emit_push_r64(code, reg);
            }
            emit_u8(code, 0x48);
            emit_u8(code, 0x81);
            emit_u8(code, 0xEC);
            emit_u32(code, static_cast<uint32_t>((k_xmm_register_count - k_volatile_xmm_register_count) * sizeof(XmmBytes)));
            for (uint8_t xmm = k_volatile_xmm_register_count; xmm < k_xmm_register_count; ++xmm)
            {
                emit_movdqu_m128_rsp_disp_xmm(
                        code,
                        static_cast<int32_t>((xmm - k_volatile_xmm_register_count) * sizeof(XmmBytes)),
                        xmm);
            }

            emit_u8(code, 0x48);
            emit_u8(code, 0x8B);
            emit_u8(code, 0xD9);

            for (uint8_t xmm = 0; xmm < k_xmm_register_count; ++xmm)
            {
                const auto disp = static_cast<int32_t>(
                        offsetof(HookIntegrityFrame, xmm_expected) + (static_cast<size_t>(xmm) * sizeof(XmmBytes)));
                emit_movdqu_xmm_m128_base_disp(code, xmm, 3, disp);
            }

            for (size_t index = 0; index < k_gpr_count; ++index)
            {
                const auto gpr_index = static_cast<GprIndex>(index);
                if (gpr_index == GprIndex::Rdx || gpr_index == GprIndex::Rbx)
                {
                    continue;
                }
                emit_mov_r64_imm64(code, k_gpr_registers[index], k_gpr_sentinels[index]);
            }
            emit_mov_edx_m32_base_disp(code, 3, static_cast<int32_t>(offsetof(HookIntegrityFrame, new_level)));

            emit_u8(code, 0x48);
            emit_u8(code, 0x39);
            emit_u8(code, 0xC0);

            const size_t hook_offset = code.size();
            emit_u8(code, 0x89);
            emit_u8(code, 0x53);
            emit_u8(code, 0x40);

            emit_u8(code, 0x9C);
            emit_u8(code, 0x8F);
            emit_u8(code, 0x83);
            emit_u32(code, static_cast<uint32_t>(offsetof(HookIntegrityFrame, flags_after)));

            for (size_t index = 0; index < k_gpr_count; ++index)
            {
                const auto disp = static_cast<int32_t>(
                        offsetof(HookIntegrityFrame, gpr_after) + (index * sizeof(uint64_t)));
                emit_mov_m64_base_disp_r64(code, 3, disp, k_gpr_registers[index]);
            }

            for (uint8_t xmm = 0; xmm < k_xmm_register_count; ++xmm)
            {
                const auto disp = static_cast<int32_t>(
                        offsetof(HookIntegrityFrame, xmm_after) + (static_cast<size_t>(xmm) * sizeof(XmmBytes)));
                emit_movdqu_m128_base_disp_xmm(code, 3, disp, xmm);
            }

            emit_mov_eax_m32_base_disp(code, 3, static_cast<int32_t>(offsetof(HookIntegrityFrame, level_slot)));

            for (uint8_t xmm = k_volatile_xmm_register_count; xmm < k_xmm_register_count; ++xmm)
            {
                emit_movdqu_xmm_m128_rsp_disp(
                        code,
                        xmm,
                        static_cast<int32_t>((xmm - k_volatile_xmm_register_count) * sizeof(XmmBytes)));
            }
            emit_u8(code, 0x48);
            emit_u8(code, 0x81);
            emit_u8(code, 0xC4);
            emit_u32(code, static_cast<uint32_t>((k_xmm_register_count - k_volatile_xmm_register_count) * sizeof(XmmBytes)));

            for (uint8_t reg : {15, 14, 13, 12, 7, 6, 5, 3})
            {
                emit_pop_r64(code, reg);
            }
            emit_u8(code, 0xC3);

            auto block = allocate_executable_block(code);
            const uint64_t hook_address = block.memory
                    ? reinterpret_cast<uint64_t>(block.memory) + hook_offset
                    : 0;
            return {std::move(block), hook_address};
        }

        auto make_vanilla_level_write_style_stub(uint64_t logger_address, bool preserve_xmm) -> GeneratedStub
        {
            std::vector<uint8_t> code{};
            code.reserve(preserve_xmm ? 640 : 192);

            emit_u8(code, 0x9C);
            for (uint8_t reg : {0, 1, 2, 3, 5, 6, 7})
            {
                emit_push_r64(code, reg);
            }
            for (uint8_t reg : {8, 9, 10, 11, 12, 13, 14, 15})
            {
                emit_push_r64(code, reg);
            }

            if (preserve_xmm)
            {
                emit_u8(code, 0x48);
                emit_u8(code, 0x81);
                emit_u8(code, 0xEC);
                emit_u32(code, static_cast<uint32_t>(k_xmm_register_count * sizeof(XmmBytes)));
                for (uint8_t xmm = 0; xmm < k_xmm_register_count; ++xmm)
                {
                    emit_movdqu_m128_rsp_disp_xmm(code, static_cast<int32_t>(xmm * sizeof(XmmBytes)), xmm);
                }
            }

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
            emit_u8(code, 0x48);
            emit_u8(code, 0x8B);
            emit_u8(code, 0xCB);
            emit_u8(code, 0x48);
            emit_u8(code, 0xB8);
            emit_u64(code, logger_address);
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

            if (preserve_xmm)
            {
                for (uint8_t xmm = 0; xmm < k_xmm_register_count; ++xmm)
                {
                    emit_movdqu_xmm_m128_rsp_disp(code, xmm, static_cast<int32_t>(xmm * sizeof(XmmBytes)));
                }
                emit_u8(code, 0x48);
                emit_u8(code, 0x81);
                emit_u8(code, 0xC4);
                emit_u32(code, static_cast<uint32_t>(k_xmm_register_count * sizeof(XmmBytes)));
            }

            for (uint8_t reg : {15, 14, 13, 12, 11, 10, 9, 8})
            {
                emit_pop_r64(code, reg);
            }
            for (uint8_t reg : {7, 6, 5, 3, 2, 1, 0})
            {
                emit_pop_r64(code, reg);
            }
            emit_u8(code, 0x9D);

            emit_u8(code, 0xFF);
            emit_u8(code, 0x25);
            emit_u32(code, 0);
            const size_t trampoline_slot_offset = code.size();
            emit_u64(code, 0);

            auto block = allocate_executable_block(code);
            return {std::move(block), trampoline_slot_offset};
        }

        auto bytes_equal(const XmmBytes& lhs, const XmmBytes& rhs) -> bool
        {
            return std::memcmp(lhs.data(), rhs.data(), lhs.size()) == 0;
        }

        auto expected_gpr_value(GprIndex index, const HookIntegrityFrame& frame) -> uint64_t
        {
            if (index == GprIndex::Rdx)
            {
                return frame.new_level;
            }
            if (index == GprIndex::Rbx)
            {
                return reinterpret_cast<uint64_t>(&frame);
            }
            return k_gpr_sentinels[static_cast<size_t>(index)];
        }

        auto validate_frame(const HookIntegrityFrame& frame, HookScenarioResult& result) -> void
        {
            result.level_pass = frame.level_slot == frame.new_level;
            if (!result.level_pass && result.first_mismatch.empty())
            {
                result.first_mismatch = STR("level_slot_not_written");
            }

            constexpr uint64_t zf_mask = 1ULL << 6;
            constexpr uint64_t cf_mask = 1ULL << 0;
            result.flags_pass = (frame.flags_after & zf_mask) != 0 && (frame.flags_after & cf_mask) == 0;
            if (!result.flags_pass && result.first_mismatch.empty())
            {
                result.first_mismatch = STR("flags_changed");
            }

            result.gpr_pass = true;
            for (size_t index = 0; index < k_gpr_count; ++index)
            {
                const auto gpr_index = static_cast<GprIndex>(index);
                if (frame.gpr_after[index] != expected_gpr_value(gpr_index, frame))
                {
                    result.gpr_pass = false;
                    if (result.first_mismatch.empty())
                    {
                        result.first_mismatch = std::wstring{STR("gpr_changed:")} +
                                std::wstring{k_gpr_names[index], k_gpr_names[index] + std::strlen(k_gpr_names[index])};
                    }
                    break;
                }
            }

            result.volatile_xmm_pass = true;
            for (size_t index = 0; index < k_volatile_xmm_register_count; ++index)
            {
                if (!bytes_equal(frame.xmm_after[index], frame.xmm_expected[index]))
                {
                    result.volatile_xmm_pass = false;
                    if (result.first_mismatch.empty())
                    {
                        result.first_mismatch = std::wstring{STR("volatile_xmm_changed:xmm")} + std::to_wstring(index);
                    }
                    break;
                }
            }

            result.nonvolatile_xmm_pass = true;
            for (size_t index = k_volatile_xmm_register_count; index < k_xmm_register_count; ++index)
            {
                if (!bytes_equal(frame.xmm_after[index], frame.xmm_expected[index]))
                {
                    result.nonvolatile_xmm_pass = false;
                    if (result.first_mismatch.empty())
                    {
                        result.first_mismatch = std::wstring{STR("nonvolatile_xmm_changed:xmm")} + std::to_wstring(index);
                    }
                    break;
                }
            }
        }

        auto run_hook_scenario(std::wstring_view name, bool preserve_xmm, uint32_t iterations) -> HookScenarioResult
        {
            HookScenarioResult result{};
            result.name = std::wstring{name};

            auto logger = make_generated_logger();
            if (!logger.memory)
            {
                result.error = STR("logger_allocation_failed");
                return result;
            }

            auto target = make_generated_target();
            if (!target.block.memory || target.hook_address == 0)
            {
                result.error = STR("target_allocation_failed");
                return result;
            }

            auto stub = make_vanilla_level_write_style_stub(reinterpret_cast<uint64_t>(logger.memory), preserve_xmm);
            if (!stub.block.memory)
            {
                result.error = STR("stub_allocation_failed");
                return result;
            }

            uint64_t trampoline = 0;
            auto detour = std::make_unique<PLH::x64Detour>(
                    target.hook_address,
                    reinterpret_cast<uint64_t>(stub.block.memory),
                    &trampoline);
            result.hook_installed = detour->hook();
            if (!result.hook_installed)
            {
                result.error = STR("polyhook_install_failed");
                return result;
            }

            *reinterpret_cast<uint64_t*>(
                    reinterpret_cast<std::byte*>(stub.block.memory) + stub.trampoline_slot_offset) = trampoline;
            FlushInstructionCache(GetCurrentProcess(), stub.block.memory, stub.block.size);

            HookIntegrityFrame frame{};
            fill_xmm_patterns(frame);

            using TargetFn = uint32_t (*)(HookIntegrityFrame*);
            const auto target_fn = reinterpret_cast<TargetFn>(target.block.memory);
            for (uint32_t index = 0; index < iterations; ++index)
            {
                reset_observed_outputs(frame);
                const uint32_t returned_level = target_fn(&frame);
                ++result.calls;
                if (returned_level != frame.new_level)
                {
                    result.error = STR("target_returned_unexpected_level");
                    break;
                }

                HookScenarioResult iteration_result{};
                validate_frame(frame, iteration_result);
                if (!iteration_result.level_pass ||
                    !iteration_result.flags_pass ||
                    !iteration_result.gpr_pass ||
                    !iteration_result.volatile_xmm_pass ||
                    !iteration_result.nonvolatile_xmm_pass)
                {
                    result.level_pass = iteration_result.level_pass;
                    result.flags_pass = iteration_result.flags_pass;
                    result.gpr_pass = iteration_result.gpr_pass;
                    result.volatile_xmm_pass = iteration_result.volatile_xmm_pass;
                    result.nonvolatile_xmm_pass = iteration_result.nonvolatile_xmm_pass;
                    result.first_mismatch = iteration_result.first_mismatch;
                    result.completed = true;
                    detour->unHook();
                    return result;
                }
            }

            validate_frame(frame, result);
            result.completed = result.error.empty();
            detour->unHook();
            return result;
        }

        auto json_bool(bool value) -> const char*
        {
            return value ? "true" : "false";
        }

        auto serialize_scenario(const HookScenarioResult& result) -> std::string
        {
            std::ostringstream json;
            json << "{";
            json << "\"name\":\"" << json_escape(result.name) << "\"";
            json << ",\"hook_installed\":" << json_bool(result.hook_installed);
            json << ",\"completed\":" << json_bool(result.completed);
            json << ",\"calls\":" << result.calls;
            json << ",\"level_pass\":" << json_bool(result.level_pass);
            json << ",\"flags_pass\":" << json_bool(result.flags_pass);
            json << ",\"gpr_pass\":" << json_bool(result.gpr_pass);
            json << ",\"volatile_xmm_pass\":" << json_bool(result.volatile_xmm_pass);
            json << ",\"nonvolatile_xmm_pass\":" << json_bool(result.nonvolatile_xmm_pass);
            json << ",\"first_mismatch\":\"" << json_escape(result.first_mismatch) << "\"";
            json << ",\"error\":\"" << json_escape(result.error) << "\"";
            json << "}";
            return json.str();
        }

        auto serialize_diagnostic_result(
                std::wstring_view request_id,
                uint32_t iterations,
                std::wstring_view protocol_name,
                int64_t protocol_version,
                std::wstring_view build_stamp,
                std::wstring_view runtime_context,
                const HookScenarioResult& baseline,
                const HookScenarioResult& xmm_preserving) -> std::string
        {
            const bool expected_signal =
                    baseline.completed &&
                    baseline.level_pass &&
                    baseline.flags_pass &&
                    baseline.gpr_pass &&
                    !baseline.volatile_xmm_pass &&
                    baseline.nonvolatile_xmm_pass &&
                    xmm_preserving.completed &&
                    xmm_preserving.level_pass &&
                    xmm_preserving.flags_pass &&
                    xmm_preserving.gpr_pass &&
                    xmm_preserving.volatile_xmm_pass &&
                    xmm_preserving.nonvolatile_xmm_pass;

            std::ostringstream json;
            json << "{\n";
            json << "  \"schema\": \"kh3ap.runtime_hook_integrity_diagnostic.v1\",\n";
            json << "  \"protocol_name\": \"" << json_escape(protocol_name) << "\",\n";
            json << "  \"protocol_version\": " << protocol_version << ",\n";
            json << "  \"request_id\": \"" << json_escape(request_id) << "\",\n";
            json << "  \"status\": \"" << (expected_signal ? "completed" : "unexpected_result") << "\",\n";
            json << "  \"conclusion\": \""
                 << (expected_signal ? "volatile_xmm_requires_explicit_preservation" : "manual_review_required")
                 << "\",\n";
            json << "  \"iterations\": " << iterations << ",\n";
            json << "  \"recorded_at_utc\": \"" << now_iso8601_utc() << "\",\n";
            json << "  \"build_stamp\": \"" << json_escape(build_stamp) << "\",\n";
            json << "  \"runtime_context\": \"" << json_escape(runtime_context) << "\",\n";
            json << "  \"scenarios\": [\n";
            json << "    " << serialize_scenario(baseline) << ",\n";
            json << "    " << serialize_scenario(xmm_preserving) << "\n";
            json << "  ]\n";
            json << "}\n";
            return json.str();
        }

        auto serialize_diagnostic_event(
                std::wstring_view request_id,
                uint32_t iterations,
                std::wstring_view build_stamp,
                const HookScenarioResult& baseline,
                const HookScenarioResult& xmm_preserving) -> std::string
        {
            const bool expected_signal =
                    baseline.completed &&
                    baseline.level_pass &&
                    baseline.flags_pass &&
                    baseline.gpr_pass &&
                    !baseline.volatile_xmm_pass &&
                    baseline.nonvolatile_xmm_pass &&
                    xmm_preserving.completed &&
                    xmm_preserving.level_pass &&
                    xmm_preserving.flags_pass &&
                    xmm_preserving.gpr_pass &&
                    xmm_preserving.volatile_xmm_pass &&
                    xmm_preserving.nonvolatile_xmm_pass;

            std::ostringstream json;
            json << "{";
            json << "\"schema\":\"kh3ap.runtime_hook_integrity_diagnostic.v1\"";
            json << ",\"recorded_at_utc\":\"" << now_iso8601_utc() << "\"";
            json << ",\"request_id\":\"" << json_escape(request_id) << "\"";
            json << ",\"status\":\"" << (expected_signal ? "completed" : "unexpected_result") << "\"";
            json << ",\"conclusion\":\""
                 << (expected_signal ? "volatile_xmm_requires_explicit_preservation" : "manual_review_required")
                 << "\"";
            json << ",\"iterations\":" << iterations;
            json << ",\"build_stamp\":\"" << json_escape(build_stamp) << "\"";
            json << ",\"scenarios\":[" << serialize_scenario(baseline) << "," << serialize_scenario(xmm_preserving) << "]";
            json << "}\n";
            return json.str();
        }

        auto parse_iterations(const std::string& request_text) -> uint32_t
        {
            constexpr uint32_t default_iterations = 512;
            constexpr uint32_t max_iterations = 10000;
            const auto iterations = JsonFieldReader::int64_field(request_text, "iterations");
            if (!iterations.has_value())
            {
                return default_iterations;
            }
            return static_cast<uint32_t>(
                    std::clamp<int64_t>(*iterations, 1, static_cast<int64_t>(max_iterations)));
        }
    }

    auto process_pending_runtime_hook_integrity_diagnostic(
            const RuntimeHookIntegrityDiagnosticDeps& deps) -> void
    {
        if (!deps.bridge_dir.has_value())
        {
            return;
        }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        process_native_frame_wait_request(*deps.bridge_dir);
#endif

        const auto request_path = *deps.bridge_dir / std::filesystem::path{deps.request_filename};
        if (!path_points_to_regular_file(request_path))
        {
            return;
        }

        const auto request_text = read_text_file(request_path);
        if (!request_text.has_value())
        {
            return;
        }

        const std::wstring request_id =
                JsonFieldReader::string_field(*request_text, "request_id").value_or(STR("manual"));
        const uint32_t iterations = parse_iterations(*request_text);

        const auto baseline = run_hook_scenario(STR("vanilla_level_write_style_gpr_only"), false, iterations);
        const auto xmm_preserving = run_hook_scenario(STR("vanilla_level_write_style_xmm_preserving"), true, iterations);
        const auto result = serialize_diagnostic_result(
                request_id,
                iterations,
                deps.protocol_name,
                deps.protocol_version,
                deps.build_stamp,
                deps.runtime_context,
                baseline,
                xmm_preserving);

        write_text_file(*deps.bridge_dir / std::filesystem::path{deps.result_filename}, result);
        append_text_file(
                *deps.bridge_dir / std::filesystem::path{deps.events_filename},
                serialize_diagnostic_event(request_id, iterations, deps.build_stamp, baseline, xmm_preserving));
        try_remove_file(request_path);
    }
}
