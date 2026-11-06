#include "kh3ap/diagnostics/autosave/detour.h"
#include "kh3ap/diagnostics/kh3_autosave_probe.h"
#include "kh3ap/diagnostics/autosave/support.h"
#include "kh3ap/bridge/rewards/reconciliation.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include "kh3ap/bridge/utilities.h"
#include <intrin.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
#include <UnrealDef.hpp>

using namespace RC;
using namespace RC::Unreal;

namespace kh3ap
{
    using namespace autosave_detail;
    auto Kh3AutosaveProbe::capture_save_file_callstack(uintptr_t caller_return_address)
        -> SaveFileCallstackSnapshot
    {
        SaveFileCallstackSnapshot snapshot{};
        snapshot.pre_call_filetime_100ns = current_windows_filetime_100ns();
        snapshot.thread_id = GetCurrentThreadId();

        const std::optional<MainModuleImageRange> main_module = try_get_main_module_image_range();
        if (!main_module.has_value())
        {
            return snapshot;
        }

        const auto is_main_module_address = [&main_module](uintptr_t address)
        {
            return address >= main_module->base && address - main_module->base < main_module->size;
        };
        if (is_main_module_address(caller_return_address))
        {
            snapshot.caller_rva = static_cast<uint64_t>(caller_return_address - main_module->base);
        }

        std::array<void*, save_file_callstack_capacity> raw_frames{};
        const USHORT captured_frame_count = RtlCaptureStackBackTrace(
            0, static_cast<ULONG>(raw_frames.size()), raw_frames.data(), nullptr);
        snapshot.captured_frame_count = captured_frame_count;
        for (USHORT index = 0; index < captured_frame_count; ++index)
        {
            const uintptr_t address = reinterpret_cast<uintptr_t>(raw_frames[index]);
            if (!is_main_module_address(address) ||
                snapshot.main_module_frame_count >= snapshot.main_module_stack_rvas.size())
            {
                continue;
            }

            snapshot.main_module_stack_rvas[snapshot.main_module_frame_count++] =
                static_cast<uint64_t>(address - main_module->base);
        }

        if (snapshot.main_module_frame_count != 0)
        {
            constexpr uint64_t fnv_offset_basis = 14695981039346656037ULL;
            constexpr uint64_t fnv_prime = 1099511628211ULL;
            uint64_t stack_hash = fnv_offset_basis;
            for (uint32_t index = 0; index < snapshot.main_module_frame_count; ++index)
            {
                stack_hash ^= snapshot.main_module_stack_rvas[index];
                stack_hash *= fnv_prime;
            }
            snapshot.main_module_stack_hash = stack_hash;
        }

        return snapshot;
    }

    void __fastcall Kh3AutosaveProbe::hooked_native_save_request(void* arg0, void* arg1, void* arg2,
                                                                 void* arg3)
    {
        Kh3AutosaveProbe* const owner = s_native_save_request_probe_owner;
        if (!owner || owner->native_save_request_trampoline_ == 0)
        {
            return;
        }

        const SaveFileCallstackSnapshot callstack =
            capture_save_file_callstack(reinterpret_cast<uintptr_t>(_ReturnAddress()));
        owner->record_native_save_probe_event(callstack);
        reinterpret_cast<NativeSaveRequestFn>(owner->native_save_request_trampoline_)(arg0, arg1,
                                                                                      arg2, arg3);
    }

