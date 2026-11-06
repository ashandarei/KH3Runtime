#include "kh3ap/app/config.h"
#include "kh3ap/features/party/party_world_context.h"
#include "kh3ap/features/party/party_member_service.h"
#include "kh3ap/features/party/party_helpers.h"
#include "kh3ap/features/grants/delivery/item_call_helpers.h"
#include "kh3ap/features/grants/grant_pawn_state.h"
#include "kh3ap/app/state.h"
#include "kh3ap/bridge/transport/bridge_channels.h"
#include "kh3ap/core/objects/uobject_validity.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/FScriptArray.hpp>
#include <Unreal/FString.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <Unreal/Property/FSoftObjectProperty.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UObject.hpp>
#include <UnrealDef.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace kh3ap
{
    using ::kh3ap::settings::s_get_game_flag_path;
    using ::kh3ap::settings::s_tres_game_blueprint_library_default_object_path;

    using namespace RC;
    using namespace RC::Unreal;
    using party_catalog::vanilla_choice;
    using party_native::is_friend_component;
    using party_native::persistent_level_for;
    using party_native::read_object_like_property;
    namespace party_world
    {
        namespace
        {
            constexpr int32_t maximum_streaming_levels = 1024;
            constexpr std::size_t legacy_asset_path_offset = 0x10;
            [[nodiscard]] auto flash_tracer_course_b_requested(UObject* level) -> bool
            {
                if (!is_uobject_valid_fast(level) ||
                    level->GetFullName().find(L"/Game/Levels/bx/bx_02/") == std::wstring::npos)
                {
                    return false;
                }
                UObject* const world = level->GetOuterPrivate();
                if (!is_uobject_valid_fast(world) ||
                    !is_uobject_valid_fast(world->GetClassPrivate()) ||
                    world->GetClassPrivate()->GetName() != L"World")
                {
                    return false;
                }

                // Game-thread-only weak cache, scoped to the owning world. A loaded
                // package from an old map or a hidden/preloaded course is not active.
                static FWeakObjectPtr course_world, course_stream;
                UObject* stream = course_world.Get() == world ? course_stream.Get() : nullptr;
                if (!is_uobject_valid_fast(stream) || stream->GetOuterPrivate() != world)
                {
                    course_world = nullptr;
                    course_stream = nullptr;
                    auto* property = world->GetPropertyByNameInChain(L"StreamingLevels");
                    if (!property || !property->IsA<FArrayProperty>() ||
                        property->GetElementSize() != sizeof(FScriptArray))
                    {
                        return false;
                    }
                    auto* inner = static_cast<FArrayProperty*>(property)->GetInner();
                    if (!inner || inner->GetClass().GetName() != L"ObjectProperty" ||
                        inner->GetElementSize() != sizeof(UObject*))
                    {
                        return false;
                    }
                    const auto* levels = property->ContainerPtrToValuePtr<FScriptArray>(world);
                    if (!levels || levels->Num() <= 0 || levels->Num() > maximum_streaming_levels ||
                        !levels->GetData())
                    {
                        return false;
                    }
                    auto* const entries = static_cast<UObject* const*>(levels->GetData());
                    for (int32_t index = 0; index < levels->Num(); ++index)
                    {
                        UObject* const candidate = entries[index];
                        if (!is_uobject_valid_fast(candidate) ||
                            candidate->GetOuterPrivate() != world)
                        {
                            continue;
                        }
                        auto* asset = candidate->GetPropertyByNameInChain(L"WorldAsset");
                        if (!asset)
                        {
                            continue;
                        }
                        std::wstring path;
                        if (asset->GetClass().GetName() == L"AssetObjectProperty" &&
                            asset->GetElementSize() == legacy_asset_path_offset + sizeof(FString))
                        {
                            // KH3's UE4.17 legacy asset reference stores its path
                            // after the weak-object/tag fields, as the load helper does.
                            const auto* value = reinterpret_cast<const FString*>(
                                static_cast<const std::byte*>(
                                    asset->ContainerPtrToValuePtr<void>(candidate)) +
                                legacy_asset_path_offset);
                            if (const auto* chars = value->GetCharArray())
                            {
                                path = chars;
                            }
                        }
                        else if (asset->GetClass().GetName() == L"SoftObjectProperty" &&
                                 asset->GetElementSize() == sizeof(FSoftObjectPtr))
                        {
                            path = asset->ContainerPtrToValuePtr<FSoftObjectPtr>(candidate)
                                       ->ObjectID.AssetPathName.ToString();
                        }
                        if (!runtime_party_is_flash_tracer_course_b_asset(path))
                        {
                            continue;
                        }
                        stream = candidate;
                        course_world = world;
                        course_stream = stream;
                        break;
                    }
                }
                // This requested visibility is set before the startup's first
                // SetFriendAll, unlike waiting for the race HUD or music to start.
                if (!is_uobject_valid_fast(stream))
                {
                    return false;
                }
                auto* visible = stream->GetPropertyByNameInChain(L"bShouldBeVisible");
                // ULevelStreaming packs several flags in this byte. Reading a bool
                // pointer would mistake bShouldBeVisibleInEditor for course activity.
                return visible && visible->IsA<FBoolProperty>() &&
                       static_cast<FBoolProperty*>(visible)->GetPropertyValueInContainer(stream);
            }
        } // namespace
        [[nodiscard]] auto observed_party_map_policy(UObject* level, RuntimeState& runtime_state)
            -> RuntimePartyMapPolicy
        {
            if (!is_uobject_valid_fast(level))
            {
                return RuntimePartyMapPolicy::managed;
            }
            const std::wstring level_name = level->GetFullName();
            int32_t progress = -1;
            int32_t bx_progress = -1;
            if (level_name.find(L"/Game/Levels/kg_DLC/kg_06/") != std::wstring_view::npos)
            {
                (void)try_call_get_game_flag(
                    L"gameflow_KG_DLC", progress, runtime_state, s_get_game_flag_path,
                    s_tres_game_blueprint_library_default_object_path, false);
            }
            if (level_name.find(L"/Game/Levels/bx/bx_02/") != std::wstring_view::npos)
            {
                (void)try_call_get_game_flag(
                    L"gameflow_BX", bx_progress, runtime_state, s_get_game_flag_path,
                    s_tres_game_blueprint_library_default_object_path, false);
            }
            return runtime_party_map_policy(level_name, progress, bx_progress,
                                            flash_tracer_course_b_requested(level));
        }

        [[nodiscard]] auto is_party_spawn_excluded_level(UObject* level) -> bool
        {
            // Solo removes companions; vanilla leaves the game's party alone.
            return runtime_party_map_policy(level->GetFullName()) == RuntimePartyMapPolicy::solo;
        }

        [[nodiscard]] auto is_party_presentation_story_level(UObject* level) -> bool
        {
            if (!level || !is_uobject_valid_fast(level))
            {
                return false;
            }
            const std::wstring full_name = level->GetFullName();
            // Never touch the story/event instances of Roxas or Kairi. The
            // randomized friends are useful on the rest of the game, while
            // these maps own bespoke battle-mode choreography for them.
            const auto policy = runtime_party_map_policy(full_name);
            return flash_tracer_course_b_requested(level) ||
                   policy == RuntimePartyMapPolicy::vanilla ||
                   policy == RuntimePartyMapPolicy::vanilla_first_only;
        }
    } // namespace party_world
    using party_world::observed_party_map_policy;
    auto RuntimePartyMemberService::observe_set_friend_all(UObject* friend_manager) -> void
    {
        if (!is_friend_component(friend_manager))
        {
            return;
        }
        current_friend_manager_ = friend_manager;
        refresh_ship_control();
        const RuntimeGoaSettingsStatus settings = effective_settings();
        request_reconcile(STR("TresFriendComponent.SetFriendAll"),
                          !ship_control_active_ && settings.party_randomization_enabled &&
                              settings.party_reroll_every_room);
    }

    auto RuntimePartyMemberService::observe_controller_possess(UObject* controller) -> void
    {
        UObject* const pawn = read_object_like_property(controller, STR("Pawn"));
        (void)apply_custom_party_ai_for_possession(controller, pawn);
        if (config_.on_pawn_possessed && pawn && is_uobject_valid_fast(pawn))
        {
            config_.on_pawn_possessed(pawn);
        }
        observe_sora_state(pawn);
    }

    auto RuntimePartyMemberService::refresh_ship_control() -> void
    {
        observe_sora_state(resolve_party_player());
    }

    auto RuntimePartyMemberService::observe_sora_state(UObject* sora_pawn) -> void
    {
        if (!installed_ || !active_ || !sora_pawn || !is_uobject_valid_fast(sora_pawn))
        {
            return;
        }
        // State and possession events can belong to NPCs or a retired Sora.
        if (resolve_party_player() != sora_pawn)
        {
            return;
        }
        UObject* const level = persistent_level_for(sora_pawn);
        if (!level)
        {
            return;
        }
        maintenance_.request();
        const std::wstring level_name = level->GetFullName();
        bool ship_control = false;
        if (level_name.find(L"/Game/Levels/ca/") != std::wstring::npos)
        {
            UObject* const state_component =
                read_object_like_property(sora_pawn, STR("MyStateComp"));
            UObject* const current_state =
                read_object_like_property(state_component, STR("m_pCurrentState"));
            if (!current_state ||
                read_object_like_property(current_state, STR("MyCharPawn")) != sora_pawn)
            {
                return; // An unresolved entry state must not release the helm override.
            }
            UClass* const state_class = current_state->GetClassPrivate();
            if (!state_class || !is_uobject_valid_fast(state_class))
            {
                return;
            }
            ship_control = runtime_party_is_ship_control_state(
                level_name, state_class->GetName(),
                ship_control_active_ && ship_control_sora_.Get() == sora_pawn);
        }
        const bool owner_changed = ship_control_sora_.Get() != sora_pawn;
        const bool changed =
            ship_control_active_ != ship_control || (ship_control && owner_changed);
        ship_control_sora_ = sora_pawn;
        ship_control_active_ = ship_control;
        if (!changed)
        {
            return;
        }
        // Cancel any delayed custom spawn too. The saved choices and retained
        // random roll survive both transitions; helm removal is temporary.
        request_reconcile(
            ship_control ? STR("Caribbean helm entered") : STR("Caribbean helm exited"), false);
        next_reconcile_attempt_ = std::chrono::steady_clock::now();
        Output::send<LogLevel::Normal>(
            STR("[KH3APProbeCpp] Caribbean helm party override active={} pawn={}\n"), ship_control,
            sora_pawn->GetFullName());
    }

    auto RuntimePartyMemberService::effective_map_policy(UObject* level) const
        -> RuntimePartyMapPolicy
    {
        if (!is_uobject_valid_fast(level))
        {
            return RuntimePartyMapPolicy::managed;
        }
        const std::wstring level_name = level->GetFullName();
        if (ship_control_active_ && level_name.find(L"/Game/Levels/ca/") != std::wstring_view::npos)
        {
            const auto settings = effective_settings();
            return runtime_party_ship_policy(true, settings.party_randomization_enabled,
                                             settings.party_friend_1_choice,
                                             settings.party_friend_2_choice);
        }
        return observed_party_map_policy(level, config_.runtime_state);
    }

    auto RuntimePartyMemberService::observe_sora_context_ready() -> void
    {
        if (!installed_ || !active_)
        {
            return;
        }
        maintenance_.request();
        refresh_ship_control();
        auto& pawn_state = config_.runtime_state.grant_state().pawn_state();
        UObject* const pawn = pawn_state.candidate_trusted() ? pawn_state.object() : nullptr;
        if (!pawn || !is_uobject_valid_fast(pawn) || context_entry_pawn_.Get() == pawn)
        {
            return;
        }
        UObject* const level = persistent_level_for(pawn);
        if (!level || !runtime_party_requires_context_reconcile(level->GetFullName()))
        {
            return;
        }
        const auto settings = effective_settings();
        if (!settings.party_randomization_enabled &&
            settings.party_friend_1_choice == vanilla_choice &&
            settings.party_friend_2_choice == vanilla_choice)
        {
            return;
        }
        context_entry_pawn_ = pawn;
        // This arena can omit SetFriendAll and the map-name event. Queue
        // one transaction for the new Sora, including checkpoint reloads.
        // A subsequent native SetFriendAll supersedes this fallback normally.
        request_reconcile(STR("new Sora party entry"),
                          settings.party_randomization_enabled && settings.party_reroll_every_room);
        awaiting_entry_control_ = true;
        next_reconcile_attempt_ =
            std::max(std::chrono::steady_clock::now(),
                     pawn_state.first_seen_at() + RuntimeGrantPawnState::stability_delay);
    }
} // namespace kh3ap
