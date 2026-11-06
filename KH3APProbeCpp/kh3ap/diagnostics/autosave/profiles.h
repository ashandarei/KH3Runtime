#pragma once
#include "kh3ap/core/platform/game_build.h"
#include "kh3ap/bridge/transport/bridge_channels.h"
#include <UnrealDef.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace kh3ap::autosave_detail
{
    inline constexpr std::wstring_view autosave_basic_path =
        STR("/Script/TresGame.TresLevelScriptActor:CreateAutoSaveData");
    inline constexpr std::wstring_view autosave_basic_name = STR("CreateAutoSaveData");
    inline constexpr std::wstring_view autosave_ex_path =
        STR("/Script/TresGame.TresLevelScriptActor:CreateAutoSaveDataEx");
    inline constexpr std::wstring_view autosave_ex_name = STR("CreateAutoSaveDataEx");
    inline constexpr std::wstring_view save_game_to_slot_path =
        STR("/Script/Engine.GameplayStatics:SaveGameToSlot");
    inline constexpr std::wstring_view save_game_to_slot_name = STR("SaveGameToSlot");
    inline constexpr std::wstring_view latest_filename =
        channels::runtime_autosave_probe_event_file;
    inline constexpr std::wstring_view jsonl_filename = STR("runtime_autosave_probe_events.jsonl");
    inline constexpr std::wstring_view latest_breadcrumb_filename =
        STR("runtime_autosave_probe_breadcrumb.json");
    inline constexpr std::wstring_view breadcrumb_jsonl_filename =
        STR("runtime_autosave_probe_breadcrumbs.jsonl");
    inline constexpr std::wstring_view installed_hooks_filename =
        STR("runtime_autosave_probe_installed_hooks.jsonl");
    inline constexpr std::wstring_view save_file_latest_filename =
        channels::runtime_autosave_probe_file_event_file;
    inline constexpr std::wstring_view save_file_jsonl_filename =
        channels::runtime_autosave_probe_file_events_file;
    inline constexpr std::wstring_view native_save_latest_filename =
        channels::runtime_autosave_probe_auto_save_request_event_file;
    inline constexpr std::wstring_view native_save_jsonl_filename =
        channels::runtime_autosave_probe_auto_save_request_events_file;
    inline constexpr std::wstring_view save_completed_latest_filename =
        channels::runtime_autosave_probe_save_completed_event_file;
    inline constexpr std::wstring_view save_completed_jsonl_filename =
        channels::runtime_autosave_probe_save_completed_events_file;
    inline constexpr uint64_t save_game_to_slot_initial_sample_limit = 8;
    inline constexpr uint64_t save_game_to_slot_sample_interval = 120;
    inline constexpr uintptr_t kh3_save_validation_owner_singleton_rva = 0x86ddad0;
    inline constexpr uintptr_t kh3_save_validation_key_refresh_rva = 0x14f1870;
    inline constexpr uintptr_t kh3_fstring_copy_assign_rva = 0x45eff0;
    inline constexpr uintptr_t kh3_save_validation_rva = 0x14f5aa0;
    inline constexpr uintptr_t kh3_save_payload_version_status_rva = 0x14f2810;
    inline constexpr uintptr_t kh3_initial_system_validation_caller_rva = 0x14df6f1;
    inline constexpr uintptr_t kh3_initial_system_version_caller_rva = 0x14df7c9;
    inline constexpr uintptr_t kh3_slot_validation_caller_rva = 0x14e1438;
    inline constexpr uintptr_t kh3_slot_version_caller_rva = 0x14e1590;
    inline constexpr uintptr_t kh3_final_system_version_caller_rva = 0x14e14c4;
    inline constexpr uintptr_t kh3_corrupt_message_box_caller_rva = 0x158db73;
    // Storefront-specific entries to KH3's automatic-save request. These
    // run before the slot-0 payload is allocated/serialized and before
    // async file I/O. Each address is guarded by the exact supported PE
    // image and a unique function-entry signature.
    inline constexpr uintptr_t kh3_egs_native_save_request_rva = 0x18362c0;
    inline constexpr uintptr_t kh3_steam_native_save_request_rva = 0x14d5820;
    inline constexpr std::array<std::byte, 16> kh3_save_validation_key_refresh_signature{
        std::byte{0x48}, std::byte{0x8b}, std::byte{0xc4}, std::byte{0x57},
        std::byte{0x41}, std::byte{0x56}, std::byte{0x41}, std::byte{0x57},
        std::byte{0x48}, std::byte{0x83}, std::byte{0xec}, std::byte{0x50},
        std::byte{0x48}, std::byte{0xc7}, std::byte{0x40}, std::byte{0xb8},
    };
    inline constexpr std::array<std::byte, 16> kh3_fstring_copy_assign_signature{
        std::byte{0x40}, std::byte{0x53}, std::byte{0x48}, std::byte{0x83},
        std::byte{0xec}, std::byte{0x20}, std::byte{0x48}, std::byte{0x8b},
        std::byte{0xd9}, std::byte{0x48}, std::byte{0x3b}, std::byte{0xca},
        std::byte{0x74}, std::byte{0x59}, std::byte{0x44}, std::byte{0x8b},
    };
    inline constexpr std::array<std::byte, 16> kh3_save_validation_signature{
        std::byte{0x40}, std::byte{0x55}, std::byte{0x53}, std::byte{0x56},
        std::byte{0x57}, std::byte{0x41}, std::byte{0x54}, std::byte{0x41},
        std::byte{0x55}, std::byte{0x41}, std::byte{0x56}, std::byte{0x41},
        std::byte{0x57}, std::byte{0x48}, std::byte{0x8d}, std::byte{0xac},
    };
    inline constexpr std::array<std::byte, 16> kh3_save_payload_version_status_signature{
        std::byte{0x4c}, std::byte{0x8b}, std::byte{0x11}, std::byte{0x4c},
        std::byte{0x8d}, std::byte{0x05}, std::byte{0x16}, std::byte{0xb1},
        std::byte{0x1e}, std::byte{0x07}, std::byte{0x45}, std::byte{0x33},
        std::byte{0xc9}, std::byte{0x41}, std::byte{0x8b}, std::byte{0xc9},
    };
    inline constexpr std::array<std::byte, 16> kh3_egs_native_save_request_signature{
        std::byte{0x48}, std::byte{0x8b}, std::byte{0xc4}, std::byte{0x48},
        std::byte{0x89}, std::byte{0x48}, std::byte{0x08}, std::byte{0x55},
        std::byte{0x41}, std::byte{0x54}, std::byte{0x41}, std::byte{0x55},
        std::byte{0x41}, std::byte{0x56}, std::byte{0x41}, std::byte{0x57},
    };
    inline constexpr std::array<std::byte, 38> kh3_steam_native_save_request_signature{
        std::byte{0x40}, std::byte{0x55}, std::byte{0x56}, std::byte{0x57}, std::byte{0x41},
        std::byte{0x54}, std::byte{0x41}, std::byte{0x55}, std::byte{0x41}, std::byte{0x56},
        std::byte{0x41}, std::byte{0x57}, std::byte{0x48}, std::byte{0x8b}, std::byte{0xec},
        std::byte{0x48}, std::byte{0x83}, std::byte{0xec}, std::byte{0x40}, std::byte{0x48},
        std::byte{0xc7}, std::byte{0x45}, std::byte{0xe0}, std::byte{0xfe}, std::byte{0xff},
        std::byte{0xff}, std::byte{0xff}, std::byte{0x48}, std::byte{0x89}, std::byte{0x9c},
        std::byte{0x24}, std::byte{0x90}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x48}, std::byte{0x8b}, std::byte{0xf1},
    };

    struct NativeSaveRequestProfile
    {
        std::wstring_view storefront{};
        size_t image_size{};
        uint32_t image_timestamp{};
        uintptr_t request_rva{};
        const std::byte* signature{};
        size_t signature_size{};
    };

    inline constexpr std::array<NativeSaveRequestProfile, 2> kh3_native_save_request_profiles{{
        {
            STR("steam"),
            kh3ap::native_build::steam.image_size,
            kh3ap::native_build::steam.timestamp,
            kh3_steam_native_save_request_rva,
            kh3_steam_native_save_request_signature.data(),
            kh3_steam_native_save_request_signature.size(),
        },
        {
            STR("egs"),
            kh3ap::native_build::egs.image_size,
            kh3ap::native_build::egs.timestamp,
            kh3_egs_native_save_request_rva,
            kh3_egs_native_save_request_signature.data(),
            kh3_egs_native_save_request_signature.size(),
        },
    }};
} // namespace kh3ap::autosave_detail