    auto Kh3AutosaveProbe::install_native_save_request_probe(std::wstring_view reason) -> void
    {
        if (native_save_request_probe_attempted_ || native_save_request_hook_installed_)
        {
            return;
        }
        native_save_request_probe_attempted_ = true;

        const std::optional<MainModuleImageRange> main_module = try_get_main_module_image_range();
        const NativeSaveRequestProfile* const profile =
            main_module.has_value() ? native_save_request_profile_for_image(*main_module) : nullptr;
        native_save_request_rva_ = profile ? profile->request_rva : 0;
        native_save_request_storefront_ =
            profile ? std::wstring{profile->storefront} : STR("unsupported");
        const uintptr_t request_address =
            main_module.has_value() && profile ? main_module->base + profile->request_rva : 0;

        std::vector<std::byte> request_signature(profile ? profile->signature_size : 0);
        const bool request_signature_ok =
            profile && profile->signature && profile->signature_size != 0 &&
            read_process_bytes(request_address, request_signature.data(),
                               request_signature.size()) &&
            std::memcmp(request_signature.data(), profile->signature, profile->signature_size) == 0;

        s_native_save_request_probe_owner = this;
        if (request_signature_ok)
        {
            native_save_request_detour_.reset(new PLH::x64Detour(
                request_address,
                reinterpret_cast<uint64_t>(&Kh3AutosaveProbe::hooked_native_save_request),
                &native_save_request_trampoline_));
            native_save_request_hook_installed_ = native_save_request_detour_->hook();
            if (!native_save_request_hook_installed_)
            {
                native_save_request_detour_.reset();
                native_save_request_trampoline_ = 0;
            }
        }

        if (!native_save_request_hook_installed_ && s_native_save_request_probe_owner == this)
        {
            s_native_save_request_probe_owner = nullptr;
        }

        const std::optional<std::filesystem::path> bridge_dir =
            config_.bridge_dir_resolver ? config_.bridge_dir_resolver() : std::nullopt;
        if (bridge_dir.has_value())
        {
            const auto write_hook_status =
                [this, &bridge_dir, reason](std::wstring_view function_name, uintptr_t function_rva,
                                            uintptr_t function_address, bool signature_ok,
                                            bool installed)
            {
                std::ostringstream stream;
                stream << "{\"type\":\"autosave_probe_native_save_hook\"";
                stream << ",\"observed_at\":\"" << now_iso8601_utc() << "\"";
                json_field(stream, "build_stamp", config_.build_stamp);
                json_field(stream, "function_name", function_name);
                json_field(stream, "reason", reason);
                json_field(stream, "hook_scope", STR("kh3_main_module_rva"));
                json_field(stream, "storefront", native_save_request_storefront_);
                stream << ",\"function_rva\":\"0x" << std::hex << function_rva << std::dec << "\"";
                stream << ",\"target_address\":\"0x" << std::hex << function_address << std::dec
                       << "\"";
                stream << ",\"signature_ok\":" << (signature_ok ? "true" : "false");
                stream << ",\"installed\":" << (installed ? "true" : "false");
                stream << "}\n";
                append_text_file(*bridge_dir / std::filesystem::path{installed_hooks_filename},
                                 stream.str());
            };
            write_hook_status(STR("NativeAutoSaveRequest"), native_save_request_rva_,
                              request_address, request_signature_ok,
                              native_save_request_hook_installed_);
        }

        Output::send<LogLevel::Warning>(
            STR("[KH3APProbeCpp] Native automatic save request hook storefront={} "
                "image_size=0x{:x} image_timestamp=0x{:08x} rva=0x{:x} signature={} "
                "installed={}\n"),
            native_save_request_storefront_, main_module.has_value() ? main_module->size : 0,
            main_module.has_value() ? main_module->timestamp : 0, native_save_request_rva_,
            request_signature_ok, native_save_request_hook_installed_);
    }

    auto Kh3AutosaveProbe::uninstall_native_save_request_probe() -> void
    {
        if (native_save_request_detour_)
        {
            if (native_save_request_hook_installed_)
            {
                native_save_request_detour_->unHook();
            }
            native_save_request_detour_.reset();
        }
        native_save_request_hook_installed_ = false;
        native_save_request_trampoline_ = 0;
        if (s_native_save_request_probe_owner == this)
        {
            s_native_save_request_probe_owner = nullptr;
        }
    }

