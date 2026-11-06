#include "kh3ap/diagnostics/kh3_autosave_probe.h"
#include "kh3ap/diagnostics/autosave/support.h"
#include "kh3ap/bridge/rewards/reconciliation.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include "kh3ap/bridge/utilities.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <UnrealDef.hpp>

using namespace RC;
using namespace RC::Unreal;

namespace kh3ap
{
    using namespace autosave_detail;
    auto Kh3AutosaveProbe::record_save_file_open(const wchar_t* path,
                                                 uint64_t observed_filetime_100ns,
                                                 const SaveFileCallstackSnapshot& callstack,
                                                 uint32_t desired_access, uint32_t share_mode,
                                                 uint32_t creation_disposition,
                                                 uint32_t flags_and_attributes) -> uint64_t
    {
        if (!path)
        {
            return 0;
        }

        std::lock_guard lock{save_file_open_event_mutex_};
        const size_t index = save_file_open_event_count_ < save_file_open_events_.size()
                                 ? save_file_open_event_count_++
                                 : save_file_open_events_.size() - 1;
        save_file_open_events_[index] = SaveFileOpenEvent{
            .event_index = ++save_file_open_event_sequence_,
            .observed_filetime_100ns = observed_filetime_100ns,
            .callstack = callstack,
            .path = std::wstring{path},
            .desired_access = desired_access,
            .share_mode = share_mode,
            .creation_disposition = creation_disposition,
            .flags_and_attributes = flags_and_attributes,
        };
        const auto event = save_file_open_events_[index].event_index;
        reconciliation::file_open(path, desired_access, event, observed_filetime_100ns);
        return event;
    }

    auto Kh3AutosaveProbe::track_manual_save_handle(const wchar_t* path, uint32_t desired_access,
                                                    void* handle, uint64_t open_event_index) -> void
    {
        if (!path || !handle || handle == INVALID_HANDLE_VALUE)
        {
            return;
        }
        const auto slot_number = try_parse_manual_kh3_save_slot(path, desired_access);
        constexpr uint32_t generic_write = 0x40000000u;
        const bool olympus_coliseum_progress = (desired_access & generic_write) != 0 &&
                                               path_is_olympus_coliseum_progress_save_file(path);
        if (!slot_number.has_value() && !olympus_coliseum_progress)
        {
            return;
        }

        std::lock_guard lock{manual_save_handle_mutex_};
        for (size_t index = 0; index < manual_save_handle_count_; ++index)
        {
            if (manual_save_handles_[index].handle == handle)
            {
                manual_save_handles_[index] =
                    ManualSaveHandleEntry{.handle = handle,
                                          .slot_number = slot_number.value_or(-1),
                                          .path = std::wstring{path},
                                          .olympus_coliseum_progress = olympus_coliseum_progress,
                                          .open_event_index = open_event_index};
                return;
            }
        }
        const size_t index = manual_save_handle_count_ < manual_save_handles_.size()
                                 ? manual_save_handle_count_++
                                 : manual_save_handles_.size() - 1;
        manual_save_handles_[index] =
            ManualSaveHandleEntry{.handle = handle,
                                  .slot_number = slot_number.value_or(-1),
                                  .path = std::wstring{path},
                                  .olympus_coliseum_progress = olympus_coliseum_progress,
                                  .open_event_index = open_event_index};
    }

    auto Kh3AutosaveProbe::record_save_write(void* handle, bool success, uint64_t bytes_written)
        -> void
    {
        std::lock_guard lock{manual_save_handle_mutex_};
        for (size_t i = 0; i < manual_save_handle_count_; ++i)
        {
            auto& entry = manual_save_handles_[i];
            if (entry.handle == handle)
            {
                entry.write_failed = entry.write_failed || !success;
                entry.bytes_written += bytes_written;
                break;
            }
        }
    }

