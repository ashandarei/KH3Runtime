#include "kh3ap/features/player/characters/character_swap_assets.h"
#include "kh3ap/features/player/combat/character_magic.h"
#include "kh3ap/features/player/characters/character_swap_effects.h"
#include "kh3ap/features/player/combat/character_shotlock.h"
#include "kh3ap/features/player/movement/character_rails.h"
#include "kh3ap/features/player/movement/character_traversal.h"
#include "kh3ap/features/player/combat/character_underwater_combat.h"
#include "kh3ap/features/player/combat/character_union_cross.h"
#include "kh3ap/features/player/combat/kairi_ground_slam.h"
#include "kh3ap/features/player/characters/kairi_idle.h"
#include "kh3ap/features/player/player_animation_support.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/features/kh02/compatibility/kh02_aliases.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include "kh3ap/features/world/space/space_service.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UScriptStruct.hpp>
#include <Unreal/Property/FStructProperty.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
#include <nlohmann/json.hpp>
#include "kh3ap/bridge/utilities.h"
#endif
#define NOMINMAX
#include <Windows.h>
#include <array>
#include <vector>
#include <string>
#include <exception>

namespace kh3ap::character_swap_assets
{
    using namespace RC::Unreal;
    namespace
    {
        using Clock = std::chrono::steady_clock;
        using player_animation_support::PlayerId;
        constexpr auto preload_timeout = std::chrono::seconds(30);
        // Distinct latent action UUIDs on the current pawn, including reselection.
        constexpr uint32_t request_uuid_prefix = 0x4b4a0000;
        constexpr uint32_t request_sequence_mask = 0xffff;
        constexpr auto completion_name = L"KH3CharacterSwapAssetLoaded";

        struct Character
        {
            PlayerId id;
            const wchar_t* pawn_class;
            const wchar_t* weapon_class{};
            const wchar_t* weapon_equipment{};
        };
        // Native player-factory classes, verified against actual possessed pawns.
        constexpr std::array characters{
            Character{PlayerId::Aqua,
                      L"/Game/Blueprints/Player/p_ex002/p_ex002_Pawn.p_ex002_Pawn_C",
                      L"/Game/Blueprints/Player/p_ex002/w_aq010_Wep.w_aq010_Wep_C",
                      L"/Game/Blueprints/Player/p_ex002/w_aq010_Equip.w_aq010_Equip"},
            Character{PlayerId::Riku,
                      L"/Game/Blueprints/Player/p_ex004/p_ex004_Pawn.p_ex004_Pawn_C",
                      L"/Game/Blueprints/Player/p_ex003/w_ex120_Wep.w_ex120_Wep_C",
                      L"/Game/Blueprints/Player/p_ex003/w_ex120_Equip.w_ex120_Equip"},
            Character{PlayerId::Kairi,
                      L"/Game/Blueprints/Player/p_ex006/p_ex006_Pawn.p_ex006_Pawn_C"},
            Character{PlayerId::Roxas,
                      L"/Game/Blueprints/Player/p_ex007/p_ex007_Pawn.p_ex007_Pawn_C"},
            Character{PlayerId::KingMickey,
                      L"/Game/Blueprints/ReMind/MiRx/p_ex008/p_ex008_MiRxPawn.p_ex008_MiRxPawn_C"}};

        struct RootLease
        {
            std::vector<FWeakObjectPtr> roots;
            void retain(UObject* asset)
            {
                if (is_uobject_valid_fast(asset) &&
                    !asset->HasAnyInternalFlags(EInternalObjectFlags::RootSet))
                {
                    asset->SetRootSet();
                    roots.push_back(make_uobject_weak(asset));
                }
            }
            void release()
            {
                for (const auto& root : roots)
                {
                    if (auto* asset = root.Get(); is_uobject_valid_fast(asset))
                    {
                        asset->ClearRootSet();
                    }
                }
                roots.clear();
            }
        };
        struct Asset
        {
            const wchar_t* path;
            bool is_class;
            bool ready{};
        };
        int selected_id{-1};
        bool selection_pending{};
        bool handoff_pending{};
        bool loading{};
        bool completion_pending{};
        uint32_t request_sequence{};
        DWORD game_thread{};
        Clock::time_point started{};
        Clock::time_point deadline{};
        FWeakObjectPtr request_pawn;
        FWeakObjectPtr request_world;
        RootLease selected_assets;
        RootLease return_class;
        std::array<FWeakObjectPtr, 3> return_equipment;
        std::vector<Asset> assets;
        Kh02NativeAliases completion_alias;
        std::string last_result{"off"};
        uint64_t completion_count{};
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        uint64_t handoff_count{};
#endif

