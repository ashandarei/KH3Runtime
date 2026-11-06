#include "kh3ap/features/party/party_member_service.h"
#include "kh3ap/features/party/party_helpers.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <UnrealDef.hpp>
#include <algorithm>
#include <utility>
namespace kh3ap
{
    using namespace RC::Unreal;
    using party_catalog::party_choices;
    using party_catalog::vanilla_choice;
    using party_native::persistent_level_for;
    using party_native::read_friend_slots;
    using party_native::read_object_like_property;
    auto RuntimePartyMemberService::test_begin(uint64_t seed, bool all_choices) -> bool
    {
        if (!active_ || test_active() || reconcile_pending_)
        {
            return false;
        }
        saved_random_ = random_;
        saved_random_choices_ = random_choices_;
        saved_random_choices_valid_ = random_choices_valid_;
        saved_force_random_pair_ = force_random_pair_;
        test_settings_ = config_.goa_settings.status();
        test_all_choices_ = all_choices;
        random_.seed(seed);
        random_choices_valid_ = false;
        test_requests_ = 0;
        test_reconciles_ = 0;
        test_failures_ = 0;
        test_deletes_ = 0;
        test_spawns_ = 0;
        test_last_reconcile_ms_ = 0;
        test_native_work_ms_ = 0;
        return true;
    }

    auto RuntimePartyMemberService::test_configure(bool randomized, bool every_room,
                                                   std::size_t first, std::size_t second) -> bool
    {
        if (!test_active() || !choice_unlocked(first) || !choice_unlocked(second) ||
            (first && second && first != vanilla_choice && second != vanilla_choice &&
             party_choices[first].unique_character == party_choices[second].unique_character))
        {
            return false;
        }
        const bool enable_random = randomized && !test_settings_->party_randomization_enabled;
        test_settings_->party_randomization_enabled = randomized;
        test_settings_->party_reroll_every_room = every_room;
        test_settings_->party_friend_1_choice = static_cast<int32_t>(first);
        test_settings_->party_friend_2_choice = static_cast<int32_t>(second);
        if (enable_random)
        {
            random_choices_valid_ = false;
        }
        request_reconcile(STR("bridge_test_configure"), enable_random);
        return true;
    }

    auto RuntimePartyMemberService::test_reroll(bool room_event) -> bool
    {
        if (!test_active())
        {
            return false;
        }
        if (room_event)
        {
            // Exercise exactly the post-SetFriendAll event's reroll policy,
            // without dispatching a map jump or the vanilla spawn operation.
            UObject* const manager = resolve_friend_manager();
            if (!manager)
            {
                return false;
            }
            observe_set_friend_all(manager);
        }
        else
        {
            if (!test_settings_->party_randomization_enabled)
            {
                return false;
            }
            request_reconcile(STR("bridge_test_reroll"), true);
        }
        return true;
    }

    auto RuntimePartyMemberService::test_end() -> void
    {
        if (!test_active())
        {
            return;
        }
        test_settings_.reset();
        test_all_choices_ = false;
        random_ = saved_random_;
        random_choices_ = saved_random_choices_;
        random_choices_valid_ = saved_random_choices_valid_;
        force_random_pair_ = saved_force_random_pair_;
        request_reconcile(STR("bridge_test_restore"), false);
    }

