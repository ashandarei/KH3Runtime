#pragma once

#include "kh3ap/core/types.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

namespace PLH
{
    class x64Detour;
}

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    struct RuntimeSoraLevelMonitorState
    {
        struct VanillaLevelWritePolyhookEvent
        {
            uint64_t event_index{};
            uint32_t new_level{};
            uint32_t old_level{};
            uintptr_t base_param{};
            uintptr_t level_owner{};
        };

        struct CurrentStateWritePolyhookEvent
        {
            uint64_t event_index{};
            uintptr_t state_comp{};
            uintptr_t new_state{};
            uintptr_t replacement_state{};
            bool replaced{};
        };

        struct VanillaLevelWriteObjectProbe
        {
            bool base_readable{false};
            bool object_is_real{false};
            std::optional<uint64_t> vtable{};
            std::optional<uint32_t> object_flags{};
            std::optional<uint32_t> internal_index{};
            std::optional<uint64_t> class_private{};
            std::optional<uint64_t> name_raw{};
            DecodedNameValue decoded_name{};
            std::optional<uint64_t> outer_private{};
            std::optional<uint32_t> level_field{};
            std::optional<uint32_t> field_44{};
            std::optional<uint32_t> field_48{};
            std::optional<std::wstring> raw_header_bytes{};
            std::wstring object_name{};
            std::wstring object_full_name{};
            std::wstring class_name{};
            std::wstring class_full_name{};
            std::wstring outer_object{};
            std::wstring outer_chain{};
        };

        static constexpr size_t vanilla_level_write_polyhook_event_queue_capacity = 64;
        static constexpr size_t current_state_write_polyhook_event_queue_capacity = 128;

        void* current_state_write_vectored_handler_handle{};
        uintptr_t current_state_write_breakpoint_address{};
        size_t current_state_write_breakpoint_size{};
        std::wstring current_state_write_breakpoint_label{};
        uint64_t current_state_write_breakpoint_generation{0};
        uint64_t current_state_write_breakpoint_event_count{0};
        uintptr_t current_state_write_state_comp{};
        int current_state_write_property_offset{-1};
        std::optional<uint64_t> last_dispatched_runtime_sora_level{};
        std::optional<uint64_t> last_observed_runtime_sora_level{};
        std::wstring last_battle_level_sync_signature{};
        std::unique_ptr<PLH::x64Detour> vanilla_level_write_detour{};
        uint64_t vanilla_level_write_trampoline{};
        void* vanilla_level_write_stub{};
        size_t vanilla_level_write_stub_size{};
        bool vanilla_level_write_polyhook_attempted{false};
        bool vanilla_level_write_polyhook_installed{false};
        uintptr_t vanilla_level_write_polyhook_target{};
        uint64_t vanilla_level_write_polyhook_event_count{0};
        uint64_t vanilla_level_write_polyhook_dropped_count{0};
        std::mutex vanilla_level_write_polyhook_event_mutex{};
        std::array<VanillaLevelWritePolyhookEvent, vanilla_level_write_polyhook_event_queue_capacity>
                vanilla_level_write_polyhook_events{};
        size_t vanilla_level_write_polyhook_event_count_pending{0};
        std::unique_ptr<PLH::x64Detour> current_state_write_detour{};
        uint64_t current_state_write_trampoline{};
        void* current_state_write_stub{};
        size_t current_state_write_stub_size{};
        bool current_state_write_polyhook_attempted{false};
        bool current_state_write_polyhook_installed{false};
        uintptr_t current_state_write_polyhook_target{};
        uint64_t current_state_write_polyhook_event_count{0};
        uint64_t current_state_write_polyhook_dropped_count{0};
        std::mutex current_state_write_polyhook_event_mutex{};
        std::array<CurrentStateWritePolyhookEvent, current_state_write_polyhook_event_queue_capacity>
                current_state_write_polyhook_events{};
        size_t current_state_write_polyhook_event_count_pending{0};
        std::unique_ptr<PLH::x64Detour> lucky_mark_reward_token_to_item_id_detour{};
        uint64_t lucky_mark_reward_token_to_item_id_trampoline{};
        uintptr_t lucky_mark_reward_token_to_item_id_target{};
        bool lucky_mark_reward_token_to_item_id_installed{false};
        std::unique_ptr<PLH::x64Detour> lucky_mark_item_id_to_reward_text_detour{};
        uint64_t lucky_mark_item_id_to_reward_text_trampoline{};
        uintptr_t lucky_mark_item_id_to_reward_text_target{};
        bool lucky_mark_item_id_to_reward_text_installed{false};
        bool lucky_mark_native_ap_resolver_attempted{false};
        std::mutex lucky_mark_native_ap_resolver_diagnostic_mutex{};
        uint64_t lucky_mark_native_ap_resolver_diagnostic_event_count{0};
        uint64_t lucky_mark_native_ap_resolver_converter_seen_count{0};
        uint64_t lucky_mark_native_ap_resolver_converter_logged_count{0};
        uint64_t lucky_mark_native_ap_resolver_formatter_seen_count{0};
        uint64_t lucky_mark_native_ap_resolver_formatter_logged_count{0};
        bool lucky_mark_native_ap_resolver_datatable_dumped{false};
        RC::Unreal::UObject* cached_runtime_sora_state_comp{};
        RC::Unreal::UObject* cached_runtime_sora_current_state{};
        RC::Unreal::UObject* cached_runtime_sora_idle_state{};
        RC::Unreal::UObject* cached_runtime_sora_wallrun_state{};
        bool cached_runtime_sora_state_comp_valid{false};
        bool cached_runtime_sora_current_state_valid{false};
        bool cached_runtime_sora_idle_state_valid{false};
        bool cached_runtime_sora_wallrun_state_valid{false};
        std::optional<uint8_t> cached_runtime_sora_current_state_id{};
        std::wstring cached_runtime_sora_state_class{};
        std::wstring cached_runtime_sora_state_source{L"current_state_write_polyhook"};
        uint64_t cached_runtime_sora_state_event_index{0};
        uintptr_t cached_runtime_sora_pawn_address{};
        uintptr_t cached_runtime_sora_state_comp_address{};
        uintptr_t cached_runtime_sora_current_state_address{};
        uintptr_t cached_runtime_sora_idle_state_address{};
        uintptr_t cached_runtime_sora_wallrun_state_address{};
        uint64_t runtime_sora_wallrun_prewrite_replace_count{0};
        uint64_t runtime_sora_wallrun_clamp_attempt_count{0};
        uint64_t runtime_sora_wallrun_clamp_success_count{0};
        uint64_t runtime_sora_wallrun_clamp_failure_count{0};
    };
}
