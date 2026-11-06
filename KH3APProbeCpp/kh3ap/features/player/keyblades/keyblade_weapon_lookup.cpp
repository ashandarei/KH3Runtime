#include "keyblade_weapon_lookup.h"
#include "keyblade_options_helpers.h"
#include "keyblade_style_catalog.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/reflection/receiver_function_cache.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/features/grants/grant_internal_call_depth.h"
#include <Unreal/UFunction.hpp>
#include <Unreal/UEnum.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <algorithm>
#include <cstring>
#include <format>
#include <vector>

namespace kh3ap::keyblade_options
{
    using namespace RC::Unreal;
    auto resolve_keyblade_name_fallback(uint64_t raw_weapon_id) -> std::optional<std::wstring>
    {
        // ETresItemDefWeapon's keyblade enum values are stable numeric values
        // in the shipping game.  Do not call UEnum::GetNameByValue here:
        // that UE4SS helper is not exported by the installed v3.0.1 runtime,
        // which makes the whole probe fail to load with Win32 error 0x7f.
        // The explicit map also keeps this path independent of editor-only
        // reflection data.
        switch (raw_weapon_id)
        {
        case 0:
            return std::wstring{STR("WEP_KEYBLADE00")};
        case 1:
            return std::wstring{STR("WEP_KEYBLADE01")};
        case 2:
            return std::wstring{STR("WEP_KEYBLADE02")};
        case 3:
            return std::wstring{STR("WEP_KEYBLADE03")};
        case 4:
            return std::wstring{STR("WEP_KEYBLADE04")};
        case 5:
            return std::wstring{STR("WEP_KEYBLADE05")};
        case 6:
            return std::wstring{STR("WEP_KEYBLADE06")};
        case 7:
            return std::wstring{STR("WEP_KEYBLADE07")};
        case 8:
            return std::wstring{STR("WEP_KEYBLADE08")};
        case 9:
            return std::wstring{STR("WEP_KEYBLADE09")};
        case 10:
            return std::wstring{STR("WEP_KEYBLADE10")};
        case 11:
            return std::wstring{STR("WEP_KEYBLADE11")};
        case 12:
            return std::wstring{STR("WEP_KEYBLADE12")};
        case 13:
            return std::wstring{STR("WEP_KEYBLADE13")};
        case 14:
            return std::wstring{STR("WEP_KEYBLADE14")};
        case 17:
            return std::wstring{STR("WEP_KEYBLADE17")};
        case 18:
            return std::wstring{STR("WEP_KEYBLADE18")};
        default:
            return std::nullopt;
        }
    }

    auto resolve_keyblade_name_via_blueprint_enum_utility(uint64_t raw_weapon_id,
                                                          std::wstring* diagnostics)
        -> std::optional<std::wstring>
    {
        const RuntimeHotPathProfileScope profile{L"keyblade_style:enum_name"};
        try
        {
            // Cache reflected identities, not the equipped weapon or enum result.
            // Weak serials force resolution again after unload or replacement.
            static FWeakObjectPtr enum_ref, utility_ref, default_ref;
            static ReceiverFunctionCache<> utility_functions;
            auto* cached_enum = static_cast<UEnum*>(enum_ref.Get());
            auto* cached_utility = static_cast<UFunction*>(utility_ref.Get());
            auto* cached_default = default_ref.Get();
            bool lookup_complete = is_uobject_valid_fast(cached_enum) &&
                                   is_uobject_valid_fast(cached_utility) &&
                                   is_uobject_valid_fast(cached_default);
            if (!lookup_complete)
            {
                // Resolve already-loaded objects through UE's soft-reference
                // resolver rather than scanning the global object array.
                auto* enum_object =
                    find_loaded_runtime_asset(L"/Script/TresGame.ETresItemDefWeapon");
                cached_enum = is_uobject_a_fast(enum_object, UEnum::StaticClass())
                                  ? static_cast<UEnum*>(enum_object)
                                  : nullptr;
                if (!cached_enum)
                {
                    cached_enum = UObjectGlobals::StaticFindObject<UEnum*>(
                        nullptr, nullptr, L"/Script/TresGame.ETresItemDefWeapon");
                }
                cached_default =
                    find_loaded_runtime_asset(L"/Script/Engine.Default__KismetNodeHelperLibrary");
                if (!cached_default)
                {
                    cached_default = UObjectGlobals::StaticFindObject<UObject*>(
                        nullptr, nullptr, L"/Script/Engine.Default__KismetNodeHelperLibrary");
                }
                cached_utility = utility_functions.resolve(
                    cached_default,
                    L"/Script/Engine.KismetNodeHelperLibrary:GetEnumeratorUserFriendlyName");
                if (!cached_utility)
                {
                    cached_utility = UObjectGlobals::StaticFindObject<UFunction*>(
                        nullptr, nullptr,
                        L"/Script/Engine.KismetNodeHelperLibrary:GetEnumeratorUserFriendlyName");
                }
                enum_ref = make_uobject_weak(cached_enum);
                utility_ref = make_uobject_weak(cached_utility);
                default_ref = make_uobject_weak(cached_default);
                // Retry is allowed only until all three reflected objects are
                // available; after that no StaticFindObject call is repeated.
                lookup_complete = is_uobject_valid_fast(cached_enum) &&
                                  is_uobject_valid_fast(cached_utility) &&
                                  is_uobject_valid_fast(cached_default);
            }
            if (!lookup_complete)
            {
                return std::nullopt;
            }

            UEnum* const weapon_enum = cached_enum;
            UFunction* const utility_function = cached_utility;
            UObject* const utility_default_object = cached_default;

            std::vector<std::byte> params(static_cast<size_t>(utility_function->GetParmsSize()));
            std::fill(params.begin(), params.end(), std::byte{0});
            FProperty* const enum_property =
                find_function_param_by_name(utility_function, STR("Enum"));
            FProperty* const value_property =
                find_function_param_by_name(utility_function, STR("EnumeratorValue"));
            FProperty* const return_property =
                find_function_param_by_name(utility_function, STR("ReturnValue"));
            if (!enum_property || !value_property || !return_property ||
                enum_property->GetElementSize() < static_cast<int>(sizeof(UObject*)) ||
                !write_integral_function_param(value_property, params.data(), raw_weapon_id))
            {
                return std::nullopt;
            }

            void* const enum_value = enum_property->ContainerPtrToValuePtr<void>(params.data());
            if (!enum_value)
            {
                return std::nullopt;
            }
            std::memcpy(enum_value, &weapon_enum, sizeof(UObject*));

            {
                const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
                utility_default_object->ProcessEvent(utility_function, params.data());
            }

            const std::wstring resolved_name = read_function_param_value_as_string(
                utility_function, params.data(), STR("ReturnValue"));
            if (diagnostics)
            {
                *diagnostics += std::format(STR(" enum_utility={}"),
                                            resolved_name.empty() ? std::wstring{STR("<empty>")}
                                                                  : resolved_name);
            }
            if (is_valid_keyblade_name(resolved_name))
            {
                return resolved_name;
            }
        }
        catch (...)
        {
            if (diagnostics)
            {
                *diagnostics += STR(" enum_utility=failed");
            }
        }
        return std::nullopt;
    }

