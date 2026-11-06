#include "kh02_challenge_hooks.h"
#include "kh3ap/features/kh02/core/kh02_profile.h"
#include "kh3ap/features/kh02/combat/kh02_combat_swap.h"
#include "kh3ap/features/journal/adversary_service.h"
#include "kh3ap/core/events/character_state_events.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/features/grants/abilities/ability_snapshot_helpers.h"
#include <polyhook2/Detour/x64Detour.hpp>
#include <Unreal/UObject.hpp>
#define NOMINMAX
#include <Windows.h>
#include <atomic>
#include <cstring>

namespace kh3ap
{
    using RC::Unreal::UObject;

    namespace
    {
        constexpr std::size_t rainbow_shower_substep_offset = 0x78;
        constexpr std::size_t rainbow_shower_grade_offset = 0x160;
        constexpr int rainbow_shower_consume_result_substep = 8;
        constexpr int first_shotlock_grade = 1;
        constexpr int last_shotlock_grade = 5;
    } // namespace

    struct Kh02ChallengeHooks::Impl
    {
        inline static std::atomic<Impl*> instance{};
        inline static uint64_t damage_trampoline{};
        inline static uint64_t shotlock_trampoline{};
        Callbacks callbacks;
        std::unique_ptr<PLH::x64Detour> damage_hook;
        std::unique_ptr<PLH::x64Detour> shotlock_hook;
        bool hook_attempted{}, hook_installed{}, shotlock_installed{};

        explicit Impl(Callbacks observers) : callbacks(observers)
        {
            instance = this;
            set_adversary_defeat_observer(&defeat_dispatch);
            character_state_observer = &state_dispatch;
        }

        void install(std::string& error)
        {
            if (hook_attempted)
            {
                return;
            }
            hook_attempted = true;
            const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
            using kh02_native::Site;
            if (!kh02_native::verify_sites({Site::take_damage}))
            {
                error = "kh02_challenge_damage_profile";
                return;
            }
            damage_hook =
                std::make_unique<PLH::x64Detour>(base + kh02_native::rva(Site::take_damage),
                                                 reinterpret_cast<uint64_t>(&damage_dispatch), &damage_trampoline);
            hook_installed = damage_hook->hook();
            if (!hook_installed)
            {
                error = "kh02_challenge_damage_hook";
            }
            // Both builds consume the native result at substep 8. Their
            // compiler-generated result branches differ; verify each profile.
            if (!kh02_native::verify_sites({Site::rainbow_update, Site::rainbow_result}))
            {
                error = "The Rainbow Shower result layout does not match this build.";
                return;
            }
            shotlock_hook =
                std::make_unique<PLH::x64Detour>(base + kh02_native::rva(Site::rainbow_update),
                                                 reinterpret_cast<uint64_t>(&shotlock_dispatch), &shotlock_trampoline);
            shotlock_installed = shotlock_hook->hook();
        }

        void stop()
        {
            if (hook_installed)
            {
                damage_hook->unHook();
                hook_installed = false;
            }
            if (shotlock_installed)
            {
                shotlock_hook->unHook();
                shotlock_installed = false;
            }
            if (instance.load() == this)
            {
                set_adversary_defeat_observer(nullptr);
                character_state_observer = nullptr;
                instance = nullptr;
            }
        }

        static float damage_dispatch(UObject* victim, float amount, const void* event, UObject* by, UObject* causer)
        {
            const auto original = reinterpret_cast<DamageFunction>(damage_trampoline);
            if (Kh02CombatSwap::blocks_swing_damage(victim, by, causer, event))
            {
                return 0.0f;
            }
            float result{};
            if (auto* self = instance.load())
            {
                // Attribution samples before and after the original call: defeat
                // notifications emitted inside it must be visible to the tracker.
                result = self->callbacks.damage(self->callbacks.context, victim, amount, event, by, causer, original);
            }
            else
            {
                result = original(victim, amount, event, by, causer);
            }
            if (result > 0.0f)
            {
                Kh02CombatSwap::record_swing_damage(victim, by, causer, event);
            }
            return result;
        }

        static void defeat_dispatch(UObject* enemy, int32_t amount) noexcept
        {
            if (auto* self = instance.load())
            {
                self->callbacks.defeated(self->callbacks.context, enemy, amount);
            }
        }

        static void state_dispatch(void* component, void* state) noexcept
        {
            if (auto* self = instance.load())
            {
                self->callbacks.state_changed(self->callbacks.context, component, state);
            }
        }

        static void shotlock_dispatch(UObject* state, float delta)
        {
            auto* self = instance.load();
            bool consume = false;
            int result = 0;
            if (self && self->callbacks.observes_shotlock(self->callbacks.context, state))
            {
                const auto bytes = try_read_process_memory_bytes(
                    reinterpret_cast<uintptr_t>(state) + rainbow_shower_substep_offset, sizeof(int32_t));
                const auto grade = try_read_process_memory_bytes(
                    reinterpret_cast<uintptr_t>(state) + rainbow_shower_grade_offset, sizeof(int32_t));
                int step = -1;
                if (bytes && grade)
                {
                    std::memcpy(&step, bytes->data(), sizeof(step));
                    std::memcpy(&result, grade->data(), sizeof(result));
                    consume = step == rainbow_shower_consume_result_substep && result >= first_shotlock_grade &&
                              result <= last_shotlock_grade;
                }
            }
            reinterpret_cast<void (*)(UObject*, float)>(shotlock_trampoline)(state, delta);
            int next_step = rainbow_shower_consume_result_substep;
            if (consume && is_uobject_valid_fast(state))
            {
                const auto step = try_read_process_memory_bytes(
                    reinterpret_cast<uintptr_t>(state) + rainbow_shower_substep_offset, sizeof(int32_t));
                if (step)
                {
                    std::memcpy(&next_step, step->data(), sizeof(next_step));
                }
            }
            if (consume && next_step != rainbow_shower_consume_result_substep)
            {
                self->callbacks.shotlock_result(self->callbacks.context, result);
            }
        }
    };

    Kh02ChallengeHooks::Kh02ChallengeHooks(Callbacks callbacks) : impl(std::make_unique<Impl>(callbacks)) {}

    Kh02ChallengeHooks::~Kh02ChallengeHooks()
    {
        stop();
    }

    void Kh02ChallengeHooks::install(std::string& error)
    {
        impl->install(error);
    }

    void Kh02ChallengeHooks::stop()
    {
        impl->stop();
    }

    auto Kh02ChallengeHooks::damage_installed() const -> bool
    {
        return impl->hook_installed;
    }

    auto Kh02ChallengeHooks::shotlock_installed() const -> bool
    {
        return impl->shotlock_installed;
    }

    auto Kh02ChallengeHooks::contains_image_address(uintptr_t image_base, uintptr_t address) -> bool
    {
        return address >= image_base && address - image_base < kh02_native::image_size();
    }
} // namespace kh3ap