    auto
    Kh3AutosaveProbe::record_native_save_probe_event(const SaveFileCallstackSnapshot& callstack)
        -> void
    {
        reconciliation::auto_save_begin(callstack.pre_call_filetime_100ns);
        std::lock_guard lock{native_save_probe_event_mutex_};
        const size_t index = native_save_probe_event_count_ < native_save_probe_events_.size()
                                 ? native_save_probe_event_count_++
                                 : native_save_probe_events_.size() - 1;
        native_save_probe_events_[index] = NativeSaveProbeEvent{
            .event_index = ++native_save_probe_event_sequence_,
            .callstack = callstack,
        };
    }

    auto Kh3AutosaveProbe::flush_native_save_probe_events() -> void
    {
        std::array<NativeSaveProbeEvent, 32> events{};
        size_t event_count = 0;
        {
            std::lock_guard lock{native_save_probe_event_mutex_};
            event_count = native_save_probe_event_count_;
            if (event_count == 0)
            {
                return;
            }
            for (size_t index = 0; index < event_count; ++index)
            {
                events[index] = native_save_probe_events_[index];
            }
            native_save_probe_events_ = {};
            native_save_probe_event_count_ = 0;
        }

        const std::optional<std::filesystem::path> bridge_dir =
            config_.bridge_dir_resolver ? config_.bridge_dir_resolver() : std::nullopt;
        for (size_t index = 0; index < event_count; ++index)
        {
            const NativeSaveProbeEvent& event = events[index];
            if (!bridge_dir.has_value())
            {
                continue;
            }

            std::string observed_at =
                format_windows_filetime_utc(event.callstack.pre_call_filetime_100ns);
            if (observed_at.empty())
            {
                observed_at = now_iso8601_utc();
            }
            std::ostringstream stream;
            stream << "{\"type\":\"autosave_probe_auto_save_request\"";
            stream << ",\"observed_at\":\"" << observed_at << "\"";
            stream << ",\"observed_filetime_100ns\":" << event.callstack.pre_call_filetime_100ns;
            stream << ",\"timestamp_semantics\":\"entry to KH3 automatic save request before "
                      "slot-0 serialization\"";
            stream << ",\"process_id\":" << GetCurrentProcessId();
            stream << ",\"process_creation_filetime_100ns\":"
                   << current_process_creation_filetime_100ns();
            json_field(stream, "build_stamp", config_.build_stamp);
            stream << ",\"event_index\":" << event.event_index;
            stream << ",\"request_rva\":\"0x" << std::hex << native_save_request_rva_ << std::dec
                   << "\"";
            stream << ",\"thread_id\":" << event.callstack.thread_id;
            stream << ",\"caller_rva\":\"0x" << std::hex << event.callstack.caller_rva << std::dec
                   << "\"";
            stream << ",\"captured_frame_count\":" << event.callstack.captured_frame_count;
            stream << ",\"main_module_frame_count\":" << event.callstack.main_module_frame_count;
            stream << ",\"main_module_stack_hash\":\"0x" << std::hex
                   << event.callstack.main_module_stack_hash << std::dec << "\"";
            stream << ",\"main_module_stack_rvas\":[";
            for (uint32_t frame_index = 0; frame_index < event.callstack.main_module_frame_count;
                 ++frame_index)
            {
                if (frame_index != 0)
                {
                    stream << ',';
                }
                stream << "\"0x" << std::hex << event.callstack.main_module_stack_rvas[frame_index]
                       << std::dec << "\"";
            }
            stream << ']';
            if (config_.runtime_context_resolver)
            {
                json_field(stream, "runtime_context_at_flush", config_.runtime_context_resolver());
            }
            stream << '}';

            const std::string json = stream.str();
            write_text_file(*bridge_dir / std::filesystem::path{native_save_latest_filename}, json);
            append_text_file(*bridge_dir / std::filesystem::path{native_save_jsonl_filename},
                             json + "\n");
        }
    }
} // namespace kh3ap
