#pragma once

#include "kh3ap/hooks/hook_phase.h"
#include "kh3ap/core/types.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace RC::Unreal
{
    class UnrealScriptFunctionCallableContext;
    class UFunction;
}

namespace PLH
{
    class x64Detour;
}

namespace kh3ap
{
    struct Kh3AutosaveProbeConfig
    {
        bool enabled{false};
        bool steam_save_startup_guard_enabled{false};
        bool save_game_to_slot_counter_enabled{false};
        bool manual_save_close_tracking_enabled{false};
        std::wstring build_stamp{};
        std::wstring protocol_name{};
        int64_t protocol_version{0};
        std::function<std::optional<std::filesystem::path>()> bridge_dir_resolver{};
        std::function<std::wstring()> runtime_context_resolver{};
        std::function<void(int32_t slot_number)> manual_save_completed_handler{};
        std::function<void(const std::filesystem::path&)>
                olympus_coliseum_save_completed_handler{};
    };

    class Kh3AutosaveProbe
    {
    public:
        Kh3AutosaveProbe() = default;
        Kh3AutosaveProbe(const Kh3AutosaveProbe&) = delete;
        auto operator=(const Kh3AutosaveProbe&) -> Kh3AutosaveProbe& = delete;
        ~Kh3AutosaveProbe();

        auto install_exit_confirmation_bypass() -> void;
        auto install_early_steam_save_startup_guard() -> void;
        auto install(Kh3AutosaveProbeConfig config) -> void;
        auto uninstall(std::wstring_view reason) -> void;
        // Called only on the game thread. Writers publish the pending flag
        // under the queue mutex; idle frames need neither a lock nor a clock read.
        [[nodiscard]] auto needs_update(std::chrono::steady_clock::time_point now) const -> bool
        {
            return installed_ &&
                   (manual_save_completed_events_pending_.load(std::memory_order_acquire) ||
                    now >= next_counter_flush_);
        }
        auto update(std::chrono::steady_clock::time_point now) -> void;

    private:
        struct X64DetourDeleter
        {
            auto operator()(PLH::x64Detour* detour) const noexcept -> void;
        };

        struct DecodedSaveSlot
        {
            std::wstring slot_name{};
            int slot_offset{-1};
            std::optional<int32_t> user_index{};
            std::wstring raw_param_prefix{};
        };

        struct NonRandomizerSlotBucket
        {
            std::wstring caller_name{};
            std::wstring slot_name{};
            int slot_offset{-1};
            std::optional<int32_t> user_index{};
            std::wstring raw_param_prefix{};
            uint64_t pre_hits{0};
        };

        static constexpr size_t save_file_callstack_capacity = 48;

        struct SaveFileCallstackSnapshot
        {
            uint64_t pre_call_filetime_100ns{0};
            uint64_t caller_rva{0};
            uint64_t main_module_stack_hash{0};
            uint32_t thread_id{0};
            uint32_t captured_frame_count{0};
            uint32_t main_module_frame_count{0};
            std::array<uint64_t, save_file_callstack_capacity>
                    main_module_stack_rvas{};
        };

        struct SaveFileOpenEvent
        {
            uint64_t event_index{0};
            uint64_t observed_filetime_100ns{0};
            SaveFileCallstackSnapshot callstack{};
            std::wstring path{};
            uint32_t desired_access{0};
            uint32_t share_mode{0};
            uint32_t creation_disposition{0};
            uint32_t flags_and_attributes{0};
        };

        struct NativeSaveProbeEvent
        {
            uint64_t event_index{0};
            SaveFileCallstackSnapshot callstack{};
        };

        struct ManualSaveHandleEntry
        {
            void* handle{};
            int32_t slot_number{-1};
            std::wstring path{};
            bool olympus_coliseum_progress{};
            uint64_t open_event_index{};
            uint64_t bytes_written{};
            bool write_failed{};
        };

        struct ManualSaveCompletedEvent
        {
            uint64_t event_index{0};
            int32_t slot_number{-1};
            std::wstring path{};
            bool olympus_coliseum_progress{};
            uint64_t open_event_index{};
            uint64_t bytes_written{};
            bool write_failed{};
        };

