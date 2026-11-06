#include "kh3ap/diagnostics/kh3_autosave_probe.h"
#include "kh3ap/diagnostics/autosave/support.h"
#include "kh3ap/core/input/exit_confirmation.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <intrin.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <iterator>
#include <mutex>
#include <string>
#include <string_view>
#include <UnrealDef.hpp>

using namespace RC;
using namespace RC::Unreal;

namespace kh3ap
{
    namespace
    {
        constexpr auto startup_validation_slot_count = 5;
        constexpr auto startup_validation_slot_mask = (1u << startup_validation_slot_count) - 1;

        constexpr auto max_payload_version_log_samples = 32;
        constexpr auto max_save_integrity_log_samples = 64;
    } // namespace

    using namespace autosave_detail;
    bool __fastcall Kh3AutosaveProbe::hooked_save_integrity_key_refresh(void* owner_context,
                                                                        void* output_key)
    {
        Kh3AutosaveProbe* const owner = s_steam_save_startup_guard_owner;
        if (!owner || owner->save_integrity_key_refresh_trampoline_ == 0)
        {
            return false;
        }

        const bool result = reinterpret_cast<SaveIntegrityKeyRefreshFn>(
            owner->save_integrity_key_refresh_trampoline_)(owner_context, output_key);
        if (result)
        {
            owner->redirect_steam_save_root_after_identity_refresh(owner_context, output_key);
        }
        return result;
    }

    bool __fastcall Kh3AutosaveProbe::hooked_save_integrity_validation(
        void* owner_context, const wchar_t* file_name_prefix, const wchar_t* file_name_suffix,
        void* output_bytes)
    {
        Kh3AutosaveProbe* const owner = s_save_integrity_validation_probe_owner;
        if (!owner || owner->save_integrity_validation_trampoline_ == 0)
        {
            return false;
        }

        const uintptr_t main_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        const uintptr_t return_address = reinterpret_cast<uintptr_t>(_ReturnAddress());
        const uintptr_t caller_rva =
            main_base != 0 && return_address >= main_base ? return_address - main_base : 0;

        const bool native_result = reinterpret_cast<SaveIntegrityValidationFn>(
            owner->save_integrity_validation_trampoline_)(owner_context, file_name_prefix,
                                                          file_name_suffix, output_bytes);
        owner->record_save_integrity_validation(file_name_prefix, file_name_suffix, caller_rva,
                                                native_result);
        return native_result;
    }

