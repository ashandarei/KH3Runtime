#include "kh3ap/features/party/party_member_service.h"
#include "kh3ap/features/party/party_helpers.h"
#include "kh3ap/features/party/party_runtime_constants.h"
#include "kh3ap/features/party/party_world_context.h"
#include "kh3ap/app/state.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/features/grants/grant_pawn_state.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/FScriptArray.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UObject.hpp>
#include <UnrealDef.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

namespace kh3ap
{
    using namespace RC;
    using namespace RC::Unreal;
    using namespace party_runtime;
    using party_catalog::action_viable_state_definitions;
    using party_catalog::ActionViableStateDefinition;
    using party_catalog::custom_party_ai_definitions;
    using party_catalog::CustomPartyAiDefinition;
    using party_catalog::lea_choice;
    using party_native::actor_position;
    using party_native::configure_party_navigation;
    using party_native::invoke_bool_getter;
    using party_native::invoke_named_bool_getter;
    using party_native::invoke_named_single_bool_function;
    using party_native::invoke_set_disable_battle_mode_ai;
    using party_native::outside_native_follow_distance;
    using party_native::persistent_level_for;
    using party_native::read_friend_slots;
    using party_native::read_object_like_property;
    using party_native::run_behavior_tree;
    using party_native::write_object_like_property;
    using party_world::is_party_presentation_story_level;
    using party_world::observed_party_map_policy;

    namespace
    {
        constexpr int maximum_viable_state_element_size = 8;
        constexpr int maximum_viable_state_count = 256;
    } // namespace

    auto RuntimePartyMemberService::observe_can_battle(UObject* pawn, bool can_battle) -> void
    {
        if (party_presentation_event_active_ || !pawn || !is_uobject_valid_fast(pawn))
        {
            return;
        }

        size_t definition_index = custom_party_ai_definitions.size();
        for (size_t index = 0; index < custom_party_assigned_pawns_.size(); ++index)
        {
            if (custom_party_assigned_pawns_[index].Get() == pawn)
            {
                definition_index = index;
                break;
            }
        }
        if (definition_index >= custom_party_ai_definitions.size())
        {
            return;
        }

        const CustomPartyAiDefinition& definition = custom_party_ai_definitions[definition_index];
        for (size_t slot = 0; slot < active_choices_.size(); ++slot)
        {
            const size_t effective_choice =
                reconcile_pending_ ? pending_choices_[slot] : active_choices_[slot];
            if (effective_choice != definition.choice ||
                (party_presentation_modes_[slot].has_value() &&
                 *party_presentation_modes_[slot] == can_battle))
            {
                continue;
            }

            party_presentation_event_active_ = true;
            const auto battle_mode_before =
                invoke_named_bool_getter(pawn, is_battle_mode_function_name);
            std::wstring_view invoked_function = set_battle_mode_function_name;
            bool invoked =
                invoke_named_single_bool_function(pawn, set_battle_mode_function_name, can_battle);
            if (!invoked)
            {
                invoked_function = on_change_battle_mode_function_name;
                invoked = invoke_named_single_bool_function(
                    pawn, on_change_battle_mode_function_name, can_battle);
            }
            party_presentation_event_active_ = false;
            if (!invoked)
            {
                continue;
            }

            party_presentation_modes_[slot] = can_battle;
            const auto battle_mode_after =
                invoke_named_bool_getter(pawn, is_battle_mode_function_name);
            Output::send<LogLevel::Normal>(
                STR("[KH3APProbeCpp] CanBattle event synced party "
                    "presentation slot={} member={} desired={} "
                    "before={} after={} function={} pawn={}\n"),
                slot + 1, definition.display_name, can_battle,
                battle_mode_before.has_value() ? (*battle_mode_before ? STR("true") : STR("false"))
                                               : STR("unknown"),
                battle_mode_after.has_value() ? (*battle_mode_after ? STR("true") : STR("false"))
                                              : STR("unknown"),
                invoked_function, pawn->GetFullName());
        }
    }

