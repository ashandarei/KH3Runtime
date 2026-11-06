#include <optional>
#include "kh3ap/app/config.h"
#include "kh3ap/hooks/memory/memory_hook_platform.h"
#include "kh3ap/hooks/memory/kh3_memory_hooks.h"
#include "kh3ap/hooks/memory/memory_hook_detail.h"
#include "kh3ap/bridge/transport/bridge_channels.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/bridge/utilities.h"
#include <UnrealDef.hpp>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <Unreal/UObjectGlobals.hpp>
#include <algorithm>
#include <array>
#include <cstring>
#include <cwctype>
#include <format>
#include <mutex>
#include <sstream>
#include <utility>

namespace kh3ap
{
    namespace
    {
        constexpr auto lucky_row_summary_log_chars = 1800;
        constexpr auto lucky_object_pointer_probe_bytes = 0x100;

        constexpr auto lucky_mark_capture_prefix_bytes = 32;
    } // namespace

    using ::kh3ap::settings::s_enable_lucky_mark_native_ap_datatable_row_dump_diagnostic;
    using ::kh3ap::settings::s_enable_lucky_mark_native_ap_resolver_event_diagnostics;
    using ::kh3ap::settings::s_enable_lucky_mark_native_ap_resolver_install_diagnostics;
    using ::kh3ap::settings::s_enable_lucky_mark_native_ftext_data_diagnostic;
    using ::kh3ap::settings::s_lucky_mark_native_ap_resolver_converter_sample_events;
    using ::kh3ap::settings::s_lucky_mark_native_ap_resolver_max_diagnostic_events;

    using namespace memory_hook_detail;
    auto Kh3MemoryHooks::append_lucky_mark_native_ap_resolver_install_diagnostic_log(
        std::wstring_view event, std::wstring_view target_label, uint64_t expected_offset,
        uintptr_t module_base, size_t module_size, uintptr_t target, bool hook_result,
        std::wstring_view detail) const -> void
    {
        if constexpr (!s_enable_lucky_mark_native_ap_resolver_install_diagnostics)
        {
            return;
        }
        if (!config_.bridge_dir_resolver)
        {
            return;
        }
        const auto bridge_dir = config_.bridge_dir_resolver();
        if (!bridge_dir.has_value())
        {
            return;
        }

        const uintptr_t capture_start = target >= lucky_mark_capture_prefix_bytes
                                            ? target - lucky_mark_capture_prefix_bytes
                                            : target;
        const std::wstring bytes =
            target != 0 ? try_probe_pointer_bytes(static_cast<uint64_t>(capture_start), 128)
                              .value_or(STR("<unreadable>"))
                        : std::wstring{};

        std::ostringstream json;
        json << "{\"schema\":\"kh3ap.lucky_mark_native_ap_resolver_install_diagnostic.v1\"";
        json << ",\"recorded_at_utc\":\"" << now_iso8601_utc() << "\"";
        json << ",\"build_stamp\":\"" << json_escape(config_.build_stamp) << "\"";
        json << ",\"event\":\"" << json_escape(event) << "\"";
        json << ",\"target_label\":\"" << json_escape(target_label) << "\"";
        json << ",\"module_base\":\"0x" << std::hex << module_base << std::dec << "\"";
        json << ",\"module_size\":" << module_size;
        json << ",\"expected_offset\":\"0x" << std::hex << expected_offset << std::dec << "\"";
        json << ",\"target\":\"0x" << std::hex << target << std::dec << "\"";
        if (target >= module_base && module_base != 0)
        {
            json << ",\"target_offset\":\"0x" << std::hex << (target - module_base) << std::dec
                 << "\"";
        }
        json << ",\"hook_result\":" << (hook_result ? "true" : "false");
        json << ",\"detail\":\"" << json_escape(detail) << "\"";
        json << ",\"bytes_around_target\":\"" << json_escape(bytes) << "\"";
        json << "}\n";
        append_text_file(*bridge_dir / "lucky_mark_native_ap_resolver_install_diagnostics.jsonl",
                         json.str());
    }

