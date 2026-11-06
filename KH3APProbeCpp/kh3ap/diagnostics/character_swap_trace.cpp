#include "character_swap_trace.h"
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/bridge/utilities.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include <Unreal/Hooks.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#define NOMINMAX
#include <Windows.h>

namespace kh3ap::character_swap_trace
{
    using namespace RC::Unreal;
    namespace
    {
        using Clock = std::chrono::steady_clock;
        constexpr size_t phase_count = 8;
        constexpr size_t entry_limit = 512;
        constexpr size_t native_scope_limit = 16;
        constexpr size_t depth_limit = 64;
        constexpr auto request_lifetime = std::chrono::seconds(5);
        constexpr std::array phase_names{
            "unused", "creation_callback", "first_engine", "first_callback",
            "second_engine", "second_callback", "third_engine", "third_callback"};
        struct Timing
        {
            uint64_t calls{}, inclusive_us{}, exclusive_us{}, maximum_us{};
        };
        struct Entry
        {
            FWeakObjectPtr function, receiver_class;
            std::array<Timing, phase_count> phases{};
        };
        struct Invocation
        {
            Clock::time_point started{};
            uint64_t children_us{};
            size_t entry{}, phase{};
        };
        struct Phase
        {
            uint64_t elapsed_us{}, slow_scope_us{};
            uint64_t thread_cycles{}, process_read_bytes{}, process_read_operations{};
            bool counters_valid{};
            std::array<wchar_t, 192> slow_scope{};
        };
        struct NativeEntry
        {
            std::string_view name;
            std::array<Timing, phase_count> phases{};
        };
        std::array<Entry, entry_limit> entries;
        std::array<Invocation, depth_limit> stack;
        std::array<Phase, phase_count> phases;
        std::array<NativeEntry, native_scope_limit> native_entries;
        std::atomic<bool> active{};
        bool registered{};
        DWORD game_thread{};
        Clock::time_point requested_until{}, capture_until{}, phase_started{};
        FWeakObjectPtr world;
        size_t entry_count{}, depth{}, phase_index{};
        size_t native_entry_count{};
        ULONG64 phase_cycles{};
        IO_COUNTERS phase_io{};
        bool phase_counters_valid{};
        uint64_t dropped{};
        int target{-1};
        const char* status = "idle";

        uint64_t elapsed(Clock::time_point started)
        {
            return std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - started).count();
        }

        bool recording()
        {
            return active.load(std::memory_order_relaxed) && GetCurrentThreadId() == game_thread;
        }

        void start_phase()
        {
            phase_counters_valid = QueryThreadCycleTime(GetCurrentThread(), &phase_cycles) &&
                GetProcessIoCounters(GetCurrentProcess(), &phase_io);
            phase_started = Clock::now();
        }

        void finish_phase()
        {
            auto& phase = phases[phase_index];
            phase.elapsed_us = elapsed(phase_started);
            ULONG64 cycles{};
            IO_COUNTERS io{};
            phase.counters_valid = phase_counters_valid &&
                QueryThreadCycleTime(GetCurrentThread(), &cycles) &&
                GetProcessIoCounters(GetCurrentProcess(), &io);
            if (phase.counters_valid)
            {
                phase.thread_cycles = cycles - phase_cycles;
                phase.process_read_bytes = io.ReadTransferCount - phase_io.ReadTransferCount;
                phase.process_read_operations = io.ReadOperationCount - phase_io.ReadOperationCount;
            }
        }

        void before(UObject* receiver, UFunction* function, void*)
        {
            if (!recording()) return;
            if (depth++ >= stack.size())
            {
                ++dropped;
                return;
            }
            auto* receiver_class = is_uobject_valid_fast(receiver) ? receiver->GetClassPrivate() : nullptr;
            size_t index{};
            for (; index < entry_count; ++index)
            {
                if (entries[index].function.Get() == function &&
                    entries[index].receiver_class.Get() == receiver_class) break;
            }
            if (index == entry_count && entry_count < entries.size())
            {
                entries[index].function = make_uobject_weak(function);
                entries[index].receiver_class = make_uobject_weak(receiver_class);
                ++entry_count;
            }
            if (index == entries.size()) ++dropped;
            stack[depth - 1] = {Clock::now(), 0, index, phase_index};
        }

