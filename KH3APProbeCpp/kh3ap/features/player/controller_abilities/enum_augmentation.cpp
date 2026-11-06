#include "kh3ap/features/player/controller_abilities/host.h"

#include "kh3ap/core/reflection/reflection_core.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <cstddef>
#include <cstring>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#define NOMINMAX
#include <windows.h>

namespace kh3ap
{
    using namespace RC;
    using namespace RC::Unreal;
    using namespace controller_ability_detail;
    auto RuntimeControllerAbilityService::Impl::read_enum_names_header(UObject* enum_object)
        -> std::optional<ArrayHeader>
    {
        if (!enum_object || !kh3ap::is_uobject_valid_fast(enum_object))
        {
            return std::nullopt;
        }

        ArrayHeader names_header{};
        std::memcpy(&names_header,
                    reinterpret_cast<const std::byte*>(enum_object) + enum_layout::names_offset,
                    sizeof(names_header));
        if (names_header.data_ptr == 0 || names_header.num < 0 ||
            names_header.max < names_header.num || names_header.num > enum_layout::maximum_names ||
            names_header.max > enum_layout::maximum_capacity)
        {
            return std::nullopt;
        }
        return names_header;
    }

    auto RuntimeControllerAbilityService::Impl::write_enum_names_header(
        UObject* enum_object, const ArrayHeader& names_header) -> bool
    {
        if (!enum_object || !kh3ap::is_uobject_valid_fast(enum_object))
        {
            return false;
        }

        auto* const header_ptr =
            reinterpret_cast<std::byte*>(enum_object) + enum_layout::names_offset;
        DWORD old_protect = 0;
        const bool changed_protection =
            VirtualProtect(header_ptr, sizeof(names_header), PAGE_READWRITE, &old_protect) != 0;
        std::memcpy(header_ptr, &names_header, sizeof(names_header));
        if (changed_protection)
        {
            DWORD ignored = 0;
            VirtualProtect(header_ptr, sizeof(names_header), old_protect, &ignored);
        }
        return true;
    }

    auto RuntimeControllerAbilityService::Impl::try_apply_ability_enum_augmentation() -> bool
    {
        UObject* const enum_object = UObjectGlobals::StaticFindObject<UObject*>(
            nullptr, nullptr, std::wstring{s_ability_enum_path});
        const auto names_header = read_enum_names_header(enum_object);
        if (!enum_object || !names_header.has_value())
        {
            return false;
        }

        std::vector<EnumNamePairRaw> existing{};
        existing.reserve(static_cast<size_t>(names_header->num));
        std::unordered_map<int64_t, std::wstring> names_by_value{};
        std::unordered_map<std::wstring, int64_t> values_by_name{};
        for (int entry_index = 0; entry_index < names_header->num; ++entry_index)
        {
            EnumNamePairRaw entry{};
            const auto* const entry_ptr = reinterpret_cast<const EnumNamePairRaw*>(
                static_cast<uintptr_t>(names_header->data_ptr) +
                static_cast<uintptr_t>(entry_index) * sizeof(EnumNamePairRaw));
            std::memcpy(&entry, entry_ptr, sizeof(entry));
            existing.push_back(entry);
            const std::wstring entry_name = entry.name.ToString();
            names_by_value[entry.value] = entry_name;
            values_by_name[entry_name] = entry.value;
        }

        std::vector<EnumNamePairRaw> additions{};
        const auto collect_additions = [&](const auto& definitions) -> bool
        {
            for (const ControllerAbilityDefinition& definition : definitions)
            {
                const int64_t expected_value = definition.ability_value;
                const std::wstring expected_name{definition.enum_name};
                const auto value_entry = names_by_value.find(expected_value);
                const auto name_entry = values_by_name.find(expected_name);
                if (value_entry != names_by_value.end() && value_entry->second != expected_name)
                {
                    Output::send<LogLevel::Error>(
                        STR("[KH3APProbeCpp] Controller ability enum augmentation failed value "
                            "collision value={} existing_name={} expected_name={}\n"),
                        expected_value, value_entry->second, expected_name);
                    return false;
                }
                if (name_entry != values_by_name.end() && name_entry->second != expected_value)
                {
                    Output::send<LogLevel::Error>(
                        STR("[KH3APProbeCpp] Controller ability enum augmentation failed name "
                            "collision name={} existing_value={} expected_value={}\n"),
                        expected_name, name_entry->second, expected_value);
                    return false;
                }
                if (value_entry == names_by_value.end())
                {
                    additions.push_back(EnumNamePairRaw{
                        FName(expected_name, FNAME_Add),
                        expected_value,
                    });
                }
            }
            return true;
        };
        if (!collect_additions(s_controller_abilities) ||
            !collect_additions(s_keyblade_style_abilities) || !collect_additions(s_link_abilities))
        {
            return true;
        }

        if (additions.empty())
        {
            Output::send<LogLevel::Verbose>(STR("[KH3APProbeCpp] Controller ability enum "
                                                "augmentation already present entries={}\n"),
                                            s_controller_abilities.size());
            return true;
        }

        constexpr int64_t highest_reserved_ability_value = ability_id::links;
        size_t insertion_index = existing.size();
        for (size_t entry_index = 0; entry_index < existing.size(); ++entry_index)
        {
            if (existing[entry_index].value > highest_reserved_ability_value)
            {
                insertion_index = entry_index;
                break;
            }
        }
        if (insertion_index == existing.size())
        {
            Output::send<LogLevel::Error>(
                STR("[KH3APProbeCpp] Controller ability enum augmentation failed: no post-gap enum "
                    "entry found\n"));
            return true;
        }

        std::vector<EnumNamePairRaw> patched{};
        patched.reserve(existing.size() + additions.size());
        patched.insert(patched.end(), existing.begin(), existing.begin() + insertion_index);
        patched.insert(patched.end(), additions.begin(), additions.end());
        patched.insert(patched.end(), existing.begin() + insertion_index, existing.end());

        const size_t bytes = patched.size() * sizeof(EnumNamePairRaw);
        void* const new_storage =
            ue_realloc_for_script_array(nullptr, bytes, alignof(EnumNamePairRaw));
        if (!new_storage)
        {
            return false;
        }
        std::memcpy(new_storage, patched.data(), bytes);

        const ArrayHeader new_header{
            reinterpret_cast<uint64_t>(new_storage),
            static_cast<int32_t>(patched.size()),
            static_cast<int32_t>(patched.size()),
        };
        const bool wrote_header = write_enum_names_header(enum_object, new_header);
        Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] Controller ability enum augmentation "
                                            "event={} old_num={} new_num={} additions={}\n"),
                                        wrote_header ? STR("patched") : STR("failed"),
                                        names_header->num, new_header.num, additions.size());
        return wrote_header;
    }
} // namespace kh3ap
