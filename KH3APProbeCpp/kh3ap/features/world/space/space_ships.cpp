#include <array>
#include <optional>
#include <utility>
#include <limits>
#include "kh3ap/features/world/space/space_ships.h"
#include "kh3ap/features/world/space/space_ship_catalog.h"
#include "kh3ap/features/world/space/space_ship_policy.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/bridge/utilities.h"
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <sstream>
#include <vector>

namespace kh3ap
{
    using namespace RC::Unreal;
    namespace
    {
        constexpr auto ship_query_name_offset = sizeof(UObject*);
        constexpr auto ship_query_result_offset = ship_query_name_offset + sizeof(FName);
        constexpr auto ship_boolean_query_parameter_bytes = ship_query_result_offset + sizeof(bool);
        constexpr auto ship_integer_query_parameter_bytes =
            ship_query_result_offset + sizeof(int32_t);
        constexpr auto ship_query_storage_bytes = 24;

        constexpr auto flag_query_result_offset = sizeof(FName);
        constexpr auto flag_boolean_query_parameter_bytes = sizeof(FName) + sizeof(bool);
        constexpr auto flag_integer_query_parameter_bytes = sizeof(FName) + sizeof(int32_t);
        constexpr auto flag_query_storage_bytes = 16;

        // These shipping Blueprint APIs call the authoritative item stock.
        // DropBackyardItemOne clamps the byte count at zero; GetItem rejects
        // negative amounts. No saved-file access or Debug API is involved.
        std::optional<int> inventory_call(UObject* context, std::wstring_view token, bool drop)
        {
            if (!is_uobject_valid_fast(context))
            {
                return {};
            }
            auto* library = UObjectGlobals::StaticFindObject<UObject*>(
                nullptr, nullptr, L"/Script/TresGame.Default__TresGameBlueprintLibrary");
            if (!is_uobject_valid_fast(library))
            {
                return {};
            }
            auto* function = library->GetFunctionByNameInChain(drop ? L"BP_TresDropBackyardItemOne"
                                                                    : L"BP_TresGetBackyardItemNum");
            // Reflection excludes tail padding: query is 20 bytes, drop is 17.
            if (!is_uobject_valid_fast(function) ||
                function->GetParmsSize() != (drop ? ship_boolean_query_parameter_bytes
                                                  : ship_integer_query_parameter_bytes))
            {
                return {};
            }
            auto* name = find_function_param_by_name(function, L"InItemKeyName");
            auto* result = find_function_param_by_name(function, L"ReturnValue");
            if (!name || name->GetOffset_Internal() != ship_query_name_offset ||
                name->GetElementSize() != sizeof(FName) || !result ||
                result->GetOffset_Internal() != ship_query_result_offset ||
                (drop ? !result->IsA<FBoolProperty>() || result->GetElementSize() != 1
                      : result->GetClass().GetName() != L"IntProperty" ||
                            result->GetElementSize() != sizeof(int32_t)))
            {
                return {};
            }
            std::array<std::byte, ship_query_storage_bytes> params{};
            if (!write_object_function_param(function, params.data(), L"WorldContextObject",
                                             context) ||
                !write_name_or_string_function_param(name, params.data(), std::wstring{token}))
            {
                return {};
            }
            library->ProcessEvent(function, params.data());
            if (drop)
            {
                return static_cast<FBoolProperty*>(result)->GetPropertyValueInContainer(
                           params.data())
                           ? 1
                           : 0;
            }
            const auto value = *result->ContainerPtrToValuePtr<int32_t>(params.data());
            return value >= 0 && value <= std::numeric_limits<uint8_t>::max()
                       ? std::optional<int>{value}
                       : std::nullopt;
        }
    } // namespace
    bool RuntimeSpaceShips::inspect(UObject* context)
    {
        inventory_verified = false;
        completed_worlds = 0;
        auto* library = UObjectGlobals::StaticFindObject<UObject*>(
            nullptr, nullptr, L"/Script/TresGame.Default__TresGameBlueprintLibrary");
        auto* flag_function = is_uobject_valid_fast(library)
                                  ? library->GetFunctionByNameInChain(L"GetGameFlag")
                                  : nullptr;
        if (!is_uobject_valid_fast(flag_function) ||
            flag_function->GetParmsSize() != flag_integer_query_parameter_bytes)
        {
            return false;
        }
        auto* flag_name = find_function_param_by_name(flag_function, L"FlagName");
        auto* flag_result = find_function_param_by_name(flag_function, L"ReturnValue");
        if (!flag_name || flag_name->GetOffset_Internal() != 0 ||
            flag_name->GetElementSize() != sizeof(FName) ||
            flag_name->GetClass().GetName() != L"NameProperty" || !flag_result ||
            flag_result->GetOffset_Internal() != flag_query_result_offset ||
            flag_result->GetClass().GetName() != L"IntProperty" ||
            flag_result->GetElementSize() != sizeof(int32_t))
        {
            return false;
        }
        for (const auto& flag : space::world_completion_flags)
        {
            alignas(uint64_t) std::array<std::byte, flag_query_storage_bytes> params{};
            if (!write_name_or_string_function_param(flag_name, params.data(), flag.name))
            {
                return false;
            }
            library->ProcessEvent(flag_function, params.data());
            completed_worlds +=
                *flag_result->ContainerPtrToValuePtr<int32_t>(params.data()) >= flag.threshold;
        }
        std::map<std::wstring, int> observed;
        for (const auto& ship : space::ship_blueprints)
        {
            const auto count = inventory_call(context, ship.token, false);
            if (!count)
            {
                return false;
            }
            observed.emplace(ship.name, *count);
        }
        stock = std::move(observed);
        inventory_verified = true;
        return true;
    }
    bool RuntimeSpaceShips::reconcile(UObject* context, const RuntimeSpaceAccess& access)
    {
        if (!access.access_ready || !access.include_space || !inspect(context))
        {
            return false;
        }
        for (const auto& ship : space::ship_blueprints)
        {
            if (access.items.contains(std::wstring{ship.name}))
            {
                continue;
            }
            auto& count = stock.at(std::wstring{ship.name});
            while (count > 0)
            {
                const auto dropped = inventory_call(context, ship.token, true);
                const auto after = inventory_call(context, ship.token, false);
                if (!dropped || *dropped != 1 || !after || *after != count - 1)
                {
                    inventory_verified = false;
                    return false;
                }
                count = *after;
                ++removed;
            }
        }
        return true;
    }
    unsigned RuntimeSpaceShips::unlocked_zones(const RuntimeSpaceAccess& access) const
    {
        return space::unlocked_ship_zones(access.access_ready, access.include_space,
                                          inventory_verified, completed_worlds,
                                          [&](std::wstring_view name)
                                          {
                                              const auto found = stock.find(std::wstring{name});
                                              return found != stock.end() && found->second > 0 &&
                                                     access.items.contains(found->first);
                                          });
    }
    bool RuntimeSpaceShips::usable(const RuntimeSpaceAccess& access, int zone) const
    {
        return space::ship_zone_available(unlocked_zones(access), zone);
    }
    void RuntimeSpaceShips::write_status(std::ostream& json, const RuntimeSpaceAccess& access) const
    {
        json << "{\"required\":" << (access.include_space ? "true" : "false")
             << ",\"inventory_verified\":" << (inventory_verified ? "true" : "false")
             << ",\"travel_unlocked\":" << (usable(access) ? "true" : "false")
             << ",\"unreceived_blueprints_removed\":" << removed
             << ",\"completed_main_worlds\":" << completed_worlds << ",\"unlocked_zones\":[";
        const auto zones = unlocked_zones(access);
        bool first_zone = true;
        for (unsigned i = 0; i < space::ship_zone_names.size(); ++i)
        {
            if (zones & (1u << i))
            {
                if (!first_zone)
                {
                    json << ',';
                }
                first_zone = false;
                json << '"' << json_escape(space::ship_zone_names[i]) << '"';
            }
        }
        json << "],\"owned_blueprints\":[";
        bool first = true;
        for (const auto& [name, count] : stock)
        {
            if (count > 0)
            {
                if (!first)
                {
                    json << ',';
                }
                first = false;
                json << "{\"name\":\"" << json_escape(name) << "\",\"count\":" << count << '}';
            }
        }
        json << "]}";
    }
} // namespace kh3ap