    auto try_read_equipped_weapon_id(UObject* sora, std::wstring_view function_path,
                                     std::wstring* failure_details, std::wstring* diagnostics)
        -> std::optional<uint64_t>
    {
        const RuntimeHotPathProfileScope profile{L"keyblade_style:weapon_read"};
        if (!sora)
        {
            if (failure_details)
            {
                *failure_details = STR("Sora pawn is unavailable");
            }
            return std::nullopt;
        }

        static ReceiverFunctionCache<> functions;
        UFunction* function_object = functions.resolve(sora, function_path);
        if (!function_object)
        {
            function_object =
                UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, function_path);
        }
        if (!function_object)
        {
            if (failure_details)
            {
                *failure_details =
                    std::format(STR("equipped weapon function unavailable path={}"), function_path);
            }
            return std::nullopt;
        }

        if (diagnostics)
        {
            *diagnostics =
                std::format(STR("function={} parms_size={}"), function_object->GetFullName(),
                            function_object->GetParmsSize());
        }

        std::vector<std::byte> params(static_cast<size_t>(function_object->GetParmsSize()));
        std::fill(params.begin(), params.end(), std::byte{0});
        {
            const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
            sora->ProcessEvent(function_object, params.empty() ? nullptr : params.data());
        }

        FProperty* const return_property =
            find_function_param_by_name(function_object, STR("ReturnValue"));
        void* const return_value =
            return_property ? return_property->ContainerPtrToValuePtr<void>(params.data())
                            : nullptr;
        if (!return_property || !return_value || return_property->GetElementSize() <= 0)
        {
            if (failure_details)
            {
                *failure_details = STR("equipped weapon ReturnValue is unavailable");
            }
            return std::nullopt;
        }

        if (diagnostics)
        {
            *diagnostics = std::format(
                STR("{} return_property={} property_class={} element_size={} offset={} raw_bytes="),
                *diagnostics, return_property->GetName(), return_property->GetClass().GetName(),
                return_property->GetElementSize(), return_property->GetOffset_Internal());
            const auto* const bytes = static_cast<const unsigned char*>(return_value);
            const int byte_count = std::min(return_property->GetElementSize(), 8);
            for (int index = 0; index < byte_count; ++index)
            {
                if (index != 0)
                {
                    diagnostics->append(STR(" "));
                }
                *diagnostics += std::format(STR("{:02X}"), bytes[index]);
            }
        }

        return read_unsigned_param_value(return_value, return_property->GetElementSize());
    }

} // namespace kh3ap::keyblade_options

namespace kh3ap
{
    using namespace RC::Unreal;
    using namespace keyblade_options;
    auto read_equipped_keyblade_name(UObject* sora) -> std::optional<std::wstring>
    {
        const auto raw_weapon_id = try_read_equipped_weapon_id(
            sora, STR("/Script/TresGame.TresCharPawnBase:BP_GetEquipWeaponID"), nullptr, nullptr);
        // The raw ETresItemDefWeapon value is not the WEP_KEYBLADE number
        // (Wheel of Fate reads 10 for WEP_KEYBLADE05), so only the reflected
        // enum name is trusted here.
        return raw_weapon_id
                   ? resolve_keyblade_name_via_blueprint_enum_utility(*raw_weapon_id, nullptr)
                   : std::nullopt;
    }
} // namespace kh3ap
