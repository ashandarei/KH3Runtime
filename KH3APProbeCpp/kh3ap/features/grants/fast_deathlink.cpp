#include <polyhook2/Detour/x64Detour.hpp>
#include "kh3ap/features/grants/fast_deathlink.h"
#include "kh3ap/core/objects/actor_owning_world.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/features/player/characters/character_profile.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/FWeakObjectPtr.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UObject.hpp>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <memory>
#include <optional>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace kh3ap::fast_deathlink
{
    using namespace RC::Unreal;
    namespace
    {
        using Clock = std::chrono::steady_clock;
        using UpdateGameOver = void (*)(UObject*);

        // UTresGameInstance's normal Game Over state on EGS CL 1065258.
        constexpr uintptr_t update_game_over_rva = 0x111d500;
        constexpr size_t game_state = 0x1d0;
        constexpr size_t game_over_step = 0x214;
        constexpr size_t game_over_wait = 0x218;
        constexpr size_t game_over_kind = 0x2f8;
        constexpr int game_over_state = 3;
        constexpr int start_fade_out = 0;
        constexpr int start_wait = 4;
        constexpr int open_menu = 5;
        constexpr int wait_for_menu_choice = 6;

        // Layer 0's pending fade command. The renderer consumes this on its
        // next update, using the original color, layer and completion path.
        constexpr size_t game_viewport_context = 0x48;
        constexpr size_t screen_effects = 0x238;
        constexpr size_t fade_out_virtual = 0x340;
        constexpr uintptr_t fade_out_rva = 0x18837b0;
        constexpr size_t fade_in_virtual = 0x348;
        constexpr uintptr_t fade_in_rva = 0x1883840;
        constexpr size_t pending_fade = 0x7d8;
        constexpr size_t pending_fade_duration = 0x7e4;
        constexpr size_t pending_fade_delay = 0x7e8;

        struct Episode
        {
            FWeakObjectPtr instance;
            Clock::time_point expires;
        };
        thread_local std::optional<Episode> episode;
        std::unique_ptr<PLH::x64Detour> update_hook;
        uint64_t original_update{};

        template <typename T>
        auto field(void* object, size_t offset) -> T&
        {
            return *reinterpret_cast<T*>(static_cast<std::byte*>(object) + offset);
        }

        auto module_base() -> uintptr_t
        {
            return reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        }

        void shorten_fade(UObject* instance, int command)
        {
            auto* viewport = field<void*>(instance, game_viewport_context);
            auto* effects = viewport ? field<void*>(viewport, screen_effects) : nullptr;
            if (!effects || field<int>(effects, pending_fade) != command)
            {
                return;
            }
            auto* vtable = field<void*>(effects, 0);
            const auto base = module_base();
            if (!vtable || field<uintptr_t>(vtable, fade_out_virtual) != base + fade_out_rva ||
                field<uintptr_t>(vtable, fade_in_virtual) != base + fade_in_rva)
            {
                return;
            }
            field<float>(effects, pending_fade_duration) = 0.05f;
            field<float>(effects, pending_fade_delay) = 0.f;
        }

        void update(UObject* instance)
        {
            const auto original = reinterpret_cast<UpdateGameOver>(original_update);
            if (!episode || episode->instance.Get() != instance ||
                Clock::now() >= episode->expires)
            {
                episode.reset();
                original(instance);
                return;
            }

            const auto step = field<int>(instance, game_over_step);
            original(instance);
            const auto next_step = field<int>(instance, game_over_step);
            if (step == start_fade_out && next_step == start_fade_out + 1)
            {
                shorten_fade(instance, 2);
            }
            else if (step == start_wait && next_step == open_menu)
            {
                field<float>(instance, game_over_wait) = 0.f;
            }
            else if (step == open_menu && next_step == wait_for_menu_choice)
            {
                shorten_fade(instance, 1);
            }

            if (next_step >= wait_for_menu_choice ||
                field<int>(instance, game_state) != game_over_state)
            {
                episode.reset();
            }
        }
    }

    auto supported() -> bool
    {
        static const bool verified = []
        {
            if (character_native::build() != native_build::Build::Egs)
            {
                return false;
            }
            constexpr std::array<uint8_t, 23> update_entry{
                0x48, 0x8b, 0xc4, 0x55, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
                0x48, 0x8d, 0x68, 0xc8, 0x48, 0x81, 0xec, 0x10, 0x01, 0x00, 0x00};
            constexpr std::array<uint8_t, 10> start_menu_wait{
                0xc7, 0x87, 0x18, 0x02, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3f};
            const auto base = module_base();
            return !std::memcmp(reinterpret_cast<const void*>(base + update_game_over_rva),
                                update_entry.data(), update_entry.size()) &&
                   !std::memcmp(reinterpret_cast<const void*>(base + 0x111e4a6),
                                start_menu_wait.data(), start_menu_wait.size());
        }();
        return verified;
    }

    auto arm(UObject* pawn) -> bool
    {
        if (!supported())
        {
            return false;
        }
        auto* world = actor_owning_world(pawn);
        auto* instance = try_read_reflected_object_property_value(world, L"OwningGameInstance");
        if (!is_uobject_valid_fast(instance) ||
            instance->GetClassPrivate()->GetName() != L"TresGameInstance" ||
            field<int>(instance, game_state) == game_over_state)
        {
            return false;
        }
        if (!update_hook)
        {
            auto hook = std::make_unique<PLH::x64Detour>(
                module_base() + update_game_over_rva, reinterpret_cast<uintptr_t>(&update),
                &original_update);
            if (!hook->hook())
            {
                RC::Output::send<RC::LogLevel::Warning>(
                    L"[KH3APProbeCpp] Fast DeathLink could not install the Game Over hook\n");
                return false;
            }
            update_hook = std::move(hook);
        }
        auto& pending = episode.emplace();
        pending.instance = instance;
        pending.expires = Clock::now() + std::chrono::seconds{5};
        return true;
    }

    void confirm()
    {
        auto* instance = episode ? episode->instance.Get() : nullptr;
        if (!is_uobject_valid_fast(instance) || field<int>(instance, game_state) != game_over_state ||
            field<uint8_t>(instance, game_over_kind) != 0)
        {
            episode.reset();
        }
    }

    void shutdown()
    {
        episode.reset();
        if (update_hook && update_hook->isHooked())
        {
            update_hook->unHook();
        }
        update_hook.reset();
    }
}