    auto RuntimePartyMemberService::restore_action_viable_state_patches() -> void
    {
        for (ActionViableStatePatch& patch : action_viable_state_patches_)
        {
            if (!patch.applied)
            {
                continue;
            }
            UObject* const weak_default = patch.default_object.Get();
            UObject* const default_object =
                weak_default ? weak_default : patch.rooted_default_object;
            FProperty* const property =
                default_object && is_uobject_valid_fast(default_object)
                    ? default_object->GetPropertyByNameInChain(STR("m_ViableStates"))
                    : nullptr;
            void* const value =
                default_object && property
                    ? default_object->GetValuePtrByPropertyNameInChain(STR("m_ViableStates"))
                    : nullptr;
            if (!property || !value || !patch.snapshot_valid ||
                property->GetClass().GetName() != STR("ArrayProperty") || patch.element_size <= 0 ||
                patch.element_alignment <= 0)
            {
                patch = {};
                continue;
            }

            auto* const states = static_cast<FScriptArray*>(value);
            const int original_count = static_cast<int>(patch.original_values.size() /
                                                        static_cast<size_t>(patch.element_size));
            states->Empty(original_count, patch.element_size,
                          static_cast<uint32_t>(patch.element_alignment));
            if (original_count > 0)
            {
                const int first = states->Add(original_count, patch.element_size,
                                              static_cast<uint32_t>(patch.element_alignment));
                std::memcpy(static_cast<std::byte*>(states->GetData()) +
                                static_cast<size_t>(first) *
                                    static_cast<size_t>(patch.element_size),
                            patch.original_values.data(), patch.original_values.size());
            }
            Output::send<LogLevel::Normal>(
                STR("[KH3APProbeCpp] Restored party action viable states "
                    "class={} count={}\n"),
                patch.class_path, original_count);
            patch = {};
        }
    }