    auto Kh3MemoryHooks::append_lucky_mark_reward_token_to_item_id_diagnostic_event(
        void* resolver, uint64_t reward_token_name, int32_t result) -> void
    {
        if constexpr (!s_enable_lucky_mark_native_ap_resolver_event_diagnostics)
        {
            return;
        }
        if (!config_.bridge_dir_resolver)
        {
            return;
        }

        const auto decoded_name = decode_name_value(reward_token_name);
        const bool name_interesting = decoded_name_contains_interest(decoded_name);
        std::lock_guard<std::mutex> lock{state_.lucky_mark_native_ap_resolver_diagnostic_mutex};
        const uint64_t seen_count = ++state_.lucky_mark_native_ap_resolver_converter_seen_count;
        const bool sample_event =
            seen_count <=
            static_cast<uint64_t>(s_lucky_mark_native_ap_resolver_converter_sample_events);
        const bool result_interesting = result <= 0;
        if (!sample_event && !name_interesting && !result_interesting)
        {
            return;
        }
        if (state_.lucky_mark_native_ap_resolver_diagnostic_event_count >=
            static_cast<uint64_t>(s_lucky_mark_native_ap_resolver_max_diagnostic_events))
        {
            return;
        }
        const uint64_t event_index = ++state_.lucky_mark_native_ap_resolver_diagnostic_event_count;
        const uint64_t logged_count = ++state_.lucky_mark_native_ap_resolver_converter_logged_count;

        const auto bridge_dir = config_.bridge_dir_resolver();
        if (!bridge_dir.has_value())
        {
            return;
        }

        const bool token_looks_like_pointer = looks_like_process_pointer(reward_token_name);
        const uintptr_t reward_address = static_cast<uintptr_t>(reward_token_name);
        const std::wstring reward_bytes =
            token_looks_like_pointer
                ? try_probe_pointer_bytes(static_cast<uint64_t>(reward_address), 112)
                      .value_or(STR("<unreadable>"))
                : std::wstring{STR("<not_pointer>")};
        const std::wstring candidate_names =
            token_looks_like_pointer
                ? describe_name_candidates(reward_address,
                                           {0x00, 0x08, 0x10, 0x18, 0x20, 0x28, 0x30})
                : std::wstring{STR("<not_pointer>")};
        const std::wstring candidate_qwords =
            token_looks_like_pointer
                ? describe_qword_candidates(reward_address,
                                            {0x00, 0x08, 0x10, 0x18, 0x20, 0x28, 0x30})
                : std::wstring{STR("<not_pointer>")};

        std::ostringstream json;
        json << "{\"schema\":\"kh3ap.lucky_mark_native_ap_resolver_diagnostic_event.v1\"";
        json << ",\"recorded_at_utc\":\"" << now_iso8601_utc() << "\"";
        json << ",\"build_stamp\":\"" << json_escape(config_.build_stamp) << "\"";
        json << ",\"event_index\":" << event_index;
        json << ",\"phase_seen_count\":" << seen_count;
        json << ",\"phase_logged_count\":" << logged_count;
        json << ",\"phase\":\"reward_token_to_item_id\"";
        json << ",\"thread_id\":" << GetCurrentThreadId();
        json << ",\"resolver\":\"" << format_hex_pointer(reinterpret_cast<uintptr_t>(resolver))
             << "\"";
        json << ",\"reward_token_name\":\"0x" << std::hex << reward_token_name << std::dec << "\"";
        json << ",\"reward_token_decoded\":\"" << json_escape(format_decoded_name(decoded_name))
             << "\"";
        json << ",\"reward_token_name_interesting\":" << (name_interesting ? "true" : "false");
        json << ",\"result_item_id\":" << result;
        json << ",\"candidate_fnames\":\"" << json_escape(candidate_names) << "\"";
        json << ",\"candidate_qwords\":\"" << json_escape(candidate_qwords) << "\"";
        json << ",\"reward_bytes\":\"" << json_escape(reward_bytes) << "\"";
        json << "}\n";
        append_text_file(*bridge_dir / channels::lucky_mark_native_ap_resolver_diagnostics,
                         json.str());
    }