    auto RuntimePartyMemberService::test_snapshot() -> RuntimePartyTestSnapshot
    {
        const auto current = effective_settings();
        RuntimePartyTestSnapshot snapshot{};
        snapshot.available = active_;
        snapshot.test_active = test_active();
        snapshot.pending = reconcile_pending_;
        snapshot.randomized = current.party_randomization_enabled;
        snapshot.every_room = current.party_reroll_every_room;
        snapshot.selected = {static_cast<std::size_t>(std::max(0, current.party_friend_1_choice)),
                             static_cast<std::size_t>(std::max(0, current.party_friend_2_choice))};
        snapshot.active = active_choices_;
        snapshot.vanilla_npc_ids = vanilla_npc_ids_;
        snapshot.active_npc_ids = active_npc_ids_;
        snapshot.requests = test_requests_;
        snapshot.reconciles = test_reconciles_;
        snapshot.failures = test_failures_;
        snapshot.deletes = test_deletes_;
        snapshot.spawns = test_spawns_;
        snapshot.last_reconcile_ms = test_last_reconcile_ms_;
        snapshot.last_native_work_ms = test_native_work_ms_;
        UObject* const manager = resolve_friend_manager();
        if (!manager)
        {
            return snapshot;
        }
        snapshot.manager = manager->GetFullName();
        UObject* const level = persistent_level_for(manager);
        if (!level)
        {
            return snapshot;
        }
        snapshot.level = level->GetFullName();
        const auto policy = effective_map_policy(level);
        snapshot.map_policy =
            policy == RuntimePartyMapPolicy::managed
                ? L"managed"
                : (policy == RuntimePartyMapPolicy::solo                 ? L"solo"
                   : policy == RuntimePartyMapPolicy::vanilla_first_only ? L"vanilla_first_only"
                                                                         : L"vanilla");
        std::array<UObject*, runtime_party_member_slot_count> slots{};
        snapshot.slots_readable = read_friend_slots(manager, slots);
        snapshot.slots_match = snapshot.slots_readable && !reconcile_pending_;
        for (size_t slot = 0; slot < slots.size(); ++slot)
        {
            UObject* const pawn = slots[slot];
            if (pawn)
            {
                snapshot.pawns[slot] = pawn->GetFullName();
                UClass* const klass = pawn->GetClassPrivate();
                if (klass && is_uobject_valid_fast(klass))
                {
                    snapshot.classes[slot] = klass->GetName();
                }
                UObject* const controller = read_object_like_property(pawn, STR("Controller"));
                UObject* const following =
                    read_object_like_property(controller, STR("PathFollowingComponent"));
                UObject* const navigation = read_object_like_property(following, STR("MyNavData"));
                if (navigation)
                {
                    snapshot.navigation[slot] = navigation->GetName();
                }
            }
            const size_t choice = active_choices_[slot];
            const bool matches =
                choice == 0 ? !pawn
                            : pawn && choice < party_choices.size() &&
                                  persistent_level_for(pawn) == level &&
                                  snapshot.classes[slot] == active_npc_ids_[slot] + L"_Pawn_C";
            snapshot.slots_match = snapshot.slots_match && matches;
        }
        if (slots[0] && slots[0] == slots[1])
        {
            snapshot.slots_match = false;
        }
        return snapshot;
    }

    auto RuntimePartyMemberService::active_party_pawns()
        -> std::array<UObject*, runtime_party_member_slot_count>
    {
        UObject* const manager = resolve_friend_manager();
        std::array<UObject*, runtime_party_member_slot_count> pawns{};
        std::array<UObject*, runtime_party_member_slot_count> slots{};
        if (!read_friend_slots(manager, slots))
        {
            return pawns;
        }
        pawns[0] = slots[0];
        pawns[1] = slots[1];
        return pawns;
    }