    auto RuntimePartyMemberService::reconcile_action_viable_state_patches(UObject* sora_level)
        -> void
    {
        static_assert(std::tuple_size_v<decltype(action_viable_state_patches_)> ==
                      action_viable_state_definitions.size());
        static_assert(std::tuple_size_v<decltype(custom_party_trees_)> ==
                      custom_party_ai_definitions.size());
        static_assert(std::tuple_size_v<decltype(custom_party_assigned_pawns_)> ==
                      custom_party_ai_definitions.size());
        static_assert(std::tuple_size_v<decltype(custom_party_tree_resolution_attempted_)> ==
                      custom_party_ai_definitions.size());
        const bool story_level = is_party_presentation_story_level(sora_level);
        for (size_t index = 0; index < action_viable_state_patches_.size(); ++index)
        {
            const ActionViableStateDefinition& definition = action_viable_state_definitions[index];
            ActionViableStatePatch& patch = action_viable_state_patches_[index];
            const bool should_apply =
                !story_level &&
                (active_choices_[0] == definition.choice ||
                 active_choices_[1] == definition.choice ||
                 (reconcile_pending_ && (pending_choices_[0] == definition.choice ||
                                         pending_choices_[1] == definition.choice)));

            if (!patch.class_path.empty() && patch.class_path != definition.class_path)
            {
                restore_action_viable_state_patches();
            }
            if (patch.class_path.empty())
            {
                patch.class_path = definition.class_path;
            }
            if (!should_apply)
            {
                if (!patch.applied)
                {
                    continue;
                }
                UObject* const weak_default = patch.default_object.Get();
                UObject* const default_object =
                    weak_default ? weak_default : patch.rooted_default_object;
                FProperty* const property =
                    default_object && is_uobject_valid_fast(default_object)
                        ? default_object->GetPropertyByNameInChain(STR("m_ViableStates"))
                        : nullptr;
                void* const value =
                    default_object && property
                        ? default_object->GetValuePtrByPropertyNameInChain(STR("m_ViableStates"))
                        : nullptr;
                if (property && value && patch.snapshot_valid &&
                    property->GetClass().GetName() == STR("ArrayProperty"))
                {
                    auto* const states = static_cast<FScriptArray*>(value);
                    const int original_count = static_cast<int>(
                        patch.original_values.size() / static_cast<size_t>(patch.element_size));
                    states->Empty(original_count, patch.element_size,
                                  static_cast<uint32_t>(patch.element_alignment));
                    if (original_count > 0)
                    {
                        const int first =
                            states->Add(original_count, patch.element_size,
                                        static_cast<uint32_t>(patch.element_alignment));
                        std::memcpy(static_cast<std::byte*>(states->GetData()) +
                                        static_cast<size_t>(first) *
                                            static_cast<size_t>(patch.element_size),
                                    patch.original_values.data(), patch.original_values.size());
                    }
                }
                Output::send<LogLevel::Normal>(STR("[KH3APProbeCpp] Restored party action viable "
                                                   "states class={}\n"),
                                               patch.class_path);
                patch = {};
                patch.class_path = definition.class_path;
                continue;
            }
            if (patch.applied)
            {
                continue;
            }

            // The selected GoA tree hard-references these generated classes.
            // Never synchronously load them from this maintenance pass: a
            // failed soft-class resolution can cost hundreds of milliseconds
            // and used to repeat on every slow frame.
            UClass* const action_class = find_loaded_runtime_asset_class(definition.class_path);
            // ClassDefaultObject is native UClass storage, not a reflected
            // UPROPERTY. Property-name lookup silently skips every attack.
            UObject* const default_object = is_uobject_valid_fast(action_class)
                                                ? action_class->GetClassDefaultObject()
                                                : nullptr;
            FProperty* const property =
                default_object && is_uobject_valid_fast(default_object)
                    ? default_object->GetPropertyByNameInChain(STR("m_ViableStates"))
                    : nullptr;
            void* const value =
                default_object && property
                    ? default_object->GetValuePtrByPropertyNameInChain(STR("m_ViableStates"))
                    : nullptr;
            if (!action_class || !default_object || !property || !value ||
                property->GetClass().GetName() != STR("ArrayProperty"))
            {
                continue;
            }

            auto* const array_property = static_cast<FArrayProperty*>(property);
            FProperty* const inner = const_cast<FProperty*>(array_property->GetInner());
            const int element_size = inner ? inner->GetElementSize() : 0;
            const int element_alignment = inner ? inner->GetMinAlignment() : 0;
            auto* const states = static_cast<FScriptArray*>(value);
            const int state_count = states->Num();
            if (!inner || element_size <= 0 || element_size > maximum_viable_state_element_size ||
                element_alignment <= 0 || state_count < 0 ||
                state_count > maximum_viable_state_count || (state_count > 0 && !states->GetData()))
            {
                continue;
            }

            patch.default_object = default_object;
            if (!patch.default_object.Get())
            {
                // FWeakObjectPtr::operator= copies an existing serial; it does
                // not allocate one. Without this root, restore silently drops
                // the original array while the patched CDO remains live.
                // Only the bounded catalog CDOs are retained; never unroot them
                // during DLL teardown or store an unprotected cross-frame pointer.
                default_object->SetRootSet();
                patch.rooted_default_object = default_object;
            }
            patch.element_size = element_size;
            patch.element_alignment = element_alignment;
            patch.original_values.resize(static_cast<size_t>(state_count) *
                                         static_cast<size_t>(element_size));
            if (!patch.original_values.empty())
            {
                std::memcpy(patch.original_values.data(), states->GetData(),
                            patch.original_values.size());
            }
            patch.snapshot_valid = true;

            auto contains_state = [&](uint8_t state_id)
            {
                const auto* data = static_cast<const std::byte*>(states->GetData());
                for (int state_index = 0; state_index < states->Num(); ++state_index)
                {
                    uint8_t current = 0;
                    std::memcpy(&current,
                                data + static_cast<size_t>(state_index) *
                                           static_cast<size_t>(element_size),
                                sizeof(current));
                    if (current == state_id)
                    {
                        return true;
                    }
                }
                return false;
            };
            for (const uint8_t required_state : {state_id_idle, state_id_turn})
            {
                if (contains_state(required_state))
                {
                    continue;
                }
                const int added =
                    states->AddZeroed(1, element_size, static_cast<uint32_t>(element_alignment));
                std::memcpy(static_cast<std::byte*>(states->GetData()) +
                                static_cast<size_t>(added) * static_cast<size_t>(element_size),
                            &required_state, sizeof(required_state));
            }
            patch.applied = true;
            Output::send<LogLevel::Normal>(STR("[KH3APProbeCpp] Patched party action viable states "
                                               "class={} idle=true turn=true original_count={} "
                                               "patched_count={}\n"),
                                           definition.class_path, state_count, states->Num());
        }
    }

