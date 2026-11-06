#include "kh3ap/features/player/context/player_context_internal.h"
#include "kh3ap/bridge/transport/bridge_channels.h"
#include "kh3ap/app/state.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/bridge/utilities.h"
#include "kh3ap/features/player/objects/object_resolver.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <UnrealDef.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <algorithm>
#include <cstring>
#include <format>
#define NOMINMAX
#include <windows.h>

namespace kh3ap
{
    using namespace RC;
    using namespace RC::Unreal;
    using namespace player_context_detail;

    namespace player_context_detail
    {
        auto lower_ascii_copy(std::wstring_view value) -> std::wstring
        {
            std::wstring lowered{value};
            std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                           [](wchar_t ch)
                           {
                               if (ch >= L'A' && ch <= L'Z')
                               {
                                   return static_cast<wchar_t>(ch + (L'a' - L'A'));
                               }
                               return ch;
                           });
            return lowered;
        }

        auto contains_new_game_prep_runtime_path(std::wstring_view value) -> bool
        {
            return value.find(STR("/Game/Maps/StationOfAwakening/")) != std::wstring_view::npos ||
                   value.find(STR("/Game/Maps/StationOfAwakening.")) != std::wstring_view::npos ||
                   value.find(STR("StationOfAwakening")) != std::wstring_view::npos;
        }

        auto try_read_process_u8(uintptr_t address) -> std::optional<uint8_t>
        {
            if (address == 0)
            {
                return std::nullopt;
            }
            uint8_t value = 0;
            SIZE_T bytes_read = 0;
            if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address),
                                   &value, sizeof(value), &bytes_read) ||
                bytes_read != sizeof(value))
            {
                return std::nullopt;
            }
            return value;
        }

        auto try_read_process_u64(uintptr_t address) -> std::optional<uint64_t>
        {
            if (address == 0)
            {
                return std::nullopt;
            }
            uint64_t value = 0;
            SIZE_T bytes_read = 0;
            if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address),
                                   &value, sizeof(value), &bytes_read) ||
                bytes_read != sizeof(value))
            {
                return std::nullopt;
            }
            return value;
        }

        auto read_u8_object_property(UObject* object, const wchar_t* property_name)
            -> std::optional<uint8_t>
        {
            if (!object || !property_name || !kh3ap::is_uobject_valid_fast(object))
            {
                return std::nullopt;
            }
            FProperty* const property = object->GetPropertyByNameInChain(property_name);
            void* const value_ptr = object->GetValuePtrByPropertyNameInChain(property_name);
            if (!property || !value_ptr || property->GetElementSize() <= 0)
            {
                return std::nullopt;
            }
            uint8_t value = 0;
            std::memcpy(&value, value_ptr, sizeof(value));
            return value;
        }

        auto format_object_class_name(UObject* object) -> std::wstring
        {
            if (!object)
            {
                return STR("<null>");
            }
            if (!kh3ap::is_uobject_valid_fast(object))
            {
                return STR("<not_real>");
            }
            UClass* const object_class = object->GetClassPrivate();
            return object_class ? object_class->GetName() : std::wstring{STR("<none>")};
        }
    } // namespace player_context_detail

    auto RuntimePlayerContextService::Impl::read_object_property_in_chain(
        UObject* object, const wchar_t* property_name) const -> UObject*
    {
        if (!object || !property_name || !kh3ap::is_uobject_valid_fast(object))
        {
            return nullptr;
        }
        if (UObject* const value = try_read_object_property_object(object, property_name))
        {
            return value;
        }

        FProperty* const property = object->GetPropertyByNameInChain(property_name);
        void* const value_ptr = object->GetValuePtrByPropertyNameInChain(property_name);
        if (!property || !value_ptr || property->GetClass().GetName() != STR("ObjectProperty"))
        {
            return nullptr;
        }
        UObject* value = nullptr;
        std::memcpy(&value, value_ptr, sizeof(value));
        return value;
    }

    auto RuntimePlayerContextService::Impl::format_probe_object(UObject* object) const
        -> std::wstring
    {
        if (!object)
        {
            return STR("<null>");
        }
        if (!kh3ap::is_uobject_valid_fast(object))
        {
            return std::format(STR("<not real @{}>"), reinterpret_cast<uintptr_t>(object));
        }
        return format_object_debug_name(object);
    }

    auto RuntimePlayerContextService::Impl::is_player_controller_name(
        std::wstring_view object_name) const -> bool
    {
        const std::wstring lowered_name = lower_ascii_copy(object_name);
        return lowered_name.find(STR("playercontroller")) != std::wstring::npos ||
               lowered_name.find(STR("tresuiplayercontroller")) != std::wstring::npos;
    }

    auto RuntimePlayerContextService::Impl::is_player_controller_object(UObject* object) const
        -> bool
    {
        return object && kh3ap::is_uobject_valid_fast(object) &&
               is_player_controller_name(format_object_debug_name(object));
    }

    auto RuntimePlayerContextService::Impl::log_controller_pawn_cache_probe(
        UObject* controller, std::wstring_view source) const -> void
    {
        if (!controller || !kh3ap::is_uobject_valid_fast(controller))
        {
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] Controller pawn cache probe source={} controller={} "
                    "skipped=invalid_controller | {}\n"),
                source, format_probe_object(controller), runtime_context());
            return;
        }

        UObject* const pawn = read_object_property_in_chain(controller, STR("Pawn"));
        UObject* const acknowledged_pawn =
            read_object_property_in_chain(controller, STR("AcknowledgedPawn"));
        UObject* const character = read_object_property_in_chain(controller, STR("Character"));
        const auto is_candidate = [](UObject* object)
        {
            return object && kh3ap::is_uobject_valid_fast(object) &&
                   RuntimeObjectResolver::is_sora_ability_pawn_candidate(object);
        };
        Output::send<LogLevel::Warning>(
            STR("[KH3APProbeCpp] Controller pawn cache probe source={} controller={} Pawn={} "
                "pawn_candidate={} AcknowledgedPawn={} acknowledged_candidate={} Character={} "
                "character_candidate={} | {}\n"),
            source, format_probe_object(controller), format_probe_object(pawn), is_candidate(pawn),
            format_probe_object(acknowledged_pawn), is_candidate(acknowledged_pawn),
            format_probe_object(character), is_candidate(character), runtime_context());
    }
} // namespace kh3ap
