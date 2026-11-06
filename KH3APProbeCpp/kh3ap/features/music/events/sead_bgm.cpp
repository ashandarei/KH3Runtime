#include <array>
#include <optional>
#include "kh3ap/features/music/events/sead_bgm.h"

#include "kh3ap/features/grants/grant_internal_call_depth.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_validity.h"

#include <Unreal/FProperty.hpp>
#include <Unreal/FString.hpp>
#include <Unreal/NameTypes.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#include <Unreal/Property/FStructProperty.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UScriptStruct.hpp>
#include <UnrealDef.hpp>
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
#include <DynamicOutput/DynamicOutput.hpp>
#include <chrono>
#endif

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <cwctype>
#include <unordered_map>
#include <utility>

using namespace RC;
using namespace RC::Unreal;

namespace kh3ap::sead_bgm
{
    namespace
    {
        constexpr auto bgm_volume_gain_offset = sizeof(UObject*);
        constexpr auto all_volume_parameter_bytes = sizeof(float) + sizeof(float);
        constexpr auto bgm_volume_parameter_bytes = 16;

        // KH3's UE4.17 TAssetPtr (AssetObjectProperty) is
        // {FWeakObjectPtr, int32 TagAtLastTest, pad, FString path}: the path
        // sits at +0x10.
        constexpr size_t legacy_asset_path_offset = 0x10;

        struct ScriptArrayHeader
        {
            void* data;
            int32_t num;
            int32_t max;
        };

        // Arrays owned by the BGM manager hold nine slots; anything past this
        // is a misread.
        constexpr int32_t max_manager_array = 64;

        auto property_of(UObject* object, const wchar_t* name, std::wstring_view expected_class)
            -> std::pair<FProperty*, void*>
        {
            if (!object || !is_uobject_valid_fast(object))
            {
                return {nullptr, nullptr};
            }
            FProperty* const property = object->GetPropertyByNameInChain(name);
            void* const value = object->GetValuePtrByPropertyNameInChain(name);
            if (!property || !value || property->GetClass().GetName() != expected_class)
            {
                return {nullptr, nullptr};
            }
            return {property, value};
        }

        auto asset_property_fits(FProperty* property) -> bool
        {
            return property->GetElementSize() >=
                   static_cast<int>(legacy_asset_path_offset + sizeof(FString));
        }

        auto read_object_array(UObject* object, const wchar_t* name) -> std::vector<UObject*>
        {
            const auto [property, value] = property_of(object, name, STR("ArrayProperty"));
            if (!property)
            {
                return {};
            }
            FProperty* const inner = static_cast<FArrayProperty*>(property)->GetInner();
            if (!inner || inner->GetClass().GetName() != STR("ObjectProperty"))
            {
                return {};
            }
            const auto* const header = static_cast<const ScriptArrayHeader*>(value);
            if (!header->data || header->num <= 0 || header->num > max_manager_array ||
                header->max < header->num)
            {
                return {};
            }
            std::vector<UObject*> result{};
            const auto* const items = static_cast<UObject* const*>(header->data);
            for (int32_t index = 0; index < header->num; ++index)
            {
                UObject* const item = items[index];
                result.push_back(item && is_uobject_valid_fast(item) ? item : nullptr);
            }
            return result;
        }

        // FSQEXSEAD_BGMSlotSetting::SlotName for each SlotSettings_ entry.
        auto read_slot_setting_names(UObject* manager) -> std::vector<std::wstring>
        {
            const auto [property, value] =
                property_of(manager, STR("SlotSettings_"), STR("ArrayProperty"));
            if (!property)
            {
                return {};
            }
            FProperty* const inner = static_cast<FArrayProperty*>(property)->GetInner();
            if (!inner || inner->GetClass().GetName() != STR("StructProperty"))
            {
                return {};
            }
            FProperty* const slot_name = find_struct_field_by_name(
                static_cast<FStructProperty*>(inner)->GetStruct(), STR("SlotName"));
            const auto* const header = static_cast<const ScriptArrayHeader*>(value);
            const int stride = inner->GetElementSize();
            if (!slot_name || slot_name->GetClass().GetName() != STR("NameProperty") ||
                !header->data || header->num <= 0 || header->num > max_manager_array || stride <= 0)
            {
                return {};
            }
            std::vector<std::wstring> result{};
            for (int32_t index = 0; index < header->num; ++index)
            {
                auto* const element =
                    static_cast<std::byte*>(header->data) + static_cast<size_t>(index) * stride;
                result.push_back(reinterpret_cast<FName*>(element + slot_name->GetOffset_Internal())
                                     ->ToString());
            }
            return result;
        }

