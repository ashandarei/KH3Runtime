#include <chrono>
#include <memory>
#include <optional>
#include "moogle_material_enum_augmentation.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/bridge/utilities.h"
#include "kh3ap/bridge/catalog/update_catalog_state.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/NameTypes.hpp>
#include <algorithm>
#include <cstring>
#include <format>
#include <sstream>
#include <unordered_map>
#include <vector>
#include <utility>
#define NOMINMAX
#include <windows.h>

namespace kh3ap
{
    using namespace RC;
    using namespace RC::Unreal;
    namespace
    {
        constexpr auto initial_material_enum_log_chars = 3000;
        constexpr auto updated_material_enum_log_chars = 640;

        constexpr auto max_material_enum_entries = 512;
        constexpr auto shop_initialization_delay = std::chrono::milliseconds(250);
        constexpr auto shop_pending_retry_interval = std::chrono::milliseconds(500);
        constexpr auto max_material_enum_names = 2048;
        constexpr auto max_material_enum_capacity = 4096;
        constexpr auto first_custom_material_enum_value = 100;
        constexpr auto material_enum_retry_limit = 30;
    } // namespace

    struct MoogleMaterialEnumAugmentation::Impl
    {
        explicit Impl(MoogleMaterialEnumConfig value) : config(std::move(value))
        {
        }

        struct EnumNamePairRaw
        {
            FName name{};
            int64_t value{};
        };

        static_assert(sizeof(EnumNamePairRaw) == 16, "Unexpected raw UEnum name pair size");

        static auto read_enum_names_header(UObject* enum_object) -> std::optional<ArrayHeader>
        {
            if (!enum_object || !kh3ap::is_uobject_valid_fast(enum_object))
            {
                return std::nullopt;
            }

            constexpr size_t names_offset = 0x58;
            ArrayHeader names_header{};
            std::memcpy(&names_header,
                        reinterpret_cast<const std::byte*>(enum_object) + names_offset,
                        sizeof(names_header));
            if (names_header.data_ptr == 0 || names_header.num < 0 ||
                names_header.max < names_header.num || names_header.num > max_material_enum_names ||
                names_header.max > max_material_enum_capacity)
            {
                return std::nullopt;
            }
            return names_header;
        }

