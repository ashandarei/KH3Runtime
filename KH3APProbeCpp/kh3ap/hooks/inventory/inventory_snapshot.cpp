#include "kh3ap/core/platform/game_build.h"
#include "kh3ap/hooks/inventory/inventory_snapshot.h"
#include "kh3ap/bridge/rewards/reconciliation.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/core/performance_build.h"
#include "kh3ap/features/grants/grant_internal_call_depth.h"
#include <polyhook2/Detour/x64Detour.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <array>
#include <cstring>
#include <memory>

namespace kh3ap::inventory_snapshot
{
    namespace
    {
        constexpr auto snapshot_player_data_offset = 0x9640;
        constexpr auto snapshot_bonus_counts_offset = 0x960;
        constexpr auto max_snapshot_pe_header_offset = 0x1000;
        constexpr auto snapshot_steam_timestamp = kh3ap::native_build::steam.timestamp;
        constexpr auto snapshot_steam_image_size = kh3ap::native_build::steam.image_size;
        constexpr auto snapshot_steam_assign_rva = 0xE9CDE0;
        constexpr auto snapshot_egs_timestamp = kh3ap::native_build::egs.timestamp;
        constexpr auto snapshot_egs_image_size = kh3ap::native_build::egs.image_size;
        constexpr auto snapshot_egs_assign_rva = 0x1128F70;

        // The native save-data assignment is used in BOTH directions. Its
        // reverse copy is the authoritative restore, including an in-memory
        // Retry that performs no slot-file I/O. Never hook the UFunction entry.
        constexpr uintptr_t live_offset=0x870, backup_offset=0x961770;
        constexpr auto distance=backup_offset-live_offset;
        constexpr std::array<unsigned char, 32> signature{
            0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,
            0x48,0x89,0x74,0x24,0x18,0x57,0x41,0x54,0x41,0x55,
            0x41,0x56,0x41,0x57,0x48,0x83,0xec,0x20,0x0f,0x10,0x02,0x48};
        std::unique_ptr<PLH::x64Detour> detour;
        uint64_t trampoline{};
        using Assign=void*(__fastcall*)(void*,const void*);

        auto direction(void* destination,const void* source) -> int
        {
            const auto dst=reinterpret_cast<uintptr_t>(destination), src=reinterpret_cast<uintptr_t>(source);
            const bool capture=dst>src && dst-src==distance;
            const bool restore=src>dst && src-dst==distance;
            if (!capture && !restore) return 0;
            const auto live=capture?src:dst;
            if (live<live_offset) return 0;
            auto* owner=reinterpret_cast<RC::Unreal::UObject*>(live-live_offset);
            if (!is_uobject_valid_fast(owner)) return 0;
            auto* cls=owner->GetClassPrivate();
            if (!is_uobject_valid_fast(cls) || cls->GetName()!=L"TresGameInstance") return 0;
            return restore?2:1;
        }
        auto __fastcall assigned(void* destination,const void* source) -> void*
        {
            const auto kind=direction(destination,source);
            std::array<uint8_t,32> bonuses{};
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            if (kind) std::memcpy(bonuses.data(),static_cast<const std::byte*>(source)+snapshot_player_data_offset+snapshot_bonus_counts_offset,bonuses.size());
#endif
            auto* result=reinterpret_cast<Assign>(trampoline)(destination,source);
            if (kind)
            {
                FILETIME at{}; GetSystemTimeAsFileTime(&at);
                // No UObject is retained and no JSON/file work runs here.
                reconciliation::inventory_snapshot(kind==2,
                    (uint64_t{at.dwHighDateTime}<<32)|at.dwLowDateTime,
                    !RuntimeGrantInternalCallDepth::is_active(),bonuses);
            }
            return result;
        }
    }
    auto install() -> void
    {
        if (detour) return;
        const auto base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (!base || dos->e_magic!=IMAGE_DOS_SIGNATURE || dos->e_lfanew<=0 || dos->e_lfanew>max_snapshot_pe_header_offset) return;
        const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
        if (nt->Signature!=IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC) return;
        uintptr_t rva{};
        if (nt->FileHeader.TimeDateStamp==snapshot_steam_timestamp && nt->OptionalHeader.SizeOfImage==snapshot_steam_image_size) rva=snapshot_steam_assign_rva;
        if (nt->FileHeader.TimeDateStamp==snapshot_egs_timestamp && nt->OptionalHeader.SizeOfImage==snapshot_egs_image_size) rva=snapshot_egs_assign_rva;
        const bool matched=rva && std::memcmp(reinterpret_cast<const void*>(base+rva),signature.data(),signature.size())==0;
        bool installed=false;
        if (matched)
        {
            detour=std::make_unique<PLH::x64Detour>(base+rva,reinterpret_cast<uint64_t>(&assigned),&trampoline);
            installed=detour->hook();
            if (!installed) { detour.reset(); trampoline=0; }
        }
        reconciliation::inventory_snapshot_observer(installed);
        RC::Output::send<RC::LogLevel::Warning>(STR("[KH3APProbeCpp] Native inventory snapshot observer rva=0x{:x} signature={} installed={}\n"),rva,matched,installed);
    }
    auto uninstall() -> void
    {
        if (detour) { detour->unHook(); detour.reset(); }
        trampoline=0;
        reconciliation::inventory_snapshot_observer(false);
    }
}