        auto slot_of(UObject* controller) -> UObject*
        {
            return controller ? try_read_object_property_object(controller, STR("Slot_")) : nullptr;
        }
    } // namespace

    auto find_live_objects(const wchar_t* class_name) -> std::vector<UObject*>
    {
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        const auto started = std::chrono::steady_clock::now();
#endif
        std::vector<UObject*> found{};
        UObjectGlobals::FindAllOf(class_name, found);
        std::erase_if(found,
                      [](UObject* object)
                      {
                          return !object || !is_uobject_valid_fast(object) ||
                                 object->GetName().starts_with(STR("Default__"));
                      });
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        Output::send<LogLevel::Warning>(
            STR("[KH3APProbeCpp] Music object scan class={} found={} elapsed_us={}\n"), class_name,
            found.size(),
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() -
                                                                  started)
                .count());
#endif
        return found;
    }

    auto still_live(UObject* object, std::wstring_view class_name) -> bool
    {
        return object && is_uobject_valid_fast(object) &&
               object->GetClassPrivate()->GetName() == class_name;
    }

    auto read_asset_path(UObject* object, const wchar_t* property_name)
        -> std::optional<std::wstring>
    {
        const auto [property, value] =
            property_of(object, property_name, STR("AssetObjectProperty"));
        if (!property || !asset_property_fits(property))
        {
            return std::nullopt;
        }
        const auto* const path = reinterpret_cast<const FString*>(
            static_cast<const std::byte*>(value) + legacy_asset_path_offset);
        const wchar_t* const chars = path->GetCharArray();
        return std::wstring{chars ? chars : STR("")};
    }

    auto write_asset_path(UObject* object, const wchar_t* property_name, std::wstring_view path)
        -> bool
    {
        const auto [property, value] =
            property_of(object, property_name, STR("AssetObjectProperty"));
        if (!property || !asset_property_fits(property))
        {
            return false;
        }
        auto* const bytes = static_cast<std::byte*>(value);
        // FWeakObjectPtr{ObjectIndex = INDEX_NONE, SerialNumber = 0}, then
        // TagAtLastTest = 0, so the reference no longer resolves to the object
        // it pointed at before.
        constexpr int32_t cleared[3]{-1, 0, 0};
        std::memcpy(bytes, cleared, sizeof(cleared));
        *reinterpret_cast<FString*>(bytes + legacy_asset_path_offset) =
            FString(std::wstring{path}.c_str());
        return true;
    }

    namespace
    {
        auto set_path_argument(UFunction* function, void* params) -> void*
        {
            if (!params || !is_uobject_valid_fast(function))
            {
                return nullptr;
            }
            FProperty* const property =
                find_function_param_by_name(function, STR("SoundReference"));
            if (!property || property->GetClass().GetName() != STR("AssetObjectProperty") ||
                !asset_property_fits(property))
            {
                return nullptr;
            }
            return property->ContainerPtrToValuePtr<void>(params);
        }
    } // namespace

    auto read_set_path_argument(UFunction* function, void* params) -> std::optional<std::wstring>
    {
        const auto* value = static_cast<const std::byte*>(set_path_argument(function, params));
        if (!value)
        {
            return std::nullopt;
        }
        const auto* path = reinterpret_cast<const FString*>(value + legacy_asset_path_offset);
        const auto* chars = path->GetCharArray();
        return std::wstring{chars ? chars : STR("")};
    }

    auto write_set_path_argument(UFunction* function, void* params, std::wstring_view path) -> bool
    {
        auto* value = static_cast<std::byte*>(set_path_argument(function, params));
        if (!value)
        {
            return false;
        }
        constexpr int32_t cleared[3]{-1, 0, 0};
        std::memcpy(value, cleared, sizeof(cleared));
        *reinterpret_cast<FString*>(value + legacy_asset_path_offset) =
            FString(std::wstring{path}.c_str());
        return true;
    }

    auto slot_controllers(UObject* manager) -> std::vector<SlotController>
    {
        if (!manager || !is_uobject_valid_fast(manager))
        {
            return {};
        }
        const std::vector<std::wstring> names = read_slot_setting_names(manager);
        const std::vector<UObject*> slots = read_object_array(manager, STR("Slots_"));
        const std::vector<UObject*> controllers = read_object_array(manager, STR("Controllers_"));
        std::vector<UObject*> controller_slots{};
        for (UObject* const controller : controllers)
        {
            controller_slots.push_back(slot_of(controller));
        }
        std::vector<SlotController> result{};
        for (size_t index = 0; index < names.size() && index < slots.size(); ++index)
        {
            SlotController entry{names[index], nullptr};
            for (size_t candidate = 0; slots[index] && candidate < controllers.size(); ++candidate)
            {
                if (controllers[candidate] && controller_slots[candidate] == slots[index])
                {
                    entry.controller = controllers[candidate];
                    break;
                }
            }
            result.push_back(std::move(entry));
        }
        return result;
    }

    auto controller_for_slot(UObject* manager, std::wstring_view slot_name) -> UObject*
    {
        if (!manager || !is_uobject_valid_fast(manager))
        {
            return nullptr;
        }
        const std::vector<std::wstring> names = read_slot_setting_names(manager);
        const std::vector<UObject*> slots = read_object_array(manager, STR("Slots_"));
        UObject* slot{};
        // FNames compare without case.
        const auto same_name = [slot_name](std::wstring_view name)
        {
            return name.size() == slot_name.size() &&
                   std::equal(name.begin(), name.end(), slot_name.begin(),
                              [](wchar_t a, wchar_t b)
                              {
                                  return std::towlower(a) == std::towlower(b);
                              });
        };
        for (size_t index = 0; index < names.size() && index < slots.size(); ++index)
        {
            if (same_name(names[index]))
            {
                slot = slots[index];
                break;
            }
        }
        if (!slot)
        {
            return nullptr;
        }
        for (UObject* const controller : read_object_array(manager, STR("Controllers_")))
        {
            if (controller && slot_of(controller) == slot)
            {
                return controller;
            }
        }
        return nullptr;
    }

    auto current_track(UObject* controller) -> std::wstring
    {
        UObject* const slot = slot_of(controller);
        // BGM_ is a plain ObjectProperty in the shipped game, not the
        // AssetObjectProperty the modding kit headers declare.
        UObject* const bgm = slot ? try_read_object_property_object(slot, STR("BGM_")) : nullptr;
        UObject* const sound = bgm ? try_read_object_property_object(bgm, STR("Sound_")) : nullptr;
        return sound && is_uobject_valid_fast(sound) ? sound->GetPathName() : std::wstring{};
    }

    auto slot_state(UObject* controller) -> std::optional<uint8_t>
    {
        UObject* const slot = slot_of(controller);
        if (!slot || !is_uobject_valid_fast(slot))
        {
            return std::nullopt;
        }
        FProperty* const property = slot->GetPropertyByNameInChain(STR("State"));
        void* const value = slot->GetValuePtrByPropertyNameInChain(STR("State"));
        if (!property || !value || property->GetElementSize() != 1)
        {
            return std::nullopt;
        }
        return *static_cast<const uint8_t*>(value);
    }

    static auto dispatch_path(UObject* controller, std::wstring_view path, bool internal) -> bool
    {
        if (!controller || !is_uobject_valid_fast(controller))
        {
            return false;
        }
        UFunction* const function = controller->GetFunctionByNameInChain(STR("SetPath"));
        FProperty* const reference =
            function ? find_function_param_by_name(function, STR("SoundReference")) : nullptr;
        if (!function || !reference || function->GetParmsSize() <= 0 ||
            reference->GetClass().GetName() != STR("AssetObjectProperty") ||
            !asset_property_fits(reference))
        {
            return false;
        }
        std::vector<std::byte> params(static_cast<size_t>(function->GetParmsSize()), std::byte{0});
        void* const value = reference->ContainerPtrToValuePtr<void>(params.data());
        if (!initialize_property_value_dynamic(reference, value))
        {
            return false;
        }
        *reinterpret_cast<FString*>(static_cast<std::byte*>(value) + legacy_asset_path_offset) =
            FString(std::wstring{path}.c_str());
        if (internal)
        {
            const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
            controller->ProcessEvent(function, params.data());
        }
        else
        {
            controller->ProcessEvent(function, params.data());
        }
        destroy_property_value_dynamic(reference, value);
        return true;
    }

    auto set_path(UObject* controller, std::wstring_view path) -> bool
    {
        return dispatch_path(controller, path, true);
    }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    auto test_request_path(UObject* controller, std::wstring_view path) -> bool
    {
        return dispatch_path(controller, path, false);
    }