    auto Kh3AutosaveProbe::record_manual_save_handle_closed(void* handle) -> void
    {
        ManualSaveHandleEntry closed{};
        {
            std::lock_guard lock{manual_save_handle_mutex_};
            size_t found_index = manual_save_handle_count_;
            for (size_t index = 0; index < manual_save_handle_count_; ++index)
            {
                if (manual_save_handles_[index].handle == handle)
                {
                    found_index = index;
                    break;
                }
            }
            if (found_index == manual_save_handle_count_)
            {
                return;
            }
            closed = std::move(manual_save_handles_[found_index]);
            --manual_save_handle_count_;
            if (found_index != manual_save_handle_count_)
            {
                manual_save_handles_[found_index] =
                    std::move(manual_save_handles_[manual_save_handle_count_]);
            }
            manual_save_handles_[manual_save_handle_count_] = {};
        }

        reconciliation::file_closed(closed.open_event_index,
                                    !closed.write_failed && closed.bytes_written > 0);
        std::lock_guard lock{manual_save_completed_event_mutex_};
        const size_t index =
            manual_save_completed_event_count_ < manual_save_completed_events_.size()
                ? manual_save_completed_event_count_++
                : manual_save_completed_events_.size() - 1;
        manual_save_completed_events_[index] =
            ManualSaveCompletedEvent{.event_index = ++manual_save_completed_event_sequence_,
                                     .slot_number = closed.slot_number,
                                     .path = std::move(closed.path),
                                     .olympus_coliseum_progress = closed.olympus_coliseum_progress,
                                     .open_event_index = closed.open_event_index,
                                     .bytes_written = closed.bytes_written,
                                     .write_failed = closed.write_failed};
        manual_save_completed_events_pending_.store(true, std::memory_order_release);
    }

