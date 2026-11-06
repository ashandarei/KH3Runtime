#include "kh3ap/features/party/party_member_service.h"
#include "kh3ap/features/party/party_helpers.h"
#include "kh3ap/features/grants/grant_pawn_state.h"
#include "kh3ap/app/state.h"
#include <Unreal/UObject.hpp>
#include <UnrealDef.hpp>
#include <algorithm>
#include <limits>

namespace kh3ap
{
    using party_catalog::party_choices;
    using party_catalog::vanilla_choice;
    using party_native::persistent_level_for;
    using RC::Unreal::UObject;

    auto RuntimePartyMemberService::effective_settings() const -> RuntimeGoaSettingsStatus
    {
        return test_settings_ ? *test_settings_ : config_.goa_settings.status();
    }

    auto RuntimePartyMemberService::choice_definition(std::size_t choice) const
        -> RuntimePartyMemberDefinition
    {
        return choice < party_choices.size() ? party_choices[choice]
                                             : RuntimePartyMemberDefinition{};
    }

    auto RuntimePartyMemberService::choice_unlocked(std::size_t choice) const -> bool
    {
        return choice < party_choices.size() &&
               (choice == 0 || choice == vanilla_choice || (test_active() && test_all_choices_) ||
                unlocked_[choice - 1].load());
    }

    auto RuntimePartyMemberService::status() const -> RuntimePartyMemberStatus
    {
        std::size_t unlocked_count = 0;
        for (const auto& unlocked : unlocked_)
        {
            unlocked_count += unlocked.load() ? 1U : 0U;
        }
        const RuntimeGoaSettingsStatus settings = effective_settings();
        return RuntimePartyMemberStatus{
            .available = active_,
            .randomization_enabled = settings.party_randomization_enabled,
            .reroll_every_room = settings.party_reroll_every_room,
            .unlocked_count = unlocked_count,
            .friend_1_choice =
                static_cast<std::size_t>(std::max(settings.party_friend_1_choice, 0)),
            .friend_2_choice =
                static_cast<std::size_t>(std::max(settings.party_friend_2_choice, 0)),
            .active_friend_1_choice = active_choices_[0],
            .active_friend_2_choice = active_choices_[1],
            .hook_installed = hook_installed_,
        };
    }

    auto RuntimePartyMemberService::choice_count() const -> std::size_t
    {
        return party_choices.size();
    }

    auto RuntimePartyMemberService::choice_label(std::size_t choice) const -> std::wstring_view
    {
        return choice < party_choices.size() ? party_choices[choice].display_name
                                             : std::wstring_view{};
    }

    auto RuntimePartyMemberService::choice_available(int32_t friend_slot, std::size_t choice) const
        -> bool
    {
        if ((friend_slot != 1 && friend_slot != 2) || choice >= party_choices.size() || !active_)
        {
            return false;
        }
        if (choice == 0 || choice == vanilla_choice)
        {
            return true;
        }
        if (!choice_unlocked(choice))
        {
            return false;
        }

        auto* level = persistent_level_for(resolve_party_player());
        if (level && runtime_party_is_character_disabled(level->GetFullName(),
                                                         party_choices[choice].unique_character))
        {
            return false;
        }

        const RuntimeGoaSettingsStatus settings = effective_settings();
        const int32_t other_choice =
            friend_slot == 1 ? settings.party_friend_2_choice : settings.party_friend_1_choice;
        if (other_choice <= 0 || static_cast<std::size_t>(other_choice) >= party_choices.size())
        {
            return true;
        }
        const std::size_t other = static_cast<std::size_t>(other_choice);
        return !choice_unlocked(other) ||
               party_choices[other].unique_character != party_choices[choice].unique_character;
    }

    auto RuntimePartyMemberService::set_randomization_enabled(bool enabled) -> bool
    {
        if (!active_)
        {
            return false;
        }
        bool persisted = true;
        if (test_active())
        {
            test_settings_->party_randomization_enabled = enabled;
        }
        else
        {
            persisted = config_.goa_settings.set_party_randomization_enabled(enabled);
        }
        if (enabled)
        {
            random_choices_valid_ = false;
        }
        request_reconcile(STR("goa_menu_randomization"), enabled);
        return persisted;
    }

    auto RuntimePartyMemberService::set_reroll_every_room(bool enabled) -> bool
    {
        if (!active_)
        {
            return false;
        }
        if (test_active())
        {
            test_settings_->party_reroll_every_room = enabled;
            return true;
        }
        return config_.goa_settings.set_party_reroll_every_room(enabled);
    }

    auto RuntimePartyMemberService::set_friend_choice(int32_t friend_slot, std::size_t choice)
        -> bool
    {
        if (!active_ || !choice_available(friend_slot, choice) ||
            choice > static_cast<std::size_t>(std::numeric_limits<int32_t>::max()))
        {
            return false;
        }
        bool persisted = true;
        if (test_active())
        {
            (friend_slot == 1 ? test_settings_->party_friend_1_choice
                              : test_settings_->party_friend_2_choice) =
                static_cast<int32_t>(choice);
        }
        else
        {
            persisted = config_.goa_settings.set_party_friend_choice(friend_slot,
                                                                     static_cast<int32_t>(choice));
        }
        request_reconcile(STR("goa_menu_friend_choice"), false);
        return persisted;
    }
} // namespace kh3ap