#endif

    namespace
    {
        auto call_without_params(UObject* controller, const wchar_t* name) -> bool
        {
            if (!controller || !is_uobject_valid_fast(controller))
            {
                return false;
            }
            UFunction* const function = controller->GetFunctionByNameInChain(name);
            if (!function || function->GetParmsSize() != 0)
            {
                return false;
            }
            const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
            controller->ProcessEvent(function, nullptr);
            return true;
        }
    } // namespace

    auto play(UObject* controller) -> bool
    {
        return call_without_params(controller, STR("Play"));
    }

    auto stop(UObject* controller) -> bool
    {
        return call_without_params(controller, STR("Stop"));
    }

    auto set_section(UObject* controller, int32_t index) -> bool
    {
        if (!controller || !is_uobject_valid_fast(controller))
        {
            return false;
        }
        UFunction* const function = controller->GetFunctionByNameInChain(STR("SetSection"));
        FProperty* const param =
            function ? find_function_param_by_name(function, STR("Index")) : nullptr;
        if (!function || !param || param->GetElementSize() != sizeof(int32_t) ||
            function->GetParmsSize() <= 0)
        {
            return false;
        }
        std::vector<std::byte> params(static_cast<size_t>(function->GetParmsSize()), std::byte{0});
        *param->ContainerPtrToValuePtr<int32_t>(params.data()) = index;
        const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
        controller->ProcessEvent(function, params.data());
        return true;
    }

    auto section_index(UObject* controller) -> std::optional<int32_t>
    {
        if (!controller || !is_uobject_valid_fast(controller))
        {
            return std::nullopt;
        }
        UFunction* const function = controller->GetFunctionByNameInChain(STR("GetSectionIndex"));
        FProperty* const result =
            function ? find_function_param_by_name(function, STR("ReturnValue")) : nullptr;
        if (!function || !result || result->GetElementSize() != sizeof(int32_t) ||
            function->GetParmsSize() <= 0)
        {
            return std::nullopt;
        }
        std::vector<std::byte> params(static_cast<size_t>(function->GetParmsSize()), std::byte{0});
        {
            const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
            controller->ProcessEvent(function, params.data());
        }
        return *result->ContainerPtrToValuePtr<int32_t>(params.data());
    }

    auto set_seek_time(UObject* controller, float seconds) -> bool
    {
        if (!controller || !is_uobject_valid_fast(controller))
        {
            return false;
        }
        UFunction* const function = controller->GetFunctionByNameInChain(STR("SetSeekTime"));
        FProperty* const seek =
            function ? find_function_param_by_name(function, STR("SeekTime")) : nullptr;
        if (!function || !seek || seek->GetClass().GetName() != STR("FloatProperty") ||
            function->GetParmsSize() <= 0)
        {
            return false;
        }
        std::vector<std::byte> params(static_cast<size_t>(function->GetParmsSize()), std::byte{0});
        *seek->ContainerPtrToValuePtr<float>(params.data()) = seconds;
        const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
        controller->ProcessEvent(function, params.data());
        return true;
    }

    auto default_options(UObject* controller) -> std::optional<BgmOptions>
    {
        UObject* const slot = slot_of(controller);
        if (!slot || !is_uobject_valid_fast(slot))
        {
            return std::nullopt;
        }
        const auto [property, value] =
            property_of(slot, STR("DefaultOptions_"), STR("StructProperty"));
        if (!property)
        {
            return std::nullopt;
        }
        UScriptStruct* const options = static_cast<FStructProperty*>(property)->GetStruct();
        FProperty* const store = find_struct_field_by_name(options, STR("StoreBehaviour"));
        FProperty* const stay = find_struct_field_by_name(options, STR("StaySuspendTime"));
        if (!store || store->GetElementSize() != 1 || !stay ||
            stay->GetClass().GetName() != STR("FloatProperty"))
        {
            return std::nullopt;
        }
        const auto* const bytes = static_cast<const std::byte*>(value);
        return BgmOptions{
            *reinterpret_cast<const uint8_t*>(bytes + store->GetOffset_Internal()),
            *reinterpret_cast<const float*>(bytes + stay->GetOffset_Internal()),
        };
    }

    auto set_store_behaviour(UObject* controller, uint8_t behaviour) -> bool
    {
        if (!controller || !is_uobject_valid_fast(controller))
        {
            return false;
        }
        UFunction* const function = controller->GetFunctionByNameInChain(STR("SetStoreBehaviour"));
        FProperty* const param =
            function ? find_function_param_by_name(function, STR("StoreBehaviour")) : nullptr;
        if (!function || !param || param->GetElementSize() != 1 || function->GetParmsSize() <= 0)
        {
            return false;
        }
        std::vector<std::byte> params(static_cast<size_t>(function->GetParmsSize()), std::byte{0});
        *param->ContainerPtrToValuePtr<uint8_t>(params.data()) = behaviour;
        const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
        controller->ProcessEvent(function, params.data());
        return true;
    }

    auto set_volume(UObject* controller, float volume, float fade_time) -> bool
    {
        if (!controller || !is_uobject_valid_fast(controller))
        {
            return false;
        }
        UFunction* const function = controller->GetFunctionByNameInChain(STR("SetVolume"));
        FProperty* const volume_param =
            function ? find_function_param_by_name(function, STR("Volume")) : nullptr;
        FProperty* const fade_param =
            function ? find_function_param_by_name(function, STR("FadeTime")) : nullptr;
        if (!function || !volume_param || !fade_param || function->GetParmsSize() <= 0 ||
            volume_param->GetClass().GetName() != STR("FloatProperty") ||
            fade_param->GetClass().GetName() != STR("FloatProperty"))
        {
            return false;
        }
        std::vector<std::byte> params(static_cast<size_t>(function->GetParmsSize()), std::byte{0});
        *volume_param->ContainerPtrToValuePtr<float>(params.data()) = volume;
        *fade_param->ContainerPtrToValuePtr<float>(params.data()) = fade_time;
        const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
        controller->ProcessEvent(function, params.data());
        return true;
    }

    auto read_volume_settings(UObject* game_instance) -> std::optional<VolumeSettings>
    {
        if (!game_instance || !is_uobject_valid_fast(game_instance))
        {
            return std::nullopt;
        }
        UObject* const settings =
            try_read_object_property_object(game_instance, STR("m_pGameSettings"));
        if (!settings || !is_uobject_valid_fast(settings))
        {
            return std::nullopt;
        }
        const auto read_byte = [settings](const wchar_t* name) -> std::optional<uint8_t>
        {
            FProperty* const property = settings->GetPropertyByNameInChain(name);
            void* const value = settings->GetValuePtrByPropertyNameInChain(name);
            if (!property || !value || property->GetElementSize() != 1)
            {
                return std::nullopt;
            }
            return *static_cast<const uint8_t*>(value);
        };
        const auto master = read_byte(STR("SoundMasterVolume"));
        const auto bgm = read_byte(STR("SoundBgmVolume"));
        if (!master || !bgm)
        {
            return std::nullopt;
        }
        return VolumeSettings{*master, *bgm};
    }