        auto on_hook(
                RC::Unreal::UnrealScriptFunctionCallableContext& callback_context,
                HookTargetInfo* hook_info,
                HookPhase phase) -> void;
        auto get_or_find_function_object(HookTargetInfo& hook_info) -> RC::Unreal::UFunction*;
        auto count_save_game_to_slot(
                RC::Unreal::UnrealScriptFunctionCallableContext& callback_context,
                HookPhase phase) -> void;
        auto record_non_randomizer_caller(std::wstring_view caller_name) -> void;
        auto record_non_randomizer_slot(std::wstring_view caller_name, const DecodedSaveSlot& slot) -> void;
        auto try_decode_save_slot(void* params) -> DecodedSaveSlot;
        auto install_save_file_open_probe(std::wstring_view reason) -> void;
        auto uninstall_save_file_open_probe() -> void;
        auto install_native_save_request_probe(std::wstring_view reason) -> void;
        auto uninstall_native_save_request_probe() -> void;
        static auto capture_save_file_callstack(uintptr_t caller_return_address)
                -> SaveFileCallstackSnapshot;
        auto record_native_save_probe_event(
                const SaveFileCallstackSnapshot& callstack) -> void;
        auto flush_native_save_probe_events() -> void;
        auto record_save_file_open(
                const wchar_t* path,
                uint64_t observed_filetime_100ns,
                const SaveFileCallstackSnapshot& callstack,
                uint32_t desired_access,
                uint32_t share_mode,
                uint32_t creation_disposition,
                uint32_t flags_and_attributes) -> uint64_t;
        auto track_manual_save_handle(
                const wchar_t* path,
                uint32_t desired_access,
                void* handle, uint64_t open_event_index) -> void;
        auto record_save_write(void* handle, bool success, uint64_t bytes_written) -> void;
        auto record_manual_save_handle_closed(void* handle) -> void;
        auto flush_manual_save_completed_events() -> void;
        auto flush_save_file_open_events() -> void;
        auto redirect_steam_save_root_after_identity_refresh(
                void* owner_context,
                void* output_key) -> bool;
        auto redirect_save_root_to_account(
                uintptr_t fstring_address) -> bool;
        auto record_save_integrity_validation(
                const wchar_t* file_name_prefix,
                const wchar_t* file_name_suffix,
                uintptr_t caller_rva,
                bool native_result) -> void;
        auto record_save_payload_version_status(
                uintptr_t caller_rva,
                int result) -> void;
        auto try_retire_prequeued_corrupt_dialog_after_native_success(
                uintptr_t caller_rva,
                const wchar_t* text,
                const wchar_t* caption,
                uint32_t type) -> bool;
        auto uninstall_steam_save_startup_guard() -> void;
        static bool __fastcall hooked_save_integrity_key_refresh(
                void* owner_context,
                void* output_key);
        static bool __fastcall hooked_save_integrity_validation(
                void* owner_context,
                const wchar_t* file_name_prefix,
                const wchar_t* file_name_suffix,
                void* output_bytes);
        static int __fastcall hooked_save_payload_version_status(
                void* payload_bytes,
                int* matched_version);
        static int __stdcall hooked_message_box_w(
                void* owner_window,
                const wchar_t* text,
                const wchar_t* caption,
                uint32_t type);
        static void* __stdcall hooked_create_file_w(
                const wchar_t* file_name,
                uint32_t desired_access,
                uint32_t share_mode,
                void* security_attributes,
                uint32_t creation_disposition,
                uint32_t flags_and_attributes,
                void* template_file);
        static int __stdcall hooked_create_directory_w(
                const wchar_t* path,
                void* security_attributes);
        static int __stdcall hooked_delete_file_w(const wchar_t* path);
        static void* __stdcall hooked_find_first_file_w(
                const wchar_t* path,
                void* find_data);
        static uint32_t __stdcall hooked_get_file_attributes_w(
                const wchar_t* path);
        static int __stdcall hooked_get_file_attributes_ex_w(
                const wchar_t* path,
                int info_level_id,
                void* file_information);
        static int __stdcall hooked_move_file_w(
                const wchar_t* existing_path,
                const wchar_t* new_path);
        static int __stdcall hooked_remove_directory_w(const wchar_t* path);
        static int __stdcall hooked_set_file_attributes_w(
                const wchar_t* path,
                uint32_t file_attributes);
        static int __stdcall hooked_close_handle(void* handle);
        static int __stdcall hooked_write_file(void* handle, const void* buffer, uint32_t size, uint32_t* written, void* overlapped);
        static void __fastcall hooked_native_save_request(
                void* arg0,
                void* arg1,
                void* arg2,
                void* arg3);
        auto should_write_event(const HookTargetInfo& hook_info, HookPhase phase) -> bool;
        auto seen_count_for(const HookTargetInfo& hook_info) const -> uint64_t;
        auto write_counter_snapshot(std::wstring_view reason) -> void;
        auto write_event(
                const HookTargetInfo& hook_info,
                HookPhase phase,
                RC::Unreal::UnrealScriptFunctionCallableContext& callback_context,
                RC::Unreal::UFunction* target_function,
                void* params) -> void;
        auto write_breadcrumb(
                const HookTargetInfo& hook_info,
                HookPhase phase,
                RC::Unreal::UnrealScriptFunctionCallableContext& callback_context,
                std::wstring_view stage,
                RC::Unreal::UFunction* target_function = nullptr,
                void* params = nullptr) -> void;