    int __fastcall Kh3AutosaveProbe::hooked_save_payload_version_status(void* payload_bytes,
                                                                        int* matched_version)
    {
        Kh3AutosaveProbe* const owner = s_save_payload_version_status_probe_owner;
        if (!owner || owner->save_payload_version_status_trampoline_ == 0)
        {
            return -1;
        }

        const uintptr_t main_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        const uintptr_t return_address = reinterpret_cast<uintptr_t>(_ReturnAddress());
        const uintptr_t caller_rva =
            main_base != 0 && return_address >= main_base ? return_address - main_base : 0;
        const int result = reinterpret_cast<SavePayloadVersionStatusFn>(
            owner->save_payload_version_status_trampoline_)(payload_bytes, matched_version);
        const uint64_t count =
            owner->save_payload_version_status_count_.fetch_add(1, std::memory_order_relaxed) + 1;
        if (count <= max_payload_version_log_samples)
        {
            Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] SavePayloadVersionStatus count={} "
                                                "caller_rva=0x{:x} result={}\n"),
                                            count, caller_rva, result);
        }
        owner->record_save_payload_version_status(caller_rva, result);
        return result;
    }

    int __stdcall Kh3AutosaveProbe::hooked_message_box_w(void* owner_window, const wchar_t* text,
                                                         const wchar_t* caption, uint32_t type)
    {
        Kh3AutosaveProbe* const owner = s_message_box_w_probe_owner;
        if (!owner || owner->message_box_w_trampoline_ == 0)
        {
            return 0;
        }

        // Preserve the game's normal confirmed-exit path without creating a dialog.
        // All other messages, including save warnings, keep their original handling.
        if (is_exit_confirmation_dialog(text, caption, type))
        {
            Output::send<LogLevel::Verbose>(
                STR("[KH3APProbeCpp] ExitConfirmationBypass accepted exit request\n"));
            return IDYES;
        }

        const uintptr_t main_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        const uintptr_t return_address = reinterpret_cast<uintptr_t>(_ReturnAddress());
        const uintptr_t caller_rva =
            main_base != 0 && return_address >= main_base ? return_address - main_base : 0;
        if (owner->try_retire_prequeued_corrupt_dialog_after_native_success(caller_rva, text,
                                                                            caption, type))
        {
            return IDCANCEL;
        }

        return reinterpret_cast<MessageBoxWFn>(owner->message_box_w_trampoline_)(
            reinterpret_cast<HWND>(owner_window), text, caption, type);
    }

    auto Kh3AutosaveProbe::record_save_integrity_validation(const wchar_t* file_name_prefix,
                                                            const wchar_t* file_name_suffix,
                                                            uintptr_t caller_rva,
                                                            bool native_result) -> void
    {
        const std::wstring prefix = read_bounded_wstring(file_name_prefix);
        const std::wstring suffix = read_bounded_wstring(file_name_suffix);
        {
            std::scoped_lock lock{startup_save_validation_mutex_};
            if (caller_rva == kh3_initial_system_validation_caller_rva)
            {
                startup_save_sequence_started_ = true;
                startup_save_sequence_failed_ = false;
                startup_initial_system_native_valid_ = false;
                startup_initial_system_version_valid_ = false;
                startup_slot_native_valid_mask_ = 0;
                startup_slot_native_valid_count_ = 0;
                startup_slot_version_valid_count_ = 0;
                startup_final_system_native_valid_ = false;
                startup_prequeued_corrupt_dialog_can_be_retired_ = false;
                startup_initial_system_native_valid_ =
                    prefix == STR("kh3sv2") && suffix == STR("/data/system.bin") && native_result;
                startup_save_sequence_failed_ = !startup_initial_system_native_valid_;
            }
            else if (caller_rva == kh3_slot_validation_caller_rva && startup_save_sequence_started_)
            {
                if (prefix != STR("kh3sv2") || !native_result)
                {
                    startup_save_sequence_failed_ = true;
                }
                else if (suffix == STR("/data/system.bin"))
                {
                    startup_final_system_native_valid_ =
                        startup_slot_native_valid_mask_ == startup_validation_slot_mask &&
                        startup_slot_native_valid_count_ == startup_validation_slot_count;
                    startup_save_sequence_failed_ =
                        startup_save_sequence_failed_ || !startup_final_system_native_valid_;
                }
                else
                {
                    constexpr std::array<std::wstring_view, 5> slot_suffixes{
                        STR("/data/slot0.bin"), STR("/data/slot1.bin"), STR("/data/slot2.bin"),
                        STR("/data/slot3.bin"), STR("/data/slot4.bin"),
                    };
                    const auto slot = std::find(slot_suffixes.begin(), slot_suffixes.end(),
                                                std::wstring_view{suffix});
                    if (slot == slot_suffixes.end())
                    {
                        startup_save_sequence_failed_ = true;
                    }
                    else
                    {
                        const uint8_t bit =
                            static_cast<uint8_t>(1u << std::distance(slot_suffixes.begin(), slot));
                        if ((startup_slot_native_valid_mask_ & bit) != 0)
                        {
                            startup_save_sequence_failed_ = true;
                        }
                        else
                        {
                            startup_slot_native_valid_mask_ |= bit;
                            ++startup_slot_native_valid_count_;
                        }
                    }
                }
            }
        }

        const uint64_t count =
            save_integrity_validation_count_.fetch_add(1, std::memory_order_relaxed) + 1;
        if (count > max_save_integrity_log_samples)
        {
            return;
        }

        Output::send<LogLevel::Warning>(
            STR("[KH3APProbeCpp] SaveIntegrityValidation count={} prefix={} suffix={} "
                "caller_rva=0x{:x} native_result={} identity_authority=vanilla_steam "
                "identity_mutation=false\n"),
            count, prefix, suffix, caller_rva, native_result);
    }

    auto Kh3AutosaveProbe::record_save_payload_version_status(uintptr_t caller_rva, int result)
        -> void
    {
        bool guard_armed = false;
        {
            std::scoped_lock lock{startup_save_validation_mutex_};
            if (!startup_save_sequence_started_)
            {
                return;
            }

            if (caller_rva == kh3_initial_system_version_caller_rva)
            {
                startup_initial_system_version_valid_ =
                    startup_initial_system_native_valid_ && result == 1;
                startup_save_sequence_failed_ =
                    startup_save_sequence_failed_ || !startup_initial_system_version_valid_;
            }
            else if (caller_rva == kh3_slot_version_caller_rva)
            {
                if (result != 1 ||
                    startup_slot_version_valid_count_ >= startup_validation_slot_count)
                {
                    startup_save_sequence_failed_ = true;
                }
                else
                {
                    ++startup_slot_version_valid_count_;
                }
            }
            else if (caller_rva == kh3_final_system_version_caller_rva)
            {
                const bool exact_native_sequence =
                    result == 1 && !startup_save_sequence_failed_ &&
                    startup_initial_system_native_valid_ && startup_initial_system_version_valid_ &&
                    startup_slot_native_valid_mask_ == startup_validation_slot_mask &&
                    startup_slot_native_valid_count_ == startup_validation_slot_count &&
                    startup_slot_version_valid_count_ == 5 && startup_final_system_native_valid_;
                startup_prequeued_corrupt_dialog_can_be_retired_ = exact_native_sequence;
                guard_armed = exact_native_sequence;
            }
        }

        if (caller_rva == kh3_final_system_version_caller_rva)
        {
            Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] PrequeuedCorruptDialogGuard "
                                                "armed={} exact_native_sequence={}\n"),
                                            guard_armed, guard_armed);
        }
    }

    auto Kh3AutosaveProbe::try_retire_prequeued_corrupt_dialog_after_native_success(
        uintptr_t caller_rva, const wchar_t* text, const wchar_t* caption, uint32_t type) -> bool
    {
        const std::wstring observed_text = read_bounded_wstring(text);
        const std::wstring observed_caption = read_bounded_wstring(caption);
        {
            std::scoped_lock lock{startup_save_validation_mutex_};
            const bool exact_native_sequence_still_valid =
                startup_prequeued_corrupt_dialog_can_be_retired_ &&
                !startup_save_sequence_failed_ && startup_initial_system_native_valid_ &&
                startup_initial_system_version_valid_ &&
                startup_slot_native_valid_mask_ == startup_validation_slot_mask &&
                startup_slot_native_valid_count_ == startup_validation_slot_count &&
                startup_slot_version_valid_count_ == 5 && startup_final_system_native_valid_;
            const bool exact_corrupt_dialog =
                caller_rva == kh3_corrupt_message_box_caller_rva &&
                observed_caption == STR("KINGDOM HEARTS III") &&
                observed_text.find(STR("Save data is corrupt and will be deleted.")) !=
                    std::wstring::npos &&
                (type & MB_SYSTEMMODAL) != 0;
            if (!exact_native_sequence_still_valid || !exact_corrupt_dialog)
            {
                return false;
            }
            startup_prequeued_corrupt_dialog_can_be_retired_ = false;
        }

        Output::send<LogLevel::Warning>(
            STR("[KH3APProbeCpp] PrequeuedCorruptDialogGuard retired stale dialog after exact "
                "native validation caller_rva=0x{:x} type=0x{:x}\n"),
            caller_rva, type);
        return true;
    }
} // namespace kh3ap