#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    auto test_request_stop(UObject* controller) -> bool
    {
        if (!is_uobject_valid_fast(controller))
        {
            return false;
        }
        auto* function = controller->GetFunctionByNameInChain(STR("Stop"));
        if (!is_uobject_valid_fast(function) || function->GetParmsSize() != 0)
        {
            return false;
        }
        controller->ProcessEvent(function, nullptr); // Exercise the game's external request hook.
        return true;
    }

    auto test_volume_settings(UObject* game_instance, VolumeSettings volume) -> bool
    {
        if (volume.master > maximum_volume_setting || volume.bgm > maximum_volume_setting ||
            !read_volume_settings(game_instance))
        {
            return false;
        }
        auto* settings = try_read_object_property_object(game_instance, STR("m_pGameSettings"));
        auto* owner = UObjectGlobals::StaticFindObject<UObject*>(
            nullptr, nullptr, STR("/Script/SQEXSEAD.Default__SQEXSEADStatics"));
        auto* all =
            owner ? owner->GetFunctionByNameInChain(STR("SetConfigVolumeAllCategory")) : nullptr;
        auto* bgm =
            owner ? owner->GetFunctionByNameInChain(STR("SetConfigCategoryVolume")) : nullptr;
        if (!is_uobject_valid_fast(all) || !is_uobject_valid_fast(bgm) ||
            all->GetParmsSize() != all_volume_parameter_bytes ||
            bgm->GetParmsSize() != bgm_volume_parameter_bytes)
        {
            return false;
        }
        auto* category = find_function_param_by_name(bgm, STR("CategoryName"));
        auto* gain = find_function_param_by_name(bgm, STR("Volume"));
        if (!category || category->GetOffset_Internal() != 0 || !gain ||
            gain->GetOffset_Internal() != bgm_volume_gain_offset)
        {
            return false;
        }
        alignas(uint64_t) std::array<std::byte, bgm_volume_parameter_bytes> params{};
        if (!write_name_or_string_function_param(category, params.data(), STR("BGM")))
        {
            return false;
        }
        *static_cast<uint8_t*>(
            settings->GetValuePtrByPropertyNameInChain(STR("SoundMasterVolume"))) = volume.master;
        *static_cast<uint8_t*>(settings->GetValuePtrByPropertyNameInChain(STR("SoundBgmVolume"))) =
            volume.bgm;
        const std::array<float, 2> master{
            volume.master / static_cast<float>(maximum_volume_setting), 0.0f};
        owner->ProcessEvent(all, const_cast<float*>(master.data()));
        const float music = volume.master * volume.bgm /
                            static_cast<float>(maximum_volume_setting * maximum_volume_setting);
        std::memcpy(params.data() + bgm_volume_gain_offset, &music, sizeof(music));
        owner->ProcessEvent(bgm, params.data());
        return true;
    }
