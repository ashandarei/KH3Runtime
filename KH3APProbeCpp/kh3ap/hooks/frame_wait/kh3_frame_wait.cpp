#include "kh3ap/core/platform/game_build.h"
#include "kh3ap/hooks/frame_wait/kh3_frame_wait.h"
#include "kh3ap/core/performance_build.h"

#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
#ifdef S
#pragma push_macro("S")
#undef S
#define KH3AP_FRAME_WAIT_RESTORE_S
#endif
#include <polyhook2/Detour/x64Detour.hpp>
#ifdef KH3AP_FRAME_WAIT_RESTORE_S
#pragma pop_macro("S")
#undef KH3AP_FRAME_WAIT_RESTORE_S
#endif
#include "kh3ap/hooks/frame_wait/frame_wait_policy.h"
#include "kh3ap/hooks/frame_wait/frame_wait_stub.h"
#include "kh3ap/bridge/transport/bridge_paths.h"
#include "kh3ap/bridge/serialization/json_fields.h"
#include "kh3ap/bridge/transport/bridge_io_helpers.h"
#include "kh3ap/bridge/utilities.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <UnrealDef.hpp>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace kh3ap
{
    namespace
    {
        // Patch after SwitchToThread, so the loop's back edge remains outside
        // the overwritten range and PolyHook only relocates xor ecx + Sleep(0).
        constexpr uintptr_t site_rva = 0x4B3E356;
        constexpr std::array<uint8_t, 14> expected_site{
            0xFF,0x15,0x7A,0x17,0x77,0x01, // call SwitchToThread
            0x33,0xC9,                     // xor ecx,ecx
            0xFF,0x15,0x22,0x16,0x77,0x01, // call Sleep(0)
        };
        // The deadline is xmm8; QPC seconds are biased by xmm9 immediately
        // before comparison. Verify this exact loop, not just its first call.
        constexpr std::array<uint8_t, 47> expected_tail{
            0x48,0x8D,0x8C,0x24,0xC0,0,0,0,
            0xFF,0x15,0x6C,0x16,0x77,0x01,
            0x0F,0x57,0xC0,
            0xF2,0x48,0x0F,0x2A,0x84,0x24,0xC0,0,0,0,
            0xF2,0x0F,0x59,0x05,0x9F,0xDE,0x62,0x04,
            0xF2,0x41,0x0F,0x58,0xC1,
            0x66,0x41,0x0F,0x2F,0xC0,
            0x72,0xC3,
        };
        struct State
        {
            std::unique_ptr<PLH::x64Detour> hook;
            void* stub{};
            HANDLE timer{};
            uint64_t trampoline{};
            std::atomic<uint8_t> enabled{0};
            std::atomic<bool> installed{false};
            std::atomic<bool> install_requested{false};
            std::wstring build_stamp;
            DWORD thread{};
            double seconds_per_tick{};
            uint64_t calls{}, waits{}, failures{};
            double waited_ms{}, maximum_late_ms{};
            std::array<double,8192> intervals{};
            size_t interval_count{}, dropped_intervals{};
            int64_t previous_tick{};
        };
        State state;
        static_assert(std::atomic<uint8_t>::is_always_lock_free);

        auto wait_until_tail(double unbiased_deadline) noexcept -> void
        {
            if (GetCurrentThreadId() != state.thread || !state.timer) return;
            ++state.calls;
            LARGE_INTEGER before{};
            if (!QueryPerformanceCounter(&before)) return;
            const auto due = frame_wait_due_time(unbiased_deadline - before.QuadPart * state.seconds_per_tick);
            if (due == 0) return;
            LARGE_INTEGER relative{};
            relative.QuadPart = due;
            if (!SetWaitableTimerEx(state.timer,&relative,0,nullptr,nullptr,nullptr,0))
            {
                ++state.failures;
                state.enabled.store(0,std::memory_order_relaxed);
                return;
            }
            const DWORD result = WaitForSingleObject(state.timer,20);
            LARGE_INTEGER after{};
            QueryPerformanceCounter(&after);
            ++state.waits;
            state.waited_ms += (after.QuadPart - before.QuadPart) * state.seconds_per_tick * 1000;
            state.maximum_late_ms = std::max(state.maximum_late_ms,
                (after.QuadPart * state.seconds_per_tick - unbiased_deadline) * 1000);
            if (result != WAIT_OBJECT_0)
            {
                ++state.failures;
                state.enabled.store(0,std::memory_order_relaxed);
                CancelWaitableTimer(state.timer);
            }
        }

        auto reset_metrics() -> void
        {
            state.calls=state.waits=state.failures=0;
            state.waited_ms=state.maximum_late_ms=0;
            state.interval_count=state.dropped_intervals=0;
            state.previous_tick=0;
        }
    }

    // Called from the verified UGameEngine::Tick callback on the game thread.
    // The frame limiter has already returned, so patching this site cannot
    // race that thread executing the overwritten call or an unpublished trampoline.
    static auto activate_native_frame_wait(std::wstring_view build_stamp) -> void
    {
        if (state.installed) return;
        const auto base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
        const auto site=base+site_rva;
        const bool verified=kh3ap::native_build::egs.matches(nt->FileHeader.TimeDateStamp, nt->OptionalHeader.SizeOfImage) &&
            std::memcmp(reinterpret_cast<const void*>(site-6),expected_site.data(),expected_site.size())==0 &&
            std::memcmp(reinterpret_cast<const void*>(site+8),expected_tail.data(),expected_tail.size())==0;
        if (!verified)
        {
            RC::Output::send<RC::LogLevel::Warning>(STR("[KH3APProbeCpp] NativeFrameWait unavailable reason=unsupported_image_or_signature\n"));
            return;
        }
        DWORD64 unwind_base{};
        const auto* fn=RtlLookupFunctionEntry(site,&unwind_base,nullptr);
        if (!fn || unwind_base!=base || fn->BeginAddress!=0x4B3E2B5 || fn->EndAddress!=0x4B3E434) return;
        LARGE_INTEGER frequency{};
        if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart<=0) return;
        state.seconds_per_tick=1.0/static_cast<double>(frequency.QuadPart);
        const double native_factor=*reinterpret_cast<const double*>(base+0x916C220);
        if (!std::isfinite(native_factor) || std::abs(native_factor-state.seconds_per_tick)>state.seconds_per_tick*1e-9) return;
        state.timer=CreateWaitableTimerExW(nullptr,nullptr,CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,TIMER_MODIFY_STATE|SYNCHRONIZE);
        if (!state.timer) return; // Do not fall back to a coarse timer.
        state.thread=GetCurrentThreadId();
        // This is a mid-function call site, so RSP is already 16-byte aligned.
        // xmm8/xmm9 are nonvolatile. xmm0 and rax are call-clobbered by the
        // original Win32 calls. Keep the original calls/deadline check in the
        // trampoline; only insert a bounded wait before them.
        // Jump through the stable address of the trampoline field. Installation
        // and subsequent execution of this frame-loop site share the game thread.
        const auto bytes=build_native_frame_wait_stub(
            reinterpret_cast<uint64_t>(&state.enabled),reinterpret_cast<uint64_t>(&wait_until_tail),
            reinterpret_cast<uint64_t>(&state.trampoline));
        state.stub=VirtualAlloc(nullptr,bytes.size(),MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
        if (!state.stub) { CloseHandle(state.timer); state.timer=nullptr; return; }
        std::memcpy(state.stub,bytes.data(),bytes.size());
        DWORD previous{};
        if (!VirtualProtect(state.stub,bytes.size(),PAGE_EXECUTE_READ,&previous))
        { uninstall_native_frame_wait(); return; }
        FlushInstructionCache(GetCurrentProcess(),state.stub,bytes.size());
        state.hook=std::make_unique<PLH::x64Detour>(site,reinterpret_cast<uint64_t>(state.stub),&state.trampoline);
        // Never follow a call if a future verified site starts with one.
        // Avoid register-spoiling in-place jumps at this internal call site.
        configure_native_frame_wait_detour(*state.hook);
        state.installed=state.hook->hook();
        if (!state.installed || !state.trampoline)
        {
            RC::Output::send<RC::LogLevel::Warning>(STR("[KH3APProbeCpp] NativeFrameWait unavailable reason=detour_install_failed\n"));
            uninstall_native_frame_wait(); return;
        }
        if (*reinterpret_cast<const uint16_t*>(site)!=0x25FF)
        { uninstall_native_frame_wait(); return; }
        RC::Output::send<RC::LogLevel::Warning>(STR("[KH3APProbeCpp] NativeFrameWait installed enabled=false rva=0x{:X} thread={} tail_us=500 build_stamp={}\n"),site_rva,state.thread,build_stamp);
    }

    auto install_native_frame_wait(std::wstring_view build_stamp) -> void
    {
        state.build_stamp=build_stamp;
        state.install_requested.store(true,std::memory_order_release);
    }

    auto uninstall_native_frame_wait() -> void
    {
        state.enabled.store(0,std::memory_order_relaxed);
        state.install_requested.store(false,std::memory_order_release);
        if (state.hook && state.installed && !state.hook->unHook()) return;
        state.installed=false;
        state.hook.reset();
        if (state.stub) { VirtualFree(state.stub,0,MEM_RELEASE); state.stub=nullptr; }
        if (state.timer) { CloseHandle(state.timer); state.timer=nullptr; }
        state.trampoline=0;
    }

    auto observe_native_frame_tick() noexcept -> void
    {
        if (state.install_requested.exchange(false,std::memory_order_acq_rel))
        {
            try { activate_native_frame_wait(state.build_stamp); }
            catch (...) { uninstall_native_frame_wait(); return; }
        }
        if (!state.installed || GetCurrentThreadId()!=state.thread) return;
        LARGE_INTEGER now{};
        if (!QueryPerformanceCounter(&now)) return;
        if (state.previous_tick)
        {
            if (state.interval_count<state.intervals.size())
                state.intervals[state.interval_count++]=(now.QuadPart-state.previous_tick)*state.seconds_per_tick*1000;
            else ++state.dropped_intervals;
        }
        state.previous_tick=now.QuadPart;
    }

    auto process_native_frame_wait_request(const std::filesystem::path& bridge) -> void
    {
        const auto path=bridge/"runtime_frame_wait_request.json";
        if (!path_points_to_regular_file(path)) return;
        const auto text=read_text_file(path);
        if (!text) return;
        const auto id=JsonFieldReader::string_field(*text,"request_id").value_or(L"");
        const auto command=JsonFieldReader::string_field(*text,"command").value_or(L"");
        const auto requested_pid=JsonFieldReader::int64_field(*text,"process_id").value_or(0);
        const auto expires=JsonFieldReader::int64_field(*text,"expires_unix_ms").value_or(0);
        const auto now=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        std::string error;
        const auto schema=JsonFieldReader::string_field(*text,"request_schema").value_or(L"");
        if (schema!=L"kh3ap.runtime_control.v1" || id.empty() || requested_pid!=GetCurrentProcessId() || expires<now || expires>now+60000) error="invalid_request_identity_or_expiry";
        else if (!state.installed) error="patch_unavailable";
        else if (GetCurrentThreadId()!=state.thread) error="wrong_game_thread";
        else if (command==L"configure")
        {
            const auto enabled=JsonFieldReader::bool_field(*text,"enabled");
            if (!enabled) error="enabled_required";
            else { state.enabled.store(*enabled?1:0,std::memory_order_relaxed); reset_metrics(); }
        }
        else if (command!=L"snapshot") error="unknown_command";
        std::vector<double> sorted(state.intervals.begin(),state.intervals.begin()+state.interval_count);
        std::sort(sorted.begin(),sorted.end());
        double sum=0; for(double value:sorted) sum+=value;
        auto percentile=[&](double fraction) { return sorted.empty()?0:sorted[static_cast<size_t>((sorted.size()-1)*fraction)]; };
        std::ostringstream json;
        json << "{\"schema\":\"kh3ap.native_frame_wait.v1\",\"request_id\":\"" << json_escape(id)
             << "\",\"process_id\":" << GetCurrentProcessId() << ",\"status\":\"" << (error.empty()?"completed":"error")
             << "\",\"reason\":\"" << error << "\",\"installed\":" << (state.installed?"true":"false")
             << ",\"enabled\":" << (state.enabled.load(std::memory_order_relaxed)?"true":"false")
             << ",\"helper_calls\":" << state.calls << ",\"timer_waits\":" << state.waits << ",\"timer_failures\":" << state.failures
             << ",\"waited_ms\":" << state.waited_ms << ",\"maximum_late_ms\":" << state.maximum_late_ms
             << ",\"tick_intervals\":" << sorted.size() << ",\"dropped_intervals\":" << state.dropped_intervals
             << ",\"tick_mean_ms\":" << (sorted.empty()?0:sum/sorted.size()) << ",\"tick_p50_ms\":" << percentile(.5)
             << ",\"tick_p95_ms\":" << percentile(.95) << ",\"tick_p99_ms\":" << percentile(.99)
             << ",\"tick_max_ms\":" << (sorted.empty()?0:sorted.back()) << "}\n";
        write_text_file(bridge/"runtime_frame_wait_result.json",json.str());
        append_text_file(bridge/"runtime_frame_wait_events.jsonl",json.str());
        try_remove_file(path);
    }
}
#else
namespace kh3ap
{
    auto install_native_frame_wait(std::wstring_view) -> void {}
    auto uninstall_native_frame_wait() -> void {}
    auto observe_native_frame_tick() noexcept -> void {}
    auto process_native_frame_wait_request(const std::filesystem::path&) -> void {}
}
#endif
