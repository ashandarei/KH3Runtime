#include "kh3ap/features/enemies/remap_catalog.h"
#include "kh3ap/features/enemies/remap_catalog.h"
#include "kh3ap/features/enemies/boss_remap_policy.h"
#include "kh3ap/bridge/serialization/json_fields.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <UnrealDef.hpp>
#include <cstdint>

using namespace RC;

namespace kh3ap::enemy_remap_detail
{
    constexpr std::string_view runtime_entries_field = "enemy_runtime_entries";
    constexpr std::string_view boss_runtime_entries_field = "boss_runtime_entries";
    static auto parse_nonnegative_index(
            std::wstring_view value,
            size_t* parsed_index) -> bool
    {
        if (!parsed_index || value.empty())
        {
            return false;
        }
        size_t result = 0;
        for (wchar_t ch : value)
        {
            if (ch < L'0' || ch > L'9')
            {
                return false;
            }
            const size_t digit = static_cast<size_t>(ch - L'0');
            if (result > (SIZE_MAX - digit) / 10)
            {
                return false;
            }
            result = result * 10 + digit;
        }
        *parsed_index = result;
        return true;
    }

    auto parse_runtime_replacements(const std::string& metadata)
            -> std::optional<
                    std::unordered_map<std::wstring, std::wstring>>
    {
        const auto remap = JsonObject{metadata}.object_field("enemy_runtime_remap");
        if (!remap.valid()) return std::nullopt;

        const auto declared_entry_count =
                remap.int64_field("entry_count");
        const auto packages = remap.string_array_field("enemy_runtime_packages");
        const auto classes = remap.string_array_field("enemy_runtime_classes");
        const auto entries = remap.string_array_field(runtime_entries_field);
        const auto format_version = remap.int64_field("enemy_runtime_format_version");
        if (!format_version.has_value() || *format_version != 2 ||
            !declared_entry_count.has_value() ||
            *declared_entry_count <= 0 ||
            packages.empty() || classes.empty() ||
            static_cast<uint64_t>(*declared_entry_count) != entries.size())
        {
            return std::nullopt;
        }

        std::unordered_map<std::wstring, std::wstring> replacements{};
        replacements.reserve(entries.size());
        for (const std::wstring& entry : entries)
        {
            const size_t first_separator = entry.find(L'|');
            const size_t last_separator = entry.rfind(L'|');
            if (first_separator == std::wstring::npos ||
                last_separator == first_separator ||
                last_separator + 1 >= entry.size())
            {
                return std::nullopt;
            }
            size_t package_index = 0;
            size_t class_index = 0;
            if (!parse_nonnegative_index(
                        std::wstring_view{entry}.substr(0, first_separator),
                        &package_index) ||
                !parse_nonnegative_index(
                        std::wstring_view{entry}.substr(last_separator + 1),
                        &class_index) ||
                package_index >= packages.size() ||
                class_index >= classes.size())
            {
                return std::nullopt;
            }
            const std::wstring_view actor_name =
                    std::wstring_view{entry}.substr(
                            first_separator + 1,
                            last_separator - first_separator - 1);
            const std::wstring& package = packages[package_index];
            const std::wstring& class_path = classes[class_index];
            if (actor_name.empty() || actor_name.find(L'|') != std::wstring::npos ||
                !package.starts_with(STR("/Game/Levels/")) ||
                !class_path.starts_with(
                        STR("/Game/Blueprints/Enemy/")) ||
                !class_path.ends_with(STR("_C")))
            {
                return std::nullopt;
            }

            std::wstring object_path = package;
            object_path.push_back(L'.');
            object_path.append(actor_name);
            const bool inserted = replacements.emplace(
                    std::move(object_path),
                    class_path).second;
            if (!inserted)
            {
                return std::nullopt;
            }
        }
        if (replacements.size() != entries.size())
        {
            return std::nullopt;
        }
        return replacements;
    }

    static auto is_boss_class_path(std::wstring_view value) -> bool
    {
        return value.starts_with(STR("/Game/Blueprints/Enemy/")) &&
               value.ends_with(STR("_C"));
    }

    auto parse_runtime_boss_replacements(const std::string& metadata, bool apply_arena_policy)
            -> std::optional<
                    std::unordered_map<std::wstring, std::wstring>>
    {
        const auto remap = JsonObject{metadata}.object_field("boss_runtime_remap");
        if (!remap.valid()) return std::nullopt;

        const auto declared_entry_count = remap.int64_field("boss_runtime_entry_count");
        const auto classes = remap.string_array_field("boss_runtime_classes");
        const auto entries = remap.string_array_field(boss_runtime_entries_field);
        const auto format_version = remap.int64_field("boss_runtime_format_version");
        if (!format_version.has_value() || *format_version != 1 ||
            !declared_entry_count.has_value() ||
            *declared_entry_count < 0 ||
            static_cast<uint64_t>(*declared_entry_count) != entries.size() ||
            (!entries.empty() && classes.empty()))
        {
            return std::nullopt;
        }

        std::unordered_map<std::wstring, std::wstring> replacements{};
        replacements.reserve(entries.size());
        for (const std::wstring& entry : entries)
        {
            const size_t first_separator = entry.find(L'|');
            const size_t last_separator = entry.rfind(L'|');
            if (first_separator == std::wstring::npos ||
                last_separator == first_separator ||
                last_separator + 1 >= entry.size())
            {
                return std::nullopt;
            }

            const std::wstring_view boss_id =
                    std::wstring_view{entry}.substr(0, first_separator);
            size_t source_class_index = 0;
            size_t replacement_class_index = 0;
            if (!boss_id.starts_with(STR("BOSS_")) ||
                !parse_nonnegative_index(
                        std::wstring_view{entry}.substr(
                                first_separator + 1,
                                last_separator - first_separator - 1),
                        &source_class_index) ||
                !parse_nonnegative_index(
                        std::wstring_view{entry}.substr(last_separator + 1),
                        &replacement_class_index) ||
                source_class_index >= classes.size() ||
                replacement_class_index >= classes.size())
            {
                return std::nullopt;
            }

            const std::wstring& source_class = classes[source_class_index];
            const std::wstring& replacement_class =
                    classes[replacement_class_index];
            if (!is_boss_class_path(source_class) ||
                !is_boss_class_path(replacement_class) ||
                source_class == replacement_class ||
                !replacements.emplace(
                        source_class,
                        replacement_class).second)
            {
                return std::nullopt;
            }
        }
        if (replacements.size() != entries.size())
        {
            return std::nullopt;
        }
        const size_t lich_swaps = remove_lich_boss_replacements(replacements);
        const size_t arena_swaps = apply_arena_policy ? remove_incompatible_boss_arena_replacements(replacements) : 0;
        if (arena_swaps)
            Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] Excluded incompatible boss/arena replacements count={} original_boss_retained=true\n"), arena_swaps);
        const size_t final_swaps = remove_final_xehanort_replacements(replacements);
        if (final_swaps != 0)
        {
            Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] Boss runtime final Xehanort swaps ignored entries={} native_final_fight_preserved=true\n"),
                    final_swaps);
        }
        if (lich_swaps != 0)
        {
            Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] Boss runtime Lich swaps ignored entries={} "
                        "source_and_replacement_pinned=true native_encounters_preserved=true\n"),
                    lich_swaps);
        }
        return replacements;
    }
}
