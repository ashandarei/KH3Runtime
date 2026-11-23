#include "kh3ap/core/objects/actor_owning_world.h"
#include "kh3ap/features/player/movement/alternate_wall_run.h"
#include "kh3ap/features/player/player_animation_support.h"
#include "kh3ap/features/player/characters/character_swap.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_weak.h"
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
#include "kh3ap/core/input/runtime_controller_input.h"
#include "kh3ap/core/input/runtime_player_control.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include "kh3ap/core/reflection/reflected_function_call.h"
#endif
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UScriptStruct.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <Unreal/Property/FStructProperty.hpp>
#include <Unreal/TArray.hpp>
#include <Unreal/FScriptArray.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#define NOMINMAX
#include <Windows.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <stdexcept>
#include <cmath>
#include <vector>

namespace kh3ap::alternate_wall_run
{
    using namespace RC::Unreal;
    namespace
    {
        constexpr std::array animation_names{
            L"WallRunF", L"WallRunR", L"WallRunL", L"WallRunLoopF", L"WallRunLoopR",
            L"WallRunLoopL", L"WallRunRollF", L"WallRunRollR", L"WallRunRollL", L"WallRunChangeR",
            L"WallRunChangeRF", L"WallRunChangeFR", L"WallRunChangeL", L"WallRunChangeLF",
            L"WallRunChangeFL", L"WallRunJump", L"WallRunJumpH", L"WallRunJumpF",
            // WallRun also plays ClimbingH as the player crests a wall.
            L"ClimbingR", L"ClimbingRB", L"ClimbingL", L"ClimbingLB", L"ClimbingMR", L"ClimbingMRB",
            L"ClimbingML", L"ClimbingMLB", L"ClimbingH", L"ClimbingHB",
            // Dangle has its own hang and pull-up animations.
            L"GripStart", L"GripLoop", L"GripRise"};
        using player_animation_support::AnimationEntry;
        using player_animation_support::PlayerId;
        constexpr std::array supported_players{PlayerId::Riku, PlayerId::Kairi, PlayerId::Roxas,
                                               PlayerId::Aqua};
        constexpr std::array voice_source_roots{
            L"/Game/Character/pc/p_ex003/", L"/Game/Character/pc/p_ex006/",
            L"/Game/Character/pc/p_ex007/", L"/Game/Character/pc/p_ex002/"};
        constexpr auto asset_load_timeout = std::chrono::seconds(4);
        // Reserved latent request ID for Sora's animation set.
        constexpr int animation_asset_request_id = 0x4b4833c0;
        // Shared sanity bound for the copied speed scales and speed in world units/second.
        constexpr float maximum_movement_value = 10000.0f;
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        constexpr auto diagnostic_move_duration = std::chrono::seconds(2);
        constexpr auto screenshot_time_before_release = std::chrono::milliseconds(1200);
        constexpr size_t maximum_screenshot_parameter_bytes = 256;
        constexpr auto diagnostic_screenshot_command =
            L"HighResShot 1920x1080 filename=wall-run-latest.png";
#endif
        struct Prepared
        {
            FWeakObjectPtr pawn;
            FWeakObjectPtr animation_instance;
            FWeakObjectPtr movement_voice;
            std::vector<FWeakObjectPtr> borrowed_movement_animations;
        };
        std::array<Prepared, supported_players.size()> prepared;
        constexpr std::array speed_fields{L"MyWallRunSpeedScaleUp", L"MyWallRunSpeedScaleSide",
                                          L"MyWallRunSideMoveSpeed"};
        std::array<float, speed_fields.size()> sora_speeds{};
        using DangleOffset = std::array<float, 3>;
        DangleOffset sora_dangle_offset{};
        bool speeds_captured{};
        bool sora_climbing_enabled{};
        FWeakObjectPtr requested_pawn;
        std::chrono::steady_clock::time_point deadline{};

