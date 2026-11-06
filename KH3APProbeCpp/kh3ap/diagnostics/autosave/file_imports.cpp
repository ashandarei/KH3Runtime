#include "kh3ap/diagnostics/kh3_autosave_probe.h"
#include "kh3ap/diagnostics/autosave/support.h"
#include "kh3ap/core/platform/steam_identity.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include "kh3ap/bridge/utilities.h"
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string_view>
#include <UnrealDef.hpp>

using namespace RC;
using namespace RC::Unreal;

namespace kh3ap
{
    using namespace autosave_detail;
    auto Kh3AutosaveProbe::install_save_file_open_probe(std::wstring_view reason) -> void
    {
        const bool should_install_create_file = !save_file_open_probe_installed_;
        const bool should_install_close_handle =
            config_.manual_save_close_tracking_enabled && !save_file_close_probe_installed_;
        const bool all_redirect_hooks_installed =
            create_directory_w_redirect_installed_ && delete_file_w_redirect_installed_ &&
            find_first_file_w_redirect_installed_ && get_file_attributes_w_redirect_installed_ &&
            get_file_attributes_ex_w_redirect_installed_ && move_file_w_redirect_installed_ &&
            remove_directory_w_redirect_installed_ && set_file_attributes_w_redirect_installed_;
        const bool should_install_redirect_hooks =
            steam_shared_save_path_redirect_enabled_ && !all_redirect_hooks_installed;
        if (!should_install_create_file && !should_install_close_handle &&
            !should_install_redirect_hooks)
        {
            return;
        }
        save_file_open_probe_attempted_ = true;

        void** const create_file_w_iat_slot =
            should_install_create_file ? find_main_module_import_slot("kernel32.dll", "CreateFileW")
                                       : create_file_w_iat_slot_;
        void* const create_file_w =
            should_install_create_file
                ? (create_file_w_iat_slot ? *create_file_w_iat_slot : nullptr)
                : reinterpret_cast<void*>(create_file_w_trampoline_);
        void** const close_handle_iat_slot =
            should_install_close_handle
                ? find_main_module_import_slot("kernel32.dll", "CloseHandle")
                : close_handle_iat_slot_;
        void* const close_handle = should_install_close_handle
                                       ? (close_handle_iat_slot ? *close_handle_iat_slot : nullptr)
                                       : reinterpret_cast<void*>(close_handle_trampoline_);
        const bool has_target = save_file_open_probe_installed_ || create_file_w != nullptr;
        const bool has_close_target = save_file_close_probe_installed_ || close_handle != nullptr;
        s_save_file_open_probe_owner = this;

        const auto install_redirect_hook = [this](const char* api_name, void* replacement,
                                                  void**& iat_slot, uint64_t& trampoline,
                                                  bool& installed)
        {
            if (installed)
            {
                return;
            }
            iat_slot = find_main_module_import_slot("kernel32.dll", api_name);
            void* const target = iat_slot ? *iat_slot : nullptr;
            if (target)
            {
                trampoline = reinterpret_cast<uint64_t>(target);
                installed = replace_import_slot_if_matches(iat_slot, target, replacement);
            }
            if (!installed)
            {
                iat_slot = nullptr;
                trampoline = 0;
            }
        };

        if (should_install_create_file && create_file_w != nullptr)
        {
            create_file_w_iat_slot_ = create_file_w_iat_slot;
            create_file_w_trampoline_ = reinterpret_cast<uint64_t>(create_file_w);
            save_file_open_probe_installed_ = replace_import_slot_if_matches(
                create_file_w_iat_slot_, create_file_w,
                reinterpret_cast<void*>(&Kh3AutosaveProbe::hooked_create_file_w));
        }
        if (should_install_redirect_hooks)
        {
            install_redirect_hook(
                "CreateDirectoryW",
                reinterpret_cast<void*>(&Kh3AutosaveProbe::hooked_create_directory_w),
                create_directory_w_iat_slot_, create_directory_w_trampoline_,
                create_directory_w_redirect_installed_);
            install_redirect_hook("DeleteFileW",
                                  reinterpret_cast<void*>(&Kh3AutosaveProbe::hooked_delete_file_w),
                                  delete_file_w_iat_slot_, delete_file_w_trampoline_,
                                  delete_file_w_redirect_installed_);
            install_redirect_hook(
                "FindFirstFileW",
                reinterpret_cast<void*>(&Kh3AutosaveProbe::hooked_find_first_file_w),
                find_first_file_w_iat_slot_, find_first_file_w_trampoline_,
                find_first_file_w_redirect_installed_);
            install_redirect_hook(
                "GetFileAttributesW",
                reinterpret_cast<void*>(&Kh3AutosaveProbe::hooked_get_file_attributes_w),
                get_file_attributes_w_iat_slot_, get_file_attributes_w_trampoline_,
                get_file_attributes_w_redirect_installed_);
            install_redirect_hook(
                "GetFileAttributesExW",
                reinterpret_cast<void*>(&Kh3AutosaveProbe::hooked_get_file_attributes_ex_w),
                get_file_attributes_ex_w_iat_slot_, get_file_attributes_ex_w_trampoline_,
                get_file_attributes_ex_w_redirect_installed_);
            install_redirect_hook(
                "MoveFileW", reinterpret_cast<void*>(&Kh3AutosaveProbe::hooked_move_file_w),
                move_file_w_iat_slot_, move_file_w_trampoline_, move_file_w_redirect_installed_);
            install_redirect_hook(
                "RemoveDirectoryW",
                reinterpret_cast<void*>(&Kh3AutosaveProbe::hooked_remove_directory_w),
                remove_directory_w_iat_slot_, remove_directory_w_trampoline_,
                remove_directory_w_redirect_installed_);
            install_redirect_hook(
                "SetFileAttributesW",
                reinterpret_cast<void*>(&Kh3AutosaveProbe::hooked_set_file_attributes_w),
                set_file_attributes_w_iat_slot_, set_file_attributes_w_trampoline_,
                set_file_attributes_w_redirect_installed_);
        }
        if (config_.manual_save_close_tracking_enabled)
        {
            install_redirect_hook(
                "WriteFile", reinterpret_cast<void*>(&Kh3AutosaveProbe::hooked_write_file),
                write_file_iat_slot_, write_file_trampoline_, save_write_probe_installed_);
        }
        if (should_install_close_handle && close_handle != nullptr)
        {
            close_handle_iat_slot_ = close_handle_iat_slot;
            close_handle_trampoline_ = reinterpret_cast<uint64_t>(close_handle);
            save_file_close_probe_installed_ = replace_import_slot_if_matches(
                close_handle_iat_slot_, close_handle,
                reinterpret_cast<void*>(&Kh3AutosaveProbe::hooked_close_handle));
        }

        const std::optional<std::filesystem::path> bridge_dir =
            config_.bridge_dir_resolver ? config_.bridge_dir_resolver() : std::nullopt;
        if (bridge_dir.has_value())
        {
            std::ostringstream stream;
            stream << "{\"type\":\"autosave_probe_file_open_hook\"";
            stream << ",\"observed_at\":\"" << now_iso8601_utc() << "\"";
            json_field(stream, "build_stamp", config_.build_stamp);
            json_field(stream, "function_name", STR("CreateFileW"));
            json_field(stream, "reason", reason);
            json_field(stream, "hook_scope", STR("kh3_main_module_iat"));
            stream << ",\"target_found\":" << (has_target ? "true" : "false");
            stream << ",\"installed\":" << (save_file_open_probe_installed_ ? "true" : "false");
            stream << ",\"target_address\":\"0x" << std::hex
                   << reinterpret_cast<uintptr_t>(create_file_w) << std::dec << "\"";
            stream << "}\n";
            append_text_file(*bridge_dir / std::filesystem::path{installed_hooks_filename},
                             stream.str());

            std::ostringstream close_stream;
            close_stream << "{\"type\":\"autosave_probe_file_close_hook\"";
            close_stream << ",\"observed_at\":\"" << now_iso8601_utc() << "\"";
            json_field(close_stream, "build_stamp", config_.build_stamp);
            json_field(close_stream, "function_name", STR("CloseHandle"));
            json_field(close_stream, "reason", reason);
            json_field(close_stream, "hook_scope", STR("kh3_main_module_iat"));
            close_stream << ",\"enabled\":"
                         << (config_.manual_save_close_tracking_enabled ? "true" : "false");
            close_stream << ",\"target_found\":" << (has_close_target ? "true" : "false");
            close_stream << ",\"installed\":"
                         << (save_file_close_probe_installed_ ? "true" : "false");
            close_stream << ",\"target_address\":\"0x" << std::hex
                         << reinterpret_cast<uintptr_t>(close_handle) << std::dec << "\"";
            close_stream << "}\n";
            append_text_file(*bridge_dir / std::filesystem::path{installed_hooks_filename},
                             close_stream.str());

            std::ostringstream redirect_stream;
            redirect_stream << "{\"type\":\"steam_save_path_redirect_hooks\"";
            redirect_stream << ",\"observed_at\":\"" << now_iso8601_utc() << "\"";
            json_field(redirect_stream, "build_stamp", config_.build_stamp);
            json_field(redirect_stream, "reason", reason);
            json_field(redirect_stream, "target_account", kh3_steam_shared_save_account_id);
            redirect_stream << ",\"enabled\":"
                            << (steam_shared_save_path_redirect_enabled_ ? "true" : "false");
            redirect_stream << ",\"create_directory_w\":"
                            << (create_directory_w_redirect_installed_ ? "true" : "false");
            redirect_stream << ",\"delete_file_w\":"
                            << (delete_file_w_redirect_installed_ ? "true" : "false");
            redirect_stream << ",\"find_first_file_w\":"
                            << (find_first_file_w_redirect_installed_ ? "true" : "false");
            redirect_stream << ",\"get_file_attributes_w\":"
                            << (get_file_attributes_w_redirect_installed_ ? "true" : "false");
            redirect_stream << ",\"get_file_attributes_ex_w\":"
                            << (get_file_attributes_ex_w_redirect_installed_ ? "true" : "false");
            redirect_stream << ",\"move_file_w\":"
                            << (move_file_w_redirect_installed_ ? "true" : "false");
            redirect_stream << ",\"remove_directory_w\":"
                            << (remove_directory_w_redirect_installed_ ? "true" : "false");
            redirect_stream << ",\"set_file_attributes_w\":"
                            << (set_file_attributes_w_redirect_installed_ ? "true" : "false");
            redirect_stream << "}\n";
            append_text_file(*bridge_dir / std::filesystem::path{installed_hooks_filename},
                             redirect_stream.str());
        }

        Output::send<LogLevel::Warning>(
            STR("[KH3APProbeCpp] AutosaveProbe file open hook CreateFileW "
                "scope=kh3_main_module_iat target={} installed={}\n"),
            reinterpret_cast<void*>(create_file_w), save_file_open_probe_installed_);
        Output::send<LogLevel::Warning>(
            STR("[KH3APProbeCpp] AutosaveProbe file close hook CloseHandle "
                "scope=kh3_main_module_iat enabled={} target={} installed={}\n"),
            config_.manual_save_close_tracking_enabled, close_handle,
            save_file_close_probe_installed_);
        Output::send<LogLevel::Warning>(
            STR("[KH3APProbeCpp] SteamSavePathRedirect IAT hooks enabled={} target_account={} "
                "CreateDirectoryW={} DeleteFileW={} FindFirstFileW={} GetFileAttributesW={} "
                "GetFileAttributesExW={} MoveFileW={} RemoveDirectoryW={} SetFileAttributesW={}\n"),
            steam_shared_save_path_redirect_enabled_, kh3_steam_shared_save_account_id,
            create_directory_w_redirect_installed_, delete_file_w_redirect_installed_,
            find_first_file_w_redirect_installed_, get_file_attributes_w_redirect_installed_,
            get_file_attributes_ex_w_redirect_installed_, move_file_w_redirect_installed_,
            remove_directory_w_redirect_installed_, set_file_attributes_w_redirect_installed_);

        if (!save_file_open_probe_installed_)
        {
            create_file_w_iat_slot_ = nullptr;
            create_file_w_trampoline_ = 0;
        }
        if (!save_file_close_probe_installed_)
        {
            close_handle_iat_slot_ = nullptr;
            close_handle_trampoline_ = 0;
        }
        const bool any_redirect_hook_installed =
            create_directory_w_redirect_installed_ || delete_file_w_redirect_installed_ ||
            find_first_file_w_redirect_installed_ || get_file_attributes_w_redirect_installed_ ||
            get_file_attributes_ex_w_redirect_installed_ || move_file_w_redirect_installed_ ||
            remove_directory_w_redirect_installed_ || set_file_attributes_w_redirect_installed_;
        if (!save_file_open_probe_installed_ && !save_file_close_probe_installed_ &&
            !any_redirect_hook_installed && s_save_file_open_probe_owner == this)
        {
            s_save_file_open_probe_owner = nullptr;
        }
    }

