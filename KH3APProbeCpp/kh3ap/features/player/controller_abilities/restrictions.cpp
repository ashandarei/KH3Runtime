#include "kh3ap/core/platform/game_build.h"
#include "kh3ap/features/player/characters/character_swap.h"
#include "kh3ap/features/player/combat/aqua_style_unlock.h"
#include "kh3ap/features/player/movement/alternate_wall_run.h"
#include "kh3ap/features/player/movement/kairi_glide.h"
#include "kh3ap/features/player/characters/character_exploration.h"
#include "kh3ap/features/player/combat/character_magic.h"
#include "kh3ap/features/player/controller_abilities/host.h"

#include "kh3ap/features/kh02/story/kh02_story.h"
#include "kh3ap/features/kh02/rewards/kh02_shared_unlocks.h"
#include "kh3ap/features/player/controller_abilities/magic_link_gates.h"
#include "kh3ap/features/grants/vbonus/vbonus_grant_call_helpers.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <cstddef>
#include <cstring>
#include <utility>

#define NOMINMAX
#include <windows.h>

namespace kh3ap::controller_ability_detail
{
    using namespace RC;
    using namespace RC::Unreal;
    namespace restriction_layout
    {
        constexpr uint32_t egs_timestamp = kh3ap::native_build::egs.timestamp;
        constexpr uint32_t egs_image_bytes = kh3ap::native_build::egs.image_size;
        constexpr size_t egs_magic_setter_rva = 0x13c2720;
        constexpr size_t egs_links_setter_rva = 0x13c2780;
        constexpr uint32_t steam_timestamp = kh3ap::native_build::steam.timestamp;
        constexpr uint32_t steam_image_bytes = kh3ap::native_build::steam.image_size;
        constexpr size_t steam_magic_setter_rva = 0x1112750;
        constexpr size_t steam_links_setter_rva = 0x1112730;
        constexpr size_t pawn_flags_offset = 0xe60;
    } // namespace restriction_layout
    auto try_read_command_restrictions(UObject* pawn, uint32_t& flags) -> bool
    {
        // Passive read only. BP_SetRestrictMagic/Link own these bits; all
        // writes still go through those reflected native functions. Guard
        // both supported layouts with their actual native setter bodies.
        static const bool supported = []
        {
            const auto* base = reinterpret_cast<const std::byte*>(GetModuleHandleW(nullptr));
            const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
            const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
            size_t magic = 0, links = 0;
            if (nt->FileHeader.TimeDateStamp == restriction_layout::egs_timestamp &&
                nt->OptionalHeader.SizeOfImage == restriction_layout::egs_image_bytes)
            {
                magic = restriction_layout::egs_magic_setter_rva;
                links = restriction_layout::egs_links_setter_rva;
            }
            else if (nt->FileHeader.TimeDateStamp == restriction_layout::steam_timestamp &&
                     nt->OptionalHeader.SizeOfImage == restriction_layout::steam_image_bytes)
            {
                magic = restriction_layout::steam_magic_setter_rva;
                links = restriction_layout::steam_links_setter_rva;
            }
            else
            {
                return false;
            }
            constexpr unsigned char magic_signature[]{0x83, 0xa1, 0x60, 0x0e, 0,    0,    0xfd,
                                                      0x0f, 0xb6, 0xc2, 0x03, 0xc0, 0x09, 0x81,
                                                      0x60, 0x0e, 0,    0,    0xc3};
            constexpr unsigned char link_signature[]{0x83, 0xa1, 0x60, 0x0e, 0,    0,   0xef,
                                                     0x0f, 0xb6, 0xc2, 0xc1, 0xe0, 4,   0x09,
                                                     0x81, 0x60, 0x0e, 0,    0,    0xc3};
            return std::memcmp(base + magic, magic_signature, sizeof(magic_signature)) == 0 &&
                   std::memcmp(base + links, link_signature, sizeof(link_signature)) == 0;
        }();
        if (!supported || !kh3ap::is_uobject_valid_fast(pawn))
        {
            return false;
        }
        std::memcpy(&flags,
                    reinterpret_cast<const std::byte*>(pawn) +
                        restriction_layout::pawn_flags_offset,
                    sizeof(flags));
        return true;
    }
} // namespace kh3ap::controller_ability_detail
namespace kh3ap
{
    using namespace RC;
    using namespace RC::Unreal;
    using namespace controller_ability_detail;
    auto RuntimeControllerAbilityService::Impl::observe_restriction_pawn(UObject* pawn,
                                                                         uint32_t flags) -> void
    {
        if (restriction_pawn_.Get() == pawn)
        {
            return;
        }
        restriction_pawn_ = pawn;
        story_restricts_magic_ = (flags & restriction_bits::magic) != 0;
        story_restricts_links_ = (flags & restriction_bits::links) != 0;
        applied_states_.reset();
    }

    auto RuntimeControllerAbilityService::Impl::apply_magic_links(UObject* controller,
                                                                  UObject* pawn,
                                                                  std::wstring* failure) -> bool
    {
        // Grant callers need a fresh post-grant read. Event refreshes pass the
        // snapshot they already captured through the overload below.
        constexpr std::array<uint8_t, 2> abilities{ability_id::magic, ability_id::links};
        std::array<RuntimeSoraAbilityEntryState, abilities.size()> states{};
        if (!try_read_runtime_sora_ability_entry_states(abilities, pawn, states, failure))
        {
            return false;
        }
        return apply_magic_links(controller, pawn, states[0].owned && states[0].equipped,
                                 states[1].owned && states[1].equipped, failure);
    }

