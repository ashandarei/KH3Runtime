#include "kh3ap/features/player/characters/character_swap.h"
#include "kh3ap/features/player/combat/aqua_style_unlock.h"
#include "kh3ap/features/player/controller_abilities/host.h"

#include "kh3ap/app/state.h"
#include "kh3ap/features/world/space/space_service.h"
#include "kh3ap/features/kh02/story/kh02_story.h"
#include "kh3ap/features/grants/vbonus/vbonus_grant_call_helpers.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <format>
#include <string>

namespace kh3ap
{
    using namespace RC;
    using namespace RC::Unreal;
    using namespace controller_ability_detail;
    auto RuntimeControllerAbilityService::Impl::apply_unlock_item(
        std::wstring_view runtime_token, UObject* sora_pawn,
        RuntimeControllerAbilityGrantResult* result, std::wstring* failure_reason) -> bool
    {
        if (result)
        {
            *result = RuntimeControllerAbilityGrantResult{};
        }
        if (failure_reason)
        {
            failure_reason->clear();
        }
        const std::wstring_view key = unlock_key(runtime_token);
        if (key.empty())
        {
            if (failure_reason)
            {
                *failure_reason =
                    std::format(STR("unsupported controller unlock token={}"), runtime_token);
            }
            return false;
        }

        const bool persistent_only =
            sora_pawn && ((RuntimeSpaceService::is_space_active() &&
                           RuntimeSpaceService::item_delivery_context() == sora_pawn) ||
                          persistent_player_reward_context() == sora_pawn);
        if (runtime_token == aqua_style_unlock::token)
        {
            // Permanent AP ownership is rebuilt from receipt history. It is
            // independent of Sora's ability records and equipped styles.
            request_refresh(RuntimeControllerAbilityEvent::Grant);
            return true;
        }
        UObject* const controller = persistent_only ? nullptr : resolve_controller();
        if (!controller && !persistent_only)
        {
            if (failure_reason)
            {
                *failure_reason = STR("runtime controller unavailable");
            }
            return false;
        }
        if (controller)
        {
            observe_controller(controller);
        }

        if (key == STR("airstep"))
        {
            // Airstep is already baseline movement; its legacy receipt
            // needs no ownership write. The next Sora refresh enables it.
            if (controller && !call_setter(controller, setter_index::airstep, true, failure_reason))
            {
                return false;
            }
            if (controller)
            {
                applied_states_.record(setter_index::airstep, true);
            }
            request_refresh(RuntimeControllerAbilityEvent::Grant);
            return true;
        }

        const ControllerAbilityDefinition* const definition = find_definition(key);
        if (!definition)
        {
            if (failure_reason)
            {
                *failure_reason =
                    std::format(STR("unsupported controller unlock token={}"), runtime_token);
            }
            return false;
        }

        bool newly_granted = false;
        RuntimeSoraAbilityEntryState state{};
        if (!try_grant_unique_equipped_runtime_sora_ability(definition->ability_value, sora_pawn,
                                                            &newly_granted, &state, failure_reason))
        {
            return false;
        }
        if (!controller && persistent_player_reward_context() == sora_pawn)
        {
            if (!refresh_shared_aqua_unlocks(sora_pawn, failure_reason))
            {
                request_refresh(RuntimeControllerAbilityEvent::Grant);
                return false;
            }
        }
        else if (!controller)
        {
            Output::send<LogLevel::Verbose>(STR("[KH3APProbeCpp] Controller unlock ownership "
                                                "stored; live effects deferred token={}\n"),
                                            runtime_token);
        }
        else if (key == STR("magic") || key == STR("links"))
        {
            if (!apply_magic_links(controller, sora_pawn, failure_reason))
            {
                return false;
            }
        }
        else if (!call_setter(controller, definition->setter_index, state.equipped, failure_reason))
        {
            return false;
        }
        else
        {
            applied_states_.record(definition->setter_index, state.equipped);
        }
        request_refresh(RuntimeControllerAbilityEvent::Grant);
        if (result)
        {
            *result = RuntimeControllerAbilityGrantResult{
                std::wstring{definition->enum_name},
                definition->ability_value,
                newly_granted,
                state.equipped,
            };
        }
        Output::send<LogLevel::Verbose>(STR("[KH3APProbeCpp] Controller ability AP item applied "
                                            "token={} ability={} newly_granted={} equipped={}\n"),
                                        runtime_token, definition->ability_value,
                                        newly_granted ? STR("true") : STR("false"),
                                        state.equipped ? STR("true") : STR("false"));
        return true;
    }

    auto RuntimeControllerAbilityService::Impl::try_read_unlock_owned(
        std::wstring_view key, bool* owned, std::wstring* failure_reason) const -> bool
    {
        if (owned)
        {
            *owned = false;
        }
        if (!owned)
        {
            if (failure_reason)
            {
                *failure_reason = STR("controller ability owned result missing");
            }
            return false;
        }

        if (key == L"spellweaver")
        {
            *owned = aqua_style_unlock::owned();
            return true;
        }
        const ControllerAbilityDefinition* const definition = find_definition(key);
        if (!definition)
        {
            if (failure_reason)
            {
                *failure_reason =
                    std::format(STR("controller ability has no inventory marker key={}"), key);
            }
            return false;
        }

        UObject* const pawn = config_.runtime_state.grant_state().pawn_state().object();
        RuntimeSoraAbilityEntryState state{};
        if (!try_read_runtime_sora_ability_entry_state(definition->ability_value, pawn, &state,
                                                       failure_reason))
        {
            return false;
        }
        *owned = state.owned;
        return true;
    }
} // namespace kh3ap
