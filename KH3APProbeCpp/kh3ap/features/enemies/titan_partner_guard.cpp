#include <polyhook2/Detour/x64Detour.hpp>
#include "kh3ap/features/enemies/titan_arena.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/core/platform/game_build.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/UObject.hpp>
#include <array>
#include <cstring>
#include <memory>
#include <Windows.h>

namespace kh3ap
{
    using namespace RC;
    using namespace RC::Unreal;

    namespace
    {
        using TitanHitActorTick = void (*)(UObject*, float);
        constexpr size_t titan_hit_actor_player_offset = 0x400;
        std::array<uint64_t, 2> titan_partner_trampolines{};
        std::array<std::unique_ptr<PLH::x64Detour>, 2> titan_partner_hooks{};
        uint64_t titan_hit_actor_trampoline{};
        std::unique_ptr<PLH::x64Detour> titan_hit_actor_hook;

        auto identify_image(const uint8_t* image) -> native_build::Build
        {
            if (!image)
            {
                return native_build::Build::Unsupported;
            }
            const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
            const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(image + dos->e_lfanew);
            return native_build::identify(nt->FileHeader.TimeDateStamp, nt->OptionalHeader.SizeOfImage);
        }

        void tick_titan_hit_actor(UObject* hit_actor, float delta_seconds)
        {
            if (is_uobject_valid_fast(hit_actor))
            {
                auto& player = *reinterpret_cast<UObject**>(
                    reinterpret_cast<uint8_t*>(hit_actor) + titan_hit_actor_player_offset);
                if (player && !is_uobject_valid_fast(player))
                {
                    // TresHitActor_e_he90x can outlive the controlled pawn across
                    // a battle transition. A null cache makes native Tick resolve
                    // the current pawn from its world again.
                    player = nullptr;
                    Output::send<LogLevel::Normal>(STR("[KH3APProbeCpp] Titan hit actor cleared stale player cache\n"));
                }
            }
            reinterpret_cast<TitanHitActorTick>(titan_hit_actor_trampoline)(hit_actor, delta_seconds);
        }

        template <size_t Titan>
        void resolve_titan_partner(UObject* pawn)
        {
            const auto* level = is_uobject_valid_fast(pawn) ? pawn->GetOuterPrivate() : nullptr;
            const auto* sublevel_world = is_uobject_valid_fast(level) ? level->GetOuterPrivate() : nullptr;
            static const FName olympus_enemy_level{L"he_05_enemy_01", FNAME_Add};
            if (is_uobject_valid_fast(sublevel_world) &&
                !sublevel_world->GetNamePrivate().Equals(olympus_enemy_level))
            {
                // Native lookup searches the whole world and caches a raw
                // pointer. Outside the authored arena, either boss can be
                // retired independently, even from a different sublevel.
                return;
            }

            using ResolvePartner = void (*)(UObject*);
            reinterpret_cast<ResolvePartner>(titan_partner_trampolines[Titan])(pawn);
        }
    }

    bool install_titan_hit_actor_guard()
    {
        if (titan_hit_actor_hook)
        {
            return true;
        }
        const auto* image = reinterpret_cast<const uint8_t*>(GetModuleHandleW(nullptr));
        const auto build = identify_image(image);
        if (build == native_build::Build::Unsupported)
        {
            return false;
        }
        const bool steam = build == native_build::Build::Steam;
        const auto* tick = image + (steam ? 0xddc910 : 0x1045620);
        constexpr uint8_t prefix[]{0x40, 0x53, 0x48, 0x83, 0xec, 0x40, 0x48, 0x8b, 0xd9, 0xe8};
        const uint8_t cache_read[]{0x48, 0x8b, static_cast<uint8_t>(steam ? 0x8b : 0x83),
                                   0x00, 0x04, 0x00, 0x00};
        constexpr uint8_t cache_write[]{0x48, 0x89, 0x83, 0x00, 0x04, 0x00, 0x00};
        if (std::memcmp(tick, prefix, sizeof(prefix)) != 0 ||
            std::memcmp(tick + 0x28, cache_read, sizeof(cache_read)) != 0 ||
            std::memcmp(tick + 0x48, cache_write, sizeof(cache_write)) != 0)
        {
            return false;
        }

        auto hook = std::make_unique<PLH::x64Detour>(reinterpret_cast<uint64_t>(tick),
            reinterpret_cast<uint64_t>(&tick_titan_hit_actor), &titan_hit_actor_trampoline);
        if (!hook->hook())
        {
            return false;
        }
        titan_hit_actor_hook = std::move(hook);
        Output::send<LogLevel::Normal>(STR("[KH3APProbeCpp] Titan hit actor player cache guard installed\n"));
        return true;
    }

    bool install_titan_partner_guard()
    {
        if (titan_partner_hooks[0] && titan_partner_hooks[1])
        {
            return true;
        }

        const auto* image = reinterpret_cast<const uint8_t*>(GetModuleHandleW(nullptr));
        const auto build = identify_image(image);
        if (build == native_build::Build::Unsupported)
        {
            return false;
        }

        const bool steam = build == native_build::Build::Steam;
        const std::array<uint32_t, 2> lookup_offsets = steam
            ? std::array<uint32_t, 2>{0xdae620, 0xdd15a0}
            : std::array<uint32_t, 2>{0x1025cc0, 0x10422b0};
        constexpr uint8_t lookup_prefix[]{
            0x41, 0x56, 0x48, 0x83, 0xec, 0x40, 0x48, 0xc7, 0x44, 0x24, 0x20, 0xfe,
            0xff, 0xff, 0xff, 0x48, 0x89, 0x5c, 0x24, 0x50, 0x48, 0x89, 0x74, 0x24,
            0x58, 0x48, 0x89, 0x7c, 0x24, 0x60, 0x4c, 0x8b, 0xf1, 0x48, 0x83, 0xb9,
            0xd8, 0x0c, 0x00, 0x00, 0x00,
        };
        const uint8_t cache_write[]{0x49, 0x89, static_cast<uint8_t>(steam ? 0xbe : 0x9e),
                                    0xd8, 0x0c, 0x00, 0x00};
        for (const auto offset : lookup_offsets)
        {
            if (std::memcmp(image + offset, lookup_prefix, sizeof(lookup_prefix)) != 0 ||
                std::memcmp(image + offset + 0x108, cache_write, sizeof(cache_write)) != 0)
            {
                return false;
            }
        }

        const std::array callbacks{&resolve_titan_partner<0>, &resolve_titan_partner<1>};
        // Keep both installations local until they succeed so an early return
        // unhooks any partial installation.
        std::array<std::unique_ptr<PLH::x64Detour>, 2> hooks;
        for (size_t i = 0; i < lookup_offsets.size(); ++i)
        {
            hooks[i] = std::make_unique<PLH::x64Detour>(
                reinterpret_cast<uint64_t>(image + lookup_offsets[i]),
                reinterpret_cast<uint64_t>(callbacks[i]), &titan_partner_trampolines[i]);
            if (!hooks[i]->hook())
            {
                return false;
            }
        }
        titan_partner_hooks = std::move(hooks);
        Output::send<LogLevel::Normal>(STR("[KH3APProbeCpp] Titan partner guard installed scope=outside_olympus_encounter\n"));
        return true;
    }
} // namespace kh3ap