        UObject* object_property(UObject* owner, const wchar_t* field)
        {
            return is_uobject_valid_fast(owner)
                       ? try_read_reflected_object_property_value(owner, field)
                       : nullptr;
        }

        UObject* movement_voice(UObject* animation_instance, size_t player_index)
        {
            auto* sets = animation_instance->GetPropertyByNameInChain(L"MyAnimSets");
            if (!sets || !sets->IsA<FArrayProperty>() ||
                static_cast<FArrayProperty*>(sets)->GetInner()->GetElementSize() !=
                    sizeof(UObject*))
            {
                throw std::runtime_error("alternate_climbing_voice_sets_layout");
            }
            for (auto* set : *sets->ContainerPtrToValuePtr<TArray<UObject*>>(animation_instance))
            {
                auto* entries =
                    is_uobject_valid_fast(set) ? set->GetPropertyByNameInChain(L"Anims") : nullptr;
                if (!entries || !entries->IsA<FArrayProperty>() ||
                    static_cast<FArrayProperty*>(entries)->GetInner()->GetElementSize() !=
                        sizeof(AnimationEntry))
                {
                    continue;
                }
                for (const auto& entry :
                     *entries->ContainerPtrToValuePtr<TArray<AnimationEntry>>(set))
                {
                    if (entry.name != FName(L"AirSlide", FNAME_Add) ||
                        !is_uobject_valid_fast(entry.asset) ||
                        !entry.asset->GetPathName().starts_with(voice_source_roots[player_index]))
                    {
                        continue;
                    }
                    auto* notifies = entry.asset->GetPropertyByNameInChain(L"Notifies");
                    if (!notifies || !notifies->IsA<FArrayProperty>())
                    {
                        throw std::runtime_error("alternate_climbing_voice_notifies_layout");
                    }
                    auto* inner =
                        const_cast<FProperty*>(static_cast<FArrayProperty*>(notifies)->GetInner());
                    if (!inner || !inner->IsA<FStructProperty>())
                    {
                        throw std::runtime_error("alternate_climbing_voice_event_layout");
                    }
                    auto* schema = static_cast<FStructProperty*>(inner)->GetStruct();
                    auto* notify_field = find_struct_field_by_name(schema, L"Notify");
                    if (!notify_field || notify_field->GetClass().GetName() != L"ObjectProperty" ||
                        notify_field->GetElementSize() != sizeof(UObject*))
                    {
                        throw std::runtime_error("alternate_climbing_voice_notify_layout");
                    }
                    auto* events = notifies->ContainerPtrToValuePtr<FScriptArray>(entry.asset);
                    if (events->Num() < 0 || events->Num() > 256)
                    {
                        throw std::runtime_error("alternate_climbing_voice_event_count");
                    }
                    for (int index = 0; index < events->Num(); ++index)
                    {
                        auto* event = static_cast<std::byte*>(events->GetData()) +
                                      index * inner->GetElementSize();
                        auto* notify = *notify_field->ContainerPtrToValuePtr<UObject*>(event);
                        if (is_uobject_valid_fast(notify) &&
                            notify->GetOuterPrivate() == entry.asset &&
                            notify->GetClassPrivate()->GetName() == L"TresAnimNotify_PlayVoice")
                        {
                            return notify;
                        }
                    }
                }
            }
            throw std::runtime_error("alternate_climbing_movement_voice_missing");
        }

        int prepared_player_index(UObject* pawn)
        {
            auto* field = is_uobject_valid_fast(pawn)
                              ? pawn->GetPropertyByNameInChain(L"m_PlayerUniqueID")
                              : nullptr;
            if (!field || field->GetElementSize() != sizeof(uint8_t))
            {
                return -1;
            }
            const auto player_id =
                static_cast<PlayerId>(*field->ContainerPtrToValuePtr<uint8_t>(pawn));
            for (size_t index = 0; index < supported_players.size(); ++index)
            {
                if (supported_players[index] == player_id)
                {
                    return static_cast<int>(index);
                }
            }
            return -1;
        }

