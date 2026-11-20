#include "kh3ap/core/platform/game_build.h"
#include "kh3ap/features/enemies/bosses/binding.h"
#include "kh3ap/features/enemies/bosses/encounter_repairs.h"
#include "kh3ap/core/reflection/reflected_function_call.h"
#include "kh3ap/features/player/player_ids.h"
#include "kh3ap/features/world/garden_return/runtime_context.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Windows.h>
#include <cstring>

namespace kh3ap::enemy_remap_detail
{
    namespace
    {
        using FinishGrab = void (*)(UObject*);
        using SetStolenWeapon = bool (*)(UObject*, UObject*, bool);
        std::atomic<FinishGrab> original_finish{};
        SetStolenWeapon set_stolen_weapon{};
        std::unique_ptr<RuntimeBossHpLimitOverride> finish_override;

        void complete_alternate_keyblade_steal(UObject* action)
        {
            if (try_read_int32_object_property(action, L"m_nSubStep") != 7)
            {
                return;
            }
            auto* victim = read_boss_owner_property(action, L"m_CatchPlayer");
            const auto id = garden_return_detail::player_id(victim);
            if (!is_alternate_keyblade_wielder(id))
            {
                return;
            }
            auto* boss = read_boss_owner_property(action, L"MyCharPawn");
            const auto binding = resolve_tracked_runtime_boss_pawn(boss);
            const auto source = tracked_boss_source_class(binding.level_entity_path);
            if (!boss || !source || *source == boss->GetClassPrivate()->GetPathName())
            {
                return;
            }

            // At the successful release (substep 7), a landed bind has bit 1 set.
            // Bit 0 is set if the cinematic already performed the weapon transfer.
            constexpr size_t heart_break_flags = 0xd48;
            if (boss->GetClassPrivate()->GetPropertiesSize() <= heart_break_flags ||
                (*(reinterpret_cast<const uint8_t*>(boss) + heart_break_flags) & 3) != 2)
            {
                return;
            }
            ReflectedFunctionCall local{L"/Script/Engine.Default__GameplayStatics", L"GetPlayerPawn",
                                    "yozora_stolen_weapon", 64};
            local.object(L"WorldContextObject", boss);
            local.run();
            if (local.result() == victim && set_stolen_weapon(boss, victim, true))
            {
                Output::send<LogLevel::Normal>(STR("[KH3APProbeCpp] Yozora Keyblade steal player={}\n"), id);
            }
        }

        void finish_grab(UObject* action)
        {
            try
            {
                complete_alternate_keyblade_steal(action);
            }
            catch (const std::exception&)
            {
                Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] Yozora Keyblade transfer unavailable\n"));
            }
            original_finish.load(std::memory_order_acquire)(action);
        }
    }

    void install_yozora_keyblade_steal()
    {
        if (finish_override)
        {
            return;
        }
        const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        if (!base)
        {
            return;
        }
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        {
            return;
        }
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE)
        {
            return;
        }
        const auto build = native_build::identify(nt->FileHeader.TimeDateStamp, nt->OptionalHeader.SizeOfImage);
        if (build == native_build::Build::Unsupported)
        {
            return;
        }
        auto* klass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, L"/Script/TresGame.TresAttack12_e_ex781_HeartBreak");
        auto* defaults = is_uobject_valid_fast(klass) ? klass->GetClassDefaultObject() : nullptr;
        if (!is_uobject_valid_fast(defaults))
        {
            return;
        }
        const bool egs = build == native_build::Build::Egs;
        auto** slot = *reinterpret_cast<void***>(defaults) + 123;
        const auto setter = base + (egs ? 0xff0950 : 0xd79af0);
        // The transfer's enable branch calls the captured pawn's SetRestrictWeapon virtual.
        constexpr uint8_t restrict_call[]{0xff, 0x90, 0xd0, 0x1e, 0x00, 0x00};
        const auto call = setter + (egs ? 0xa1 : 0x8f);
        if (reinterpret_cast<uintptr_t>(*slot) != base + (egs ? 0xfaf6d0 : 0xd42a10) ||
            std::memcmp(reinterpret_cast<const void*>(call), restrict_call, sizeof(restrict_call)) != 0)
        {
            return;
        }
        auto guard = std::make_unique<RuntimeBossHpLimitOverride>();
        guard->slot = slot;
        guard->original = *slot;
        guard->replacement = reinterpret_cast<void*>(&finish_grab);
        PLH::MemAccessor memory{};
        PLH::MemoryProtector protection{reinterpret_cast<uint64_t>(slot), sizeof(void*),
                                        PLH::ProtFlag::R | PLH::ProtFlag::W, memory};
        if (!protection.isGood())
        {
            return;
        }
        set_stolen_weapon = reinterpret_cast<SetStolenWeapon>(setter);
        original_finish.store(reinterpret_cast<FinishGrab>(*slot), std::memory_order_release);
        *slot = guard->replacement;
        finish_override = std::move(guard);
    }
}
