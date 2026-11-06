#include "kh3ap/diagnostics/autosave/detour.h"
#include "kh3ap/diagnostics/kh3_autosave_probe.h"
#include "kh3ap/diagnostics/autosave/support.h"
#include "kh3ap/core/platform/steam_identity.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <UnrealDef.hpp>

using namespace RC;
using namespace RC::Unreal;

namespace kh3ap
{
    using namespace autosave_detail;
    auto Kh3AutosaveProbe::install_exit_confirmation_bypass() -> void
    {
        if (message_box_w_hook_installed_)
        {
            return;
        }

        message_box_w_iat_slot_ = find_main_module_import_slot("USER32.dll", "MessageBoxW");
        if (message_box_w_iat_slot_ && *message_box_w_iat_slot_)
        {
            message_box_w_trampoline_ = reinterpret_cast<uint64_t>(*message_box_w_iat_slot_);
            s_message_box_w_probe_owner = this;
            message_box_w_hook_installed_ = replace_import_slot_if_matches(
                message_box_w_iat_slot_, reinterpret_cast<void*>(message_box_w_trampoline_),
                reinterpret_cast<void*>(&Kh3AutosaveProbe::hooked_message_box_w));
            if (!message_box_w_hook_installed_)
            {
                message_box_w_trampoline_ = 0;
                message_box_w_iat_slot_ = nullptr;
                s_message_box_w_probe_owner = nullptr;
            }
        }

        Output::send<LogLevel::Verbose>(
            STR("[KH3APProbeCpp] ExitConfirmationBypass message_box_installed={}\n"),
            message_box_w_hook_installed_);
    }