        auto dangle_offset(UObject* pawn) -> DangleOffset&
        {
            auto* field = is_uobject_valid_fast(pawn)
                              ? pawn->GetPropertyByNameInChain(L"m_DangleOffsetPos")
                              : nullptr;
            if (!field || !field->IsA<FStructProperty>() ||
                field->GetElementSize() != sizeof(DangleOffset) ||
                static_cast<FStructProperty*>(field)->GetStruct()->GetName() != L"Vector")
            {
                throw std::runtime_error("alternate_dangle_offset_layout");
            }
            return *field->ContainerPtrToValuePtr<DangleOffset>(pawn);
        }

    } // namespace

    UObject* replacement_voice(UObject* notify, UObject* mesh, UObject* animation)
    {
        if (!is_uobject_valid_fast(notify) || !is_uobject_valid_fast(animation) ||
            notify->GetOuterPrivate() != animation)
        {
            return nullptr;
        }
        for (const auto& entry : prepared)
        {
            auto* pawn = entry.pawn.Get();
            auto* voice = entry.movement_voice.Get();
            auto* instance = entry.animation_instance.Get();
            if (!pawn || !voice || !instance || character_swap::current_hero() != pawn ||
                object_property(pawn, L"MyMesh") != mesh ||
                object_property(mesh, L"AnimScriptInstance") != instance ||
                object_property(object_property(pawn, L"Controller"), L"Pawn") != pawn ||
                !std::ranges::any_of(entry.borrowed_movement_animations,
                                     [&](const auto& borrowed)
                                     {
                                         return borrowed.Get() == animation;
                                     }))
            {
                continue;
            }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            RC::Output::send<RC::LogLevel::Normal>(
                STR("[KH3APProbeCpp] Alternate movement voice pawn={} animation={} source={}\n"),
                pawn->GetName(), animation->GetPathName(), voice->GetPathName());
#endif
            return voice;
        }
        return nullptr;
    }

    bool ready(UObject* pawn)
    {
        const auto index = prepared_player_index(pawn);
        return index >= 0 && prepared[index].pawn.Get() == pawn &&
               prepared[index].animation_instance.Get() ==
                   object_property(object_property(pawn, L"MyMesh"), L"AnimScriptInstance") &&
               prepared[index].animation_instance.Get() != nullptr;
    }

    void refresh_unlocks(UObject* pawn, bool enabled)
    {
        if (ready(pawn) && !write_bool_function_param(
                               pawn->GetPropertyByNameInChain(L"m_bEnableWallRun"), pawn, enabled))
        {
            throw std::runtime_error("alternate_wall_run_unlock_readback");
        }
    }

    void capture_sora(UObject* pawn)
    {
        speeds_captured = false;
        auto* climbing = is_uobject_valid_fast(pawn)
                             ? pawn->GetPropertyByNameInChain(L"m_bEnableClimbing")
                             : nullptr;
        if (!climbing || !climbing->IsA<FBoolProperty>())
        {
            throw std::runtime_error("alternate_climbing_source_flag_layout");
        }
        sora_climbing_enabled =
            static_cast<FBoolProperty*>(climbing)->GetPropertyValueInContainer(pawn);
        sora_dangle_offset = dangle_offset(pawn);
        if (!std::ranges::all_of(sora_dangle_offset,
                                 [](float value)
                                 {
                                     return std::isfinite(value);
                                 }))
        {
            throw std::runtime_error("alternate_dangle_offset_value");
        }
        for (size_t index = 0; index < speed_fields.size(); ++index)
        {
            auto* field = is_uobject_valid_fast(pawn)
                              ? pawn->GetPropertyByNameInChain(speed_fields[index])
                              : nullptr;
            if (!field || field->GetClass().GetName() != L"FloatProperty" ||
                field->GetElementSize() != sizeof(float))
            {
                throw std::runtime_error("alternate_wall_run_source_speed_layout");
            }
            const auto speed = *field->ContainerPtrToValuePtr<float>(pawn);
            if (!std::isfinite(speed) || speed <= 0 || speed > maximum_movement_value)
            {
                throw std::runtime_error("alternate_wall_run_source_speed_range");
            }
            sora_speeds[index] = speed;
        }
        speeds_captured = true;
    }