    auto RuntimeControllerAbilityService::Impl::apply_magic_links(UObject* controller,
                                                                  UObject* pawn,
                                                                  bool magic_enabled,
                                                                  bool links_enabled,
                                                                  std::wstring* failure) -> bool
    {
        const RuntimeHotPathProfileScope profile{L"controller_abilities:magic_links"};
        uint32_t flags{};
        if (!try_read_command_restrictions(pawn, flags))
        {
            if (failure)
            {
                *failure = STR("Magic/Links restriction layout unavailable");
            }
            return false;
        }
        observe_restriction_pawn(pawn, flags);
        const auto gates = resolve_runtime_magic_link_gates(
            magic_enabled, links_enabled, story_restricts_magic_, story_restricts_links_);
        // Apply restrictions before enabling MP so a Links-only grant never
        // briefly enables spells (or vice versa).
        for (const auto [index, restricted] :
             {std::pair<size_t, bool>{setter_index::magic_restriction, gates.magic_restricted},
              {setter_index::links_restriction, gates.links_restricted}})
        {
            if (setter_needs_update(index, restricted))
            {
                if (!call_setter(controller, index, restricted, failure))
                {
                    return false;
                }
                applied_states_.record(index, restricted);
            }
        }
        if (setter_needs_update(setter_index::mp_enabled, gates.mp_enabled))
        {
            if (!call_setter(controller, setter_index::mp_enabled, gates.mp_enabled, failure))
            {
                return false;
            }
            applied_states_.record(setter_index::mp_enabled, gates.mp_enabled);
        }
        return true;
    }

    auto RuntimeControllerAbilityService::Impl::refresh_shared_aqua_unlocks(UObject* aqua,
                                                                            std::wstring* failure)
        -> bool
    {
        if (!aqua || persistent_player_reward_context() != aqua)
        {
            return false;
        }
        auto* controller = try_read_reflected_object_property_value(aqua, L"Controller");
        if (!is_uobject_valid_fast(controller) ||
            try_read_reflected_object_property_value(controller, L"Pawn") != aqua)
        {
            return false;
        }
        // This own-character grant must not wait for unrelated spell HUD or
        // shared command preparation to finish.
        if (!kairi_glide::refresh(aqua, failure)) { return false; }
        constexpr auto& abilities = s_refresh_abilities;
        std::array<RuntimeSoraAbilityEntryState, abilities.size()> states{};
        if (!try_read_runtime_sora_ability_entry_states(abilities, aqua, states, failure))
        {
            return false;
        }
        const auto& links = states.back();
        const auto equipped = [&](size_t index)
        {
            return states[index].owned && states[index].equipped;
        };
        const bool is_aqua = aqua->GetClassPrivate()->GetName() == L"p_ex002_Pawn_C";
        const bool style_owned = is_aqua ? aqua_style_unlock::owned() : equipped(1);
        const kh02rewards::SharedUnlockOwnership owned{equipped(0), style_owned, equipped(2),
                                                       links.owned && links.equipped, equipped(3)};
        const bool shared_style = is_aqua || !config_.style_change_mode ||
                                  config_.style_change_mode() == RuntimeStyleChangeMode::Shared;
        const auto plan = kh02rewards::shared_unlock_plan(owned, shared_style);
        const auto before = kh02rewards::read_shared_unlock_flags(aqua);
        uint32_t restrictions_before{};
        if (!before || !try_read_command_restrictions(aqua, restrictions_before))
        {
            if (failure)
            {
                *failure = L"kh02_shared_unlock_state_unavailable";
            }
            return false;
        }
        if (!kh02rewards::apply_shared_unlock_plan(*before, plan,
                                                   [&](size_t setter, bool enabled)
                                                   {
                                                       return call_setter(controller, setter,
                                                                          enabled, failure);
                                                   }))
        {
            return false;
        }
        // Their controllers already provide Shoot Flow targeting. Re-enabling
        // it here refills Focus. Only restore the shared Airstep permission;
        // alternates return before Sora's ordinary Athletic Flow refresh.
        if (character_swap::owns_swap_pawn(aqua) &&
            !call_setter(controller, setter_index::airstep, true, failure))
        {
            return false;
        }
        const auto after = kh02rewards::read_shared_unlock_flags(aqua);
        uint32_t restrictions_after{};
        if (!after || !kh02rewards::shared_unlocks_match(*after, plan) ||
            !try_read_command_restrictions(aqua, restrictions_after) ||
            restrictions_before != restrictions_after)
        {
            if (failure)
            {
                *failure = L"kh02_shared_unlock_readback_mismatch";
            }
            return false;
        }
        character_exploration::refresh_unlocks(aqua, (*after & kh02rewards::mp_enabled_mask) != 0);
        alternate_wall_run::refresh_unlocks(aqua, owned.wall_run);
        if (!character_magic::refresh_spells(aqua)) { return false; }
        // Other native command restrictions are preserved; spell tiers come
        // from Sora's bank without changing its ownership.
        if (*before != *after)
        {
            Output::send<LogLevel::Verbose>(STR("[KH3APProbeCpp] Aqua shared unlock refresh "
                                                "before=0x{:X} after=0x{:X} restrictions=0x{:X}\n"),
                                            *before, *after, restrictions_after);
        }
        return true;
    }
} // namespace kh3ap