    auto Kh3AutosaveProbe::uninstall_save_file_open_probe() -> void
    {
        const auto uninstall_import_hook =
            [](void* replacement, void**& iat_slot, uint64_t& trampoline, bool& installed)
        {
            if (iat_slot && installed && trampoline != 0)
            {
                replace_import_slot_if_matches(iat_slot, replacement,
                                               reinterpret_cast<void*>(trampoline));
            }
            iat_slot = nullptr;
            trampoline = 0;
            installed = false;
        };

        uninstall_import_hook(reinterpret_cast<void*>(&Kh3AutosaveProbe::hooked_create_file_w),
                              create_file_w_iat_slot_, create_file_w_trampoline_,
                              save_file_open_probe_installed_);
        uninstall_import_hook(reinterpret_cast<void*>(&Kh3AutosaveProbe::hooked_create_directory_w),
                              create_directory_w_iat_slot_, create_directory_w_trampoline_,
                              create_directory_w_redirect_installed_);
        uninstall_import_hook(reinterpret_cast<void*>(&Kh3AutosaveProbe::hooked_delete_file_w),
                              delete_file_w_iat_slot_, delete_file_w_trampoline_,
                              delete_file_w_redirect_installed_);
        uninstall_import_hook(reinterpret_cast<void*>(&Kh3AutosaveProbe::hooked_find_first_file_w),
                              find_first_file_w_iat_slot_, find_first_file_w_trampoline_,
                              find_first_file_w_redirect_installed_);
        uninstall_import_hook(
            reinterpret_cast<void*>(&Kh3AutosaveProbe::hooked_get_file_attributes_w),
            get_file_attributes_w_iat_slot_, get_file_attributes_w_trampoline_,
            get_file_attributes_w_redirect_installed_);
        uninstall_import_hook(
            reinterpret_cast<void*>(&Kh3AutosaveProbe::hooked_get_file_attributes_ex_w),
            get_file_attributes_ex_w_iat_slot_, get_file_attributes_ex_w_trampoline_,
            get_file_attributes_ex_w_redirect_installed_);
        uninstall_import_hook(reinterpret_cast<void*>(&Kh3AutosaveProbe::hooked_move_file_w),
                              move_file_w_iat_slot_, move_file_w_trampoline_,
                              move_file_w_redirect_installed_);
        uninstall_import_hook(reinterpret_cast<void*>(&Kh3AutosaveProbe::hooked_remove_directory_w),
                              remove_directory_w_iat_slot_, remove_directory_w_trampoline_,
                              remove_directory_w_redirect_installed_);
        uninstall_import_hook(
            reinterpret_cast<void*>(&Kh3AutosaveProbe::hooked_set_file_attributes_w),
            set_file_attributes_w_iat_slot_, set_file_attributes_w_trampoline_,
            set_file_attributes_w_redirect_installed_);
        uninstall_import_hook(reinterpret_cast<void*>(&Kh3AutosaveProbe::hooked_write_file),
                              write_file_iat_slot_, write_file_trampoline_,
                              save_write_probe_installed_);
        uninstall_import_hook(reinterpret_cast<void*>(&Kh3AutosaveProbe::hooked_close_handle),
                              close_handle_iat_slot_, close_handle_trampoline_,
                              save_file_close_probe_installed_);
        if (s_save_file_open_probe_owner == this)
        {
            s_save_file_open_probe_owner = nullptr;
        }
        steam_shared_save_path_redirect_enabled_ = false;
    }
} // namespace kh3ap