    bool prepare(UObject* pawn)
    {
        const auto index = prepared_player_index(pawn);
        if (index < 0 || ready(pawn))
        {
            return true;
        }
        if (!player_animation_support::supported_skeleton_layout())
        {
            return true;
        }
        auto* animation_instance =
            object_property(object_property(pawn, L"MyMesh"), L"AnimScriptInstance");
        if (!is_uobject_valid_fast(animation_instance))
        {
            return false;
        }
        auto* source = find_loaded_runtime_asset(player_animation_support::sora_animation_set_path);
        if (!is_uobject_valid_fast(source))
        {
            if (requested_pawn.Get() != pawn)
            {
                requested_pawn = make_uobject_weak(pawn);
                deadline = std::chrono::steady_clock::now() + asset_load_timeout;
                request_runtime_asset_load(pawn, player_animation_support::sora_animation_set_path,
                                           nullptr, animation_asset_request_id);
            }
            return std::chrono::steady_clock::now() >= deadline;
        }
        auto* entries = source->GetPropertyByNameInChain(L"Anims");
        auto* sets = animation_instance->GetPropertyByNameInChain(L"MyAnimSets");
        if (!entries || !entries->IsA<FArrayProperty>() ||
            static_cast<FArrayProperty*>(entries)->GetInner()->GetElementSize() !=
                sizeof(AnimationEntry) ||
            !sets || !sets->IsA<FArrayProperty>() ||
            static_cast<FArrayProperty*>(sets)->GetInner()->GetElementSize() != sizeof(UObject*))
        {
            throw std::runtime_error("alternate_wall_run_animset_layout");
        }
        TArray<AnimationEntry>* aqua_vaults{};
        if (supported_players[index] == PlayerId::Kairi)
        {
            // Kairi selection preloads Aqua for exploration locomotion. Reuse
            // her authored obstacle vaults where she has the same native name;
            // keep the shared wall-run/hanging motions and unmatched variants.
            auto* aqua = find_loaded_runtime_asset(player_animation_support::aqua_animation_set_path);
            auto* aqua_entries = is_uobject_valid_fast(aqua) ? aqua->GetPropertyByNameInChain(L"Anims") : nullptr;
            if (!aqua_entries || !aqua_entries->IsA<FArrayProperty>() ||
                static_cast<FArrayProperty*>(aqua_entries)->GetInner()->GetElementSize() != sizeof(AnimationEntry))
            {
                throw std::runtime_error("kairi_vault_animation_layout");
            }
            aqua_vaults = aqua_entries->ContainerPtrToValuePtr<TArray<AnimationEntry>>(aqua);
        }
        std::array<AnimationEntry, animation_names.size()> animations{};
        Prepared binding{make_uobject_weak(pawn),
                         make_uobject_weak(animation_instance),
                         make_uobject_weak(movement_voice(animation_instance, index)),
                         {}};
        for (size_t index = 0; index < animation_names.size(); ++index)
        {
            const FName name(animation_names[index], FNAME_Add);
            for (const auto& entry :
                 *entries->ContainerPtrToValuePtr<TArray<AnimationEntry>>(source))
            {
                if (entry.name == name)
                {
                    animations[index] = entry;
                    break;
                }
            }
            if (aqua_vaults && std::wstring_view(animation_names[index]).starts_with(L"Climbing"))
            {
                for (const auto& entry : *aqua_vaults)
                {
                    if (entry.name == name)
                    {
                        animations[index] = entry;
                        break;
                    }
                }
            }
            if (!is_uobject_valid_fast(animations[index].asset))
            {
                throw std::runtime_error("alternate_wall_run_animation_missing");
            }
            player_animation_support::allow_animation_skeleton(animation_instance,
                                                               animations[index].asset);
            // Climbing* contains the obstacle vaults. Register the actual
            // donor so Kairi keeps her own voice with Aqua's clips as well.
            // Do not redirect wall-run, hang or attack cues.
            if (std::wstring_view(animation_names[index]).starts_with(L"Climbing") &&
                !std::ranges::any_of(binding.borrowed_movement_animations,
                                     [&](const auto& asset)
                                     {
                                         return asset.Get() == animations[index].asset;
                                     }))
            {
                binding.borrowed_movement_animations.push_back(
                    make_uobject_weak(animations[index].asset));
            }
        }
        // Traversal supplies Sora's AirDodge where the alternate leaves it
        // empty. Register only the donor identities here; do not replace the
        // character's authored AirSlide/AirSlideB animations or voice notifies.
        // SuperSlideLoop owns Airstep's zero-delay TresAnimNotifyState_PlayVoice;
        // the shared hook handles its Begin separately from one-shot notifies.
        const std::array air_movement_names{
            FName(L"AirDodge", FNAME_Add), FName(L"AirSlide", FNAME_Add),
            FName(L"AirSlideB", FNAME_Add), FName(L"SuperSlideLoop", FNAME_Add)};
        for (const auto& entry : *entries->ContainerPtrToValuePtr<TArray<AnimationEntry>>(source))
        {
            if (std::ranges::find(air_movement_names, entry.name) != air_movement_names.end() &&
                is_uobject_valid_fast(entry.asset) &&
                !std::ranges::any_of(binding.borrowed_movement_animations,
                                     [&](const auto& asset)
                                     {
                                         return asset.Get() == entry.asset;
                                     }))
            {
                binding.borrowed_movement_animations.push_back(make_uobject_weak(entry.asset));
            }
        }
        if (!player_animation_support::initialize_voice_notify())
        {
            throw std::runtime_error("alternate_climbing_voice_hook_unavailable");
        }
        FStaticConstructObjectParameters parameters(source->GetClassPrivate(), animation_instance);
        parameters.SetFlags = RF_Transient;
        auto* private_set = UObjectGlobals::StaticConstructObject(parameters);
        if (!is_uobject_valid_fast(private_set))
        {
            throw std::runtime_error("alternate_wall_run_animset_create");
        }
        auto* destination = entries->ContainerPtrToValuePtr<TArray<AnimationEntry>>(private_set);
        for (const auto& animation : animations)
        {
            destination->Add(animation);
        }
        // Start disabled until the possession/unlock event resolves Sora's
        // equipped ability. Retain the game's shared ability gate as well.
        if (!write_bool_function_param(pawn->GetPropertyByNameInChain(L"m_bEnableWallRun"), pawn,
                                       false))
        {
            throw std::runtime_error("alternate_wall_run_enable_readback");
        }
        if (!speeds_captured)
        {
            throw std::runtime_error("alternate_wall_run_source_speed_missing");
        }
        for (size_t index = 0; index < speed_fields.size(); ++index)
        {
            auto* field = pawn->GetPropertyByNameInChain(speed_fields[index]);
            if (!field || field->GetClass().GetName() != L"FloatProperty" ||
                field->GetElementSize() != sizeof(float))
            {
                throw std::runtime_error("alternate_wall_run_target_speed_layout");
            }
            *field->ContainerPtrToValuePtr<float>(pawn) = sora_speeds[index];
        }
        if (!write_bool_function_param(pawn->GetPropertyByNameInChain(L"m_bEnableClimbing"), pawn,
                                       sora_climbing_enabled))
        {
            throw std::runtime_error("alternate_climbing_enable_readback");
        }
        // GripStart and GripLoop use Sora's reach. The hang point must use
        // the same anchor as the borrowed pose for the hand to meet the lip.
        dangle_offset(pawn) = sora_dangle_offset;
        // Publish the complete per-instance set once its native flags and tuning are ready.
        sets->ContainerPtrToValuePtr<TArray<UObject*>>(animation_instance)->Add(private_set);
        prepared[index] = std::move(binding);
        RC::Output::send<RC::LogLevel::Normal>(
            STR("[KH3APProbeCpp] Alternate wall run ready pawn={} animations={}\n"),
            pawn->GetName(), animations.size());
        return true;
    }

