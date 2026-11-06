#include "kh3ap/core/platform/game_build.h"
#include "kh3ap/features/ui/ui_sound_service.h"

#include "kh3ap/features/grants/grant_internal_call_depth.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/core/memory/memory_protection_bits.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <UnrealDef.hpp>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

using namespace RC;
using namespace RC::Unreal;

namespace kh3ap
{
    namespace
    {
        struct RuntimeUiSoundDefinition
        {
            std::wstring_view property_name{};
            std::wstring_view object_path{};
            std::wstring_view object_name{};
        };

        constexpr RuntimeUiSoundDefinition s_cursor_sound{
                STR("Click"),
                STR("/Game/Sound/SE/common/00000/se00000_001.se00000_001"),
                STR("se00000_001"),
        };
        constexpr RuntimeUiSoundDefinition s_decide_sound{
                STR("Decide"),
                STR("/Game/Sound/SE/common/00000/se00000_002.se00000_002"),
                STR("se00000_002"),
        };
        constexpr RuntimeUiSoundDefinition s_cancel_sound{
                STR("Cancel"),
                STR("/Game/Sound/SE/common/00000/se00000_003.se00000_003"),
                STR("se00000_003"),
        };

        auto sound_definition(RuntimeUiSound sound)
                -> const RuntimeUiSoundDefinition&
        {
            if (sound == RuntimeUiSound::cursor) return s_cursor_sound;
            return sound == RuntimeUiSound::decide
                           ? s_decide_sound
                           : s_cancel_sound;
        }

        using NativePlayUiSound = UObject* (*)(UObject* ui_part, UObject* sound);

        struct NativeUiSoundProfile
        {
            std::wstring_view storefront{};
            size_t image_size{};
            uint32_t timestamp{};
            uintptr_t play_sound_rva{};
            const uint8_t* signature{};
            size_t signature_size{};
        };

        // UTresUIParts' native sound overload. Stock KH3 menus call this with
        // the active UI part and their resolved USoundBase; it creates and
        // plays the component through UTresUIManager's UI-audio path.
        // clang-format off: retain native signature byte grouping
        constexpr std::array<uint8_t, 50> s_egs_play_ui_sound_signature{
                0x48, 0x83, 0xEC, 0x58, 0x48, 0x85, 0xD2, 0x0F, 0x84, 0x95,
                0x00, 0x00, 0x00, 0x4C, 0x8B, 0x49, 0x70, 0x4D, 0x85, 0xC9,
                0x0F, 0x84, 0x88, 0x00, 0x00, 0x00, 0x8B, 0x89, 0xAC, 0x00,
                0x00, 0x00, 0x41, 0xB8, 0x01, 0x00, 0x00, 0x00, 0x41, 0xD3,
                0xE0, 0x33, 0xC0, 0x49, 0x8D, 0x89, 0x58, 0x01, 0x00, 0x00,
        };
        // clang-format on
        // clang-format off: retain native signature byte grouping
        constexpr std::array<uint8_t, 50> s_steam_play_ui_sound_signature{
                0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x50,
                0x48, 0x8B, 0xFA, 0x48, 0x8B, 0xD9, 0x48, 0x85, 0xD2, 0x74,
                0x68, 0x48, 0x8B, 0x49, 0x70, 0x48, 0x85, 0xC9, 0x74, 0x5F,
                0x8B, 0x93, 0xAC, 0x00, 0x00, 0x00, 0x41, 0xB8, 0x05, 0x00,
                0x00, 0x00, 0xE8, 0x31, 0x9D, 0xE2, 0xFF, 0x84, 0xC0, 0x74,
        };
        // clang-format on

        constexpr std::array<NativeUiSoundProfile, 2> s_native_profiles{{
                {
                        STR("egs"),
                        kh3ap::native_build::egs.image_size,
                        kh3ap::native_build::egs.timestamp,
                        0x1AF4910,
                        s_egs_play_ui_sound_signature.data(),
                        s_egs_play_ui_sound_signature.size(),
                },
                {
                        STR("steam"),
                        kh3ap::native_build::steam.image_size,
                        kh3ap::native_build::steam.timestamp,
                        0x176F620,
                        s_steam_play_ui_sound_signature.data(),
                        s_steam_play_ui_sound_signature.size(),
                },
        }};

        struct NativeUiSoundDispatcherResolution
        {
            NativePlayUiSound function{};
            std::wstring_view storefront{};
            std::wstring_view failure_stage{STR("unresolved")};
        };

