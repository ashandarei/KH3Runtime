#include <polyhook2/Detour/x64Detour.hpp>
#include "kh3ap/features/player/movement/character_floating.h"
#include "kh3ap/features/player/characters/character_profile.h"
#include "kh3ap/features/player/player_state_ids.h"
#include "kh3ap/features/player/characters/character_swap.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/core/reflection/reflected_property_cache.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include <Unreal/UObject.hpp>
#include <Unreal/FProperty.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#include <memory>
#include <stdexcept>
#include <string>
#define NOMINMAX
#include <Windows.h>

namespace kh3ap::character_floating
{
    using namespace RC::Unreal;
    namespace
    {
        using Notify = void (*)(UObject*, bool, bool);
        using TornadoBegin = void (*)(UObject*);
        uint64_t original_notify{};
        uint64_t original_tornado{};
        std::unique_ptr<PLH::x64Detour> notify_hook;
        std::unique_ptr<PLH::x64Detour> tornado_hook;
        FWeakObjectPtr freefall_world;
        ReflectedPropertyCache<16> properties;

        bool tornado_world(UObject* world)
        {
            return is_uobject_valid_fast(world) &&
                world->GetPathName().starts_with(L"/Game/Levels/he/he_06/he_06.");
        }

        UObject* local_pawn()
        {
            auto* instance = properties.read_object(kh02_current_viewport(), L"GameInstance");
            auto* manager = properties.read_object(instance, L"m_UIManager");
            auto* controller = properties.read_object(manager, L"m_pPlayerController");
            auto* pawn = properties.read_object(controller, L"Pawn");
            auto* local = properties.read_object(controller, L"Player");
            return is_uobject_valid_fast(pawn) && properties.read_object(pawn, L"Controller") == controller &&
                properties.read_object(local, L"PlayerController") == controller &&
                kh02_actor_world(pawn) == kh02_current_world() ? pawn : nullptr;
        }

        int state_id(UObject* pawn)
        {
            auto* current = properties.read_object(properties.read_object(pawn, L"MyStateComp"), L"m_pCurrentState");
            auto* id = properties.resolve(current, L"MyStateID");
            return id && id->GetElementSize() == 1 ? *id->ContainerPtrToValuePtr<uint8_t>(current) : 0;
        }

        void notify_floating(UObject* source_controller, bool enabled, bool special)
        {
            const auto original = reinterpret_cast<Notify>(original_notify);
            original(source_controller, enabled, special);
            auto* pawn = local_pawn();
            if (!pawn || !tornado_world(kh02_actor_world(pawn)) ||
                kh02_actor_world(source_controller) != kh02_actor_world(pawn))
            {
                return;
            }
            auto* controller = properties.read_object(pawn, L"Controller");
            if (!is_uobject_valid_fast(controller))
            {
                return;
            }
            // Preserve the actual event when its original controller was
            // replaced. Never clear queues or force Floating here: the native
            // freefall and ground slam must complete before the glide phase.
            if (controller != source_controller &&
                (freefall_world.Get() == kh02_current_world() || character_swap::owns_swap_pawn(pawn)))
            {
                original(controller, enabled, special);
            }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            RC::Output::send<RC::LogLevel::Normal>(
                STR("[KH3APProbeCpp] Tornado Floating event pawn={} enabled={} special={} state={} redirected={}\n"),
                pawn->GetName(), enabled, special, state_id(pawn), controller != source_controller);
#endif
        }

        void begin_tornado(UObject* action)
        {
            const auto original = reinterpret_cast<TornadoBegin>(original_tornado);
            auto* boss = properties.read_object(action, L"MyCharPawn");
            auto* world = kh02_actor_world(boss);
            if (!tornado_world(world) || world != kh02_current_world())
            {
                original(action);
                return;
            }
            auto* pawn = local_pawn();
            try
            {
                if (character_swap::owns_swap_pawn(pawn))
                {
                    pawn = character_swap::return_to_sora_for_encounter(pawn);
                }
            }
            catch (const std::exception& failure)
            {
                const std::string message = failure.what();
                RC::Output::send<RC::LogLevel::Warning>(STR("[KH3APProbeCpp] Tornado Sora return failed: {}\n"),
                    std::wstring(message.begin(), message.end()));
                pawn = local_pawn();
            }
            // Native initialization caches m_Player and its Sora dive data.
            // Return before that capture, then let the original suck-in,
            // projectiles and timed dive-command sequence run unchanged.
            freefall_world = make_uobject_weak(world);
            original(action);
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            auto* cached_player = properties.read_object(boss, L"m_Player");
            RC::Output::send<RC::LogLevel::Normal>(STR("[KH3APProbeCpp] Tornado freefall begin pawn={} cached_player={}\n"),
                is_uobject_valid_fast(pawn) ? pawn->GetName() : L"null",
                is_uobject_valid_fast(cached_player) ? cached_player->GetName() : L"null");
#endif
        }
    }

    bool initialize()
    {
        if (notify_hook && tornado_hook)
        {
            return true;
        }
        using character_native::Site;
        if (!character_native::verify_sites({Site::floating_notify, Site::tornado_freefall_begin}))
        {
            return false;
        }
        const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        notify_hook = std::make_unique<PLH::x64Detour>(base + character_native::rva(Site::floating_notify),
            reinterpret_cast<uintptr_t>(&notify_floating), &original_notify);
        tornado_hook = std::make_unique<PLH::x64Detour>(base + character_native::rva(Site::tornado_freefall_begin),
            reinterpret_cast<uintptr_t>(&begin_tornado), &original_tornado);
        if (!notify_hook->hook() || !original_notify || !tornado_hook->hook() || !original_tornado)
        {
            tornado_hook.reset();
            notify_hook.reset();
            return false;
        }
        return true;
    }

    bool allows_swap(UObject* pawn)
    {
        if (character_native::build() == character_native::Build::Unsupported || !tornado_world(kh02_current_world()))
        {
            return true;
        }
        const int state = state_id(pawn);
        if (state == player_state_ids::floating && freefall_world.Get() == kh02_current_world())
        {
            freefall_world = {};
        }
        auto* current = properties.read_object(properties.read_object(pawn, L"MyStateComp"), L"m_pCurrentState");
        auto* step = properties.resolve(current, L"m_nSubStep");
        const bool diving = state == player_state_ids::jump && step &&
                            step->GetElementSize() == sizeof(int32_t) &&
                            *step->ContainerPtrToValuePtr<int32_t>(current) == 8;
        return !diving && freefall_world.Get() != kh02_current_world();
    }
}
