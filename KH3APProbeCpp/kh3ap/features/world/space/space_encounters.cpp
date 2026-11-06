#include "kh3ap/features/world/space/space_service_impl.h"
#include "kh3ap/features/world/space/space_policy.h"
#include "kh3ap/features/world/space/space_eclipse_policy.h"
#include "kh3ap/features/world/space/space_ship_policy.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/AActor.hpp>
#include <Unreal/GameplayStatics.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <cstring>
#include <stdexcept>

using namespace RC;
using namespace RC::Unreal;

namespace kh3ap
{
    namespace
    {
        constexpr auto space_encounter_context_timeout = std::chrono::seconds(30);

        constexpr std::array<const wchar_t*, 5> eclipse_victories{
            L"gameflow_GM_03_BOSS_01_BATTLE_END", L"gameflow_GM_03_ARENA_EAST_END",
            L"gameflow_GM_03_ARENA_WEST_END", L"gameflow_GM_03_ARENA_SOUTH_END",
            L"gameflow_GM_03_ARENA_UNDER_END"};

        template<class T> std::optional<T> flag_value(UObject* library, const wchar_t* flag)
        {
            auto* fn = library->GetFunctionByNameInChain(sizeof(T) == 1 ? L"GetGameFlagBoolean" : L"GetGameFlag");
            if (!is_uobject_valid_fast(fn) || fn->GetParmsSize() != 8 + sizeof(T)) return {};
            auto* key = find_function_param_by_name(fn, L"FlagName");
            auto* result = find_function_param_by_name(fn, L"ReturnValue");
            if (!key || key->GetOffset_Internal() != 0 || key->GetElementSize() != 8 ||
                key->GetClass().GetName() != L"NameProperty" || !result || result->GetOffset_Internal() != 8) return {};
            alignas(8) std::array<std::byte, 16> args{};
            const FName name(flag, FNAME_Add); std::memcpy(args.data(), &name, sizeof(name));
            if constexpr (sizeof(T) == 1) {
                if (!result->IsA<FBoolProperty>()) return {};
                library->ProcessEvent(fn, args.data());
                return static_cast<FBoolProperty*>(result)->GetPropertyValueInContainer(args.data());
            } else {
                if (result->GetClass().GetName() != L"IntProperty" || result->GetElementSize() != sizeof(T)) return {};
                library->ProcessEvent(fn, args.data());
                return *result->ContainerPtrToValuePtr<T>(args.data());
            }
        }

        UObject* level_script(UObject* controller, const wchar_t* class_path)
        {
            auto* klass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, class_path);
            if (!is_uobject_valid_fast(klass)) return nullptr;
            std::vector<AActor*> actors; UGameplayStatics::GetAllActorsOfClass(controller, klass, actors);
            // Native current-world enumeration excludes streamed package templates.
            return actors.size() == 1 && is_uobject_valid_fast(actors[0]) ? actors[0] : nullptr;
        }