        auto address_is_executable(uintptr_t address) -> bool
        {
            MEMORY_BASIC_INFORMATION memory_info{};
            if (address == 0 ||
                VirtualQuery(
                        reinterpret_cast<const void*>(address),
                        &memory_info,
                        sizeof(memory_info)) != sizeof(memory_info))
            {
                return false;
            }
            if (memory_info.State != MEM_COMMIT ||
                (memory_info.Protect & PAGE_GUARD) != 0)
            {
                return false;
            }
            const DWORD protection = memory_info.Protect & memory_protection::base_mode_mask;
            return protection == PAGE_EXECUTE ||
                   protection == PAGE_EXECUTE_READ ||
                   protection == PAGE_EXECUTE_READWRITE ||
                   protection == PAGE_EXECUTE_WRITECOPY;
        }

        auto native_ui_sound_dispatcher()
                -> const NativeUiSoundDispatcherResolution&
        {
            static const NativeUiSoundDispatcherResolution resolution = []
            {
                NativeUiSoundDispatcherResolution result{};
                HMODULE const module = GetModuleHandleW(nullptr);
                if (!module)
                {
                    result.failure_stage = STR("game_module");
                    return result;
                }

                const auto* const base =
                        reinterpret_cast<const std::byte*>(module);
                const auto* const dos =
                        reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
                if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0)
                {
                    result.failure_stage = STR("dos_header");
                    return result;
                }
                const auto* const nt =
                        reinterpret_cast<const IMAGE_NT_HEADERS64*>(
                                base + dos->e_lfanew);
                if (nt->Signature != IMAGE_NT_SIGNATURE ||
                    nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
                {
                    result.failure_stage = STR("nt_header");
                    return result;
                }

                const NativeUiSoundProfile* profile = nullptr;
                for (const NativeUiSoundProfile& candidate : s_native_profiles)
                {
                    if (candidate.image_size ==
                                nt->OptionalHeader.SizeOfImage &&
                        candidate.timestamp == nt->FileHeader.TimeDateStamp)
                    {
                        profile = &candidate;
                        break;
                    }
                }
                if (!profile)
                {
                    result.failure_stage = STR("unsupported_image");
                    return result;
                }

                const uintptr_t target =
                        reinterpret_cast<uintptr_t>(module) +
                        profile->play_sound_rva;
                if (!address_is_executable(target))
                {
                    result.failure_stage = STR("target_memory");
                    return result;
                }
                if (!profile->signature || profile->signature_size == 0 ||
                    std::memcmp(
                            reinterpret_cast<const void*>(target),
                            profile->signature,
                            profile->signature_size) != 0)
                {
                    result.failure_stage = STR("signature");
                    return result;
                }

                result.function = reinterpret_cast<NativePlayUiSound>(target);
                result.storefront = profile->storefront;
                result.failure_stage = STR("none");
                return result;
            }();
            return resolution;
        }

        auto try_play_native_stock_ui_sound(
                UObject* ui_part,
                UObject* sound,
                std::wstring_view& failure_stage) -> bool
        {
            failure_stage = STR("invalid_input");
            if (!ui_part || !sound ||
                !kh3ap::is_uobject_valid_fast(ui_part) ||
                !kh3ap::is_uobject_valid_fast(sound))
            {
                return false;
            }

            // The native function reads m_pUIManager directly from the UI
            // part. Validate the reflected pointer before native field access
            // during menu transitions.
            failure_stage = STR("ui_manager");
            UObject* const ui_manager =
                    try_read_reflected_object_property_value(
                            ui_part,
                            STR("m_pUIManager"));
            if (!ui_manager || !kh3ap::is_uobject_valid_fast(ui_manager))
            {
                return false;
            }

            const NativeUiSoundDispatcherResolution& dispatcher =
                    native_ui_sound_dispatcher();
            if (!dispatcher.function)
            {
                failure_stage = dispatcher.failure_stage;
                return false;
            }

            failure_stage = STR("native_return");
            UObject* const audio_component =
                    dispatcher.function(ui_part, sound);
            if (!audio_component)
            {
                return false;
            }
            failure_stage = STR("none");
            return true;
        }
    }