    auto RuntimePartyMemberService::choose_random_pair(std::wstring_view level_name)
        -> std::array<std::size_t, runtime_party_member_slot_count>
    {
        std::array<std::size_t, party_choices.size() - 2> first_candidates{};
        std::size_t first_count = 0;
        for (std::size_t choice = 1; choice < party_choices.size(); ++choice)
        {
            if (choice != vanilla_choice && choice_unlocked(choice) &&
                !runtime_party_is_character_disabled(level_name,
                                                     party_choices[choice].unique_character) &&
                !runtime_party_is_native_world_companion(level_name,
                                                         party_choices[choice].unique_character))
            {
                first_candidates[first_count++] = choice;
            }
        }
        if (first_count == 0)
        {
            return {};
        }

        std::uniform_int_distribution<std::size_t> first_pick{0, first_count - 1};
        const std::size_t first = first_candidates[first_pick(random_)];
        std::array<std::size_t, party_choices.size() - 2> second_candidates{};
        std::size_t second_count = 0;
        for (std::size_t index = 0; index < first_count; ++index)
        {
            const std::size_t choice = first_candidates[index];
            if (party_choices[choice].unique_character != party_choices[first].unique_character)
            {
                second_candidates[second_count++] = choice;
            }
        }
        if (second_count == 0)
        {
            return {first, 0};
        }
        std::uniform_int_distribution<std::size_t> second_pick{0, second_count - 1};
        return {first, second_candidates[second_pick(random_)]};
    }

    auto RuntimePartyMemberService::desired_choices(UObject* level)
        -> std::array<std::size_t, runtime_party_member_slot_count>
    {
        const std::wstring level_name = level->GetFullName();
        if (effective_map_policy(level) == RuntimePartyMapPolicy::solo)
        {
            return {};
        }
        const RuntimeGoaSettingsStatus settings = effective_settings();
        if (settings.party_randomization_enabled)
        {
            // A retained roll can become invalid when entering its member's
            // home world, even with reroll-on-room-change disabled.
            const bool native_conflict =
                std::any_of(random_choices_.begin(), random_choices_.end(),
                            [level_name](std::size_t choice)
                            {
                                return runtime_party_is_native_world_companion(
                                           level_name, party_choices[choice].unique_character) ||
                                       runtime_party_is_character_disabled(
                                           level_name, party_choices[choice].unique_character);
                            });
            if (!random_choices_valid_ || force_random_pair_ || native_conflict)
            {
                random_choices_ = choose_random_pair(level_name);
                random_choices_valid_ = true;
            }
            force_random_pair_ = false;
            return random_choices_;
        }

        const auto normalize_manual = [this, level_name](int32_t slot, int32_t choice)
        {
            return choice >= 0 && static_cast<std::size_t>(choice) < party_choices.size() &&
                           !runtime_party_is_character_disabled(
                               level_name, party_choices[choice].unique_character) &&
                           choice_available(slot, static_cast<std::size_t>(choice))
                       ? static_cast<std::size_t>(choice)
                       : std::size_t{0};
        };
        return {
            normalize_manual(1, settings.party_friend_1_choice),
            normalize_manual(2, settings.party_friend_2_choice),
        };
    }

    auto RuntimePartyMemberService::capture_vanilla_party(UObject* friend_manager) -> bool
    {
        std::array<UObject*, runtime_party_member_slot_count> slots{};
        if (!read_friend_slots(friend_manager, slots))
        {
            return false;
        }
        std::array<std::wstring, runtime_party_member_slot_count> names{};
        for (size_t slot = 0; slot < slots.size(); ++slot)
        {
            if (!slots[slot])
            {
                continue;
            }
            auto* klass = slots[slot]->GetClassPrivate();
            if (!klass || !is_uobject_valid_fast(klass))
            {
                return false;
            }
            const std::wstring name = klass->GetName();
            constexpr std::wstring_view suffix = L"_Pawn_C";
            if (!name.ends_with(suffix))
            {
                return false;
            }
            names[slot] = name.substr(0, name.size() - suffix.size());
        }
        // Copy identities, never retain a native pawn across frames. Only real
        // SetFriendAll callbacks refresh this after our first replacement.
        vanilla_friend_manager_ = friend_manager;
        vanilla_npc_ids_ = std::move(names);
        return true;
    }

    auto RuntimePartyMemberService::unlock_choice(std::size_t choice) -> bool
    {
        if (choice == 0 || choice == vanilla_choice || choice >= party_choices.size())
        {
            return false;
        }
        return !unlocked_[choice - 1].exchange(true);
    }
} // namespace kh3ap