        UFunction* no_arg_event(UObject* actor, const wchar_t* name)
        {
            auto* fn = is_uobject_valid_fast(actor) ? actor->GetFunctionByNameInChain(name) : nullptr;
            return is_uobject_valid_fast(fn) && fn->GetParmsSize() == 0 ? fn : nullptr;
        }
    }

    void RuntimeSpaceService::Impl::install_encounter_events()
    {
        try {
            std::set<uint64_t> watched;
            const auto watch = [&](const wchar_t* key) {
                const FName name(key, FNAME_Add); uint64_t raw{};
                static_assert(sizeof(name) == sizeof(raw)); std::memcpy(&raw, &name, sizeof(raw)); watched.insert(raw);
            };
            for (auto* key : eclipse_victories) watch(key);
            for (const auto& flag : space::world_completion_flags) watch(flag.name);
            watch(L"gameflow_GM_03"); watch(L"gameflow_GM_03_SUB");
            encounter_flags.configure(std::move(watched));
            subscribe_game_flags(encounter_flags); subscribe_game_flags(encounter_flags, true);
        } catch (const std::exception& error) {
            unsubscribe_game_flags(encounter_flags);
            const std::string reason = error.what();
            Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] Eclipse completion notifications unavailable: {}\n"), std::wstring(reason.begin(), reason.end()));
        }
    }

    void RuntimeSpaceService::Impl::poll_encounter_events(std::chrono::steady_clock::time_point now)
    {
        if (!encounter_flags.pending()) return;
        encounter_flags.take();
        if (!access.access_ready || !access.include_space) return;
        ships_refresh_pending=true;
        context_refresh.request();
        context_deadline = now + space_encounter_context_timeout;
        overview_refresh.request();
        status_pending=true;
        if (current_world != space::eclipse_world) return;
        context_refresh.request(); flight_refresh.request();
        context_deadline = now + space_encounter_context_timeout;
    }

    bool RuntimeSpaceService::Impl::refresh_eclipse_unlock()
    {
        if (!access.access_ready || !access.include_space || current_world != space::eclipse_world) return true;
        const bool expired = std::chrono::steady_clock::now() >= context_deadline;
        auto* controller = delivery_context(true);
        auto* mode = object_field(world(), L"AuthorityGameMode");
        if (!controller || !mode || object_field(mode, L"m_pCurrentEnemyBattleStarter")) return expired;
        auto* library = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, L"/Script/TresGame.Default__TresGameBlueprintLibrary");
        if (!is_uobject_valid_fast(library)) return expired;
        auto* cinematic = library->GetFunctionByNameInChain(L"TresIsCinematicMode");
        auto* cinematic_result = is_uobject_valid_fast(cinematic) ? find_function_param_by_name(cinematic, L"ReturnValue") : nullptr;
        alignas(8) std::array<std::byte, 16> cinematic_args{};
        if (!is_uobject_valid_fast(cinematic) || cinematic->GetParmsSize() != 9 ||
            !cinematic_result || !cinematic_result->IsA<FBoolProperty>() || cinematic_result->GetOffset_Internal() != 8 ||
            !write_object_function_param(cinematic, cinematic_args.data(), L"WorldContextObject", controller)) return expired;
        library->ProcessEvent(cinematic, cinematic_args.data());
        if (static_cast<FBoolProperty*>(cinematic_result)->GetPropertyValueInContainer(cinematic_args.data())) return expired;
        const auto stage = flag_value<int32_t>(library, L"gameflow_GM_03_SUB");
        if (!stage) return expired;
        if (*stage >= space::eclipse_boss_ready) return true;
        const auto omega_defeated = flag_value<bool>(library, L"gameflow_GM_SYS_boss_defeat_090");
        if (!omega_defeated) return expired;
        if (*omega_defeated) return true;
        std::array<bool, 5> victories{};
        for (size_t i = 0; i < victories.size(); ++i) {
            const auto value = flag_value<bool>(library, eclipse_victories[i]);
            if (!value) return expired;
            victories[i] = *value;
        }
        if (!space::recover_eclipse_unlock(access.access_ready, access.include_space, current_world, victories, *stage, *omega_defeated)) return true;
        auto* flow = level_script(controller, L"/Game/Levels/gm/gm_03/umap/gm_03_gameflow.gm_03_gameflow_C");
        auto* battle = level_script(controller, L"/Game/Levels/gm/gm_03/umap/Battle/gm_03_battle_06.gm_03_battle_06_C");
        auto* reveal = no_arg_event(flow, L"gm_03_camera_ms090_REvt_SubArenaClear_End");
        auto* spawn = no_arg_event(battle, L"gm_03_06_SpawnBattleStarter");
        auto* setter = library->GetFunctionByNameInChain(L"SetFlagLabel");
        alignas(8) std::array<std::byte, 16> set_args{};
        if (!reveal || !spawn || !is_uobject_valid_fast(setter) || setter->GetParmsSize() != 16 ||
            !write_name_or_string_function_param(find_function_param_by_name(setter, L"Flag"), set_args.data(), L"gameflow_GM_03_SUB") ||
            !write_name_or_string_function_param(find_function_param_by_name(setter, L"label"), set_args.data(), L"GM_03_SUB_CAMERA_10")) return expired;
        auto* starters_class = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, L"/Script/TresGame.TresGumiShipEnemyBattleStarter");
        auto* enemies_class = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, L"/Script/TresGame.TresGumiShipEnemyPawnBase");
        if (!is_uobject_valid_fast(starters_class) || !is_uobject_valid_fast(enemies_class)) return expired;
        std::vector<AActor*> actors; UGameplayStatics::GetAllActorsOfClass(controller, starters_class, actors);
        UObject* starter{};
        for (auto* actor : actors) if (is_uobject_valid_fast(actor) && space::battle_first_reward_matches(
            space::battle_from_starter_path(actor->GetPathName()), L"gm_03_Battle_06_1")) {
            if (starter) return true; starter = actor;
        }
        if (!starter) return expired;
        bool already_spawned{}; actors.clear(); UGameplayStatics::GetAllActorsOfClass(controller, enemies_class, actors);
        for (auto* enemy : actors) {
            auto* owner = is_uobject_valid_fast(enemy) ? enemy->GetFunctionByNameInChain(L"GetCurrentEnemyBattleStarter") : nullptr;
            if (!is_uobject_valid_fast(owner) || owner->GetParmsSize() != sizeof(UObject*)) return expired;
            UObject* value{}; enemy->ProcessEvent(owner, &value);
            if (value == starter) already_spawned = true;
        }
        // Validate every dependency before writing. Spawn through the same level
        // script as native BeginPlay, then let the normal gameflow raise SUB to
        // BOSS (500) and play its appearance scene. Neither event awards loot.
        if (*stage < space::eclipse_final_camera) library->ProcessEvent(setter, set_args.data());
        const auto promoted = flag_value<int32_t>(library, L"gameflow_GM_03_SUB");
        if (!promoted || *promoted != space::eclipse_final_camera) return true;
        if (!already_spawned) battle->ProcessEvent(spawn, nullptr);
        flow->ProcessEvent(reveal, nullptr);
        const auto after = flag_value<int32_t>(library, L"gameflow_GM_03_SUB");
        if (after && *after >= space::eclipse_boss_ready) ++eclipse_unlock_recoveries;
        Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] Eclipse unlock recovery victories=5 stage_before={} stage_after={} existing_omega={} recoveries={}\n"),
            *stage, after.value_or(-1), already_spawned, eclipse_unlock_recoveries);
        return true;
    }

    bool RuntimeSpaceService::Impl::refresh_story_encounters()
    {
        if (current_world == space::eclipse_world) return refresh_eclipse_unlock();
        if (!access.access_ready || !access.include_space ||
            current_world != L"World /Game/Levels/gm/gm_01/gm_01.gm_01") return true;
        const bool expired = std::chrono::steady_clock::now() >= context_deadline;
        auto* controller = delivery_context(true);
        if (!controller) return expired;
        auto* mode = object_field(world(), L"AuthorityGameMode");
        if (!mode || object_field(mode, L"m_pCurrentEnemyBattleStarter")) return expired;
        auto* klass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr,
            L"/Script/TresGame.TresGumiShipEnemyBattleStarter");
        if (!is_uobject_valid_fast(klass)) return expired;
        std::vector<AActor*> actors;
        UGameplayStatics::GetAllActorsOfClass(controller, klass, actors);
        for (auto* actor : actors)
        {
            if (!is_uobject_valid_fast(actor) || !space::restore_story_encounter(
                access.access_ready, access.include_space, current_world, actor->GetPathName())) continue;
            auto* enabled = actor->GetFunctionByNameInChain(L"IsEnabled");
            auto* enable = actor->GetFunctionByNameInChain(L"Enable");
            auto* disabled_start = actor->GetPropertyByNameInChain(L"m_bDisableStart");
            if (!is_uobject_valid_fast(enabled) || enabled->GetParmsSize() != sizeof(bool) ||
                !is_uobject_valid_fast(enable) || enable->GetParmsSize() != 0 ||
                !disabled_start || !disabled_start->IsA<FBoolProperty>()) return true;
            bool is_enabled{};
            actor->ProcessEvent(enabled, &is_enabled);
            if (is_enabled) return true;
            // Astro Warrior's level BeginPlay disables its entrance until
            // WM_WORLD_TT_CLEARED. AP space checks have no Twilight Town story
            // prerequisite. Restore only this entrance through its owning
            // native method, after loading; preserve first-fight/revenge state,
            // boss/world locks, story flags, and native completion/rewards.
            static_cast<FBoolProperty*>(disabled_start)->SetPropertyValueInContainer(actor, false);
            actor->ProcessEvent(enable, nullptr);
            actor->ProcessEvent(enabled, &is_enabled);
            Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] Space story entrance restored battle=gm_01_Battle_10 enabled={}\n"), is_enabled);
            return is_enabled || expired;
        }
        // The battle sublevel can arrive after the controller. Retry only
        // within this map's bounded readiness window, never scan while idle.
        return expired;
    }
}