#endif

    auto current_level_name(UObject* world_context) -> std::optional<std::wstring>
    {
        if (!world_context || !is_uobject_valid_fast(world_context))
        {
            return std::nullopt;
        }
        UFunction* const function = UObjectGlobals::StaticFindObject<UFunction*>(
            nullptr, nullptr, STR("/Script/Engine.GameplayStatics:GetCurrentLevelName"));
        UObject* const self = UObjectGlobals::StaticFindObject<UObject*>(
            nullptr, nullptr, STR("/Script/Engine.Default__GameplayStatics"));
        FProperty* const result =
            function ? find_function_param_by_name(function, STR("ReturnValue")) : nullptr;
        FProperty* const remove_prefix =
            function ? find_function_param_by_name(function, STR("bRemovePrefixString")) : nullptr;
        if (!function || !self || !result || !remove_prefix || !is_uobject_valid_fast(function) ||
            !is_uobject_valid_fast(self) || result->GetClass().GetName() != STR("StrProperty") ||
            function->GetParmsSize() <= 0)
        {
            return std::nullopt;
        }
        std::vector<std::byte> params(static_cast<size_t>(function->GetParmsSize()), std::byte{0});
        if (!write_object_function_param(function, params.data(), STR("WorldContextObject"),
                                         world_context) ||
            !write_bool_function_param(remove_prefix, params.data(), true))
        {
            return std::nullopt;
        }
        {
            const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
            self->ProcessEvent(function, params.data());
        }
        void* const value = result->ContainerPtrToValuePtr<void>(params.data());
        const wchar_t* const chars = static_cast<const FString*>(value)->GetCharArray();
        std::wstring name{chars ? chars : STR("")};
        destroy_property_value_dynamic(result, value);
        return name;
    }
} // namespace kh3ap::sead_bgm