    auto RuntimeUiSoundService::try_load_asset_from_registry(
            std::wstring_view object_path) -> UObject*
    {
        UFunction* const get_registry =
                UObjectGlobals::StaticFindObject<UFunction*>(
                        nullptr,
                        nullptr,
                        STR("/Script/AssetRegistry.AssetRegistryHelpers:GetAssetRegistry"));
        UObject* const registry_helpers =
                try_get_function_default_object(get_registry);
        if (!get_registry || !registry_helpers ||
            !kh3ap::is_uobject_valid_fast(get_registry) ||
            !kh3ap::is_uobject_valid_fast(registry_helpers) ||
            get_registry->GetParmsSize() <= 0)
        {
            return nullptr;
        }

        std::vector<std::byte> registry_params(
                static_cast<size_t>(get_registry->GetParmsSize()),
                std::byte{0});
        {
            const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
            registry_helpers->ProcessEvent(
                    get_registry,
                    registry_params.data());
        }
        UObject* const registry = try_read_function_object_param(
                get_registry,
                registry_params.data(),
                STR("ReturnValue"));
        if (!registry || !kh3ap::is_uobject_valid_fast(registry))
        {
            return nullptr;
        }

        UFunction* const get_asset_data =
                UObjectGlobals::StaticFindObject<UFunction*>(
                        nullptr,
                        nullptr,
                        STR("/Script/AssetRegistry.AssetRegistry:GetAssetByObjectPath"));
        if (!get_asset_data ||
            !kh3ap::is_uobject_valid_fast(get_asset_data) ||
            get_asset_data->GetParmsSize() <= 0)
        {
            return nullptr;
        }
        std::vector<std::byte> asset_data_params(
                static_cast<size_t>(get_asset_data->GetParmsSize()),
                std::byte{0});
        FProperty* const object_path_property =
                find_function_param_by_name(
                        get_asset_data,
                        STR("ObjectPath"));
        if (!object_path_property ||
            !write_name_or_string_function_param(
                    object_path_property,
                    asset_data_params.data(),
                    object_path))
        {
            return nullptr;
        }
        {
            const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
            registry->ProcessEvent(get_asset_data, asset_data_params.data());
        }
        FProperty* const asset_data_return =
                find_function_param_by_name(
                        get_asset_data,
                        STR("ReturnValue"));
        void* const asset_data_value =
                asset_data_return
                        ? asset_data_return->ContainerPtrToValuePtr<void>(
                                  asset_data_params.data())
                        : nullptr;
        const int asset_data_size =
                asset_data_return ? asset_data_return->GetElementSize() : 0;
        if (!asset_data_value || asset_data_size <= 0)
        {
            return nullptr;
        }

        UFunction* const get_asset =
                UObjectGlobals::StaticFindObject<UFunction*>(
                        nullptr,
                        nullptr,
                        STR("/Script/AssetRegistry.AssetRegistryHelpers:GetAsset"));
        UObject* const get_asset_self =
                try_get_function_default_object(get_asset);
        if (!get_asset || !get_asset_self ||
            !kh3ap::is_uobject_valid_fast(get_asset) ||
            !kh3ap::is_uobject_valid_fast(get_asset_self) ||
            get_asset->GetParmsSize() <= 0)
        {
            return nullptr;
        }
        std::vector<std::byte> get_asset_params(
                static_cast<size_t>(get_asset->GetParmsSize()),
                std::byte{0});
        FProperty* const asset_data_input =
                find_function_param_by_name(get_asset, STR("AssetData"));
        void* const asset_data_input_value =
                asset_data_input
                        ? asset_data_input->ContainerPtrToValuePtr<void>(
                                  get_asset_params.data())
                        : nullptr;
        if (!asset_data_input_value ||
            asset_data_input->GetElementSize() != asset_data_size)
        {
            return nullptr;
        }
        std::memcpy(
                asset_data_input_value,
                asset_data_value,
                static_cast<size_t>(asset_data_size));
        {
            const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
            get_asset_self->ProcessEvent(get_asset, get_asset_params.data());
        }
        UObject* const loaded = try_read_function_object_param(
                get_asset,
                get_asset_params.data(),
                STR("ReturnValue"));
        return loaded && kh3ap::is_uobject_valid_fast(loaded)
                       ? loaded
                       : nullptr;
    }

