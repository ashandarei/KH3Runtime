#include "kh3ap/features/music/events/sead_music_events.h"
#include "kh3ap/features/colors/color_events.h"
#include "kh3ap/features/music/events/sead_music_profiles.h"
#include "kh3ap/features/grants/grant_internal_call_depth.h"
#ifdef S
#pragma push_macro("S")
#undef S
#define KH3AP_MUSIC_RESTORE_S
#endif
#include <polyhook2/Detour/x64Detour.hpp>
#ifdef KH3AP_MUSIC_RESTORE_S
#pragma pop_macro("S")
#undef KH3AP_MUSIC_RESTORE_S
#endif
#include <DynamicOutput/DynamicOutput.hpp>
#include <UnrealDef.hpp>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <cstring>
#include <memory>

namespace kh3ap
{
    namespace
    {
        constexpr uint16_t finished_indirect_jump_opcode = 0x25FF;
        constexpr size_t finished_basic_block_bytes = 9;
        constexpr size_t finished_following_block_bytes = 23;

        struct Hook
        {
            std::unique_ptr<PLH::x64Detour> detour;
            uint64_t trampoline{};
            bool installed{};
        };
        struct State
        {
            SeadMusicMailbox mailbox;
            std::array<Hook, sead_music_event_count> hooks;
            void* finish_stub{};
            size_t finish_stub_size{};
            bool attempted{}, installed{};
            DWORD game_thread{};
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            std::array<std::atomic<uint64_t>, sead_music_event_count> calls{};
            std::atomic<uint64_t> other_thread_calls{}, suppressed_calls{};
#endif
        };
        // Stable addresses for the machine-code stub. Retain on teardown if any
        // unhook fails; never leave installed code targeting freed storage.
        auto state() -> State& { static auto* value = new State; return *value; }