        Kh3AutosaveProbeConfig config_{};
        bool installed_{false};
        std::vector<HookTargetInfo> hook_infos_{};
        std::vector<std::pair<int, int>> hook_ids_{};
        std::atomic<uint64_t> save_game_to_slot_pre_hits_{0};
        std::atomic<uint64_t> save_game_to_slot_post_hits_{0};
        std::atomic<uint64_t> save_game_to_slot_randomizer_pre_hits_{0};
        std::atomic<uint64_t> save_game_to_slot_randomizer_post_hits_{0};
        std::atomic<uint64_t> save_game_to_slot_non_randomizer_pre_hits_{0};
        std::atomic<uint64_t> save_game_to_slot_non_randomizer_post_hits_{0};
        std::array<std::wstring, 8> non_randomizer_caller_names_{};
        std::array<uint64_t, 8> non_randomizer_caller_pre_hits_{};
        size_t non_randomizer_caller_count_{0};
        std::array<NonRandomizerSlotBucket, 12> non_randomizer_slot_buckets_{};
        size_t non_randomizer_slot_bucket_count_{0};
        void** create_file_w_iat_slot_{nullptr};
        uint64_t create_file_w_trampoline_{0};
        void** create_directory_w_iat_slot_{nullptr};
        uint64_t create_directory_w_trampoline_{0};
        void** delete_file_w_iat_slot_{nullptr};
        uint64_t delete_file_w_trampoline_{0};
        void** find_first_file_w_iat_slot_{nullptr};
        uint64_t find_first_file_w_trampoline_{0};
        void** get_file_attributes_w_iat_slot_{nullptr};
        uint64_t get_file_attributes_w_trampoline_{0};
        void** get_file_attributes_ex_w_iat_slot_{nullptr};
        uint64_t get_file_attributes_ex_w_trampoline_{0};
        void** move_file_w_iat_slot_{nullptr};
        uint64_t move_file_w_trampoline_{0};
        void** remove_directory_w_iat_slot_{nullptr};
        uint64_t remove_directory_w_trampoline_{0};
        void** set_file_attributes_w_iat_slot_{nullptr};
        uint64_t set_file_attributes_w_trampoline_{0};
        void** write_file_iat_slot_{nullptr};
        uint64_t write_file_trampoline_{0};
        bool save_write_probe_installed_{false};
        void** close_handle_iat_slot_{nullptr};
        uint64_t close_handle_trampoline_{0};
        bool steam_shared_save_path_redirect_enabled_{false};
        bool save_file_open_probe_attempted_{false};
        bool save_file_open_probe_installed_{false};
        bool save_file_close_probe_installed_{false};
        bool create_directory_w_redirect_installed_{false};
        bool delete_file_w_redirect_installed_{false};
        bool find_first_file_w_redirect_installed_{false};
        bool get_file_attributes_w_redirect_installed_{false};
        bool get_file_attributes_ex_w_redirect_installed_{false};
        bool move_file_w_redirect_installed_{false};
        bool remove_directory_w_redirect_installed_{false};
        bool set_file_attributes_w_redirect_installed_{false};
        uint64_t save_file_open_event_sequence_{0};
        uint64_t save_file_open_events_flushed_{0};
        std::mutex save_file_open_event_mutex_{};
        std::array<SaveFileOpenEvent, 32> save_file_open_events_{};
        size_t save_file_open_event_count_{0};
        std::unique_ptr<PLH::x64Detour, X64DetourDeleter>
                native_save_request_detour_{};
        uint64_t native_save_request_trampoline_{0};
        uintptr_t native_save_request_rva_{0};
        std::wstring native_save_request_storefront_{};
        bool native_save_request_hook_installed_{false};
        bool native_save_request_probe_attempted_{false};
        uint64_t native_save_probe_event_sequence_{0};
        std::mutex native_save_probe_event_mutex_{};
        std::array<NativeSaveProbeEvent, 32> native_save_probe_events_{};
        size_t native_save_probe_event_count_{0};
        std::mutex manual_save_handle_mutex_{};
        std::array<ManualSaveHandleEntry, 16> manual_save_handles_{};
        size_t manual_save_handle_count_{0};
        std::mutex manual_save_completed_event_mutex_{};
        std::array<ManualSaveCompletedEvent, 16> manual_save_completed_events_{};
        size_t manual_save_completed_event_count_{0};
        std::atomic<bool> manual_save_completed_events_pending_{false};
        uint64_t manual_save_completed_event_sequence_{0};
        uint64_t save_game_to_slot_pre_hits_flushed_{0};
        uint64_t save_game_to_slot_post_hits_flushed_{0};
        uint64_t save_game_to_slot_randomizer_pre_hits_flushed_{0};
        uint64_t save_game_to_slot_randomizer_post_hits_flushed_{0};
        uint64_t save_game_to_slot_non_randomizer_pre_hits_flushed_{0};
        uint64_t save_game_to_slot_non_randomizer_post_hits_flushed_{0};
        uint64_t save_game_to_slot_seen_{0};
        uint64_t autosave_basic_seen_{0};
        uint64_t autosave_ex_seen_{0};
        std::mutex steam_save_root_redirect_mutex_{};
        std::unique_ptr<PLH::x64Detour, X64DetourDeleter>
                save_integrity_key_refresh_detour_{};
        uint64_t save_integrity_key_refresh_trampoline_{0};
        uint64_t steam_save_root_fstring_copy_assign_address_{0};
        bool save_integrity_key_refresh_hook_attempted_{false};
        bool save_integrity_key_refresh_hook_installed_{false};
        uint64_t steam_save_root_redirect_count_{0};
        std::unique_ptr<PLH::x64Detour, X64DetourDeleter>
                save_integrity_validation_detour_{};
        uint64_t save_integrity_validation_trampoline_{0};
        bool save_integrity_validation_hook_installed_{false};
        std::atomic<uint64_t> save_integrity_validation_count_{0};
        std::mutex startup_save_validation_mutex_{};
        bool startup_save_sequence_started_{false};
        bool startup_save_sequence_failed_{false};
        bool startup_initial_system_native_valid_{false};
        bool startup_initial_system_version_valid_{false};
        uint8_t startup_slot_native_valid_mask_{0};
        uint8_t startup_slot_native_valid_count_{0};
        uint8_t startup_slot_version_valid_count_{0};
        bool startup_final_system_native_valid_{false};
        bool startup_prequeued_corrupt_dialog_can_be_retired_{false};
        std::unique_ptr<PLH::x64Detour, X64DetourDeleter>
                save_payload_version_status_detour_{};
        uint64_t save_payload_version_status_trampoline_{0};
        bool save_payload_version_status_hook_installed_{false};
        std::atomic<uint64_t> save_payload_version_status_count_{0};
        void** message_box_w_iat_slot_{nullptr};
        uint64_t message_box_w_trampoline_{0};
        bool message_box_w_hook_installed_{false};
        std::chrono::steady_clock::time_point last_save_game_to_slot_log_{};
        std::chrono::steady_clock::time_point next_counter_flush_{};
    };
}