    void align_kh02_aqua_ledge_grab(UObject* pawn)
    {
        static FWeakObjectPtr aligned;
        if (!is_uobject_valid_fast(pawn) || aligned.Get() == pawn ||
            pawn->GetClassPrivate()->GetName() != L"p_ex002_Pawn_C")
        {
            return;
        }
        // The chapter's native player factory bypasses alternate preparation.
        // The shared Grip clips meet the lip at Sora's anchor; Aqua's vanilla
        // -80 cm forward offset leaves her hands about 20 cm short.
        constexpr DangleOffset shared_grip_anchor{-60.0f, 0.0f, -150.0f};
        dangle_offset(pawn) = shared_grip_anchor;
        aligned = make_uobject_weak(pawn);
    }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    namespace
    {
        FWeakObjectPtr test_pawn;
        FWeakObjectPtr test_controller;
        FWeakObjectPtr test_world;
        std::chrono::steady_clock::time_point test_until{};
        float test_x{};
        float test_y{};
        bool test_active{};
        bool test_picture{};
    } // namespace

    bool diagnostic_move(UObject* pawn, float x, float y)
    {
        if (test_active || !runtime_sora_control_ready(pawn) || !std::isfinite(x) ||
            !std::isfinite(y) || std::abs(x) > 1 || std::abs(y) > 1 ||
            actor_owning_world(pawn) != kh02_current_world())
        {
            return false;
        }
        test_pawn = make_uobject_weak(pawn);
        test_controller = make_uobject_weak(object_property(pawn, L"Controller"));
        test_world = make_uobject_weak(kh02_current_world());
        test_until = std::chrono::steady_clock::now() + diagnostic_move_duration;
        test_x = x;
        test_y = y;
        test_active = true;
        test_picture = false;
        return true;
    }

