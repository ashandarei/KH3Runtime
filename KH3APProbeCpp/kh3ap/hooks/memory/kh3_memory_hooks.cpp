#include <optional>
#include "kh3ap/app/config.h"
#include "kh3ap/hooks/memory/memory_hook_platform.h"
#include "kh3ap/hooks/memory/kh3_memory_hooks.h"
#include "kh3ap/hooks/memory/memory_hook_detail.h"
#include "kh3ap/bridge/transport/bridge_channels.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/bridge/utilities.h"
#include <UnrealDef.hpp>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <cwctype>
#include <format>
#include <mutex>
#include <sstream>
#include <utility>
namespace kh3ap
{
    namespace
    {
        constexpr auto hook_thunk_probe_bytes = 32;

        constexpr auto minimum_native_module_bytes = 64;
        constexpr auto native_hook_tail_guard_bytes = 32;
    } // namespace

    using ::kh3ap::settings::s_lucky_mark_item_id_to_reward_text_egs_offset;
    using ::kh3ap::settings::s_lucky_mark_item_id_to_reward_text_steam_offset;
    using ::kh3ap::settings::s_lucky_mark_reward_token_to_item_id_egs_offset;
    using ::kh3ap::settings::s_lucky_mark_reward_token_to_item_id_steam_offset;
    using ::kh3ap::settings::s_vanilla_level_write_offset;

    Kh3MemoryHooks* Kh3MemoryHooks::s_vanilla_level_write_polyhook_owner = nullptr;
    Kh3MemoryHooks* Kh3MemoryHooks::s_current_state_write_polyhook_owner = nullptr;
    Kh3MemoryHooks* Kh3MemoryHooks::s_lucky_mark_native_ap_resolver_owner = nullptr;

