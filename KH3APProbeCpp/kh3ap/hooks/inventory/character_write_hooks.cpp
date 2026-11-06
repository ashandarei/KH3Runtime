#include <memory>
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
#include "kh3ap/core/events/character_level_events.h"
#include "kh3ap/core/events/character_state_events.h"
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
        constexpr auto max_native_character_level = 100;
        constexpr auto character_write_capture_prefix_bytes = 32;
        constexpr auto character_write_capture_bytes = 96;
    } // namespace

    using ::kh3ap::settings::s_enable_current_state_write_polyhook;
    using ::kh3ap::settings::s_enable_runtime_sora_wallrun_idle_clamp;
    using ::kh3ap::settings::s_enable_vanilla_level_write_polyhook;
    using ::kh3ap::settings::s_vanilla_level_write_offset;

    using namespace memory_hook_detail;
    auto Kh3MemoryHooks::vanilla_level_write_polyhook_logger(void* base_param, uint32_t new_level,
                                                             uint32_t old_level, void* level_owner)
        -> void
    {
        Kh3MemoryHooks* const owner = s_vanilla_level_write_polyhook_owner;
        if (!owner)
        {
            return;
        }
        owner->append_vanilla_level_write_polyhook_log(base_param, new_level, old_level,
                                                       level_owner);
    }

    auto Kh3MemoryHooks::append_vanilla_level_write_polyhook_log(void* base_param,
                                                                 uint32_t new_level,
                                                                 uint32_t old_level,
                                                                 void* level_owner) -> void
    {
        if (new_level == 0 || new_level > max_native_character_level)
        {
            return;
        }
        if (new_level != old_level)
        {
            character_level_revision.fetch_add(1, std::memory_order_release);
        }
        std::lock_guard<std::mutex> lock{state_.vanilla_level_write_polyhook_event_mutex};
        ++state_.vanilla_level_write_polyhook_event_count;
        if (state_.vanilla_level_write_polyhook_event_count_pending >=
            RuntimeSoraLevelMonitorState::vanilla_level_write_polyhook_event_queue_capacity)
        {
            ++state_.vanilla_level_write_polyhook_dropped_count;
            return;
        }
        state_.vanilla_level_write_polyhook_events
            [state_.vanilla_level_write_polyhook_event_count_pending++] = {
            state_.vanilla_level_write_polyhook_event_count,
            new_level,
            old_level,
            reinterpret_cast<uintptr_t>(base_param),
            reinterpret_cast<uintptr_t>(level_owner),
        };
    }

    auto Kh3MemoryHooks::append_vanilla_level_write_polyhook_install_log(
        std::wstring_view event, uintptr_t module_base, size_t module_size, uintptr_t target,
        bool hook_result, std::wstring_view detail) const -> void
    {
        if (!config_.bridge_dir_resolver)
        {
            return;
        }
        const auto bridge_dir = config_.bridge_dir_resolver();
        if (!bridge_dir.has_value())
        {
            return;
        }

        const uintptr_t capture_start = target >= character_write_capture_prefix_bytes
                                            ? target - character_write_capture_prefix_bytes
                                            : target;
        const std::wstring bytes =
            target != 0 ? try_probe_pointer_bytes(static_cast<uint64_t>(capture_start),
                                                  character_write_capture_bytes)
                              .value_or(L"<unreadable>")
                        : std::wstring{};

        std::ostringstream json;
        json << "{\"schema\":\"kh3ap.vanilla_level_write_polyhook_install.v1\"";
        json << ",\"recorded_at_utc\":\"" << now_iso8601_utc() << "\"";
        json << ",\"build_stamp\":\"" << json_escape(config_.build_stamp) << "\"";
        json << ",\"event\":\"" << json_escape(event) << "\"";
        json << ",\"module_base\":\"0x" << std::hex << module_base << std::dec << "\"";
        json << ",\"module_size\":" << module_size;
        json << ",\"expected_offset\":\"0x" << std::hex << s_vanilla_level_write_offset << std::dec
             << "\"";
        json << ",\"target\":\"0x" << std::hex << target << std::dec << "\"";
        if (target >= module_base && module_base != 0)
        {
            json << ",\"target_offset\":\"0x" << std::hex << (target - module_base) << std::dec
                 << "\"";
        }
        json << ",\"hook_result\":" << (hook_result ? "true" : "false");
        json << ",\"detail\":\"" << json_escape(detail) << "\"";
        json << ",\"bytes_around_target\":\"" << json_escape(bytes) << "\"";
        json << "}\n";
        append_text_file(*bridge_dir / "vanilla_level_write_polyhook_install.jsonl", json.str());
    }

    auto Kh3MemoryHooks::install_vanilla_level_write_polyhook_once(std::wstring_view reason) -> void
    {
        if constexpr (!s_enable_vanilla_level_write_polyhook)
        {
            return;
        }
        if (state_.vanilla_level_write_polyhook_attempted)
        {
            return;
        }
        state_.vanilla_level_write_polyhook_attempted = true;

        HMODULE const module = GetModuleHandleW(nullptr);
        const uintptr_t module_base = reinterpret_cast<uintptr_t>(module);
        const size_t module_size = module_size_from_pe_header(module);
        const auto target = find_vanilla_level_write_target(module_base, module_size);
        if (!target.has_value())
        {
            append_vanilla_level_write_polyhook_install_log(
                L"target_not_found", module_base, module_size,
                module_base + static_cast<uintptr_t>(s_vanilla_level_write_offset), false, reason);
            return;
        }

        state_.vanilla_level_write_polyhook_target = *target;
        s_vanilla_level_write_polyhook_owner = this;
        if (!allocate_vanilla_level_write_stub())
        {
            append_vanilla_level_write_polyhook_install_log(L"stub_alloc_failed", module_base,
                                                            module_size, *target, false, reason);
            return;
        }

        state_.vanilla_level_write_detour = std::make_unique<PLH::x64Detour>(
            static_cast<uint64_t>(*target),
            reinterpret_cast<uint64_t>(state_.vanilla_level_write_stub),
            &state_.vanilla_level_write_trampoline);
        const bool hook_result = state_.vanilla_level_write_detour->hook();
        state_.vanilla_level_write_polyhook_installed = hook_result;
        if (hook_result && !update_vanilla_level_write_stub_trampoline())
        {
            uninstall_vanilla_level_write_polyhook();
            append_vanilla_level_write_polyhook_install_log(L"stub_protection_failed", module_base,
                                                            module_size, *target, false, reason);
            return;
        }
        append_vanilla_level_write_polyhook_install_log(hook_result ? L"installed" : L"hook_failed",
                                                        module_base, module_size, *target,
                                                        hook_result, reason);
    }

    auto Kh3MemoryHooks::uninstall_vanilla_level_write_polyhook() -> void
    {
        if (state_.vanilla_level_write_detour)
        {
            if (state_.vanilla_level_write_polyhook_installed &&
                !state_.vanilla_level_write_detour->unHook())
            {
                // The native branch still targets this allocation. Disable
                // callbacks and retain its stub/trampoline for process lifetime.
                if (s_vanilla_level_write_polyhook_owner == this)
                {
                    s_vanilla_level_write_polyhook_owner = nullptr;
                }
                (void)state_.vanilla_level_write_detour.release();
                state_.vanilla_level_write_stub = nullptr;
                state_.vanilla_level_write_stub_size = 0;
                state_.vanilla_level_write_polyhook_installed = false;
                return;
            }
            state_.vanilla_level_write_detour.reset();
        }
        if (state_.vanilla_level_write_stub)
        {
            VirtualFree(state_.vanilla_level_write_stub, 0, MEM_RELEASE);
            state_.vanilla_level_write_stub = nullptr;
            state_.vanilla_level_write_stub_size = 0;
        }
        if (s_vanilla_level_write_polyhook_owner == this)
        {
            s_vanilla_level_write_polyhook_owner = nullptr;
        }
        state_.vanilla_level_write_polyhook_installed = false;
    }

    auto Kh3MemoryHooks::drain_vanilla_level_write_events() -> VanillaLevelWriteEventBatch
    {
        VanillaLevelWriteEventBatch batch{};
        std::lock_guard<std::mutex> lock{state_.vanilla_level_write_polyhook_event_mutex};
        batch.event_count = state_.vanilla_level_write_polyhook_event_count_pending;
        batch.dropped_count = state_.vanilla_level_write_polyhook_dropped_count;
        if (batch.event_count == 0 && batch.dropped_count == 0)
        {
            return batch;
        }
        std::copy_n(state_.vanilla_level_write_polyhook_events.begin(), batch.event_count,
                    batch.events.begin());
        state_.vanilla_level_write_polyhook_event_count_pending = 0;
        state_.vanilla_level_write_polyhook_dropped_count = 0;
        return batch;
    }

    auto Kh3MemoryHooks::current_state_write_polyhook_logger(void* state_comp, void* new_state)
        -> void*
    {
        Kh3MemoryHooks* const owner = s_current_state_write_polyhook_owner;
        if (!owner)
        {
            return new_state;
        }
        return owner->append_current_state_write_polyhook_log(state_comp, new_state);
    }

    auto Kh3MemoryHooks::append_current_state_write_polyhook_log(void* state_comp, void* new_state)
        -> void*
    {
        std::lock_guard<std::mutex> lock{state_.current_state_write_polyhook_event_mutex};
        ++state_.current_state_write_polyhook_event_count;
        void* replacement_state = new_state;
        bool replaced = false;
        if constexpr (s_enable_runtime_sora_wallrun_idle_clamp)
        {
            const uintptr_t state_comp_address = reinterpret_cast<uintptr_t>(state_comp);
            const uintptr_t new_state_address = reinterpret_cast<uintptr_t>(new_state);
            if (state_comp_address != 0 &&
                state_comp_address == state_.cached_runtime_sora_state_comp_address &&
                new_state_address != 0 &&
                new_state_address == state_.cached_runtime_sora_wallrun_state_address &&
                state_.cached_runtime_sora_wallrun_state_valid &&
                state_.cached_runtime_sora_idle_state_valid &&
                state_.cached_runtime_sora_idle_state_address != 0 &&
                state_.cached_runtime_sora_idle_state_address != new_state_address)
            {
                replacement_state =
                    reinterpret_cast<void*>(state_.cached_runtime_sora_idle_state_address);
                replaced = true;
                ++state_.runtime_sora_wallrun_prewrite_replace_count;
            }
        }
        if (const auto observer = character_state_observer.load(std::memory_order_acquire))
        {
            observer(state_comp, replacement_state);
        }
        if (state_.current_state_write_polyhook_event_count_pending >=
            RuntimeSoraLevelMonitorState::current_state_write_polyhook_event_queue_capacity)
        {
            ++state_.current_state_write_polyhook_dropped_count;
            return replacement_state;
        }
        state_.current_state_write_polyhook_events
            [state_.current_state_write_polyhook_event_count_pending++] = {
            state_.current_state_write_polyhook_event_count,
            reinterpret_cast<uintptr_t>(state_comp),
            reinterpret_cast<uintptr_t>(new_state),
            reinterpret_cast<uintptr_t>(replacement_state),
            replaced,
        };
        return replacement_state;
    }

    auto Kh3MemoryHooks::append_current_state_write_polyhook_install_log(
        std::wstring_view event, uintptr_t module_base, size_t module_size, uintptr_t target,
        bool hook_result, std::wstring_view detail) const -> void
    {
        if (!config_.bridge_dir_resolver)
        {
            return;
        }
        const auto bridge_dir = config_.bridge_dir_resolver();
        if (!bridge_dir.has_value())
        {
            return;
        }

        const uintptr_t capture_start = target >= character_write_capture_prefix_bytes
                                            ? target - character_write_capture_prefix_bytes
                                            : target;
        const std::wstring bytes =
            target != 0 ? try_probe_pointer_bytes(static_cast<uint64_t>(capture_start),
                                                  character_write_capture_bytes)
                              .value_or(L"<unreadable>")
                        : std::wstring{};

        std::ostringstream json;
        json << "{\"schema\":\"kh3ap.current_state_write_polyhook_install.v1\"";
        json << ",\"recorded_at_utc\":\"" << now_iso8601_utc() << "\"";
        json << ",\"build_stamp\":\"" << json_escape(config_.build_stamp) << "\"";
        json << ",\"event\":\"" << json_escape(event) << "\"";
        json << ",\"module_base\":\"0x" << std::hex << module_base << std::dec << "\"";
        json << ",\"module_size\":" << module_size;
        json << ",\"target\":\"0x" << std::hex << target << std::dec << "\"";
        if (target >= module_base && module_base != 0)
        {
            json << ",\"target_offset\":\"0x" << std::hex << (target - module_base) << std::dec
                 << "\"";
        }
        json << ",\"hook_result\":" << (hook_result ? "true" : "false");
        json << ",\"detail\":\"" << json_escape(detail) << "\"";
        json << ",\"bytes_around_target\":\"" << json_escape(bytes) << "\"";
        json << "}\n";
        append_text_file(*bridge_dir / "current_state_write_polyhook_install.jsonl", json.str());
    }

    auto Kh3MemoryHooks::install_current_state_write_polyhook_once(std::wstring_view reason) -> void
    {
        if constexpr (!s_enable_current_state_write_polyhook)
        {
            return;
        }
        if (state_.current_state_write_polyhook_attempted)
        {
            return;
        }
        state_.current_state_write_polyhook_attempted = true;

        HMODULE const module = GetModuleHandleW(nullptr);
        const uintptr_t module_base = reinterpret_cast<uintptr_t>(module);
        const size_t module_size = module_size_from_pe_header(module);
        const auto target = find_current_state_write_target(module_base, module_size);
        if (!target.has_value())
        {
            append_current_state_write_polyhook_install_log(L"target_not_found", module_base,
                                                            module_size, 0, false, reason);
            return;
        }

        state_.current_state_write_polyhook_target = *target;
        s_current_state_write_polyhook_owner = this;
        if (!allocate_current_state_write_stub())
        {
            append_current_state_write_polyhook_install_log(L"stub_alloc_failed", module_base,
                                                            module_size, *target, false, reason);
            return;
        }

        state_.current_state_write_detour = std::make_unique<PLH::x64Detour>(
            static_cast<uint64_t>(*target),
            reinterpret_cast<uint64_t>(state_.current_state_write_stub),
            &state_.current_state_write_trampoline);
        const bool hook_result = state_.current_state_write_detour->hook();
        state_.current_state_write_polyhook_installed = hook_result;
        if (hook_result && !update_current_state_write_stub_trampoline())
        {
            uninstall_current_state_write_polyhook();
            append_current_state_write_polyhook_install_log(L"stub_protection_failed", module_base,
                                                            module_size, *target, false, reason);
            return;
        }
        append_current_state_write_polyhook_install_log(hook_result ? L"installed" : L"hook_failed",
                                                        module_base, module_size, *target,
                                                        hook_result, reason);
    }

    auto Kh3MemoryHooks::uninstall_current_state_write_polyhook() -> void
    {
        if (state_.current_state_write_detour)
        {
            if (state_.current_state_write_polyhook_installed &&
                !state_.current_state_write_detour->unHook())
            {
                // The native branch still targets this allocation. Disable
                // callbacks and retain its stub/trampoline for process lifetime.
                if (s_current_state_write_polyhook_owner == this)
                {
                    s_current_state_write_polyhook_owner = nullptr;
                }
                (void)state_.current_state_write_detour.release();
                state_.current_state_write_stub = nullptr;
                state_.current_state_write_stub_size = 0;
                state_.current_state_write_polyhook_installed = false;
                return;
            }
            state_.current_state_write_detour.reset();
        }
        if (state_.current_state_write_stub)
        {
            VirtualFree(state_.current_state_write_stub, 0, MEM_RELEASE);
            state_.current_state_write_stub = nullptr;
            state_.current_state_write_stub_size = 0;
        }
        if (s_current_state_write_polyhook_owner == this)
        {
            s_current_state_write_polyhook_owner = nullptr;
        }
        state_.current_state_write_polyhook_installed = false;
    }

    auto Kh3MemoryHooks::drain_current_state_write_events() -> CurrentStateWriteEventBatch
    {
        CurrentStateWriteEventBatch batch{};
        std::lock_guard<std::mutex> lock{state_.current_state_write_polyhook_event_mutex};
        batch.event_count = state_.current_state_write_polyhook_event_count_pending;
        batch.dropped_count = state_.current_state_write_polyhook_dropped_count;
        if (batch.event_count == 0 && batch.dropped_count == 0)
        {
            return batch;
        }
        std::copy_n(state_.current_state_write_polyhook_events.begin(), batch.event_count,
                    batch.events.begin());
        state_.current_state_write_polyhook_event_count_pending = 0;
        state_.current_state_write_polyhook_dropped_count = 0;
        return batch;
    }
} // namespace kh3ap
