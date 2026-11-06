#include "kh3ap/features/tutorials/tutorial_patch_helpers.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectArray.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <UnrealDef.hpp>
#include <array>
#include <string>

namespace kh3ap::tutorial_patch
{
    using namespace RC;
    using namespace RC::Unreal;
    namespace
    {
        constexpr size_t s_function_outer_validation_depth = 16;
    }
    auto disable_cactuar_camera_directors() -> void
    {
        auto* klass = UObjectGlobals::StaticFindObject<UClass*>(
            nullptr, nullptr, L"/Script/Engine.InterpTrackDirector");
        if (!is_uobject_valid_fast(klass))
        {
            return;
        }
        constexpr std::array paths{
            L"/Game/Levels/ts/ts_02/umap/"
            L"ts_02_camera_ms142.ts_02_camera_ms142:PersistentLevel.MatineeActor_ms142_blockA_1."
            L"InterpData_0.InterpGroupDirector_0.InterpTrackDirector_0",
            L"/Game/Levels/ts/ts_02/umap/"
            L"ts_02_camera_ms142.ts_02_camera_ms142:PersistentLevel.MatineeActor_ms142_blockA_2."
            L"InterpData_0.InterpGroupDirector_2.InterpTrackDirector_1",
            L"/Game/Levels/ts/ts_02/umap/"
            L"ts_02_camera_ms142.ts_02_camera_ms142:PersistentLevel.MatineeActor_ms142_blockB_2."
            L"InterpData_0.InterpGroupDirector_0.InterpTrackDirector_0",
            L"/Game/Levels/ts/ts_02/umap/"
            L"ts_02_camera_ms142.ts_02_camera_ms142:PersistentLevel.MatineeActor_ms142_blockC_2."
            L"InterpData_1.InterpGroupDirector_1.InterpTrackDirector_1",
            L"/Game/Levels/ts/ts_02/umap/"
            L"ts_02_camera_ms142.ts_02_camera_ms142:PersistentLevel.MatineeActor_ms142_build01."
            L"InterpData_0.InterpGroupDirector_0.InterpTrackDirector_0",
            L"/Game/Levels/ts/ts_02/umap/"
            L"ts_02_camera_ms142.ts_02_camera_ms142:PersistentLevel.MatineeActor_ms142_build02."
            L"InterpData_1.InterpGroupDirector_1.InterpTrackDirector_1",
            L"/Game/Levels/ts/ts_02/umap/"
            L"ts_02_camera_ms142.ts_02_camera_ms142:PersistentLevel.MatineeActor_ms142_build03."
            L"InterpData_0.InterpGroupDirector_0.InterpTrackDirector_0",
            L"/Game/Levels/ts/ts_02/umap/"
            L"ts_02_camera_ms142.ts_02_camera_ms142:PersistentLevel.MatineeActor_ms142_buildEnd_0."
            L"InterpData_0.InterpGroupDirector_0.InterpTrackDirector_0",
            L"/Game/Levels/ts/ts_02/umap/"
            L"ts_02_camera_ms142.ts_02_camera_ms142:PersistentLevel.MatineeActor_ms142_buildEnd_1."
            L"InterpData_0.InterpGroupDirector_0.InterpTrackDirector_0",
        };
        size_t changed = 0;
        for (const auto* path : paths)
        {
            // Class-qualified lookup avoids UE4SS's stale raw-object cache
            // across map reloads. No pointers are retained across frames.
            auto* track = UObjectGlobals::StaticFindObject<UObject*>(klass, nullptr, path);
            if (!is_uobject_valid_fast(track) || !track->IsA(klass))
            {
                continue;
            }
            auto* property = track->GetPropertyByNameInChain(L"bDisableTrack");
            if (!property || !property->IsA<FBoolProperty>())
            {
                continue;
            }
            auto* disabled = static_cast<FBoolProperty*>(property);
            if (!disabled->GetPropertyValueInContainer(track))
            {
                disabled->SetPropertyValueInContainer(track, true);
                ++changed;
            }
        }
        if (changed)
        {
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] CactuarAssembly camera_directors_disabled={} "
                    "block_animations_preserved=true\n"),
                changed);
        }
    }

    [[nodiscard]] auto is_live_function_patch_target(UFunction* function_object) -> bool
    {
        UObject* current = function_object;
        for (size_t depth = 0; current && depth < s_function_outer_validation_depth; ++depth)
        {
            // StaticFindObject may briefly return its cached function from
            // the package being torn down during Retry. The function's
            // script can still be readable while an Outer has already
            // become stale, which makes GetFullName/GetPathName unsafe.
            if (!kh3ap::is_uobject_valid_fast(current))
            {
                return false;
            }

            FUObjectItem* const object_item = current->GetObjectItem();
            if (!object_item || object_item->GetUObject() != current ||
                !UObjectArray::IsValid(object_item, false) ||
                current->HasAnyFlags(
                    static_cast<EObjectFlags>(RF_BeginDestroyed | RF_FinishDestroyed)))
            {
                return false;
            }

            UObject* const outer = current->GetOuterPrivate();
            if (!outer)
            {
                return true;
            }
            if (outer == current)
            {
                return false;
            }
            current = outer;
        }
        return false;
    }

    [[nodiscard]] auto find_constructed_or_live_function(UObject* object, std::wstring_view path)
        -> UFunction*
    {
        const auto separator = path.rfind(L':');
        if (object && kh3ap::is_uobject_valid_fast(object) && separator != path.npos)
        {
            const std::wstring name{path.substr(separator + 1)};
            UFunction* const function = object->GetFunctionByNameInChain(name.c_str());
            if (is_live_function_patch_target(function))
            {
                return function;
            }
        }
        return find_live_function(path);
    }

    [[nodiscard]] auto tutorial_popup_result_name(RuntimeTutorialPopupBypassResult result) noexcept
        -> std::wstring_view
    {
        switch (result)
        {
        case RuntimeTutorialPopupBypassResult::Patched:
            return STR("patched");
        case RuntimeTutorialPopupBypassResult::AlreadyPatched:
            return STR("already_patched");
        case RuntimeTutorialPopupBypassResult::Rejected:
        default:
            return STR("rejected");
        }
    }

    [[nodiscard]] auto
    olympus_tutorial_result_name(RuntimeOlympusTutorialBypassResult result) noexcept
        -> std::wstring_view
    {
        switch (result)
        {
        case RuntimeOlympusTutorialBypassResult::Patched:
            return STR("patched");
        case RuntimeOlympusTutorialBypassResult::AlreadyPatched:
            return STR("already_patched");
        case RuntimeOlympusTutorialBypassResult::Rejected:
        default:
            return STR("rejected");
        }
    }

    [[nodiscard]] auto guidance_result_name(RuntimeFinalWorldGuidanceBypassResult result) noexcept
        -> std::wstring_view
    {
        switch (result)
        {
        case RuntimeFinalWorldGuidanceBypassResult::Patched:
            return STR("patched");
        case RuntimeFinalWorldGuidanceBypassResult::AlreadyPatched:
            return STR("already_patched");
        case RuntimeFinalWorldGuidanceBypassResult::Rejected:
        default:
            return STR("rejected");
        }
    }

    [[nodiscard]] auto tutorial_function_script(UFunction& function_object) -> TutorialScriptView
    {
        auto& script = function_object.GetScript();
        const int32_t script_size = script.Num();
        return {script_size > 0 ? script.GetData() : nullptr,
                script_size > 0 ? static_cast<size_t>(script_size) : 0};
    }

    [[nodiscard]] auto try_patch_final_world_function(std::wstring_view function_path,
                                                      UObject* object) -> FinalWorldPatchAttempt
    {
        FinalWorldPatchAttempt attempt{};
        attempt.function_object = find_constructed_or_live_function(object, function_path);
        if (!attempt.function_object)
        {
            return attempt;
        }

        const auto [script, script_size] = tutorial_function_script(*attempt.function_object);
        attempt.script_size = script_size;
        attempt.ability_disables_result =
            try_patch_runtime_final_world_ability_disables_bytecode(script, attempt.script_size);
        attempt.guidance_result =
            try_patch_runtime_final_world_guidance_bytecode(script, attempt.script_size);
        if (attempt.ability_disables_result != RuntimeTutorialPopupBypassResult::AlreadyPatched)
        {
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] EWControllerAbilityDisables result={} script_size={} "
                    "function={}\n"),
                tutorial_popup_result_name(attempt.ability_disables_result), attempt.script_size,
                function_path);
        }
        attempt.combat_basics_result =
            try_patch_runtime_final_world_combat_basics_tutorial_bytecode(script,
                                                                          attempt.script_size);
        return attempt;
    }

    [[nodiscard]] auto try_patch_tutorial_function_object(UFunction* function_object,
                                                          TutorialPatcher patcher)
        -> TutorialPatchAttempt
    {
        TutorialPatchAttempt attempt{};
        attempt.function_object =
            is_live_function_patch_target(function_object) ? function_object : nullptr;
        if (!attempt.function_object)
        {
            return attempt;
        }

        const auto [script, script_size] = tutorial_function_script(*attempt.function_object);
        attempt.script_size = script_size;
        attempt.result = patcher(script, attempt.script_size);
        return attempt;
    }

    [[nodiscard]] auto try_patch_tutorial_function(std::wstring_view function_path,
                                                   TutorialPatcher patcher) -> TutorialPatchAttempt
    {
        return try_patch_tutorial_function_object(find_live_function(function_path), patcher);
    }
} // namespace kh3ap::tutorial_patch