    namespace memory_hook_detail
    {
        auto try_read_process_u64_local(uintptr_t address) -> std::optional<uint64_t>
        {
            if (address == 0)
            {
                return std::nullopt;
            }

            uint64_t value = 0;
            SIZE_T bytes_read = 0;
            if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address),
                                   &value, sizeof(value), &bytes_read) ||
                bytes_read != sizeof(value))
            {
                return std::nullopt;
            }
            return value;
        }

        auto try_read_process_i32_local(uintptr_t address) -> std::optional<int32_t>
        {
            if (address == 0)
            {
                return std::nullopt;
            }

            int32_t value = 0;
            SIZE_T bytes_read = 0;
            if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address),
                                   &value, sizeof(value), &bytes_read) ||
                bytes_read != sizeof(value))
            {
                return std::nullopt;
            }
            return value;
        }

        auto bytes_match_at(uintptr_t address, std::initializer_list<uint8_t> expected) -> bool
        {
            if (address == 0 || expected.size() == 0)
            {
                return false;
            }

            std::array<uint8_t, hook_thunk_probe_bytes> buffer{};
            if (expected.size() > buffer.size())
            {
                return false;
            }

            SIZE_T bytes_read = 0;
            if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address),
                                   buffer.data(), expected.size(), &bytes_read) ||
                bytes_read != expected.size())
            {
                return false;
            }

            size_t index = 0;
            for (const uint8_t byte : expected)
            {
                if (buffer[index++] != byte)
                {
                    return false;
                }
            }
            return true;
        }
    } // namespace memory_hook_detail

    using namespace memory_hook_detail;

    Kh3MemoryHooks::Kh3MemoryHooks(RuntimeSoraLevelMonitorState& state, Kh3MemoryHookConfig config)
        : state_{state}, config_{std::move(config)}
    {
    }

    Kh3MemoryHooks::~Kh3MemoryHooks()
    {
        uninstall_lucky_mark_native_ap_resolver();
        uninstall_current_state_write_polyhook();
        uninstall_vanilla_level_write_polyhook();
    }

    auto Kh3MemoryHooks::state() -> RuntimeSoraLevelMonitorState&
    {
        return state_;
    }

    auto Kh3MemoryHooks::state() const -> const RuntimeSoraLevelMonitorState&
    {
        return state_;
    }

    auto Kh3MemoryHooks::module_size_from_pe_header(void* module) -> size_t
    {
        if (!module)
        {
            return 0;
        }
        auto* const base = reinterpret_cast<std::byte*>(module);
        auto* const dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
        if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE)
        {
            return 0;
        }
        auto* const nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
        if (!nt || nt->Signature != IMAGE_NT_SIGNATURE)
        {
            return 0;
        }
        return static_cast<size_t>(nt->OptionalHeader.SizeOfImage);
    }

    auto Kh3MemoryHooks::vanilla_level_write_pattern_matches(const std::byte* address) -> bool
    {
        if (!address)
        {
            return false;
        }
        static constexpr std::array<int, 29> pattern{{
            0x89, 0x53, 0x40, 0x48, 0x39, 0x69, 0x58, 0x0F, 0x84, -1,
            -1,   -1,   -1,   0x48, 0x89, 0x6C, 0x24, 0x30, 0x0F, 0x57,
            0xC0, 0xF3, 0x0F, 0x7F, 0x44, 0x24, 0x20, 0x85, 0xD2,
        }};
        for (size_t index = 0; index < pattern.size(); ++index)
        {
            if (pattern[index] >= 0 &&
                static_cast<uint8_t>(address[index]) != static_cast<uint8_t>(pattern[index]))
            {
                return false;
            }
        }
        return true;
    }

    auto Kh3MemoryHooks::find_vanilla_level_write_target(uintptr_t module_base, size_t module_size)
        -> std::optional<uintptr_t>
    {
        if (module_base == 0 || module_size < minimum_native_module_bytes)
        {
            return std::nullopt;
        }

        const uintptr_t expected_target =
            module_base + static_cast<uintptr_t>(s_vanilla_level_write_offset);
        if (expected_target + hook_thunk_probe_bytes < module_base + module_size &&
            vanilla_level_write_pattern_matches(
                reinterpret_cast<const std::byte*>(expected_target)))
        {
            return expected_target;
        }

        auto* const image = reinterpret_cast<const std::byte*>(module_base);
        static constexpr size_t pattern_size = 29;
        for (size_t offset = 0; offset + pattern_size < module_size; ++offset)
        {
            if (vanilla_level_write_pattern_matches(image + offset))
            {
                return module_base + offset;
            }
        }
        return std::nullopt;
    }

    auto Kh3MemoryHooks::current_state_write_pattern_matches(const std::byte* address) -> bool
    {
        if (!address)
        {
            return false;
        }
        static constexpr std::array<int, 27> pattern{{
            0x48, 0x8B, 0xDD, 0x48, 0x89, 0x9E, 0x70, 0x02, 0x00, 0x00, 0x48, 0x85, 0xDB, 0x74,
            -1,   0x48, 0x8B, 0x03, 0x48, 0x8B, 0xCB, 0xFF, 0x90, 0xF0, 0x02, 0x00, 0x00,
        }};
        for (size_t index = 0; index < pattern.size(); ++index)
        {
            if (pattern[index] >= 0 &&
                static_cast<uint8_t>(address[index]) != static_cast<uint8_t>(pattern[index]))
            {
                return false;
            }
        }
        return true;
    }

    auto Kh3MemoryHooks::find_current_state_write_target(uintptr_t module_base, size_t module_size)
        -> std::optional<uintptr_t>
    {
        if (module_base == 0 || module_size < minimum_native_module_bytes)
        {
            return std::nullopt;
        }

        auto* const image = reinterpret_cast<const std::byte*>(module_base);
        static constexpr size_t pattern_size = 27;
        static constexpr size_t target_offset_in_pattern = 3;
        for (size_t offset = 0; offset + pattern_size < module_size; ++offset)
        {
            if (current_state_write_pattern_matches(image + offset))
            {
                return module_base + offset + target_offset_in_pattern;
            }
        }
        return std::nullopt;
    }

    auto Kh3MemoryHooks::find_lucky_mark_native_ap_resolver_target(uintptr_t module_base,
                                                                   size_t module_size,
                                                                   uint64_t offset,
                                                                   std::wstring_view label)
        -> std::optional<uintptr_t>
    {
        if (module_base == 0 || module_size < minimum_native_module_bytes ||
            offset + native_hook_tail_guard_bytes >= module_size)
        {
            return std::nullopt;
        }

        const uintptr_t target = module_base + static_cast<uintptr_t>(offset);
        if (label == STR("reward_token_to_item_id"))
        {
            if (offset != s_lucky_mark_reward_token_to_item_id_steam_offset &&
                offset != s_lucky_mark_reward_token_to_item_id_egs_offset)
            {
                return std::nullopt;
            }
            if (!bytes_match_at(target,
                                {0x48, 0x83, 0x79, 0x40, 0x00, 0x4C, 0x8B, 0xD1, 0x0F, 0x84}))
            {
                return std::nullopt;
            }
        }
        else if (label == STR("item_id_to_reward_text"))
        {
            const bool steam_target =
                offset == s_lucky_mark_item_id_to_reward_text_steam_offset &&
                bytes_match_at(target, {0x40, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x30});
            const bool egs_target =
                offset == s_lucky_mark_item_id_to_reward_text_egs_offset &&
                bytes_match_at(target, {0x40, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48, 0xC7, 0x44, 0x24,
                                        0x20, 0xFE, 0xFF, 0xFF, 0xFF});
            if (!steam_target && !egs_target)
            {
                return std::nullopt;
            }
        }

        return target;
    }

} // namespace kh3ap