    auto Kh3AutosaveProbe::install_early_steam_save_startup_guard() -> void
    {
        if (save_integrity_key_refresh_hook_attempted_ ||
            save_integrity_key_refresh_hook_installed_)
        {
            return;
        }
        save_integrity_key_refresh_hook_attempted_ = true;

        const std::optional<MainModuleImageRange> main_module_image =
            try_get_main_module_image_range();
        const NativeSaveRequestProfile* const build_profile =
            main_module_image.has_value()
                ? native_save_request_profile_for_image(*main_module_image)
                : nullptr;
        if (!build_profile || build_profile->storefront != STR("steam"))
        {
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] SteamSaveStartupGuard skipped "
                    "reason=unsupported_non_steam_image image_size=0x{:x} "
                    "image_timestamp=0x{:08x}\n"),
                main_module_image.has_value() ? main_module_image->size : 0,
                main_module_image.has_value() ? main_module_image->timestamp : 0);
            return;
        }

        steam_shared_save_path_redirect_enabled_ = true;
        install_save_file_open_probe(STR("steam_save_startup_guard"));

        HMODULE const main_module = GetModuleHandleW(nullptr);
        const uintptr_t main_base = reinterpret_cast<uintptr_t>(main_module);
        const uintptr_t refresh_address =
            main_base != 0 ? main_base + kh3_save_validation_key_refresh_rva : 0;
        const uintptr_t fstring_copy_assign_address =
            main_base != 0 ? main_base + kh3_fstring_copy_assign_rva : 0;
        std::array<std::byte, kh3_save_validation_key_refresh_signature.size()> signature{};
        const bool signature_ok =
            read_process_bytes(refresh_address, signature.data(), signature.size()) &&
            signature == kh3_save_validation_key_refresh_signature;
        std::array<std::byte, kh3_fstring_copy_assign_signature.size()>
            fstring_copy_assign_signature{};
        const bool fstring_copy_assign_signature_ok =
            read_process_bytes(fstring_copy_assign_address, fstring_copy_assign_signature.data(),
                               fstring_copy_assign_signature.size()) &&
            fstring_copy_assign_signature == kh3_fstring_copy_assign_signature;
        steam_save_root_fstring_copy_assign_address_ =
            fstring_copy_assign_signature_ok ? fstring_copy_assign_address : 0;
        if (signature_ok && fstring_copy_assign_signature_ok)
        {
            s_steam_save_startup_guard_owner = this;
            save_integrity_key_refresh_detour_.reset(new PLH::x64Detour(
                refresh_address,
                reinterpret_cast<uint64_t>(&Kh3AutosaveProbe::hooked_save_integrity_key_refresh),
                &save_integrity_key_refresh_trampoline_));
            save_integrity_key_refresh_hook_installed_ = save_integrity_key_refresh_detour_->hook();
            if (!save_integrity_key_refresh_hook_installed_)
            {
                save_integrity_key_refresh_detour_.reset();
                save_integrity_key_refresh_trampoline_ = 0;
                s_steam_save_startup_guard_owner = nullptr;
            }
        }

        const uintptr_t validation_address =
            main_base != 0 ? main_base + kh3_save_validation_rva : 0;
        std::array<std::byte, kh3_save_validation_signature.size()> validation_signature{};
        const bool validation_signature_ok =
            read_process_bytes(validation_address, validation_signature.data(),
                               validation_signature.size()) &&
            validation_signature == kh3_save_validation_signature;
        if (validation_signature_ok)
        {
            s_save_integrity_validation_probe_owner = this;
            save_integrity_validation_detour_.reset(new PLH::x64Detour(
                validation_address,
                reinterpret_cast<uint64_t>(&Kh3AutosaveProbe::hooked_save_integrity_validation),
                &save_integrity_validation_trampoline_));
            save_integrity_validation_hook_installed_ = save_integrity_validation_detour_->hook();
            if (!save_integrity_validation_hook_installed_)
            {
                save_integrity_validation_detour_.reset();
                save_integrity_validation_trampoline_ = 0;
                s_save_integrity_validation_probe_owner = nullptr;
            }
        }

        const uintptr_t version_status_address =
            main_base != 0 ? main_base + kh3_save_payload_version_status_rva : 0;
        std::array<std::byte, kh3_save_payload_version_status_signature.size()>
            version_status_signature{};
        const bool version_status_signature_ok =
            read_process_bytes(version_status_address, version_status_signature.data(),
                               version_status_signature.size()) &&
            version_status_signature == kh3_save_payload_version_status_signature;
        if (version_status_signature_ok)
        {
            s_save_payload_version_status_probe_owner = this;
            save_payload_version_status_detour_.reset(new PLH::x64Detour(
                version_status_address,
                reinterpret_cast<uint64_t>(&Kh3AutosaveProbe::hooked_save_payload_version_status),
                &save_payload_version_status_trampoline_));
            save_payload_version_status_hook_installed_ =
                save_payload_version_status_detour_->hook();
            if (!save_payload_version_status_hook_installed_)
            {
                save_payload_version_status_detour_.reset();
                save_payload_version_status_trampoline_ = 0;
                s_save_payload_version_status_probe_owner = nullptr;
            }
        }

        install_exit_confirmation_bypass();

        Output::send<LogLevel::Warning>(
            STR("[KH3APProbeCpp] SteamSaveStartupGuard early hooks refresh_address={} "
                "refresh_signature_ok={} refresh_installed={} fstring_copy_assign_address={} "
                "fstring_copy_assign_signature_ok={} validation_address={} "
                "validation_signature_ok={} validation_installed={} version_status_address={} "
                "version_status_signature_ok={} version_status_installed={} "
                "message_box_installed={} save_root_redirect_enabled={} "
                "save_root_target_account={} identity_authority=vanilla_steam "
                "identity_mutation=false\n"),
            reinterpret_cast<void*>(refresh_address), signature_ok,
            save_integrity_key_refresh_hook_installed_,
            reinterpret_cast<void*>(fstring_copy_assign_address), fstring_copy_assign_signature_ok,
            reinterpret_cast<void*>(validation_address), validation_signature_ok,
            save_integrity_validation_hook_installed_,
            reinterpret_cast<void*>(version_status_address), version_status_signature_ok,
            save_payload_version_status_hook_installed_, message_box_w_hook_installed_, true,
            kh3_steam_shared_save_account_id);
    }

    auto Kh3AutosaveProbe::uninstall_steam_save_startup_guard() -> void
    {
        if (message_box_w_hook_installed_ && message_box_w_iat_slot_ &&
            message_box_w_trampoline_ != 0)
        {
            replace_import_slot_if_matches(
                message_box_w_iat_slot_,
                reinterpret_cast<void*>(&Kh3AutosaveProbe::hooked_message_box_w),
                reinterpret_cast<void*>(message_box_w_trampoline_));
        }
        message_box_w_hook_installed_ = false;
        message_box_w_iat_slot_ = nullptr;
        message_box_w_trampoline_ = 0;
        if (s_message_box_w_probe_owner == this)
        {
            s_message_box_w_probe_owner = nullptr;
        }

        if (save_payload_version_status_detour_)
        {
            if (save_payload_version_status_hook_installed_)
            {
                save_payload_version_status_detour_->unHook();
            }
            save_payload_version_status_detour_.reset();
        }
        save_payload_version_status_hook_installed_ = false;
        save_payload_version_status_trampoline_ = 0;
        if (s_save_payload_version_status_probe_owner == this)
        {
            s_save_payload_version_status_probe_owner = nullptr;
        }

        if (save_integrity_validation_detour_)
        {
            if (save_integrity_validation_hook_installed_)
            {
                save_integrity_validation_detour_->unHook();
            }
            save_integrity_validation_detour_.reset();
        }
        save_integrity_validation_hook_installed_ = false;
        save_integrity_validation_trampoline_ = 0;
        if (s_save_integrity_validation_probe_owner == this)
        {
            s_save_integrity_validation_probe_owner = nullptr;
        }

        if (save_integrity_key_refresh_detour_)
        {
            if (save_integrity_key_refresh_hook_installed_)
            {
                save_integrity_key_refresh_detour_->unHook();
            }
            save_integrity_key_refresh_detour_.reset();
        }
        save_integrity_key_refresh_hook_installed_ = false;
        save_integrity_key_refresh_trampoline_ = 0;
        steam_save_root_fstring_copy_assign_address_ = 0;
        if (s_steam_save_startup_guard_owner == this)
        {
            s_steam_save_startup_guard_owner = nullptr;
        }
    }
} // namespace kh3ap
