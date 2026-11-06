#pragma once

#include "kh3ap/features/player/state/sora_level_monitor_state.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kh3ap
{
    struct Kh3MemoryHookConfig
    {
        std::wstring_view build_stamp{};
        std::function<std::optional<std::filesystem::path>()> bridge_dir_resolver{};
    };

    class Kh3MemoryHooks
    {
    public:
        using VanillaLevelWritePolyhookEvent = RuntimeSoraLevelMonitorState::VanillaLevelWritePolyhookEvent;
        using CurrentStateWritePolyhookEvent = RuntimeSoraLevelMonitorState::CurrentStateWritePolyhookEvent;

        struct VanillaLevelWriteEventBatch
        {
            std::array<
                    VanillaLevelWritePolyhookEvent,
                    RuntimeSoraLevelMonitorState::vanilla_level_write_polyhook_event_queue_capacity>
                    events{};
            size_t event_count{0};
            uint64_t dropped_count{0};
        };

        struct CurrentStateWriteEventBatch
        {
            std::array<
                    CurrentStateWritePolyhookEvent,
                    RuntimeSoraLevelMonitorState::current_state_write_polyhook_event_queue_capacity>
                    events{};
            size_t event_count{0};
            uint64_t dropped_count{0};
        };

        explicit Kh3MemoryHooks(RuntimeSoraLevelMonitorState& state, Kh3MemoryHookConfig config);
        ~Kh3MemoryHooks();

        Kh3MemoryHooks(const Kh3MemoryHooks&) = delete;
        auto operator=(const Kh3MemoryHooks&) -> Kh3MemoryHooks& = delete;
        Kh3MemoryHooks(Kh3MemoryHooks&&) = delete;
        auto operator=(Kh3MemoryHooks&&) -> Kh3MemoryHooks& = delete;

        auto state() -> RuntimeSoraLevelMonitorState&;
        auto state() const -> const RuntimeSoraLevelMonitorState&;

        auto install_vanilla_level_write_polyhook_once(std::wstring_view reason) -> void;
        auto uninstall_vanilla_level_write_polyhook() -> void;
        auto drain_vanilla_level_write_events() -> VanillaLevelWriteEventBatch;

        auto install_current_state_write_polyhook_once(std::wstring_view reason) -> void;
        auto uninstall_current_state_write_polyhook() -> void;
        auto drain_current_state_write_events() -> CurrentStateWriteEventBatch;

        // Original native catalog lookup owned by our installed resolver hook.
        // Zero for an unowned or different target; callers must not follow arbitrary jumps.
        static auto original_item_catalog_lookup(uintptr_t expected_target) -> uintptr_t;
        auto install_lucky_mark_native_ap_resolver_once(std::wstring_view reason) -> void;
        auto uninstall_lucky_mark_native_ap_resolver() -> void;
        auto reapply_lucky_mark_native_ap_retirements(std::wstring_view reason) -> void;

    private:
        RuntimeSoraLevelMonitorState& state_;
        Kh3MemoryHookConfig config_{};
        std::mutex lucky_mark_native_retirement_mutex_{};
        std::vector<int32_t> lucky_mark_native_retired_milestones_{};

        static Kh3MemoryHooks* s_vanilla_level_write_polyhook_owner;
        static Kh3MemoryHooks* s_current_state_write_polyhook_owner;
        static Kh3MemoryHooks* s_lucky_mark_native_ap_resolver_owner;

        static auto module_size_from_pe_header(void* module) -> size_t;
        static auto vanilla_level_write_pattern_matches(const std::byte* address) -> bool;
        static auto find_vanilla_level_write_target(uintptr_t module_base, size_t module_size) -> std::optional<uintptr_t>;
        static auto current_state_write_pattern_matches(const std::byte* address) -> bool;
        static auto find_current_state_write_target(uintptr_t module_base, size_t module_size) -> std::optional<uintptr_t>;
        static auto find_lucky_mark_native_ap_resolver_target(
                uintptr_t module_base,
                size_t module_size,
                uint64_t offset,
                std::wstring_view label) -> std::optional<uintptr_t>;
        static auto emit_u8(std::vector<uint8_t>& code, uint8_t value) -> void;
        static auto emit_u32(std::vector<uint8_t>& code, uint32_t value) -> void;
        static auto emit_u64(std::vector<uint8_t>& code, uint64_t value) -> void;
        static auto emit_sub_rsp_u32(std::vector<uint8_t>& code, uint32_t value) -> void;
        static auto emit_add_rsp_u32(std::vector<uint8_t>& code, uint32_t value) -> void;
        static auto emit_movdqu_m128_rsp_disp_xmm(std::vector<uint8_t>& code, uint32_t disp, uint8_t xmm_reg) -> void;
        static auto emit_movdqu_xmm_m128_rsp_disp(std::vector<uint8_t>& code, uint8_t xmm_reg, uint32_t disp) -> void;
        static auto emit_save_xmm_state(std::vector<uint8_t>& code) -> void;
        static auto emit_restore_xmm_state(std::vector<uint8_t>& code) -> void;
        static auto vanilla_level_write_polyhook_logger(
                void* base_param,
                uint32_t new_level,
                uint32_t old_level,
                void* level_owner) -> void;
        static auto current_state_write_polyhook_logger(void* state_comp, void* new_state) -> void*;
        static auto lucky_mark_reward_token_to_item_id_hook(void* resolver, uint64_t reward_token_name) -> int32_t;
        static auto lucky_mark_item_id_to_reward_text_hook(
                void* formatter,
                void* out_text,
                int32_t item_id,
                bool use_display_name) -> void*;

        auto retire_lucky_mark_native_ap_milestone_for_reward_token(
                uint64_t reward_token_name) -> void;

        auto allocate_vanilla_level_write_stub() -> bool;
        auto update_vanilla_level_write_stub_trampoline() -> bool;
        auto append_vanilla_level_write_polyhook_log(
                void* base_param,
                uint32_t new_level,
                uint32_t old_level,
                void* level_owner) -> void;
        auto append_vanilla_level_write_polyhook_install_log(
                std::wstring_view event,
                uintptr_t module_base,
                size_t module_size,
                uintptr_t target,
                bool hook_result,
                std::wstring_view detail) const -> void;

        auto allocate_current_state_write_stub() -> bool;
        auto update_current_state_write_stub_trampoline() -> bool;
        auto append_current_state_write_polyhook_log(void* state_comp, void* new_state) -> void*;
        auto append_current_state_write_polyhook_install_log(
                std::wstring_view event,
                uintptr_t module_base,
                size_t module_size,
                uintptr_t target,
                bool hook_result,
                std::wstring_view detail) const -> void;
        auto append_lucky_mark_native_ap_resolver_install_diagnostic_log(
                std::wstring_view event,
                std::wstring_view target_label,
                uint64_t expected_offset,
                uintptr_t module_base,
                size_t module_size,
                uintptr_t target,
                bool hook_result,
                std::wstring_view detail) const -> void;
        auto record_lucky_mark_native_ap_check(
                uint64_t reward_token_name,
                int32_t result) -> void;
        auto append_lucky_mark_reward_token_to_item_id_diagnostic_event(
                void* resolver,
                uint64_t reward_token_name,
                int32_t result) -> void;
        auto append_lucky_mark_item_id_to_reward_text_diagnostic_event(
                void* formatter,
                void* out_text,
                int32_t item_id,
                bool use_display_name,
                void* return_value,
                std::wstring_view out_text_before,
                bool datatable_text_applied,
                std::wstring_view datatable_display_name,
                bool fstring_text_applied,
                std::wstring_view formatter_display_text,
                bool shell_text_applied,
                int32_t formatter_item_id_used) -> void;
        auto should_dump_lucky_mark_native_ap_datatable_diagnostic(int32_t item_id) -> bool;
        auto append_lucky_mark_native_ap_datatable_diagnostic(
                void* formatter,
                int32_t item_id,
                std::wstring_view reason) -> void;
    };
}