        template<SeadMusicEvent Event> auto notify() noexcept -> void
        {
            auto& s = state();
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            s.calls[static_cast<size_t>(Event)].fetch_add(1, std::memory_order_relaxed);
            if (GetCurrentThreadId() != s.game_thread)
                s.other_thread_calls.fetch_add(1, std::memory_order_relaxed);
#endif
            // Our own synchronous changes are already reconciled by the caller.
            // The later async callback still wakes us after that scope ends.
            if (Event != SeadMusicEvent::Loaded && Event != SeadMusicEvent::EquipmentRebuilt &&
                RuntimeGrantInternalCallDepth::is_active())
            {
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
                s.suppressed_calls.fetch_add(1, std::memory_order_relaxed);
#endif
                return;
            }
            s.mailbox.post(Event);
        }
        template<SeadMusicEvent Event> auto one(void* object) -> void
        {
            const auto original = state().hooks[static_cast<size_t>(Event)].trampoline;
            reinterpret_cast<void (*)(void*)>(original)(object);
            notify<Event>();
        }
        template<SeadMusicEvent Event> auto two(void* object, void* argument) -> void
        {
            const auto original = state().hooks[static_cast<size_t>(Event)].trampoline;
            reinterpret_cast<void (*)(void*, void*)>(original)(object, argument);
            notify<Event>();
        }
        auto config_category_volume(uint64_t category, float volume, float fade) -> void
        {
            const auto original = state().hooks[static_cast<size_t>(SeadMusicEvent::ConfigCategoryVolume)].trampoline;
            reinterpret_cast<void (*)(uint64_t, float, float)>(original)(category, volume, fade);
            notify<SeadMusicEvent::ConfigCategoryVolume>();
        }
        auto config_volume_all(float volume, float fade) -> void
        {
            const auto original = state().hooks[static_cast<size_t>(SeadMusicEvent::ConfigVolumeAll)].trampoline;
            reinterpret_cast<void (*)(float, float)>(original)(volume, fade);
            notify<SeadMusicEvent::ConfigVolumeAll>();
        }
        auto equipment_rebuilt(void* equipment, void* entries, void* effect, int32_t weapon,
                               uint8_t appear, void* previous) -> void
        {
            // TresEquipmentComponent's spawn/reuse operation, not its tick.
            // The original populates m_WeaponEquips synchronously, including
            // replacements whose caller does not change the pawn's state.
            const auto original = state().hooks[static_cast<size_t>(SeadMusicEvent::EquipmentRebuilt)].trampoline;
            reinterpret_cast<void (*)(void*, void*, void*, int32_t, uint8_t, void*)>(original)(
                equipment, entries, effect, weapon, appear, previous);
            notify_color_equipment_changed();
            notify<SeadMusicEvent::EquipmentRebuilt>();
        }
        auto callback(size_t index) -> uint64_t
        {
            if (index == static_cast<size_t>(SeadMusicEvent::EquipmentRebuilt))
                return reinterpret_cast<uint64_t>(&equipment_rebuilt);
            if (index == static_cast<size_t>(SeadMusicEvent::ConfigCategoryVolume))
                return reinterpret_cast<uint64_t>(&config_category_volume);
            if (index == static_cast<size_t>(SeadMusicEvent::ConfigVolumeAll))
                return reinterpret_cast<uint64_t>(&config_volume_all);
            const std::array<uint64_t, 10> callbacks{
                reinterpret_cast<uint64_t>(&one<SeadMusicEvent::Loaded>),
                reinterpret_cast<uint64_t>(&two<SeadMusicEvent::Set>),
                reinterpret_cast<uint64_t>(&two<SeadMusicEvent::SetPath>),
                reinterpret_cast<uint64_t>(&one<SeadMusicEvent::Play>),
                reinterpret_cast<uint64_t>(&one<SeadMusicEvent::Stop>),
                reinterpret_cast<uint64_t>(&one<SeadMusicEvent::Suspend>),
                reinterpret_cast<uint64_t>(&one<SeadMusicEvent::AutoPlay>),
                reinterpret_cast<uint64_t>(&one<SeadMusicEvent::AutoResume>),
                reinterpret_cast<uint64_t>(&one<SeadMusicEvent::Reset>),
                reinterpret_cast<uint64_t>(&two<SeadMusicEvent::Transfer>)};
            return index < callbacks.size() ? callbacks[index] : reinterpret_cast<uint64_t>(state().finish_stub);
        }
    }

    auto uninstall_sead_music_events() -> void
    {
        auto& s = state();
        bool retained = false;
        for (auto& hook : s.hooks)
        {
            if (hook.installed && !hook.detour->unHook()) { retained = true; continue; }
            hook.installed = false;
            hook.detour.reset();
            hook.trampoline = 0;
        }
        if (!retained && s.finish_stub)
        {
            VirtualFree(s.finish_stub, 0, MEM_RELEASE);
            s.finish_stub = nullptr;
        }
        s.installed = false;
        (void)s.mailbox.take();
    }