    auto RuntimeUiSoundService::try_resolve_common_sound(RuntimeUiSound sound)
            -> UObject*
    {
        const RuntimeUiSoundDefinition& definition = sound_definition(sound);
        FWeakObjectPtr& cache =
                sound == RuntimeUiSound::cursor ? cursor_sound_ :
                sound == RuntimeUiSound::decide ? decide_sound_ : cancel_sound_;
        if (UObject* const cached = cache.Get())
        {
            return cached;
        }

        UObject* resolved = UObjectGlobals::StaticFindObject<UObject*>(
                nullptr,
                nullptr,
                std::wstring{definition.object_path}.c_str());
        if (!resolved || !kh3ap::is_uobject_valid_fast(resolved))
        {
            std::vector<UObject*> loaded_sounds{};
            UObjectGlobals::FindAllOf(STR("SQEXSEADSound"), loaded_sounds);
            for (UObject* const candidate : loaded_sounds)
            {
                if (candidate && kh3ap::is_uobject_valid_fast(candidate) &&
                    candidate->GetName() == definition.object_name)
                {
                    resolved = candidate;
                    break;
                }
            }
        }
        if (!resolved || !kh3ap::is_uobject_valid_fast(resolved))
        {
            resolved = try_load_asset_from_registry(definition.object_path);
        }
        if (!resolved || !kh3ap::is_uobject_valid_fast(resolved))
        {
            return nullptr;
        }
        cache = resolved;
        return resolved;
    }

    auto RuntimeUiSoundService::try_resolve_active_sound(
            UObject* ui_part,
            std::wstring_view property_name) -> UObject*
    {
        if (!ui_part || !kh3ap::is_uobject_valid_fast(ui_part))
        {
            return nullptr;
        }
        UObject* const ui_manager =
                try_read_reflected_object_property_value(
                        ui_part,
                        STR("m_pUIManager"));
        UObject* const sound_data_set =
                ui_manager && kh3ap::is_uobject_valid_fast(ui_manager)
                        ? try_read_reflected_object_property_value(
                                  ui_manager,
                                  STR("m_OverrideSystemSEDataSet"))
                        : nullptr;
        UObject* const sound =
                sound_data_set &&
                                kh3ap::is_uobject_valid_fast(sound_data_set)
                        ? try_read_reflected_object_property_value(
                                  sound_data_set,
                                  property_name)
                        : nullptr;
        return sound && kh3ap::is_uobject_valid_fast(sound)
                       ? sound
                       : nullptr;
    }

    auto RuntimeUiSoundService::play(
            UObject* ui_part,
            RuntimeUiSound requested_sound) -> bool
    {
        const RuntimeUiSoundDefinition& definition =
                sound_definition(requested_sound);
        UObject* sound =
                try_resolve_active_sound(ui_part, definition.property_name);
        const bool used_active_override = sound != nullptr;
        if (!sound)
        {
            sound = try_resolve_common_sound(requested_sound);
        }
        if (!sound || !kh3ap::is_uobject_valid_fast(sound))
        {
            if (!failure_logged_)
            {
                failure_logged_ = true;
                Output::send<LogLevel::Error>(
                        STR("[KH3APProbeCpp] Runtime UI sound unavailable "
                            "property={} asset={} context={}\n"),
                        definition.property_name,
                        definition.object_path,
                        format_object_debug_name(ui_part));
            }
            return false;
        }

        std::wstring_view failure_stage{};
        if (!try_play_native_stock_ui_sound(
                    ui_part,
                    sound,
                    failure_stage))
        {
            if (!failure_logged_)
            {
                failure_logged_ = true;
                Output::send<LogLevel::Error>(
                        STR("[KH3APProbeCpp] Runtime UI sound playback failed "
                            "stage={} property={} sound={} context={}\n"),
                        failure_stage,
                        definition.property_name,
                        format_object_debug_name(sound),
                        format_object_debug_name(ui_part));
            }
            return false;
        }

        bool& route_logged = route_logged_[static_cast<size_t>(requested_sound)];
        if (!route_logged)
        {
            route_logged = true;
            const NativeUiSoundDispatcherResolution& dispatcher =
                    native_ui_sound_dispatcher();
            Output::send<LogLevel::Normal>(
                    STR("[KH3APProbeCpp] Runtime UI stock sound restored "
                        "route=kh3_native_ui_dispatcher pause_safe=true "
                        "feedback=stock profile={} source={} property={} "
                        "sound={}\n"),
                    dispatcher.storefront,
                    used_active_override
                            ? STR("override_system_se_dataset")
                            : STR("common_fallback"),
                    definition.property_name,
                    format_object_debug_name(sound));
        }
        return true;
    }
}
