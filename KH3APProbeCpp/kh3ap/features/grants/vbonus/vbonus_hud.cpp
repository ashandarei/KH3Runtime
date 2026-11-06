#include "kh3ap/features/player/characters/character_swap.h"
#include "kh3ap/features/grants/vbonus/vbonus_grant_internal.h"
#include "kh3ap/features/kh02/story/kh02_story.h"
#include "kh3ap/app/enums.h"
#include "kh3ap/features/player/objects/object_resolver.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/app/state.h"
#include "kh3ap/core/types.h"
#include <UnrealDef.hpp>
#include <Unreal/UObject.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#include <format>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace kh3ap
{
    using namespace RC;
    using namespace RC::Unreal;
    using namespace vbonus_detail;

    namespace vbonus_detail
    {
        constexpr uint64_t hud_presenter_pointer_offset = 0x88;
        constexpr int hud_entry_value_shift = std::numeric_limits<uint32_t>::digits;

        auto try_resolve_native_vbonus_hud_presenter(RuntimeState& runtime_state,
                                                     NativeVBonusHudPresenter* presenter,
                                                     std::wstring* failure_reason,
                                                     UObject* display_hero) -> bool
        {
            if (!presenter)
            {
                if (failure_reason)
                {
                    *failure_reason = STR("native_vbonus_hud_presenter_result_missing");
                }
                return false;
            }
            *presenter = NativeVBonusHudPresenter{};

            UObject* const controller =
                display_hero ? try_read_reflected_object_property_value(display_hero, L"Controller")
                             : runtime_state.grant_state().pawn_state().controller_object();
            if (display_hero &&
                try_read_reflected_object_property_value(controller, L"Pawn") != display_hero)
            {
                if (failure_reason)
                {
                    *failure_reason = L"native_vbonus_hud_possession_changed";
                }
                return false;
            }
            if (!controller || !kh3ap::is_uobject_valid_fast(controller))
            {
                if (failure_reason)
                {
                    *failure_reason = STR("native_vbonus_hud_controller_unavailable");
                }
                return false;
            }

            const auto& function_cache = native_vbonus_hud_function_cache();
            if (!function_cache.ready)
            {
                if (failure_reason)
                {
                    *failure_reason = function_cache.failure_reason;
                }
                return false;
            }

            UObject* const hud_owner = function_cache.resolve_hud_owner(controller);
            if (!hud_owner || !kh3ap::is_uobject_valid_fast(hud_owner))
            {
                if (failure_reason)
                {
                    *failure_reason = STR("native_vbonus_hud_owner_unavailable");
                }
                return false;
            }
            const auto hud_presenter = try_read_process_value<uint64_t>(
                reinterpret_cast<uint64_t>(hud_owner) + hud_presenter_pointer_offset);
            if (!hud_presenter.has_value() || !looks_like_process_pointer(*hud_presenter))
            {
                if (failure_reason)
                {
                    *failure_reason = STR("native_vbonus_hud_presenter_unavailable");
                }
                return false;
            }

            *presenter = NativeVBonusHudPresenter{
                controller,
                hud_owner,
                *hud_presenter,
                function_cache.present,
            };
            return true;
        }

        auto try_resolve_native_vbonus_display_helpers(uint64_t module_base, bool need_stat_amounts,
                                                       bool need_magic,
                                                       NativeVBonusDisplayHelpers* helpers,
                                                       std::wstring* failure_reason) -> bool
        {
            if (!helpers || module_base == 0)
            {
                if (failure_reason)
                {
                    *failure_reason = STR("native_vbonus_display_helper_result_missing");
                }
                return false;
            }
            *helpers = NativeVBonusDisplayHelpers{};

            if (need_stat_amounts)
            {
                const auto& function_cache = native_vbonus_stat_function_cache();
                if (!function_cache.ready)
                {
                    if (failure_reason)
                    {
                        *failure_reason = function_cache.failure_reason;
                    }
                    return false;
                }
                helpers->resolve_stat_amounts = function_cache.resolve_stat_amounts;
            }

            if (need_magic)
            {
                const auto& function_cache = native_vbonus_magic_function_cache();
                if (!function_cache.ready)
                {
                    if (failure_reason)
                    {
                        *failure_reason = function_cache.failure_reason;
                    }
                    return false;
                }
                helpers->resolve_magic_command = function_cache.resolve_magic_command;
                helpers->resolve_magic_shortcut_manager =
                    function_cache.resolve_magic_shortcut_manager;
                helpers->refresh_magic_shortcuts = function_cache.refresh_magic_shortcuts;
            }
            return true;
        }

        auto pack_native_vbonus_hud_entry(uint32_t type, uint32_t value) -> uint64_t
        {
            return static_cast<uint64_t>(type) |
                   (static_cast<uint64_t>(value) << hud_entry_value_shift);
        }

        auto present_native_vbonus_hud_entries(const NativeVBonusHudPresenter& presenter,
                                               UObject* sora_pawn,
                                               const std::vector<uint64_t>& display_entries,
                                               std::wstring_view route_name) -> void
        {
            if (display_entries.empty())
            {
                return;
            }
            const ArrayHeader display_header{
                reinterpret_cast<uint64_t>(display_entries.data()),
                static_cast<int32_t>(display_entries.size()),
                static_cast<int32_t>(display_entries.size()),
            };
            presenter.present(presenter.hud_owner, sora_pawn, &display_header);

            Output::send<LogLevel::Verbose>(
                STR("[KH3APProbeCpp] Native VBonus HUD presenter dispatched route={} entries={} "
                    "controller=0x{:X} hud_owner=0x{:X} presenter=0x{:X}\n"),
                route_name, display_entries.size(),
                reinterpret_cast<uint64_t>(presenter.controller),
                reinterpret_cast<uint64_t>(presenter.hud_owner), presenter.hud_presenter);
        }

        auto present_native_vbonus_hud_abilities(const NativeVBonusHudPresenter& presenter,
                                                 UObject* sora_pawn,
                                                 const std::vector<uint64_t>& ability_values,
                                                 std::wstring_view route_name) -> void
        {
            std::vector<uint64_t> display_entries{};
            display_entries.reserve(ability_values.size());
            for (const uint64_t ability_value : ability_values)
            {
                // BP_TresGetVictoryBonus emits one packed 64-bit display
                // record per line. Type 7 denotes an ability and the high
                // dword is the ETresAbilityKind value.
                display_entries.push_back(pack_native_vbonus_hud_entry(
                    s_vbonus_hud_ability_type, static_cast<uint32_t>(ability_value)));
            }
            present_native_vbonus_hud_entries(presenter, sora_pawn, display_entries, route_name);
        }
    } // namespace vbonus_detail

    auto
    try_present_native_vbonus_hud_ability_tokens(const std::vector<std::wstring>& ability_tokens,
                                                 UObject* sora_pawn, RuntimeState& runtime_state,
                                                 std::wstring* failure_reason) -> bool
    {
        if (ability_tokens.empty() || ability_tokens.size() > s_native_vbonus_hud_max_ability_count)
        {
            if (failure_reason)
            {
                *failure_reason =
                    std::format(STR("native_vbonus_hud_requires_one_to_{}_abilities_got_{}"),
                                s_native_vbonus_hud_max_ability_count, ability_tokens.size());
            }
            return false;
        }
        const bool ready_aqua = sora_pawn && persistent_player_reward_context() == sora_pawn;
        if (!sora_pawn || !kh3ap::is_uobject_valid_fast(sora_pawn) ||
            (!ready_aqua && !RuntimeObjectResolver::is_sora_ability_pawn_candidate(sora_pawn)))
        {
            if (failure_reason)
            {
                *failure_reason = STR("native_vbonus_hud_requires_ready_player_context");
            }
            return false;
        }

        NativeVBonusHudPresenter presenter{};
        if (!try_resolve_native_vbonus_hud_presenter(runtime_state, &presenter, failure_reason,
                                                     ready_aqua ? sora_pawn : nullptr))
        {
            return false;
        }

        std::vector<uint64_t> ability_values{};
        ability_values.reserve(ability_tokens.size());
        for (const std::wstring& ability_token : ability_tokens)
        {
            if (!ability_token.starts_with(STR("ETresAbilityKind::")) ||
                ability_token.ends_with(STR("::NONE")))
            {
                if (failure_reason)
                {
                    *failure_reason = std::format(
                        STR("native_vbonus_hud_requires_ability_token:{}"), ability_token);
                }
                return false;
            }
            const auto ability_value = try_resolve_runtime_enum_value(ability_token);
            if (!ability_value.has_value() || *ability_value == 0 || *ability_value > UINT8_MAX)
            {
                if (failure_reason)
                {
                    *failure_reason = std::format(
                        STR("native_vbonus_hud_ability_enum_unresolved:{}"), ability_token);
                }
                return false;
            }
            ability_values.push_back(*ability_value);
        }

        present_native_vbonus_hud_abilities(presenter, sora_pawn, ability_values,
                                            STR("normal_ap_batch"));
        return true;
    }
} // namespace kh3ap