        void after(UObject*, UFunction*, void*)
        {
            if (!recording() || !depth) return;
            if (--depth >= stack.size()) return;
            const auto& call = stack[depth];
            const auto duration = elapsed(call.started);
            if (depth) stack[depth - 1].children_us += duration;
            if (call.entry >= entries.size()) return;
            auto& timing = entries[call.entry].phases[call.phase];
            ++timing.calls;
            timing.inclusive_us += duration;
            timing.exclusive_us += duration - std::min(duration, call.children_us);
            timing.maximum_us = std::max(timing.maximum_us, duration);
        }
    }

    void arm()
    {
        if (active.load()) return;
        if (!registered)
        {
            game_thread = GetCurrentThreadId();
            Hook::RegisterProcessEventPreCallback(before);
            Hook::RegisterProcessEventPostCallback(after);
            registered = true;
        }
        requested_until = Clock::now() + request_lifetime;
        status = "armed";
    }

    void begin(int player_id)
    {
        if (Clock::now() >= requested_until) return;
        requested_until = {};
        entries = {};
        phases = {};
        native_entries = {};
        native_entry_count = 0;
        entry_count = depth = dropped = 0;
        target = player_id;
        world = make_uobject_weak(kh02_current_world());
        phase_index = 1;
        start_phase();
        capture_until = phase_started + request_lifetime;
        status = "capturing";
        active.store(true);
    }

    void frame_begin()
    {
        if (!recording()) return;
        if (Clock::now() >= capture_until || world.Get() != kh02_current_world() ||
            phase_index % 2 == 0 || phase_index + 1 >= phases.size())
        {
            active.store(false);
            status = "context_lost";
            return;
        }
        ++phase_index;
        start_phase();
    }

    void engine_finished()
    {
        if (!recording()) return;
        if (phase_index % 2 != 0 || phase_index + 1 >= phases.size())
        {
            active.store(false);
            status = "invalid_frame_boundary";
            return;
        }
        finish_phase();
        ++phase_index;
        start_phase();
    }

    void frame_end()
    {
        if (!recording()) return;
        finish_phase();
        if (phase_index == phases.size() - 1)
        {
            active.store(false);
            status = depth ? "unbalanced_events" : "completed";
        }
    }

    void record_scope(std::wstring_view name, uint64_t duration)
    {
        if (!recording() || duration <= phases[phase_index].slow_scope_us) return;
        auto& phase = phases[phase_index];
        phase.slow_scope_us = duration;
        const auto length = std::min(name.size(), phase.slow_scope.size() - 1);
        std::copy_n(name.data(), length, phase.slow_scope.data());
        phase.slow_scope[length] = 0;
    }

    NativeScope::NativeScope(std::string_view name) : name_(name)
    {
        active_ = recording();
        if (active_)
        {
            phase_ = phase_index;
            started_ = Clock::now();
        }
    }

    NativeScope::~NativeScope()
    {
        if (!active_ || !recording() || phase_ != phase_index) return;
        const auto duration = elapsed(started_);
        size_t index{};
        for (; index < native_entry_count; ++index)
        {
            if (native_entries[index].name == name_) break;
        }
        if (index == native_entries.size())
        {
            ++dropped;
            return;
        }
        if (index == native_entry_count)
        {
            native_entries[index].name = name_;
            ++native_entry_count;
        }
        auto& timing = native_entries[index].phases[phase_];
        ++timing.calls;
        timing.inclusive_us += duration;
        timing.maximum_us = std::max(timing.maximum_us, duration);
    }

    nlohmann::json snapshot()
    {
        nlohmann::json result{{"status", status}, {"target", target}, {"dropped", dropped}};
        result["phases"] = nlohmann::json::array();
        for (size_t index = 1; index < phases.size(); ++index)
        {
            const auto& phase = phases[index];
            result["phases"].push_back({{"name", phase_names[index]}, {"elapsed_us", phase.elapsed_us},
                {"slow_scope", to_utf8(phase.slow_scope.data())}, {"slow_scope_us", phase.slow_scope_us},
                {"thread_cycles", phase.thread_cycles}, {"process_read_bytes", phase.process_read_bytes},
                {"process_read_operations", phase.process_read_operations}, {"counters_valid", phase.counters_valid}});
        }
        result["native_scopes"] = nlohmann::json::array();
        for (size_t index = 0; index < native_entry_count; ++index)
        {
            const auto& entry = native_entries[index];
            for (size_t phase = 1; phase < phases.size(); ++phase)
            {
                const auto& timing = entry.phases[phase];
                if (!timing.calls) continue;
                result["native_scopes"].push_back({{"name", entry.name}, {"phase", phase_names[phase]},
                    {"calls", timing.calls}, {"inclusive_us", timing.inclusive_us}, {"maximum_us", timing.maximum_us}});
            }
        }
        result["events"] = nlohmann::json::array();
        for (size_t index = 0; index < entry_count; ++index)
        {
            const auto& entry = entries[index];
            auto* function = entry.function.Get();
            auto* type = entry.receiver_class.Get();
            if (!function || !type) continue;
            for (size_t phase = 1; phase < phases.size(); ++phase)
            {
                const auto& timing = entry.phases[phase];
                if (!timing.calls) continue;
                result["events"].push_back({{"function", to_utf8(function->GetPathName())},
                    {"class", to_utf8(type->GetPathName())}, {"phase", phase_names[phase]},
                    {"calls", timing.calls}, {"inclusive_us", timing.inclusive_us},
                    {"exclusive_us", timing.exclusive_us}, {"maximum_us", timing.maximum_us}});
            }
        }
        return result;
    }
}
#endif