    void diagnostic_tick()
    {
        if (!test_active)
        {
            return;
        }
        auto* controller = test_controller.Get();
        auto* pawn = test_pawn.Get();
        if (std::chrono::steady_clock::now() >= test_until || !pawn || !controller ||
            object_property(controller, L"Pawn") != pawn ||
            test_world.Get() != kh02_current_world() || !runtime_sora_control_ready(pawn))
        {
            if (controller)
            {
                runtime_controller_move_input(controller, 0, 0);
            }
            test_active = false;
            return;
        }
        runtime_controller_move_input(controller, test_x, test_y);
        if (!test_picture &&
            std::chrono::steady_clock::now() >= test_until - screenshot_time_before_release)
        {
            auto* state =
                object_property(object_property(pawn, L"MyStateComp"), L"m_pCurrentState");
            if (is_uobject_valid_fast(state) &&
                state->GetClassPrivate()->GetName() == L"TresPlayerState_WallRun")
            {
                test_picture = true;
                ReflectedFunctionCall picture(L"/Script/Engine.Default__KismetSystemLibrary",
                                          L"ExecuteConsoleCommand", "alternate_wall_run_picture",
                                          maximum_screenshot_parameter_bytes);
                picture.object(L"WorldContextObject", pawn);
                picture.string(L"Command", diagnostic_screenshot_command);
                picture.run();
            }
        }
    }
#endif
} // namespace kh3ap::alternate_wall_run