    auto Kh3AutosaveProbe::flush_manual_save_completed_events() -> void
    {
        std::array<ManualSaveCompletedEvent, 16> events{};
        size_t event_count = 0;
        {
            std::lock_guard lock{manual_save_completed_event_mutex_};
            event_count = manual_save_completed_event_count_;
            // Clear while holding the producer's mutex so an event arriving
            // after this drain publishes a fresh wakeup and cannot be stranded.
            manual_save_completed_events_pending_.store(false, std::memory_order_release);
            if (event_count == 0)
            {
                return;
            }
            for (size_t index = 0; index < event_count; ++index)
            {
                events[index] = std::move(manual_save_completed_events_[index]);
            }
            manual_save_completed_events_ = {};
            manual_save_completed_event_count_ = 0;
        }

        const auto bridge_dir =
            config_.bridge_dir_resolver ? config_.bridge_dir_resolver() : std::nullopt;
        for (size_t index = 0; index < event_count; ++index)
        {
            const ManualSaveCompletedEvent& event = events[index];
            const bool save_succeeded = !event.write_failed && event.bytes_written > 0;
            if (event.olympus_coliseum_progress)
            {
                Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] OlympusColiseum progress save "
                        "completed trigger=CloseHandle:post file={}\n"),
                    std::filesystem::path{event.path}.filename().wstring());
                if (save_succeeded && config_.olympus_coliseum_save_completed_handler)
                {
                    config_.olympus_coliseum_save_completed_handler(
                        std::filesystem::path{event.path});
                }
                continue;
            }
            if (bridge_dir.has_value())
            {
                std::ostringstream stream;
                stream << "{\"type\":\"autosave_probe_manual_save_completed\"";
                stream << ",\"observed_at\":\"" << now_iso8601_utc() << "\"";
                json_field(stream, "build_stamp", config_.build_stamp);
                stream << ",\"event_index\":" << event.event_index;
                stream << ",\"slot_number\":" << event.slot_number;
                stream << ",\"open_event_index\":" << event.open_event_index;
                stream << ",\"process_id\":" << GetCurrentProcessId();
                stream << ",\"process_creation_filetime_100ns\":"
                       << current_process_creation_filetime_100ns();
                stream << ",\"save_succeeded\":" << (save_succeeded ? "true" : "false");
                stream << ",\"bytes_written\":" << event.bytes_written;
                stream << ",\"write_failed\":" << (event.write_failed ? "true" : "false");
                json_field(stream, "path", event.path);
                json_field(stream, "completion_signal", STR("CloseHandle:post"));
                stream << "}";
                const std::string json = stream.str();
                write_text_file(*bridge_dir / std::filesystem::path{save_completed_latest_filename},
                                json);
                append_text_file(*bridge_dir / std::filesystem::path{save_completed_jsonl_filename},
                                 json + "\n");
            }
            Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] AutosaveProbe manual save "
                                                "completed slot={} trigger=CloseHandle:post\n"),
                                            event.slot_number);
            if (save_succeeded && event.slot_number > 0 && config_.manual_save_completed_handler)
            {
                config_.manual_save_completed_handler(event.slot_number);
            }
        }
    }

    auto Kh3AutosaveProbe::flush_save_file_open_events() -> void
    {
        std::array<SaveFileOpenEvent, 32> events{};
        size_t event_count = 0;
        {
            std::lock_guard lock{save_file_open_event_mutex_};
            event_count = save_file_open_event_count_;
            if (event_count == 0)
            {
                return;
            }
            for (size_t index = 0; index < event_count; ++index)
            {
                events[index] = save_file_open_events_[index];
            }
            save_file_open_event_count_ = 0;
            save_file_open_events_ = {};
        }

        const std::optional<std::filesystem::path> bridge_dir =
            config_.bridge_dir_resolver ? config_.bridge_dir_resolver() : std::nullopt;
        for (size_t index = 0; index < event_count; ++index)
        {
            const SaveFileOpenEvent& event = events[index];

            if (bridge_dir.has_value())
            {
                std::string observed_at =
                    format_windows_filetime_utc(event.observed_filetime_100ns);
                if (observed_at.empty())
                {
                    observed_at = now_iso8601_utc();
                }
                std::string pre_call_observed_at =
                    format_windows_filetime_utc(event.callstack.pre_call_filetime_100ns);
                std::ostringstream stream;
                stream << "{\"type\":\"autosave_probe_file_open\"";
                stream << ",\"observed_at\":\"" << observed_at << "\"";
                stream << ",\"observed_filetime_100ns\":" << event.observed_filetime_100ns;
                stream << ",\"timestamp_semantics\":\"CreateFileW post-open callback time\"";
                stream << ",\"process_id\":" << GetCurrentProcessId();
                stream << ",\"process_creation_filetime_100ns\":"
                       << current_process_creation_filetime_100ns();
                stream << ",\"pre_call_observed_at\":\"" << pre_call_observed_at << "\"";
                stream << ",\"pre_call_filetime_100ns\":"
                       << event.callstack.pre_call_filetime_100ns;
                stream << ",\"pre_call_timestamp_semantics\":\"CreateFileW pre-call hook time "
                          "before native file open\"";
                stream << ",\"callstack_semantics\":\"pre-CreateFileW capture; KH3 main executable "
                          "RVAs\"";
                json_field(stream, "build_stamp", config_.build_stamp);
                stream << ",\"event_index\":" << event.event_index;
                stream << ",\"receipt_snapshot_version\":1";
                json_field(stream, "function_name", STR("CreateFileW"));
                json_field(stream, "path", event.path);
                stream << ",\"thread_id\":" << event.callstack.thread_id;
                stream << ",\"caller_rva\":\"0x" << std::hex << event.callstack.caller_rva
                       << std::dec << "\"";
                stream << ",\"captured_frame_count\":" << event.callstack.captured_frame_count;
                stream << ",\"main_module_frame_count\":"
                       << event.callstack.main_module_frame_count;
                stream << ",\"main_module_stack_hash\":\"0x" << std::hex
                       << event.callstack.main_module_stack_hash << std::dec << "\"";
                stream << ",\"main_module_stack_rvas\":[";
                for (uint32_t frame_index = 0;
                     frame_index < event.callstack.main_module_frame_count; ++frame_index)
                {
                    if (frame_index != 0)
                    {
                        stream << ',';
                    }
                    stream << "\"0x" << std::hex
                           << event.callstack.main_module_stack_rvas[frame_index] << std::dec
                           << "\"";
                }
                stream << ']';
                stream << ",\"desired_access\":" << event.desired_access;
                stream << ",\"share_mode\":" << event.share_mode;
                stream << ",\"creation_disposition\":" << event.creation_disposition;
                stream << ",\"flags_and_attributes\":" << event.flags_and_attributes;
                if (config_.runtime_context_resolver)
                {
                    json_field(stream, "runtime_context", config_.runtime_context_resolver());
                }
                stream << "}";

                const std::string json = stream.str();
                write_text_file(*bridge_dir / std::filesystem::path{save_file_latest_filename},
                                json);
                append_text_file(*bridge_dir / std::filesystem::path{save_file_jsonl_filename},
                                 json + "\n");
                save_file_open_events_flushed_ =
                    std::max(save_file_open_events_flushed_, event.event_index);
            }
        }
    }
} // namespace kh3ap