    auto RuntimePartyMemberService::initialize_party_knockout() -> void
    {
        UObject* const manager = resolve_friend_manager();
        UObject* const level = persistent_level_for(manager);
        std::array<UObject*, runtime_party_member_slot_count> slots{};
        if (!level ||
            observed_party_map_policy(level, config_.runtime_state) !=
                RuntimePartyMapPolicy::managed ||
            !read_friend_slots(manager, slots))
        {
            return;
        }
        for (size_t slot = 0; slot < slots.size(); ++slot)
        {
            UObject* const pawn = slots[slot];
            // Lea's stock pawn starts with a story-only 1 HP floor.
            // Clear it only on an explicitly selected, authoritative friend;
            // never change the CDO, story Lea, or a Vanilla selection. Native
            // TresNpcState_Die owns knockout and recovery, independently of BT.
            if (active_choices_[slot] != lea_choice || !is_uobject_valid_fast(pawn) ||
                persistent_level_for(pawn) != level ||
                pawn->GetClassPrivate()->GetName() != STR("n_ex010_Pawn_C"))
            {
                continue;
            }
            const auto floor = invoke_named_bool_getter(pawn, STR("IsConditionHpLimitOne"));
            if (floor == false)
            {
                continue;
            }
            if (!floor.has_value() ||
                !invoke_named_single_bool_function(pawn, STR("SetConditionHpLimitOne"), false) ||
                invoke_named_bool_getter(pawn, STR("IsConditionHpLimitOne")) != false)
            {
                Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] Party knockout initialization failed pawn={}\n"),
                    pawn->GetFullName());
                continue;
            }
            Output::send<LogLevel::Normal>(
                STR("[KH3APProbeCpp] Party knockout enabled pawn={} native_hp_floor=false\n"),
                pawn->GetFullName());
        }
    }

    auto RuntimePartyMemberService::maintain_party_navigation() -> bool
    {
        if (ship_control_active_)
        {
            return true;
        }
        bool complete = true;
        // The navigation fix also applies to stock-AI companions. Inspect
        // all six authoritative native friend slots, including world allies
        // outside the two configurable slots, never unrelated story actors.
        std::array<UObject*, native_friend_slot_count> navigation_pawns{};
        UObject* const navigation_manager = resolve_friend_manager();
        UObject* const navigation_level = persistent_level_for(navigation_manager);
        if (navigation_level &&
            observed_party_map_policy(navigation_level, config_.runtime_state) ==
                RuntimePartyMapPolicy::managed)
        {
            if (!read_friend_slots(navigation_manager, navigation_pawns))
            {
                return false;
            }
            for (UObject* const party_pawn : navigation_pawns)
            {
                if (!party_pawn || !is_uobject_valid_fast(party_pawn) ||
                    persistent_level_for(party_pawn) != navigation_level)
                {
                    continue;
                }
                UObject* const controller =
                    read_object_like_property(party_pawn, STR("Controller"));
                if (!controller)
                {
                    complete = false;
                    continue;
                }
                if (!configure_party_navigation(party_pawn, controller))
                {
                    complete = false;
                    Output::send<LogLevel::Warning>(
                        STR("[KH3APProbeCpp] Party navigation initialization failed pawn={}\n"),
                        party_pawn->GetFullName());
                }
            }
        }
        return complete;
    }

    auto RuntimePartyMemberService::maintain_party_placement() -> bool
    {
        if (reconcile_pending_)
        {
            return false;
        }
        if (ship_control_active_)
        {
            return true;
        }
        UObject* const sora = resolve_party_player();
        UObject* const level = persistent_level_for(sora);
        if (!level || observed_party_map_policy(level, config_.runtime_state) !=
                          RuntimePartyMapPolicy::managed)
        {
            return true;
        }
        UObject* const controller = read_object_like_property(sora, STR("Controller"));
        const auto input_ignored = invoke_named_bool_getter(controller, STR("IsMoveInputIgnored"));
        if (!input_ignored.has_value() || *input_ignored ||
            read_object_like_property(controller, STR("Pawn")) != sora)
        {
            return false;
        }
        const auto position = actor_position(sora);
        if (!position)
        {
            return false;
        }
        if (placement_sora_.Get() != sora ||
            outside_native_follow_distance(*position, placement_sora_position_))
        {
            placement_pending_ = true;
        }
        placement_sora_ = sora;
        placement_sora_position_ = *position;
        if (!placement_pending_)
        {
            return true;
        }

        UObject* const manager = resolve_friend_manager();
        if (read_object_like_property(sora, STR("MyFrdMgr")) != manager)
        {
            return false;
        }
        std::array<UObject*, runtime_party_member_slot_count> slots{};
        if (!read_friend_slots(manager, slots))
        {
            return false;
        }
        bool distant_custom_friend = false;
        for (UObject* const friend_pawn : slots)
        {
            if (!friend_pawn || !is_uobject_valid_fast(friend_pawn) ||
                persistent_level_for(friend_pawn) != level)
            {
                continue;
            }
            // Only the instances assigned our custom AI, never story actors
            // or the native world allies in the additional four slots.
            if (std::none_of(custom_party_assigned_pawns_.begin(),
                             custom_party_assigned_pawns_.end(),
                             [friend_pawn](const auto& assigned)
                             {
                                 return assigned.Get() == friend_pawn;
                             }))
            {
                continue;
            }
            const auto friend_position = actor_position(friend_pawn);
            distant_custom_friend =
                distant_custom_friend ||
                (friend_position && outside_native_follow_distance(*position, *friend_position));
        }
        if (!distant_custom_friend)
        {
            placement_pending_ = false;
            return true;
        }
        const auto disabled = invoke_named_bool_getter(manager, STR("IsDisableFriendWarpHome"));
        if (!disabled.has_value() || *disabled)
        {
            return false;
        }
        // A cutscene can relocate Sora while the battle tree keeps pathing
        // from the previous area. Ask the native party warp system once per
        // entry/large relocation. It chooses valid warp points and retains
        // native allies; no direct actor teleport, respawn or AI restart.
        if (invoke_named_single_bool_function(sora, STR("BP_ReqFriendWarpHomeAll"), false))
        {
            placement_pending_ = false;
            Output::send<LogLevel::Normal>(
                STR("[KH3APProbeCpp] Party catch-up requested after Sora relocation pawn={}\n"),
                sora->GetFullName());
        }
        return !placement_pending_;
    }

    auto RuntimePartyMemberService::apply_custom_party_ai_for_friend_choice(std::size_t choice)
        -> void
    {
        if (ship_control_active_)
        {
            return;
        }
        maintain_party_navigation();
        maintenance_.request();
        const auto definition_it =
            std::find_if(custom_party_ai_definitions.begin(), custom_party_ai_definitions.end(),
                         [choice](const CustomPartyAiDefinition& definition)
                         {
                             return definition.choice == choice;
                         });
        if (definition_it == custom_party_ai_definitions.end())
        {
            return;
        }

        auto& sora_state = config_.runtime_state.grant_state().pawn_state();
        UObject* const sora_pawn = sora_state.candidate_trusted() ? sora_state.object() : nullptr;
        UObject* const sora_level = persistent_level_for(sora_pawn);
        if (!sora_level || is_party_presentation_story_level(sora_level))
        {
            return;
        }

        std::array<UObject*, runtime_party_member_slot_count> candidates{};
        if (!read_friend_slots(resolve_friend_manager(), candidates))
        {
            return;
        }
        UObject* matched_pawn = nullptr;
        UObject* matched_controller = nullptr;
        std::size_t possessed_match_count = 0;
        for (UObject* const candidate : candidates)
        {
            if (!candidate || !is_uobject_valid_fast(candidate) ||
                persistent_level_for(candidate) != sora_level)
            {
                continue;
            }
            UClass* const candidate_class = candidate->GetClassPrivate();
            if (!candidate_class || !is_uobject_valid_fast(candidate_class) ||
                candidate_class->GetName() != definition_it->pawn_class_name)
            {
                continue;
            }
            UObject* const controller = read_object_like_property(candidate, STR("Controller"));
            if (!controller)
            {
                continue;
            }
            ++possessed_match_count;
            matched_pawn = candidate;
            matched_controller = controller;
        }

        // Match only managed slots, never a same-class story/event actor.
        // Normal SetFriend spawning yields one possessed exact-class match.
        if (possessed_match_count != 1)
        {
            Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] SetFriend event could not resolve "
                                                "custom party pawn character={} matches={} "
                                                "candidates={} level={}\n"),
                                            definition_it->display_name, possessed_match_count,
                                            candidates.size(), sora_level->GetFullName());
            return;
        }

        if (!apply_custom_party_ai_for_possession(matched_controller, matched_pawn))
        {
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] SetFriend event failed to assign "
                    "clean party AI character={} pawn={} controller={}\n"),
                definition_it->display_name, matched_pawn->GetFullName(),
                matched_controller->GetFullName());
        }
    }

    auto RuntimePartyMemberService::apply_custom_party_ai_for_possession(UObject* controller,
                                                                         UObject* pawn) -> bool
    {
        if (ship_control_active_)
        {
            return false;
        }
        if (!controller || !pawn || !is_uobject_valid_fast(controller) ||
            !is_uobject_valid_fast(pawn))
        {
            return false;
        }

        auto& sora_state = config_.runtime_state.grant_state().pawn_state();
        UObject* const sora_pawn = sora_state.candidate_trusted() ? sora_state.object() : nullptr;
        UObject* const sora_level = persistent_level_for(sora_pawn);
        if (!sora_level || persistent_level_for(pawn) != sora_level ||
            is_party_presentation_story_level(sora_level))
        {
            return false;
        }

        UClass* const pawn_class = pawn->GetClassPrivate();
        if (!pawn_class || !is_uobject_valid_fast(pawn_class))
        {
            return false;
        }
        for (size_t index = 0; index < custom_party_ai_definitions.size(); ++index)
        {
            const CustomPartyAiDefinition& definition = custom_party_ai_definitions[index];
            const bool selected =
                active_choices_[0] == definition.choice ||
                active_choices_[1] == definition.choice ||
                (reconcile_pending_ && (pending_choices_[0] == definition.choice ||
                                        pending_choices_[1] == definition.choice));
            if (!selected || pawn_class->GetName() != definition.pawn_class_name ||
                custom_party_assigned_pawns_[index].Get() == pawn)
            {
                continue;
            }

            UObject* behavior_tree = custom_party_trees_[index].Get();
            if (!behavior_tree || !is_uobject_valid_fast(behavior_tree))
            {
                custom_party_trees_[index] = {};
                if (custom_party_tree_resolution_attempted_[index])
                {
                    return false;
                }
                custom_party_tree_resolution_attempted_[index] = true;
                behavior_tree = find_loaded_runtime_asset(definition.tree_path);
                if (!behavior_tree || !is_uobject_valid_fast(behavior_tree))
                {
                    Output::send<LogLevel::Warning>(
                        STR("[KH3APProbeCpp] Party event found latent-loaded "
                            "tree nonresident character={} tree={}\n"),
                        definition.display_name, definition.tree_path);
                    return false;
                }
                behavior_tree->SetRootSet();
                custom_party_trees_[index] = behavior_tree;
            }

            // Loading the tree resolves its stock action-class imports. Patch
            // their CDOs only after that event-owned load has completed.
            reconcile_action_viable_state_patches(sora_level);

            if (!definition.follow_query_path.empty())
            {
                // Native pawn initialization copies this setting to the
                // blackboard after RunBehaviorTree. Initializing only the
                // tree's key leaves the first field query empty until the
                // root restarts after a battle transition.
                UObject* const follow_query =
                    find_loaded_runtime_asset(definition.follow_query_path);
                if (!write_object_like_property(pawn, STR("m_FollowPlayerEQSQuery"), follow_query))
                {
                    Output::send<LogLevel::Warning>(
                        STR("[KH3APProbeCpp] Party follow query initialization "
                            "failed character={} query={}\n"),
                        definition.display_name, definition.follow_query_path);
                    return false;
                }
                Output::send<LogLevel::Normal>(STR("[KH3APProbeCpp] Initialized party follow query "
                                                   "character={} pawn={} query={}\n"),
                                               definition.display_name, pawn->GetFullName(),
                                               follow_query->GetFullName());
            }
            custom_party_assigned_pawns_[index] = pawn;
            // Managed companions may need native catch-up after a cutscene
            // relocates Sora. This applies only to our owned custom-AI pawn.
            if (!invoke_set_disable_battle_mode_ai(pawn, false) ||
                !invoke_named_single_bool_function(pawn, STR("SetEnableWarp"), true) ||
                !write_object_like_property(pawn, STR("MyAIBehavior"), behavior_tree) ||
                !run_behavior_tree(controller, behavior_tree))
            {
                custom_party_assigned_pawns_[index] = {};
                return false;
            }

            for (size_t slot = 0; slot < active_choices_.size(); ++slot)
            {
                const size_t effective_choice =
                    reconcile_pending_ ? pending_choices_[slot] : active_choices_[slot];
                if (effective_choice == definition.choice)
                {
                    party_presentation_modes_[slot] = std::nullopt;
                }
            }
            Output::send<LogLevel::Normal>(
                STR("[KH3APProbeCpp] Party event assigned clean party AI "
                    "character={} pawn={} controller={} tree={}\n"),
                definition.display_name, pawn->GetFullName(), controller->GetFullName(),
                behavior_tree->GetFullName());

            // Prime the presentation through the same CanBattle hook that
            // will receive all later battle-state edges from the tree.
            (void)invoke_bool_getter(pawn, can_battle_path);
            return true;
        }
        return false;
    }
} // namespace kh3ap