    auto install_sead_music_events() -> bool
    {
        auto& s = state();
        if (s.attempted) return s.installed;
        s.attempted = true;
        const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (!base || dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) return false;
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        const auto* profile = nt->Signature == IMAGE_NT_SIGNATURE && nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC
            ? sead_music_detail::select_profile(nt->FileHeader.TimeDateStamp, nt->OptionalHeader.SizeOfImage) : nullptr;
        if (!profile)
        {
            RC::Output::send<RC::LogLevel::Warning>(STR("[KH3APProbeCpp] Native music events unavailable: unsupported image\n"));
            return false;
        }
        const auto& sites = profile->sites;
        for (size_t index = 0; index < sites.size(); ++index)
        {
            const auto& site = sites[index];
            DWORD64 image{};
            const auto* function = RtlLookupFunctionEntry(base + site.rva, &image, nullptr);
            const auto expected_begin = index == static_cast<size_t>(SeadMusicEvent::Finished) ? profile->finish_owner : site.rva;
            if (!function || image != base || function->BeginAddress != expected_begin ||
                std::memcmp(reinterpret_cast<const void*>(base + site.rva), site.signature.data(), site.signature.size()) != 0)
            {
                RC::Output::send<RC::LogLevel::Warning>(STR("[KH3APProbeCpp] Native music events unavailable: signature/unwind mismatch rva=0x{:X}\n"), site.rva);
                return false;
            }
        }
        s.game_thread = GetCurrentThreadId();
        uint64_t counter{};
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        counter = reinterpret_cast<uint64_t>(&s.calls[static_cast<size_t>(SeadMusicEvent::Finished)]);
#endif
        const auto bytes = build_sead_finish_stub(reinterpret_cast<uint64_t>(&s.mailbox.pending), counter);
        s.finish_stub_size = bytes.size();
        s.finish_stub = VirtualAlloc(nullptr, bytes.size(), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!s.finish_stub) return false;
        std::memcpy(s.finish_stub, bytes.data(), bytes.size());
        DWORD old_protect{};
        if (!VirtualProtect(s.finish_stub, bytes.size(), PAGE_EXECUTE_READ, &old_protect))
        { uninstall_sead_music_events(); return false; }
        try
        {
            for (size_t index = 0; index < sites.size(); ++index)
            {
                auto& hook = s.hooks[index];
                hook.detour = std::make_unique<PLH::x64Detour>(base + sites[index].rva, callback(index), &hook.trampoline);
                hook.detour->setIsFollowCallOnFnAddress(false);
                // A six-byte indirect jump fits the nine-byte Finished block.
                // Register-spoiling or 14-byte inline detours are forbidden.
                hook.detour->setDetourScheme(PLH::x64Detour::VALLOC2);
                hook.installed = hook.detour->hook();
                if (!hook.installed || !hook.trampoline) { uninstall_sead_music_events(); return false; }
            }
            const auto finish = static_cast<size_t>(SeadMusicEvent::Finished);
            const auto address = base + sites[finish].rva;
            // The next basic block has a separate incoming branch. Verify it
            // was untouched and the patch uses the required six-byte encoding.
            if (*reinterpret_cast<const uint16_t*>(address) != finished_indirect_jump_opcode ||
                std::memcmp(reinterpret_cast<const void*>(address + finished_basic_block_bytes),
                            sites[finish].signature.data() + finished_basic_block_bytes,
                            finished_following_block_bytes) != 0)
            { uninstall_sead_music_events(); return false; }
            if (!VirtualProtect(s.finish_stub, bytes.size(), PAGE_READWRITE, &old_protect))
            { uninstall_sead_music_events(); return false; }
            std::memcpy(static_cast<std::byte*>(s.finish_stub) + bytes.size() - sizeof(uint64_t),
                        &s.hooks[finish].trampoline, sizeof(uint64_t));
            if (!VirtualProtect(s.finish_stub, bytes.size(), PAGE_EXECUTE_READ, &old_protect))
            { uninstall_sead_music_events(); return false; }
            FlushInstructionCache(GetCurrentProcess(), s.finish_stub, bytes.size());
            s.installed = true;
            RC::Output::send<RC::LogLevel::Warning>(STR("[KH3APProbeCpp] Native music events installed storefront={} hooks={} thread={}\n"), profile->storefront, sites.size(), s.game_thread);
            return true;
        }
        catch (...)
        {
            uninstall_sead_music_events();
            RC::Output::send<RC::LogLevel::Warning>(STR("[KH3APProbeCpp] Native music events unavailable: install exception\n"));
            return false;
        }
    }

    auto take_sead_music_events() noexcept -> uint32_t { return state().mailbox.take(); }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    auto sead_music_event_snapshot() -> SeadMusicEventSnapshot
    {
        const auto& s = state();
        SeadMusicEventSnapshot result{s.installed, s.game_thread, s.other_thread_calls.load(), s.suppressed_calls.load()};
        for (size_t i = 0; i < result.calls.size(); ++i) result.calls[i] = s.calls[i].load();
        return result;
    }
#endif
}