    auto Kh3MemoryHooks::append_lucky_mark_item_id_to_reward_text_diagnostic_event(
        void* formatter, void* out_text, int32_t item_id, bool use_display_name, void* return_value,
        std::wstring_view out_text_before, bool datatable_text_applied,
        std::wstring_view datatable_display_name, bool fstring_text_applied,
        std::wstring_view formatter_display_text, bool shell_text_applied,
        int32_t formatter_item_id_used) -> void
    {
        if constexpr (!s_enable_lucky_mark_native_ap_resolver_event_diagnostics)
        {
            return;
        }
        if (!config_.bridge_dir_resolver)
        {
            return;
        }

        std::lock_guard<std::mutex> lock{state_.lucky_mark_native_ap_resolver_diagnostic_mutex};
        const uint64_t seen_count = ++state_.lucky_mark_native_ap_resolver_formatter_seen_count;
        const bool interesting_formatter_call = item_id <= 0 || datatable_text_applied ||
                                                shell_text_applied ||
                                                is_lucky_mark_native_ap_synthetic_item_id(item_id);
        if (!interesting_formatter_call)
        {
            return;
        }
        if (state_.lucky_mark_native_ap_resolver_diagnostic_event_count >=
            static_cast<uint64_t>(s_lucky_mark_native_ap_resolver_max_diagnostic_events))
        {
            return;
        }
        const uint64_t event_index = ++state_.lucky_mark_native_ap_resolver_diagnostic_event_count;
        const uint64_t logged_count = ++state_.lucky_mark_native_ap_resolver_formatter_logged_count;

        const auto bridge_dir = config_.bridge_dir_resolver();
        if (!bridge_dir.has_value())
        {
            return;
        }

        const uintptr_t out_text_address = reinterpret_cast<uintptr_t>(out_text);
        const uintptr_t return_address = reinterpret_cast<uintptr_t>(return_value);
        const std::wstring out_text_after =
            out_text_address != 0
                ? try_probe_pointer_bytes(static_cast<uint64_t>(out_text_address), 80)
                      .value_or(STR("<unreadable>"))
                : std::wstring{STR("<null>")};
        const std::wstring return_bytes =
            return_address != 0 ? try_probe_pointer_bytes(static_cast<uint64_t>(return_address), 80)
                                      .value_or(STR("<unreadable>"))
                                : std::wstring{STR("<null>")};
        const std::wstring out_text_qwords =
            describe_qword_candidates(out_text_address, {0x00, 0x08, 0x10, 0x18, 0x20});
        std::wstring ftext_data_diagnostic{};
        if constexpr (s_enable_lucky_mark_native_ftext_data_diagnostic)
        {
            if (shell_text_applied || datatable_text_applied ||
                is_lucky_mark_native_ap_synthetic_item_id(item_id))
            {
                ftext_data_diagnostic = describe_lucky_mark_native_ftext_data(out_text);
            }
        }

        std::ostringstream json;
        json << "{\"schema\":\"kh3ap.lucky_mark_native_ap_resolver_diagnostic_event.v1\"";
        json << ",\"recorded_at_utc\":\"" << now_iso8601_utc() << "\"";
        json << ",\"build_stamp\":\"" << json_escape(config_.build_stamp) << "\"";
        json << ",\"event_index\":" << event_index;
        json << ",\"phase_seen_count\":" << seen_count;
        json << ",\"phase_logged_count\":" << logged_count;
        json << ",\"phase\":\"item_id_to_reward_text\"";
        json << ",\"thread_id\":" << GetCurrentThreadId();
        json << ",\"formatter\":\"" << format_hex_pointer(reinterpret_cast<uintptr_t>(formatter))
             << "\"";
        json << ",\"out_text\":\"" << format_hex_pointer(out_text_address) << "\"";
        json << ",\"item_id\":" << item_id;
        json << ",\"use_display_name\":" << (use_display_name ? "true" : "false");
        json << ",\"return_value\":\"" << format_hex_pointer(return_address) << "\"";
        json << ",\"datatable_text_applied\":" << (datatable_text_applied ? "true" : "false");
        json << ",\"datatable_display_name\":\"" << json_escape(datatable_display_name) << "\"";
        json << ",\"fstring_text_applied\":" << (fstring_text_applied ? "true" : "false");
        json << ",\"formatter_display_text\":\"" << json_escape(formatter_display_text) << "\"";
        json << ",\"shell_text_applied\":" << (shell_text_applied ? "true" : "false");
        json << ",\"formatter_item_id_used\":" << formatter_item_id_used;
        const uintptr_t formatter_address = reinterpret_cast<uintptr_t>(formatter);
        const std::wstring formatter_head =
            formatter_address != 0
                ? try_probe_pointer_bytes(static_cast<uint64_t>(formatter_address), 0x140)
                      .value_or(STR("<unreadable>"))
                : std::wstring{STR("<null>")};
        const std::wstring formatter_qwords = describe_qword_candidates(
            formatter_address,
            {0x00, 0x08, 0x10, 0x18, 0x20, 0x28, 0x30,  0x38,  0x40,  0x48,  0x50, 0x58, 0x60,
             0x68, 0x70, 0x78, 0x80, 0x88, 0x90, 0x98,  0xA0,  0xA8,  0xB0,  0xB8, 0xC0, 0xC8,
             0xD0, 0xD8, 0xE0, 0xE8, 0xF0, 0xF8, 0x100, 0x108, 0x110, 0x118, 0x120});
        json << ",\"formatter_qwords\":\"" << json_escape(formatter_qwords) << "\"";
        json << ",\"formatter_head\":\"" << json_escape(formatter_head) << "\"";
        json << ",\"out_text_qwords\":\"" << json_escape(out_text_qwords) << "\"";
        if (!ftext_data_diagnostic.empty())
        {
            json << ",\"ftext_data_diagnostic\":\"" << json_escape(ftext_data_diagnostic) << "\"";
        }
        json << ",\"out_text_before\":\"" << json_escape(out_text_before) << "\"";
        json << ",\"out_text_after\":\"" << json_escape(out_text_after) << "\"";
        json << ",\"return_bytes\":\"" << json_escape(return_bytes) << "\"";
        json << "}\n";
        append_text_file(*bridge_dir / channels::lucky_mark_native_ap_resolver_diagnostics,
                         json.str());
    }

