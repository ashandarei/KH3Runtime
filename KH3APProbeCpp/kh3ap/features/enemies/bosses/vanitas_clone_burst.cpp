#include <asmjit/x86.h>

#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/core/platform/game_build.h"
#include "kh3ap/features/enemies/boss_remap_policy.h"
#include "kh3ap/features/enemies/bosses/binding.h"
#include "kh3ap/features/enemies/bosses/encounter_repairs.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/UClass.hpp>
#include <Windows.h>
#include <bit>
#include <cstring>

using namespace RC;
using namespace RC::Unreal;

namespace kh3ap::enemy_remap_detail
{
    namespace
    {
        enum class CenterUse { initial_radius, destination, movement_radius };

        struct CenterSite
        {
            CenterUse use;
            uintptr_t rva;
            std::array<uint8_t, 16> signature;
        };

        // TresAttack10_e_ex360_KBRCloneBurst's native update. Insert the
        // corrections just before it stores/uses the two radii and the XY target.
        constexpr std::array<CenterSite, 3> center_sites{{
            {CenterUse::initial_radius, 0xdb586f,
             {0x0f, 0x57, 0xc0, 0xf3, 0x0f, 0x51, 0xc1, 0xf3,
              0x0f, 0x11, 0x86, 0x1c, 0x02, 0x00, 0x00, 0x48}},
            {CenterUse::destination, 0xdb595c,
             {0x0f, 0x10, 0x05, 0x38, 0x87, 0xf7, 0x08, 0x49,
              0x8b, 0x07, 0xf3, 0x0f, 0x11, 0x7c, 0x24, 0x40}},
            {CenterUse::movement_radius, 0xdb5e73,
             {0x0f, 0x58, 0xc8, 0xf3, 0x0f, 0x51, 0xd1, 0x0f,
              0x2f, 0x96, 0x1c, 0x02, 0x00, 0x00, 0x76, 0x0c}},
        }};

        struct AttackCenter
        {
            FWeakObjectPtr state;
            uint64_t xy{};
        };
        thread_local AttackCenter active_center;

        // Native calls reach these sites only during Clone Burst. Read the
        // owning encounter once per attack, including when a state is recycled.
        auto clone_burst_center(UObject* state, bool initialize) noexcept -> uint64_t
        {
            if (!initialize)
            {
                return active_center.xy && active_center.state.Get() == state ? active_center.xy : 0;
            }
            active_center = {};
            try
            {
                if (!s_runtime_enemy_remap.active || !s_runtime_enemy_remap.boss.boss_enabled.load())
                {
                    return 0;
                }
                auto* pawn = read_boss_owner_property(state, L"MyCharPawn");
                if (!pawn || !is_uobject_valid_fast(pawn->GetClassPrivate()) ||
                    pawn->GetClassPrivate()->GetPathName() != data_vanitas_class ||
                    !resolve_tracked_runtime_boss_pawn(pawn).level_entity)
                {
                    return 0;
                }
                const auto center = read_boss_arena_center(pawn);
                if (center)
                {
                    active_center.state = make_uobject_weak(state);
                    active_center.xy = std::bit_cast<uint64_t>(std::array{(*center)[0], (*center)[1]});
                    Output::send<LogLevel::Normal>(STR("[KH3APProbeCpp] Data Vanitas Clone Burst "
                        "arena center x={} y={} pawn={}\n"), (*center)[0], (*center)[1], pawn->GetPathName());
                }
            }
            catch (...)
            {
                active_center = {};
            }
            return active_center.xy;
        }

        struct InstalledCenterSite
        {
            uint64_t trampoline{};
            void* stub{};
            std::unique_ptr<PLH::x64Detour> hook;
        };
        asmjit::JitRuntime center_stubs;
        std::array<InstalledCenterSite, center_sites.size()> installed_sites;

        class AssemblyErrors final : public asmjit::ErrorHandler
        {
        public:
            void handleError(asmjit::Error, const char*, asmjit::BaseEmitter*) override { failed = true; }
            bool failed{};
        };