        UObject* object(UObject* owner, const wchar_t* field)
        {
            return is_uobject_valid_fast(owner)
                       ? try_read_reflected_object_property_value(owner, field)
                       : nullptr;
        }

        UObject* current_pawn()
        {
            auto* manager =
                object(object(kh02_current_viewport(), L"GameInstance"), L"m_UIManager");
            auto* controller = object(manager, L"m_pPlayerController");
            auto* pawn = object(controller, L"Pawn");
            return pawn && object(pawn, L"Controller") == controller &&
                           kh02_actor_world(pawn) == kh02_current_world()
                       ? pawn
                       : nullptr;
        }

        bool is_sora(UObject* pawn)
        {
            auto* player_id = is_uobject_valid_fast(pawn)
                                  ? pawn->GetPropertyByNameInChain(L"m_PlayerUniqueID")
                                  : nullptr;
            return player_id && player_id->GetElementSize() == sizeof(uint8_t) &&
                   *player_id->ContainerPtrToValuePtr<uint8_t>(pawn) <
                       static_cast<uint8_t>(PlayerId::Riku);
        }

        void asset_loaded(UObject* context, FFrame&, void*)
        {
            if (GetCurrentThreadId() == game_thread && loading && request_pawn.Get() == context)
            {
                completion_pending = true;
                ++completion_count;
            }
        }

        void resolve_assets()
        {
            bool all_ready = true;
            for (auto& asset : assets)
            {
                if (asset.ready)
                {
                    continue;
                }
                auto* loaded =
                    asset.is_class
                        ? static_cast<UObject*>(find_loaded_runtime_asset_class(asset.path))
                        : find_loaded_runtime_asset(asset.path);
                asset.ready = is_uobject_valid_fast(loaded);
                if (asset.ready)
                {
                    // Keep dependencies resident through GC and map travel until
                    // the user changes this selection or turns the feature off.
                    selected_assets.retain(loaded);
                }
                all_ready = all_ready && asset.ready;
            }
            if (all_ready)
            {
                if (selected_id == static_cast<int>(PlayerId::Kairi))
                {
                    // Detour construction belongs to selection-time setup,
                    // not Kairi's first possessed frame. Pawn preparation
                    // retains its existing native fallback if setup fails.
                    kairi_idle::initialize();
                    kairi_ground_slam::initialize();
                }
                loading = false;
                last_result = "ready";
                character_swap_effects::selected_class_ready(
                    find_loaded_runtime_asset_class(assets.front().path));
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
                RC::Output::send<RC::LogLevel::Normal>(
                    STR("[KH3APProbeCpp] CharacterPreload ready id={} assets={} elapsed_us={} "
                        "callbacks={}\n"),
                    selected_id, assets.size(),
                    std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - started)
                        .count(),
                    completion_count);
#endif
            }
        }