    auto Kh3MemoryHooks::should_dump_lucky_mark_native_ap_datatable_diagnostic(int32_t item_id)
        -> bool
    {
        if constexpr (!s_enable_lucky_mark_native_ap_datatable_row_dump_diagnostic)
        {
            return false;
        }
        if (!is_lucky_mark_native_ap_synthetic_item_id(item_id))
        {
            return false;
        }

        std::lock_guard<std::mutex> lock{state_.lucky_mark_native_ap_resolver_diagnostic_mutex};
        if (state_.lucky_mark_native_ap_resolver_datatable_dumped)
        {
            return false;
        }
        state_.lucky_mark_native_ap_resolver_datatable_dumped = true;
        return true;
    }

    auto Kh3MemoryHooks::append_lucky_mark_native_ap_datatable_diagnostic(void* formatter,
                                                                          int32_t item_id,
                                                                          std::wstring_view reason)
        -> void
    {
        if constexpr (!s_enable_lucky_mark_native_ap_datatable_row_dump_diagnostic)
        {
            return;
        }
        if (!config_.bridge_dir_resolver)
        {
            return;
        }

        const auto bridge_dir = config_.bridge_dir_resolver();
        if (!bridge_dir.has_value())
        {
            return;
        }

        const std::filesystem::path output_path =
            *bridge_dir / "lucky_mark_native_ap_formatter_datatable_diagnostics.jsonl";
        const std::wstring expected_item_row =
            lucky_mark_native_item_row_name_from_item_id(item_id);
        const uintptr_t formatter_address = reinterpret_cast<uintptr_t>(formatter);

        struct TableDiagnostic
        {
            std::wstring_view path;
            size_t row_read_size;
            bool is_item_table;
        };
        constexpr TableDiagnostic table_diagnostics[] = {
            {STR("/Game/Load/Tres/"
                 "TresLuckyMarkMilestoneRewardData.TresLuckyMarkMilestoneRewardData"),
             64, false},
            {STR("/Game/Load/Tres/TresItemData.TresItemData"), 160, true},
        };

        for (const TableDiagnostic& table_diagnostic : table_diagnostics)
        {
            RC::Unreal::UObject* const object =
                RC::Unreal::UObjectGlobals::StaticFindObject<RC::Unreal::UObject*>(
                    nullptr, nullptr, std::wstring{table_diagnostic.path});
            const auto candidates = collect_lucky_mark_native_row_map_candidates(object);
            const auto selected_header = select_lucky_mark_native_row_map_candidate(candidates);
            const auto header = selected_header.has_value()
                                    ? std::optional<MapHeader>{selected_header->header}
                                    : std::nullopt;
            uint64_t scanned = 0;
            uint64_t emitted = 0;
            if (object && kh3ap::is_uobject_valid_fast(object) && header.has_value())
            {
                const int limit = std::min(header->num, 12000);
                for (int index = 0; index < limit; ++index)
                {
                    const auto entry = read_lucky_mark_native_row_map_entry(*header, index);
                    if (!entry.has_value())
                    {
                        continue;
                    }
                    ++scanned;
                    const std::wstring row_name = entry->row_name.ToString();
                    const bool keep_row =
                        table_diagnostic.is_item_table
                            ? row_name == expected_item_row || row_name == STR("I02005")
                            : row_name == STR("1") || row_name == STR("5");
                    if (!keep_row)
                    {
                        continue;
                    }

                    const auto row_bytes = read_lucky_mark_native_row_bytes(
                        entry->row_ptr, table_diagnostic.row_read_size);
                    if (!row_bytes.has_value())
                    {
                        continue;
                    }
                    const std::wstring row_summary =
                        table_diagnostic.is_item_table
                            ? format_lucky_mark_native_item_row(row_name, entry->row_ptr,
                                                                *row_bytes)
                            : format_lucky_mark_native_milestone_row(row_name, entry->row_ptr,
                                                                     *row_bytes);

                    ++emitted;
                    std::ostringstream json;
                    json << "{";
                    json << "\"schema\":\"kh3ap.lucky_mark_native_formatter_datatable_row.v1\"";
                    json << ",\"recorded_at_utc\":\"" << now_iso8601_utc() << "\"";
                    json << ",\"build_stamp\":\"" << json_escape(config_.build_stamp) << "\"";
                    json << ",\"reason\":\"" << json_escape(reason) << "\"";
                    json << ",\"formatter\":\"" << format_hex_pointer(formatter_address) << "\"";
                    json << ",\"item_id\":" << item_id;
                    json << ",\"expected_item_row\":\"" << json_escape(expected_item_row) << "\"";
                    json << ",\"table\":\"" << json_escape(table_diagnostic.path) << "\"";
                    json << ",\"object\":\"" << json_escape(format_object_debug_name(object))
                         << "\"";
                    json << ",\"row_map_offset\":\"0x" << std::hex << std::uppercase
                         << selected_header->offset << std::dec << "\"";
                    json << ",\"row_map_ptr\":\"0x" << std::hex << std::uppercase
                         << header->data_ptr << std::dec << "\"";
                    json << ",\"row_map_num\":" << header->num;
                    json << ",\"row_map_max\":" << header->max;
                    json << ",\"row_index\":" << index;
                    json << ",\"row_name\":\"" << json_escape(row_name) << "\"";
                    json << ",\"row_summary\":\""
                         << json_escape(truncate_for_log(row_summary, lucky_row_summary_log_chars))
                         << "\"";
                    json << "}\n";
                    append_text_file(output_path, json.str());
                }
            }

            std::ostringstream json;
            json << "{";
            json << "\"schema\":\"kh3ap.lucky_mark_native_formatter_datatable_summary.v1\"";
            json << ",\"recorded_at_utc\":\"" << now_iso8601_utc() << "\"";
            json << ",\"build_stamp\":\"" << json_escape(config_.build_stamp) << "\"";
            json << ",\"reason\":\"" << json_escape(reason) << "\"";
            json << ",\"formatter\":\"" << format_hex_pointer(formatter_address) << "\"";
            json << ",\"item_id\":" << item_id;
            json << ",\"expected_item_row\":\"" << json_escape(expected_item_row) << "\"";
            json << ",\"table\":\"" << json_escape(table_diagnostic.path) << "\"";
            json << ",\"object\":\"" << json_escape(format_object_debug_name(object)) << "\"";
            if (header.has_value())
            {
                json << ",\"row_map_offset\":\"0x" << std::hex << std::uppercase
                     << selected_header->offset << std::dec << "\"";
                json << ",\"row_map_ptr\":\"0x" << std::hex << std::uppercase << header->data_ptr
                     << std::dec << "\"";
                json << ",\"row_map_num\":" << header->num;
                json << ",\"row_map_max\":" << header->max;
            }
            else
            {
                json << ",\"row_map_valid\":false";
            }
            json << ",\"row_map_candidates\":\""
                 << json_escape(summarize_lucky_mark_native_row_map_candidates(candidates)) << "\"";
            if (object && kh3ap::is_uobject_valid_fast(object))
            {
                json << ",\"object_head\":\""
                     << json_escape(try_probe_pointer_bytes(
                                        static_cast<uint64_t>(reinterpret_cast<uintptr_t>(object)),
                                        lucky_object_pointer_probe_bytes)
                                        .value_or(STR("<unreadable>")))
                     << "\"";
            }
            json << ",\"scanned\":" << scanned;
            json << ",\"emitted\":" << emitted;
            json << "}\n";
            append_text_file(output_path, json.str());
        }
    }
} // namespace kh3ap