        static auto write_enum_names_header(UObject* enum_object, const ArrayHeader& names_header)
            -> bool
        {
            if (!enum_object || !kh3ap::is_uobject_valid_fast(enum_object))
            {
                return false;
            }

            constexpr size_t names_offset = 0x58;
            auto* const header_ptr = reinterpret_cast<std::byte*>(enum_object) + names_offset;
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

        auto summarize_material_enum_entries(UObject* enum_object) const -> std::wstring
        {
            const auto names_header = read_enum_names_header(enum_object);
            if (!names_header.has_value())
            {
                return STR("<missing_or_invalid>");
            }

            std::vector<std::wstring> parts{};
            const int limit = std::min(names_header->num, max_material_enum_entries);
            for (int entry_index = 0; entry_index < limit; ++entry_index)
            {
                EnumNamePairRaw entry{};
                const auto* const entry_ptr = reinterpret_cast<const EnumNamePairRaw*>(
                    static_cast<uintptr_t>(names_header->data_ptr) +
                    static_cast<uintptr_t>(entry_index) * sizeof(EnumNamePairRaw));
                std::memcpy(&entry, entry_ptr, sizeof(entry));
                if ((entry.value >= config.material_item_index_min &&
                     entry.value <= config.material_item_index_max) ||
                    entry.value >= first_custom_material_enum_value)
                {
                    parts.push_back(std::format(STR("#{} {}={}"), entry_index,
                                                entry.name.ToString(), entry.value));
                }
            }
            parts.insert(parts.begin(),
                         std::format(STR("ptr=0x{:X} num={} max={}"), names_header->data_ptr,
                                     names_header->num, names_header->max));
            return join_wide_parts(parts, STR(" ; "));
        }

        auto append_material_enum_augmentation_log(
            std::wstring_view event, std::wstring_view detail, UObject* enum_object,
            const std::optional<ArrayHeader>& old_header,
            const std::optional<ArrayHeader>& new_header) const -> void
        {
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            const auto bridge_dir =
                config.bridge_dir_resolver ? config.bridge_dir_resolver() : current_bridge_dir;
            if (!bridge_dir.has_value())
            {
                return;
            }

            std::ostringstream json;
            json << "{";
            json << "\"schema\":\"kh3ap.moogle_shop_item_enum_augmentation.v1\"";
            json << ",\"recorded_at_utc\":\"" << now_iso8601_utc() << "\"";
            json << ",\"build_stamp\":\"" << json_escape(config.build_stamp) << "\"";
            json << ",\"event\":\"" << json_escape(event) << "\"";
            json << ",\"detail\":\"" << json_escape(detail) << "\"";
            json << ",\"enum_object\":\"" << json_escape(format_object_debug_name(enum_object))
                 << "\"";
            if (old_header.has_value())
            {
                json << ",\"old_num\":" << old_header->num;
                json << ",\"old_max\":" << old_header->max;
            }
            if (new_header.has_value())
            {
                json << ",\"new_num\":" << new_header->num;
                json << ",\"new_max\":" << new_header->max;
            }
            json << ",\"summary\":\""
                 << json_escape(truncate_for_log(summarize_material_enum_entries(enum_object),
                                                 initial_material_enum_log_chars))
                 << "\"";
            json << "}\n";
            append_text_file(*bridge_dir / "moogle_shop_item_enum_augmentation.jsonl", json.str());
#endif
        }

        auto try_apply_material_enum_augmentation() -> bool
        {
            UObject* const enum_object = UObjectGlobals::StaticFindObject<UObject*>(
                nullptr, nullptr, std::wstring{STR("/Script/TresGame.ETresItemDefMaterial")});
            const auto names_header = read_enum_names_header(enum_object);
            if (!enum_object || !names_header.has_value())
            {
                append_material_enum_augmentation_log(STR("retry"),
                                                      STR("missing_or_invalid_material_enum"),
                                                      enum_object, names_header, std::nullopt);
                return false;
            }

            std::vector<EnumNamePairRaw> existing{};
            existing.reserve(static_cast<size_t>(names_header->num));
            std::unordered_map<int64_t, std::wstring> names_by_reserved_value{};
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
                values_by_name[entry_name] = entry.value;
                if (entry.value >= config.material_item_index_min &&
                    entry.value <= config.material_item_index_max)
                {
                    names_by_reserved_value[entry.value] = entry_name;
                }
            }

            std::vector<EnumNamePairRaw> additions{};
            for (int item_index = config.material_item_index_min;
                 item_index <= config.material_item_index_max; ++item_index)
            {
                const std::wstring expected_name =
                    std::format(STR("ETresItemDefMaterial::AP_SHOP_ITEM{:03}"), item_index);
                const auto value_entry = names_by_reserved_value.find(item_index);
                const auto name_entry = values_by_name.find(expected_name);
                if (value_entry != names_by_reserved_value.end() &&
                    value_entry->second != expected_name)
                {
                    append_material_enum_augmentation_log(
                        STR("failed"),
                        std::format(STR("reserved_value_collision value={} name={}"), item_index,
                                    value_entry->second),
                        enum_object, names_header, std::nullopt);
                    return true;
                }
                if (name_entry != values_by_name.end() && name_entry->second != item_index)
                {
                    append_material_enum_augmentation_log(
                        STR("failed"),
                        std::format(STR("reserved_name_collision name={} value={}"), expected_name,
                                    name_entry->second),
                        enum_object, names_header, std::nullopt);
                    return true;
                }
                if (value_entry == names_by_reserved_value.end())
                {
                    additions.push_back(EnumNamePairRaw{
                        FName(expected_name, FNAME_Add),
                        item_index,
                    });
                }
            }

            if (additions.empty())
            {
                append_material_enum_augmentation_log(
                    STR("already_present"), STR("all_reserved_material_entries_registered"),
                    enum_object, names_header, names_header);
                return true;
            }

            size_t insertion_index = existing.size();
            for (size_t entry_index = 0; entry_index < existing.size(); ++entry_index)
            {
                if (existing[entry_index].value >= first_custom_material_enum_value)
                {
                    insertion_index = entry_index;
                    break;
                }
            }
            if (insertion_index == existing.size())
            {
                append_material_enum_augmentation_log(STR("failed"),
                                                      STR("material_enum_max_sentinels_not_found"),
                                                      enum_object, names_header, std::nullopt);
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
                append_material_enum_augmentation_log(STR("retry"), STR("ue_realloc_failed"),
                                                      enum_object, names_header, std::nullopt);
                return false;
            }
            std::memcpy(new_storage, patched.data(), bytes);

            const ArrayHeader new_header{
                reinterpret_cast<uint64_t>(new_storage),
                static_cast<int32_t>(patched.size()),
                static_cast<int32_t>(patched.size()),
            };
            const bool wrote_header = write_enum_names_header(enum_object, new_header);
            append_material_enum_augmentation_log(
                wrote_header ? STR("patched") : STR("failed"),
                wrote_header ? STR("registered_reserved_material_entries_before_max_sentinels")
                             : STR("header_write_failed"),
                enum_object, names_header,
                wrote_header ? std::optional<ArrayHeader>{new_header} : std::nullopt);
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] MoogleShop material enum augmentation event={} old_num={} "
                    "new_num={} additions={} summary={}\n"),
                wrote_header ? STR("patched") : STR("failed"), names_header->num, new_header.num,
                additions.size(),
                truncate_for_log(summarize_material_enum_entries(enum_object),
                                 updated_material_enum_log_chars));
            return wrote_header;
        }

        auto process_material_enum_augmentation(std::chrono::steady_clock::time_point now,
                                                bool eligible) -> void
        {
            if (!config.enabled || !eligible || material_enum_augmentation_done ||
                material_enum_augmentation_attempts >= material_enum_retry_limit)
            {
                return;
            }

            const auto unreal_init_time = config.unreal_init_time_resolver
                                              ? config.unreal_init_time_resolver()
                                              : std::chrono::steady_clock::time_point{};
            if (!has_elapsed_since(unreal_init_time, now, shop_initialization_delay))
            {
                return;
            }
            if (last_material_enum_augmentation_attempt !=
                    std::chrono::steady_clock::time_point{} &&
                !has_elapsed_since(last_material_enum_augmentation_attempt, now,
                                   shop_pending_retry_interval))
            {
                return;
            }

            ++material_enum_augmentation_attempts;
            last_material_enum_augmentation_attempt = now;
            material_enum_augmentation_done = try_apply_material_enum_augmentation();
        }

        MoogleMaterialEnumConfig config;
        std::optional<std::filesystem::path> current_bridge_dir;
        bool material_enum_augmentation_done{};
        int material_enum_augmentation_attempts{};
        std::chrono::steady_clock::time_point last_material_enum_augmentation_attempt{};
    };

    MoogleMaterialEnumAugmentation::MoogleMaterialEnumAugmentation(MoogleMaterialEnumConfig config)
        : impl_(std::make_unique<Impl>(std::move(config)))
    {
    }

    MoogleMaterialEnumAugmentation::~MoogleMaterialEnumAugmentation() = default;

    auto MoogleMaterialEnumAugmentation::has_pending_work() const -> bool
    {
        return impl_->config.enabled && !impl_->material_enum_augmentation_done &&
               impl_->material_enum_augmentation_attempts < material_enum_retry_limit;
    }

    auto MoogleMaterialEnumAugmentation::process(
        std::chrono::steady_clock::time_point now, bool eligible,
        const std::optional<std::filesystem::path>& bridge_dir) -> void
    {
        impl_->current_bridge_dir = bridge_dir;
        impl_->process_material_enum_augmentation(now, eligible);
    }
} // namespace kh3ap
