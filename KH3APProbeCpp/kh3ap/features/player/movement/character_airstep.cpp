#include "kh3ap/features/player/characters/character_layout.h"
#include <polyhook2/Detour/x64Detour.hpp>
#include "kh3ap/features/player/movement/character_airstep.h"
#include "kh3ap/features/player/movement/character_airstep_gate_stub.h"
#include "kh3ap/features/player/characters/character_profile.h"
#include "kh3ap/features/player/characters/character_swap.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include <Unreal/UObject.hpp>
#include <Unreal/FWeakObjectPtr.hpp>
#include <memory>
#include <cstring>
#define NOMINMAX
#include <Windows.h>

namespace kh3ap::character_airstep
{
    using namespace RC::Unreal;
    namespace
    {
        using CanAirstep = bool (*)(void*);
        uint64_t original_can{};
        uint64_t original_aim{};
        std::unique_ptr<PLH::x64Detour> hook;
        std::unique_ptr<PLH::x64Detour> aim_hook;
        void* aim_stub{};
        bool initialized{}, initialization_failed{};
        FWeakObjectPtr aim_pawn;
        FWeakObjectPtr aim_controller;

        class NativePlayerKindScope
        {
        public:
            explicit NativePlayerKindScope(UObject* pawn)
                : pawn_(make_uobject_weak(pawn)), original_(kind(pawn))
            {
                kind(pawn) = 1;
            }
            ~NativePlayerKindScope()
            {
                if (auto* pawn = pawn_.Get())
                {
                    kind(pawn) = original_;
                }
            }
            NativePlayerKindScope(const NativePlayerKindScope&) = delete;
            NativePlayerKindScope& operator=(const NativePlayerKindScope&) = delete;
        private:
            FWeakObjectPtr pawn_;
            uint8_t original_;
            static uint8_t& kind(UObject* pawn)
            {
                return *reinterpret_cast<uint8_t*>(reinterpret_cast<std::byte*>(pawn) + character_native::layout::character_kind_offset);
            }
        };

        uint8_t aim_kind(UObject* pawn, uint8_t native_kind) noexcept
        {
            try
            {
                if (pawn && pawn == aim_pawn.Get() && character_swap::owns_swap_pawn(pawn) &&
                    try_read_reflected_object_property_value(pawn, L"Controller") == aim_controller.Get())
                {
                    return 1;
                }
            }
            catch (...)
            {
                // No C++ exception may unwind through the native branch stub.
            }
            return native_kind;
        }

        bool install_aim_gate(uintptr_t base)
        {
            // Hook the target-list builder's kind filter, not the separate
            // shared aim-entry check. That helper owns Airstep markers and
            // +0xb18/+0xb20; it does not select or fire a Shotlock.
            const auto bytes = build_aim_kind_gate_stub(reinterpret_cast<uint64_t>(&aim_kind), 0);
            aim_stub = VirtualAlloc(nullptr, bytes.size(), MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
            if (!aim_stub) { return false; }
            std::memcpy(aim_stub, bytes.data(), bytes.size());
            const auto rollback = [&]()
            {
                if (!aim_hook || !aim_hook->isHooked() || aim_hook->unHook())
                {
                    aim_hook.reset();
                    VirtualFree(aim_stub, 0, MEM_RELEASE);
                    aim_stub = nullptr;
                }
                // An unhook failure retains its executable stub/trampoline.
            };
            aim_hook = std::make_unique<PLH::x64Detour>(
                base + character_native::rva(character_native::Site::airstep_aim_kind_compare),
                reinterpret_cast<uintptr_t>(aim_stub), &original_aim);
            configure_aim_kind_gate_detour(*aim_hook);
            if (!aim_hook->hook() || !original_aim)
            {
                rollback();
                return false;
            }
            // Installation runs before the owned pawn is prepared on the game
            // thread. Publish the relocated native cmp/jne before input resumes.
            std::memcpy(static_cast<uint8_t*>(aim_stub) + bytes.size() - sizeof(original_aim),
                &original_aim, sizeof(original_aim));
            FlushInstructionCache(GetCurrentProcess(), aim_stub, bytes.size());
            DWORD previous{};
            if (!VirtualProtect(aim_stub, bytes.size(), PAGE_EXECUTE_READ, &previous))
            {
                rollback();
                return false;
            }
            return true;
        }

        bool can_airstep(void* state_interface)
        {
            auto original = reinterpret_cast<CanAirstep>(original_can);
            if (!state_interface)
            {
                return original(state_interface);
            }
            auto* pawn = reinterpret_cast<UObject*>(reinterpret_cast<std::byte*>(state_interface) - character_native::layout::player_state_interface_offset);
            if (!is_uobject_valid_fast(pawn) || !character_swap::owns_swap_pawn(pawn))
            {
                return original(state_interface);
            }
            // This one native eligibility call hardcodes Sora's character kind.
            // Keep every other native check and state-factory request intact.
            // The real kind is restored synchronously before returning to input.
            NativePlayerKindScope player_kind(pawn);
            return original(state_interface);
        }
    }

    bool initialize()
    {
        if (initialized) { return true; }
        if (initialization_failed) { return false; }
        using character_native::Site;
        if (!character_native::verify_sites({Site::airstep_eligibility, Site::airstep_character_kind,
                                             Site::airstep_aim_kind_gate, Site::airstep_aim_kind_compare}))
        {
            return false;
        }
        const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        hook = std::make_unique<PLH::x64Detour>(base + character_native::rva(Site::airstep_eligibility),
            reinterpret_cast<uintptr_t>(&can_airstep), &original_can);
        if (!hook->hook() || !original_can || !install_aim_gate(base))
        {
            initialization_failed = true;
            hook.reset();
            return false;
        }
        initialized = true;
        return true;
    }

    void prepare(UObject* pawn, UObject* controller)
    {
        aim_pawn = make_uobject_weak(pawn);
        aim_controller = make_uobject_weak(controller);
    }
}