        void start(UObject* pawn, Clock::time_point now)
        {
            if (!player_animation_support::supported_skeleton_layout())
            {
                selection_pending = false;
                last_result = "unsupported_build";
                return;
            }
            // The native map-start pawn supplies the actual Sora variant and
            // equipped Keyblades. Preserve their loaded graphs before restoring
            // the selected alternate, rather than loading them on the first R2.
            if (is_sora(pawn))
            {
                retain_return_class(pawn);
            }
            if (completion_alias.empty())
            {
                completion_alias.add(
                    L"/Script/Engine.Actor", completion_name,
                    L"/Script/Engine.PrimitiveComponent:SetTranslucentSortPriority", asset_loaded);
            }
            game_thread = GetCurrentThreadId();
            request_pawn = make_uobject_weak(pawn);
            request_world = make_uobject_weak(kh02_actor_world(pawn));
            loading = true;
            selection_pending = false;
            handoff_pending = false;
            completion_pending = false;
            started = now;
            last_result = "loading";
            // Map travel or a player handoff may have canceled latent actions.
            // Re-resolve and request only absent assets on this new event.
            for (auto& asset : assets)
            {
                asset.ready = false;
            }
            resolve_assets();
            RuntimeAssetLoadCallbackScope completion(completion_name);
            for (const auto& asset : assets)
            {
                if (asset.ready)
                {
                    continue;
                }
                const auto uuid = static_cast<int32_t>(
                    request_uuid_prefix | (++request_sequence & request_sequence_mask));
                const bool requested =
                    asset.is_class
                        ? request_runtime_asset_class_load(pawn, asset.path, nullptr, uuid)
                        : request_runtime_asset_load(pawn, asset.path, nullptr, uuid);
                if (!requested)
                {
                    loading = false;
                    last_result = "request_failed";
                    RC::Output::send<RC::LogLevel::Warning>(
                        STR("[KH3APProbeCpp] CharacterPreload request failed id={} path={}\n"),
                        selected_id, asset.path);
                    break;
                }
            }
        }
    } // namespace

    auto pawn_key_for_class(std::wstring_view class_name) -> std::wstring_view
    {
        for (const auto& character : characters)
        {
            const std::wstring_view path{character.pawn_class};
            if (class_name == path.substr(path.rfind(L'.') + 1))
            {
                // Both p_ex002_Pawn_C and p_ex008_MiRxPawn_C use the
                // model ID before the second underscore as their palette key.
                return class_name.substr(0, class_name.find(L'_', 2));
            }
        }
        return {};
    }

    void select(int player_id)
    {
        // Reselecting an already prepared character needs no new work.
        if (selected_id == player_id)
        {
            return;
        }
        selected_id = player_id;
        selection_pending = false;
        handoff_pending = false;
        loading = false;
        completion_pending = false;
        selected_assets.release();
        assets.clear();
        last_result = "off";
        for (const auto& character : characters)
        {
            if (static_cast<int>(character.id) != player_id)
            {
                continue;
            }
            assets.push_back({character.pawn_class, true});
            // These weapons are selected by the native factory at runtime,
            // rather than referenced by their pawn classes. Preload their
            // models, effects and sounds before the first requested swap.
            if (character.weapon_class)
            {
                assets.push_back({character.weapon_class, true});
                assets.push_back({character.weapon_equipment, false});
            }
            assets.push_back({player_animation_support::sora_animation_set_path, false});
            assets.push_back({player_animation_support::sora_attack_data_path, false});
            assets.push_back({character_magic::sora_magic_set_path, false});
            assets.push_back({character_underwater_combat::animation_set_path, false});
            assets.push_back({character_union_cross::animation_set_path, false});
            assets.push_back({character_traversal::gigas_animation_set_path, false});
            assets.push_back({L"/Game/Blueprints/Player/AthleticFlowRootEffect.AthleticFlowRootEffect_C", true});
            assets.push_back({L"/Game/Effects/PC/Free_f/par/vp_ffa_arv0.vp_ffa_arv0", false});
            assets.push_back({character_underwater_combat::vertical_projectile_path, true});
            assets.push_back({character_underwater_combat::pulse_projectile_path, true});
            if (character.id == PlayerId::Aqua || character.id == PlayerId::Kairi || character.id == PlayerId::Roxas)
            {
                assets.push_back({character_rails::shot_projectile_path, true});
            }
            if (character.id == PlayerId::Roxas)
            {
                assets.push_back({character_shotlock::oblivion_max_animation_set_path, false});
                assets.push_back({character_shotlock::oblivion_projectile_class_path, true});
                assets.push_back({character_shotlock::oblivion_max_projectile_class_path, true});
            }
            if (character.id == PlayerId::Kairi)
            {
                assets.push_back({player_animation_support::kairi_idle_animation_set_path, false});
                assets.push_back({player_animation_support::kairi_attack_data_path, false});
            }
            selection_pending = true;
            deadline = Clock::now() + preload_timeout;
            last_result = "waiting_for_context";
            break;
        }
        if (assets.empty() && is_sora(current_pawn()))
        {
            // With the feature off, playable Sora owns these dependencies.
            // An alternate still needs its lease until the return completes.
            release_return_class();
        }
    }

    bool preparation_pending(int player_id)
    {
        return selected_id == player_id && (selection_pending || loading);
    }

    void notify_player_started()
    {
        if (!assets.empty())
        {
            selection_pending = true;
            handoff_pending = false;
            loading = false;
            deadline = Clock::now() + preload_timeout;
        }
    }

    void notify_player_swap_started(UObject* outgoing_pawn)
    {
        if (!loading || request_pawn.Get() != outgoing_pawn ||
            request_world.Get() != kh02_current_world())
        {
            return;
        }
        // Latent asset callbacks belong to their requesting pawn. The player
        // factory retires that pawn even though this map and selection remain
        // active. Pause before the factory destroys it: possession can still
        // refer to the outgoing pawn when the factory returns. Re-resolve once
        // the incoming pawn is published, requesting only missing assets.
        // Preserve the original deadline across any number of handoffs.
        loading = false;
        completion_pending = false;
        selection_pending = true;
        handoff_pending = true;
        last_result = "waiting_for_handoff";
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        ++handoff_count;
#endif
    }

    void tick(Clock::time_point now)
    {
        try
        {
            // No world/pawn/asset discovery after completion, failure or cancellation.
            if (!selection_pending && !loading)
            {
                return;
            }
            if (now >= deadline)
            {
                selection_pending = false;
                loading = false;
                last_result = "timeout";
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("[KH3APProbeCpp] CharacterPreload timed out id={}\n"), selected_id);
                return;
            }
            if (selection_pending)
            {
                if (handoff_pending && request_world.Get() != kh02_current_world())
                {
                    selection_pending = false;
                    handoff_pending = false;
                    last_result = "context_lost";
                    return;
                }
                // Selection can occur with the options menu paused; loading does not
                // require gameplay input. Never enter the separate KH0.2/space modes.
                auto* pawn = current_pawn();
                if (pawn && (!handoff_pending || pawn != request_pawn.Get()) &&
                    !kh02_current_story_world() && !RuntimeSpaceService::is_space_active())
                {
                    start(pawn, now);
                }
                return;
            }
            if (!request_pawn.Get() || request_world.Get() != kh02_current_world())
            {
                loading = false;
                last_result = "context_lost";
                return;
            }
            if (completion_pending)
            {
                resolve_assets();
                // A latent completion can precede final object-hash/postload
                // readiness. Retry only the unresolved entries of this bounded
                // request, stopping on completion, cancellation or its deadline.
                completion_pending = loading;
            }
        }
        catch (const std::exception& failure)
        {
            selection_pending = false;
            loading = false;
            last_result = failure.what();
            RC::Output::send<RC::LogLevel::Warning>(
                STR("[KH3APProbeCpp] CharacterPreload failed id={} reason={}\n"), selected_id,
                std::wstring(last_result.begin(), last_result.end()));
        }
    }

    void retain_return_class(UObject* sora)
    {
        release_return_class();
        if (is_uobject_valid_fast(sora))
        {
            return_class.retain(sora->GetClassPrivate());
            auto* equipment = object(sora, L"MyEquipment");
            auto* slots = is_uobject_valid_fast(equipment)
                              ? equipment->GetPropertyByNameInChain(L"m_EquipSlots")
                              : nullptr;
            auto* schema = slots && slots->IsA<FStructProperty>()
                               ? static_cast<FStructProperty*>(slots)->GetStruct()
                               : nullptr;
            auto* asset_field = is_uobject_valid_fast(schema)
                                   ? find_struct_field_by_name(schema, L"m_pAsset")
                                   : nullptr;
            if (!slots || slots->GetArrayDim() != static_cast<int>(return_equipment.size()) ||
                !is_uobject_valid_fast(schema) ||
                schema->GetName() != L"TresEquipWeaponSlotWork" ||
                slots->GetElementSize() < static_cast<int>(sizeof(UObject*)) ||
                slots->GetElementSize() != schema->GetStructureSize() || !asset_field ||
                asset_field->GetClass().GetName() != L"ObjectProperty" ||
                asset_field->GetElementSize() != sizeof(UObject*) ||
                asset_field->GetOffset_Internal() < 0 ||
                asset_field->GetOffset_Internal() >
                    slots->GetElementSize() - static_cast<int>(sizeof(UObject*)))
            {
                // Missing optional optimization must not prevent a player swap.
                return;
            }
            for (int index = 0; index < static_cast<int>(return_equipment.size()); ++index)
            {
                auto* slot = slots->ContainerPtrToValuePtr<void>(equipment, index);
                auto* asset = *asset_field->ContainerPtrToValuePtr<UObject*>(slot);
                if (is_uobject_valid_fast(asset))
                {
                    // EquipData owns every form's weapons, animation, magic,
                    // projectiles and UI. Retaining only the active weapon class
                    // lets the other equipped bundles be collected while away.
                    return_class.retain(asset);
                    return_equipment[index] = make_uobject_weak(asset);
                }
            }
        }
    }

    void release_return_class()
    {
        return_class.release();
        return_equipment = {};
    }

    void retain_return_dependency(UObject* asset)
    {
        return_class.retain(asset);
    }

#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    nlohmann::json snapshot()
    {
        auto equipment = nlohmann::json::array();
        for (const auto& reference : return_equipment)
        {
            auto* asset = reference.Get();
            equipment.push_back(is_uobject_valid_fast(asset) ? to_utf8(asset->GetPathName()) : "");
        }
        auto paths = nlohmann::json::array();
        for (const auto& asset : assets)
        {
            paths.push_back({{"path", to_utf8(asset.path)}, {"ready", asset.ready}});
        }
        return {{"selected_id", selected_id},
                {"pending", selection_pending || loading},
                {"result", last_result},
                {"callbacks", completion_count},
                {"handoffs", handoff_count},
                {"assets", paths},
                {"owned_roots", selected_assets.roots.size()},
                {"return_owned_roots", return_class.roots.size()},
                {"return_equipment", equipment}};
    }
#endif
} // namespace kh3ap::character_swap_assets