        auto build_center_stub(CenterUse use, InstalledCenterSite& site) -> bool
        {
            using namespace asmjit;
            using namespace asmjit::x86;
            CodeHolder code;
            if (code.init(center_stubs.environment()) != kErrorOk)
            {
                return false;
            }
            AssemblyErrors errors;
            code.setErrorHandler(&errors);
            Assembler out{&code};
            constexpr int32_t frame_size = 0xc8;
            constexpr int32_t gpr_base = 0x20; // Preserve the Win64 call's shadow space.
            constexpr int32_t center_slot = 0x58;
            constexpr int32_t xmm_base = 0x60;
            const std::array registers{rax, rcx, rdx, r8, r9, r10};
            const auto restore = out.newLabel();

            // The audited native body has an aligned RSP at all three sites.
            out.pushfq();
            out.sub(rsp, frame_size);
            for (size_t i = 0; i < registers.size(); ++i)
            {
                out.mov(qword_ptr(rsp, gpr_base + static_cast<int32_t>(i * 8)), registers[i]);
                out.movups(ptr(rsp, xmm_base + static_cast<int32_t>(i * 16)), Xmm{static_cast<uint32_t>(i)});
            }
            out.mov(rcx, rsi); // Native attack state at each verified instruction site.
            out.mov(edx, use == CenterUse::initial_radius ? 1 : 0);
            out.mov(rax, reinterpret_cast<uint64_t>(&clone_burst_center));
            out.call(rax);
            out.test(rax, rax);
            out.jz(restore); // Zero center leaves the original native calculation intact.
            out.mov(qword_ptr(rsp, center_slot), rax);

            if (use == CenterUse::destination)
            {
                // The engine uses this XY for the board and the rider's animation.
                out.movss(xmm7, dword_ptr(rsp, center_slot));
                out.movss(xmm8, dword_ptr(rsp, center_slot + 4));
            }
            else
            {
                if (use == CenterUse::initial_radius)
                {
                    out.mov(rax, qword_ptr(rsp, gpr_base)); // FVector returned by GetActorLocation.
                    out.movss(xmm0, dword_ptr(rax));
                    out.movss(xmm1, dword_ptr(rax, 4));
                }
                else
                {
                    out.movaps(xmm0, xmm8); // Candidate world X and Y before native movement.
                    out.movaps(xmm1, xmm9);
                }
                out.subss(xmm0, dword_ptr(rsp, center_slot));
                out.subss(xmm1, dword_ptr(rsp, center_slot + 4));
                out.mulss(xmm0, xmm0);
                out.mulss(xmm1, xmm1);
                out.addss(xmm0, xmm1);
                out.sqrtss(xmm0, xmm0);
                const auto output_slot = use == CenterUse::initial_radius ? xmm_base : xmm_base + 32;
                out.movss(dword_ptr(rsp, output_slot), xmm0);
            }

            out.bind(restore);
            for (size_t i = 0; i < registers.size(); ++i)
            {
                out.movups(Xmm{static_cast<uint32_t>(i)}, ptr(rsp, xmm_base + static_cast<int32_t>(i * 16)));
                out.mov(registers[i], qword_ptr(rsp, gpr_base + static_cast<int32_t>(i * 8)));
            }
            out.add(rsp, frame_size);
            out.popfq();
            // R11 is dead across the displaced instructions and their continuations.
            out.mov(r11, reinterpret_cast<uint64_t>(&site.trampoline));
            out.jmp(qword_ptr(r11));
            return !errors.failed && center_stubs.add(&site.stub, &code) == kErrorOk;
        }
    }

    auto install_vanitas_clone_burst_center() -> void
    {
        const auto& replacements = s_runtime_enemy_remap.boss.boss_class_replacements;
        if (installed_sites.front().hook || std::none_of(replacements.begin(), replacements.end(),
                [](const auto& pair) { return pair.second == data_vanitas_class; }))
        {
            return;
        }
        const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || nt->Signature != IMAGE_NT_SIGNATURE ||
            !native_build::egs.matches(nt->FileHeader.TimeDateStamp, nt->OptionalHeader.SizeOfImage) ||
            std::any_of(center_sites.begin(), center_sites.end(), [base](const auto& site)
            {
                return std::memcmp(reinterpret_cast<const void*>(base + site.rva - 7),
                                   site.signature.data(), site.signature.size()) != 0;
            }))
        {
            Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] Data Vanitas Clone Burst "
                "center unavailable reason=unrecognized_native_update\n"));
            return;
        }
        for (size_t i = 0; i < center_sites.size(); ++i)
        {
            auto& installed = installed_sites[i];
            if (build_center_stub(center_sites[i].use, installed))
            {
                const auto address = base + center_sites[i].rva;
                installed.hook = std::make_unique<PLH::x64Detour>(address,
                    reinterpret_cast<uint64_t>(installed.stub), &installed.trampoline);
                installed.hook->setIsFollowCallOnFnAddress(false);
                installed.hook->setDetourScheme(PLH::x64Detour::VALLOC2);
                if (installed.hook->hook() && installed.trampoline)
                {
                    continue;
                }
            }
            // All three sites use the same coordinate frame. Roll back a partial install.
            for (auto& entry : installed_sites)
            {
                entry.hook.reset();
                if (entry.stub)
                {
                    center_stubs.release(entry.stub);
                    entry.stub = nullptr;
                }
                entry.trampoline = 0;
            }
            Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] Data Vanitas Clone Burst "
                "center unavailable reason=detour_install_failed\n"));
            return;
        }
        Output::send<LogLevel::Normal>(STR("[KH3APProbeCpp] Data Vanitas Clone Burst "
            "arena-relative movement installed\n"));
    }
}
