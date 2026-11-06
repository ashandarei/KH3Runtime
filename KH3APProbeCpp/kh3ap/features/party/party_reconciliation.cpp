#include "kh3ap/core/events/revision_hooks.h"
#include "kh3ap/features/party/party_member_service.h"
#include "kh3ap/features/party/party_helpers.h"
#include "kh3ap/features/party/party_operation_timer.h"
#include "kh3ap/features/party/party_runtime_constants.h"
#include "kh3ap/features/party/party_world_context.h"
#include "kh3ap/bridge/transport/pipe_transport.h"
#include "kh3ap/app/state.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/features/grants/grant_internal_call_depth.h"
#include "kh3ap/features/grants/grant_pawn_state.h"
#include "kh3ap/features/player/characters/character_swap.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObject.hpp>
#include <UnrealDef.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kh3ap
{
    using namespace RC;
    using namespace RC::Unreal;
    using namespace party_runtime;
    using party_catalog::custom_party_ai_definitions;
    using party_catalog::CustomPartyAiDefinition;
    using party_catalog::party_choices;
    using party_catalog::vanilla_choice;
    using party_detail::PartyOperationTimer;
    using party_native::invoke_named_bool_getter;
    using party_native::is_friend_component;
    using party_native::persistent_level_for;
    using party_native::read_friend_slots;
    using party_native::read_object_like_property;
    using party_world::is_party_spawn_excluded_level;

    namespace
    {
        auto invoke_friend_function(UObject* friend_manager, std::wstring_view function_path,
                                    int32_t slot, std::wstring_view npc_id) -> bool
        {
            const PartyOperationTimer operation_timer{function_path, npc_id, slot};
            if (!is_friend_component(friend_manager))
            {
                return false;
            }
            // The manager owns these functions. Avoid UE4SS's first-use
            // global object search while the player's new party is spawning.
            const std::wstring name{function_path.substr(function_path.rfind(L':') + 1)};
            UFunction* const function = friend_manager->GetFunctionByNameInChain(name.c_str());
            if (!function || !is_uobject_valid_fast(function) || function->GetParmsSize() <= 0)
            {
                return false;
            }

            std::vector<std::byte> params(static_cast<std::size_t>(function->GetParmsSize()),
                                          std::byte{0});
            if (!write_integral_function_param(find_function_param_by_name(function, STR("inIdx")),
                                               params.data(), static_cast<uint64_t>(slot)))
            {
                return false;
            }
            if (!npc_id.empty() &&
                !write_name_or_string_function_param(
                    find_function_param_by_name(function, STR("inNpcName")), params.data(), npc_id))
            {
                return false;
            }

            const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
            {
                const PartyOperationTimer native_timer{STR("ProcessEvent"), npc_id, slot};
                friend_manager->ProcessEvent(function, params.data());
            }
            if (function_path == set_friend_path)
            {
                FProperty* const result = find_function_param_by_name(function, STR("ReturnValue"));
                bool* const accepted = result && result->GetClass().GetName() == STR("BoolProperty")
                                           ? result->ContainerPtrToValuePtr<bool>(params.data())
                                           : nullptr;
                return accepted && *accepted;
            }
            return true;
        }
    } // namespace

    auto RuntimePartyMemberService::request_reconcile(std::wstring_view reason,
                                                      bool reroll_random_pair) -> void
    {
        if (test_active() || reason == STR("bridge_test_restore"))
        {
            ++test_requests_;
            test_reconcile_started_ = std::chrono::steady_clock::now();
            test_native_work_ms_ = 0;
        }
        restore_action_viable_state_patches();
        custom_party_assigned_pawns_ = {};
        custom_party_trees_ = {};
        custom_party_tree_resolution_attempted_ = {};
        party_presentation_modes_ = {};
        reconcile_pending_ = true;
        awaiting_entry_control_ = false;
        preload_phase_complete_ = false;
        delete_phase_complete_ = false;
        settle_ticks_ = 0;
        force_random_pair_ = force_random_pair_ || reroll_random_pair;
        reconcile_reason_ = reason;
    }

    auto RuntimePartyMemberService::resolve_party_player() const -> UObject*
    {
        // Party follow/ownership is independent of the Sora-only AP grant gate.
        // Keep KH0.2's solo/scripted-companion policy outside this service.
        if (kh02_current_story_world()) { return nullptr; }
        if (auto* pawn = character_swap::party_owner()) { return pawn; }
        auto& pawn_state = config_.runtime_state.grant_state().pawn_state();
        return pawn_state.candidate_trusted() ? pawn_state.object() : nullptr;
    }

    auto RuntimePartyMemberService::resolve_friend_manager() -> UObject*
    {
        // A still-valid component can belong to the world we just left.
        // Resolve from the current Sora rather than a prior native party event.
        UObject* const sora_pawn = resolve_party_player();
        if (!sora_pawn || !is_uobject_valid_fast(sora_pawn))
        {
            return nullptr;
        }
        UObject* const manager =
            try_read_reflected_object_property_value(sora_pawn, STR("MyFrdMgr"));
        if (!is_friend_component(manager))
        {
            return nullptr;
        }
        current_friend_manager_ = manager;
        return manager;
    }

    auto RuntimePartyMemberService::observe_goa_map_ready() -> void
    {
        if (!installed_ || !active_)
        {
            return;
        }
        auto& pawn_state = config_.runtime_state.grant_state().pawn_state();
        UObject* const pawn = pawn_state.candidate_trusted() ? pawn_state.object() : nullptr;
        if (!pawn || !is_uobject_valid_fast(pawn))
        {
            goa_entry_pawn_ = {};
            return;
        }
        if (goa_entry_pawn_.Get() == pawn)
        {
            return;
        }
        UObject* const level = persistent_level_for(pawn);
        if (!level || !runtime_party_requires_entry_reconcile(level->GetFullName()))
        {
            return;
        }
        goa_entry_pawn_ = pawn;
        const auto settings = effective_settings();
        request_reconcile(STR("GoA map ready"),
                          settings.party_randomization_enabled && settings.party_reroll_every_room);
        // This deadline belongs to one queued event. Idle tick never inspects
        // Sora or a map, and duplicate map-name callbacks do not restart it.
        next_reconcile_attempt_ =
            std::max(std::chrono::steady_clock::now(),
                     pawn_state.first_seen_at() + RuntimeGrantPawnState::stability_delay);
        Output::send<LogLevel::Normal>(
            STR("[KH3APProbeCpp] GoA party entry requested source=map_ready_event pawn={}\n"),
            pawn->GetFullName());
    }

    auto RuntimePartyMemberService::yield_to_vanilla_party(UObject* level) -> bool
    {
        if (!level || !is_uobject_valid_fast(level))
        {
            return false;
        }
        const auto policy = effective_map_policy(level);
        if (policy != RuntimePartyMapPolicy::vanilla &&
            policy != RuntimePartyMapPolicy::vanilla_first_only)
        {
            return false;
        }
        if (policy == RuntimePartyMapPolicy::vanilla_first_only)
        {
            // Never delete/respawn the encounter's first companion. The
            // second slot is zero-based index 1, regardless of saved choices.
            if (!invoke_friend_function(resolve_friend_manager(), delete_friend_path, 1, {}))
            {
                // Keep the transaction pending without falling through to
                // normal reconciliation, which would delete the first slot.
                return true;
            }
        }
        // Cancel the transaction without spawning or replacing story allies.
        // This covers initial loads, every native party event, and menu edits.
        // Discard a delayed replacement too if the map changed while settling.
        restore_action_viable_state_patches();
        custom_party_assigned_pawns_ = {};
        custom_party_trees_ = {};
        custom_party_tree_resolution_attempted_ = {};
        party_presentation_modes_ = {};
        pending_choices_ = {};
        active_choices_ = {}; // No companions are owned by this service here.
        reconcile_pending_ = false;
        preload_phase_complete_ = false;
        delete_phase_complete_ = false;
        settle_ticks_ = 0;
        reconcile_friend_manager_ = nullptr;
        Output::send<LogLevel::Normal>(STR("[KH3APProbeCpp] Party manager deferred to vanilla "
                                           "level={} reason={} second_slot_cleared={}\n"),
                                       level->GetFullName(), reconcile_reason_,
                                       policy == RuntimePartyMapPolicy::vanilla_first_only);
        return true;
    }

    auto RuntimePartyMemberService::begin_reconcile(UObject* friend_manager) -> bool
    {
        if (!is_friend_component(friend_manager))
        {
            return false;
        }
        if (friend_manager != resolve_friend_manager())
        {
            return false;
        }
        if (awaiting_entry_control_)
        {
            UObject* const pawn = context_entry_pawn_.Get();
            if (!pawn || !is_uobject_valid_fast(pawn) ||
                pawn != config_.runtime_state.grant_state().pawn_state().object())
            {
                awaiting_entry_control_ = false;
                reconcile_pending_ = false;
                return false;
            }
            UObject* const controller = read_object_like_property(pawn, STR("Controller"));
            const auto input_ignored =
                invoke_named_bool_getter(controller, STR("IsMoveInputIgnored"));
            if (read_object_like_property(controller, STR("Pawn")) != pawn ||
                !input_ignored.has_value() || *input_ignored)
            {
                return false;
            }
            awaiting_entry_control_ = false;
        }
        UObject* const level = persistent_level_for(friend_manager);
        if (!level)
        {
            return false;
        }
        if (yield_to_vanilla_party(level))
        {
            return true;
        }
        // Keep the saved selection/random pair intact for the next allowed
        // map, while still deleting any existing friends on excluded maps.
        if (vanilla_friend_manager_.Get() != friend_manager &&
            !capture_vanilla_party(friend_manager))
        {
            return false;
        }
        pending_choices_ = is_party_spawn_excluded_level(level)
                               ? std::array<std::size_t, runtime_party_member_slot_count>{}
                               : desired_choices(level);
        pending_npc_ids_ = {};
        for (size_t slot = 0; slot < pending_choices_.size(); ++slot)
        {
            const auto choice = pending_choices_[slot];
            pending_npc_ids_[slot] = choice == vanilla_choice
                                         ? vanilla_npc_ids_[slot]
                                         : std::wstring{party_choices[choice].npc_id};
        }
        const auto family = [](std::wstring_view npc)
        {
            for (const auto& definition : party_choices)
            {
                if (!npc.empty() && definition.npc_id == npc)
                {
                    return definition.unique_character;
                }
            }
            return npc;
        };
        // Explicit choices take precedence if vanilla would duplicate that
        // character (including a different world costume).
        if (!pending_npc_ids_[0].empty() &&
            family(pending_npc_ids_[0]) == family(pending_npc_ids_[1]))
        {
            const size_t drop =
                pending_choices_[0] == vanilla_choice && pending_choices_[1] != vanilla_choice ? 0
                                                                                               : 1;
            pending_npc_ids_[drop].clear();
        }
        for (size_t slot = 0; slot < pending_choices_.size(); ++slot)
        {
            if (pending_npc_ids_[slot].empty())
            {
                pending_choices_[slot] = 0;
            }
        }
        if (pending_choices_[0] == 0)
        {
            std::swap(pending_npc_ids_[0], pending_npc_ids_[1]);
        }
        pending_choices_ = runtime_party_physical_choices(pending_choices_);
        reuse_native_pair_ = false;
        std::array<UObject*, runtime_party_member_slot_count> existing{};
        if (read_friend_slots(friend_manager, existing))
        {
            reuse_native_pair_ = true;
            for (size_t slot = 0; slot < pending_choices_.size(); ++slot)
            {
                reuse_native_pair_ =
                    reuse_native_pair_ &&
                    (pending_npc_ids_[slot].empty()
                         ? !existing[slot]
                         : existing[slot] && persistent_level_for(existing[slot]) == level &&
                               existing[slot]->GetClassPrivate()->GetName() ==
                                   pending_npc_ids_[slot] + L"_Pawn_C");
            }
        }
        if (reuse_native_pair_)
        {
            reconcile_friend_manager_ = friend_manager;
            pending_pawn_class_paths_ = {};
            preload_phase_complete_ = true;
            delete_phase_complete_ = true;
            settle_ticks_ = 0;
            return true;
        }
        reconcile_friend_manager_ = friend_manager;
        // These packages are the selected NPCs' shipped pawn Blueprints.
        // Load their dependencies through the latent loader before SetFriend,
        // which otherwise loads them synchronously on the game thread.
        pending_pawn_class_paths_ = {};
        party_asset_deadline_ = std::chrono::steady_clock::now() + party_preload_timeout;
        next_party_asset_request_ = std::chrono::steady_clock::now() + party_asset_request_interval;
        for (size_t slot = 0; slot < pending_choices_.size(); ++slot)
        {
            const auto choice = pending_choices_[slot];
            if (!choice || choice >= party_choices.size())
            {
                continue;
            }
            const std::wstring& npc = pending_npc_ids_[slot];
            auto& path = pending_pawn_class_paths_[slot];
            path = L"/Game/Blueprints/Npc/" + npc + L"/" + npc + L"_Pawn." + npc + L"_Pawn_C";
            const PartyOperationTimer timer{STR("request_pawn_class"), npc,
                                            static_cast<int32_t>(slot)};
            std::wstring resolution;
            // A new transaction must not reuse an earlier latent action after Retry.
            if (!request_runtime_asset_class_load(
                    friend_manager, path, &resolution,
                    static_cast<int32_t>(
                        pawn_class_load_uuid_prefix |
                        (++party_asset_request_sequence_ & pawn_class_load_sequence_mask))))
            {
                Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] Party preload unavailable member={} resolution={} "
                        "swap_cancelled=true\n"),
                    npc, resolution);
                reconcile_pending_ = false;
                return false;
            }
        }
        uint32_t requested_asset_settle_ticks = friend_settle_ticks;
        for (size_t index = 0; index < custom_party_ai_definitions.size(); ++index)
        {
            const CustomPartyAiDefinition& definition = custom_party_ai_definitions[index];
            const bool selected = pending_choices_[0] == definition.choice ||
                                  pending_choices_[1] == definition.choice;
            if (!selected)
            {
                continue;
            }

            UObject* const resident_tree = find_loaded_runtime_asset(definition.tree_path);
            if (resident_tree && is_uobject_valid_fast(resident_tree))
            {
                // The bounded custom assets are retained for the process
                // lifetime, preventing map travel from collecting a tree that
                // is still referenced by a randomized party member.
                resident_tree->SetRootSet();
                custom_party_trees_[index] = resident_tree;
                continue;
            }

            std::wstring load_resolution{};
            const bool requested =
                request_runtime_asset_load(friend_manager, definition.tree_path, &load_resolution,
                                           static_cast<int32_t>(party_tree_load_uuid_base + index));
            Output::send<LogLevel::Normal>(STR("[KH3APProbeCpp] Party reconcile requested latent "
                                               "tree load character={} requested={} resolution={} "
                                               "tree={}\n"),
                                           definition.display_name, requested, load_resolution,
                                           definition.tree_path);
            if (requested)
            {
                requested_asset_settle_ticks = custom_party_asset_settle_ticks;
            }
        }
        preload_phase_complete_ = true;
        settle_ticks_ = requested_asset_settle_ticks;
        return true;
    }

    auto RuntimePartyMemberService::delete_current_party() -> bool
    {
        UObject* const friend_manager = reconcile_friend_manager_.Get();
        if (!is_friend_component(friend_manager) || friend_manager != resolve_friend_manager())
        {
            return false;
        }
        const bool first = invoke_friend_function(friend_manager, delete_friend_path, 0, {});
        const bool second = invoke_friend_function(friend_manager, delete_friend_path, 1, {});
        if (test_active() || reconcile_reason_ == STR("bridge_test_restore"))
        {
            test_deletes_ += static_cast<uint64_t>(first) + static_cast<uint64_t>(second);
        }
        if (!first || !second)
        {
            return false;
        }
        delete_phase_complete_ = true;
        settle_ticks_ = friend_settle_ticks;
        return true;
    }

    auto RuntimePartyMemberService::party_assets_ready() -> bool
    {
        const PartyOperationTimer timer{STR("party_assets_ready"), {}, -1};
        const auto now = std::chrono::steady_clock::now();
        const bool request_again = now >= next_party_asset_request_ && now < party_asset_deadline_;
        if (request_again)
        {
            next_party_asset_request_ = now + party_asset_request_interval;
        }
        constexpr auto pending_flags =
            static_cast<EObjectFlags>(RF_NeedLoad | RF_NeedPostLoad | RF_NeedPostLoadSubobjects);
        bool ready = true;
        for (auto& path : pending_pawn_class_paths_)
        {
            if (path.empty())
            {
                continue;
            }
            // The engine's nonloading soft-class conversion uses its object
            // hash and avoids UE4SS's stale full-name cache and repeated scans.
            auto* klass = find_loaded_runtime_asset_class(path);
            bool callable =
                klass && is_uobject_valid_fast(klass) && !klass->HasAnyFlags(pending_flags);
            std::wstring_view blocked_event{};
            if (callable)
            {
                // UObject::GetFunctionByNameInChain on a UClass searches the
                // metaclass, not the pawn's Blueprint functions.
                for (auto* function : klass->ForEachFunctionInChain())
                {
                    if (!is_uobject_valid_fast(function))
                    {
                        callable = false;
                        break;
                    }
                    const auto name = function->GetName();
                    if ((name == STR("UserConstructionScript") ||
                         name == STR("ReceiveBeginPlay")) &&
                        (function->HasAnyFlags(pending_flags) || !function->GetFuncPtr()))
                    {
                        callable = false;
                        blocked_event = name == STR("UserConstructionScript")
                                            ? STR("UserConstructionScript")
                                            : STR("ReceiveBeginPlay");
                    }
                }
            }
            // Recheck every selected class until the actual spawn tick. A class
            // ready earlier may be collected during a retry or the delete delay.
            if (!callable)
            {
                ready = false;
                // Entry/Retry can retire an earlier latent action while the
                // native party setup continues. Reissue only unresolved assets,
                // bounded by this transaction's deadline; never load synchronously.
                if (request_again)
                {
                    std::wstring resolution;
                    const bool requested = request_runtime_asset_class_load(
                        reconcile_friend_manager_.Get(), path, &resolution,
                        static_cast<int32_t>(
                            pawn_class_load_uuid_prefix |
                            (++party_asset_request_sequence_ & pawn_class_load_sequence_mask)));
                    Output::send<LogLevel::Normal>(STR("[KH3APProbeCpp] Party preload renewed "
                                                       "path={} requested={} resolution={}\n"),
                                                   path, requested, resolution);
                }
                if (std::chrono::steady_clock::now() >= party_asset_deadline_)
                {
                    Output::send<LogLevel::Warning>(
                        STR("[KH3APProbeCpp] Party preload blocked path={} valid={} load_flags={} "
                            "event={}\n"),
                        path, klass && is_uobject_valid_fast(klass),
                        klass && is_uobject_valid_fast(klass) && klass->HasAnyFlags(pending_flags),
                        blocked_event);
                }
            }
        }
        if (!ready && std::chrono::steady_clock::now() >= party_asset_deadline_)
        {
            Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] Party preload deadline reached "
                                                "swap_cancelled=true deleted={} reason={}\n"),
                                            delete_phase_complete_, reconcile_reason_);
            reconcile_pending_ = false;
            if (test_active())
            {
                ++test_failures_;
            }
            return false;
        }
        return ready;
    }

    auto RuntimePartyMemberService::finish_reconcile() -> bool
    {
        const PartyOperationTimer finish_timer{STR("finish_reconcile"), {}, -1};
        UObject* const friend_manager = reconcile_friend_manager_.Get();
        if (!is_friend_component(friend_manager))
        {
            return false;
        }
        // Travel can replace Sora during the delete-to-spawn delay.
        if (friend_manager != resolve_friend_manager())
        {
            return false;
        }
        UObject* const level = persistent_level_for(friend_manager);
        if (!level)
        {
            return false;
        }
        if (yield_to_vanilla_party(level))
        {
            return true;
        }
        // Recheck at the actual spawn boundary after the latent settle delay.
        if (effective_map_policy(level) == RuntimePartyMapPolicy::solo)
        {
            pending_choices_ = {};
            pending_npc_ids_ = {};
        }
        for (std::size_t slot = 0; slot < pending_choices_.size(); ++slot)
        {
            const std::size_t choice = pending_choices_[slot];
            if (choice == 0)
            {
                continue;
            }
            if (reuse_native_pair_)
            {
                apply_custom_party_ai_for_friend_choice(choice);
                continue;
            }
            if (choice >= party_choices.size())
            {
                return false;
            }
            if (!invoke_friend_function(friend_manager, set_friend_path, static_cast<int32_t>(slot),
                                        pending_npc_ids_[slot]))
            {
                Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] Party SetFriend rejected slot={} member={} reason={}\n"),
                    slot, party_choices[choice].display_name, reconcile_reason_);
                return false;
            }

            if (test_active() || reconcile_reason_ == STR("bridge_test_restore"))
            {
                ++test_spawns_;
            }
            if (runtime_party_requires_entry_reconcile(level->GetFullName()))
            {
                // Verify the authoritative two slots, without scanning actors.
                FProperty* const property =
                    friend_manager->GetPropertyByNameInChain(STR("m_pFriendPawn"));
                void* const values =
                    friend_manager->GetValuePtrByPropertyNameInChain(STR("m_pFriendPawn"));
                const bool readable = property && values &&
                                      property->GetClass().GetName() == STR("WeakObjectProperty") &&
                                      property->GetArrayDim() > static_cast<int>(slot) &&
                                      property->GetElementSize() == sizeof(FWeakObjectPtr);
                FWeakObjectPtr weak_pawn{};
                if (readable)
                {
                    std::memcpy(&weak_pawn,
                                static_cast<std::byte*>(values) + slot * property->GetElementSize(),
                                sizeof(weak_pawn));
                }
                UObject* const party_pawn = weak_pawn.Get();
                if constexpr (PartyOperationTimer::profiling_enabled)
                {
                    if (party_pawn && is_uobject_valid_fast(party_pawn))
                    {
                        Output::send<LogLevel::Verbose>(
                            STR("[KH3APProbeCpp] PartyTiming pawn_class={}\n"),
                            party_pawn->GetClassPrivate()->GetFullName());
                    }
                }
                if (!party_pawn || !is_uobject_valid_fast(party_pawn) ||
                    persistent_level_for(party_pawn) != level)
                {
                    // An accepted spawn may not have published its weak slot
                    // yet. Never turn this observation into another destructive
                    // delete/spawn cycle, or skip the other selected slot.
                    Output::send<LogLevel::Warning>(
                        STR("[KH3APProbeCpp] Party spawn accepted but immediate slot unverified "
                            "slot={} member={} readable={} reason={} no_respawn=true\n"),
                        slot, party_choices[choice].display_name, readable, reconcile_reason_);
                }
            }

            // SetFriend is the authoritative party-spawn event. Its native
            // path does not necessarily dispatch Controller.Possess through
            // ProcessEvent, so bind the clean tree as soon as SetFriend has
            // returned the fully spawned and possessed party pawn.
            {
                const PartyOperationTimer ai_timer{STR("custom_ai"),
                                                   party_choices[choice].display_name,
                                                   static_cast<int32_t>(slot)};
                apply_custom_party_ai_for_friend_choice(choice);
            }
        }
        active_choices_ = pending_choices_;
        active_npc_ids_ = pending_npc_ids_;
        initialize_party_knockout();
        Output::send<LogLevel::Normal>(
            STR("[KH3APProbeCpp] Runtime party reconciled reason={} "
                "friend_1={} friend_2={} randomized={} unlocked={}\n"),
            reconcile_reason_, party_choices[active_choices_[0]].display_name,
            party_choices[active_choices_[1]].display_name,
            effective_settings().party_randomization_enabled, status().unlocked_count);
        return true;
    }

    auto RuntimePartyMemberService::tick_maintenance(std::chrono::steady_clock::time_point now)
        -> void
    {
        const auto navigation_revision = navigation_hooks_ ? navigation_hooks_->revision() : 0;
        if (navigation_revision != navigation_revision_)
        {
            navigation_revision_ = navigation_revision;
            maintenance_.request();
        }
        if (maintenance_.take(now))
        {
            const RuntimeHotPathProfileScope profile{L"party:maintenance"};
            const bool navigated = maintain_party_navigation();
            const bool placed = maintain_party_placement();
            maintenance_.finish(now, navigated && placed);
        }
    }

    auto RuntimePartyMemberService::tick() -> void
    {
        if (!installed_ || !active_)
        {
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        const auto generation = pipe::input_generation();
        if (generation != unlock_input_generation_)
        {
            unlock_input_generation_ = generation;
            (void)refresh_unlocks_from_bridge();
        }
        tick_maintenance(now);
        if (!reconcile_pending_)
        {
            return;
        }
        // Only inspect at a queued transaction boundary. Native Sora state
        // events drive entry/exit while idle; no new per-frame state polling.
        // Revalidate before deleting or finishing a delayed replacement.
        refresh_ship_control();
        if (now < next_reconcile_attempt_)
        {
            return;
        }
        if (settle_ticks_ > 0)
        {
            --settle_ticks_;
            return;
        }
        if (!preload_phase_complete_)
        {
            const auto started = std::chrono::steady_clock::now();
            (void)begin_reconcile(resolve_friend_manager());
            if (test_active() || reconcile_reason_ == STR("bridge_test_restore"))
            {
                test_native_work_ms_ += std::chrono::duration<double, std::milli>(
                                            std::chrono::steady_clock::now() - started)
                                            .count();
            }
            return;
        }
        // Never finish a transaction against a manager replaced by Retry/travel.
        if (reconcile_friend_manager_.Get() != resolve_friend_manager())
        {
            request_reconcile(STR("party manager changed during preload"), false);
            return;
        }
        if (!party_assets_ready())
        {
            next_reconcile_attempt_ = now + party_reconcile_retry_delay;
            return;
        }
        // Preserve the current companions until all replacement classes are ready.
        // Keep the original delete-to-spawn delay, then revalidate before spawning.
        if (!delete_phase_complete_)
        {
            if (!delete_current_party())
            {
                reconcile_pending_ = false;
            }
            return;
        }
        const auto started = std::chrono::steady_clock::now();
        const bool completed = finish_reconcile();
        if (test_active() || reconcile_reason_ == STR("bridge_test_restore"))
        {
            test_reconciles_ += completed ? 1 : 0;
            test_failures_ += completed ? 0 : 1;
            const auto finished = std::chrono::steady_clock::now();
            test_native_work_ms_ +=
                std::chrono::duration<double, std::milli>(finished - started).count();
            test_last_reconcile_ms_ =
                std::chrono::duration<double, std::milli>(finished - test_reconcile_started_)
                    .count();
        }
        if (!completed)
        {
            // A partial native spawn must not automatically restart deletion.
            // A later native party event or explicit menu edit can request a
            // new transaction against the then-current Sora and manager.
            Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] Party reconcile stopped reason={} "
                                                "spawn_incomplete=true automatic_retry=false\n"),
                                            reconcile_reason_);
        }
        reconcile_pending_ = false;
        preload_phase_complete_ = false;
        delete_phase_complete_ = false;
        reconcile_friend_manager_ = nullptr;
    }
} // namespace kh3ap
