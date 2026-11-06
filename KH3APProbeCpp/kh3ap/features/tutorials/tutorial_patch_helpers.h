#pragma once

#include "kh3ap/features/story/world_fixes/final_world_guidance_bypass.h"
#include "kh3ap/features/tutorials/olympus_tutorial_bypass.h"
#include "kh3ap/features/tutorials/tutorial_popup_bypass.h"
#include "kh3ap/features/tutorials/tutorial_patch_batch.h"
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace RC::Unreal
{
    class UObject;
    class UFunction;
} // namespace RC::Unreal
namespace kh3ap::tutorial_patch
{
    using RC::Unreal::UFunction;
    using RC::Unreal::UObject;
    // Game-thread-only results; function pointers are consumed in the same call.
    struct TutorialScriptView
    {
        uint8_t* data{};
        size_t size{};
    };

    // The caller has resolved a live function; consume its script synchronously.
    [[nodiscard]] auto tutorial_function_script(UFunction& function_object) -> TutorialScriptView;

    struct FinalWorldPatchAttempt
    {
        UFunction* function_object{};
        size_t script_size{};
        RuntimeFinalWorldGuidanceBypassResult guidance_result{
            RuntimeFinalWorldGuidanceBypassResult::Rejected};
        RuntimeTutorialPopupBypassResult ability_disables_result{
            RuntimeTutorialPopupBypassResult::Rejected};
        RuntimeTutorialPopupBypassResult combat_basics_result{
            RuntimeTutorialPopupBypassResult::Rejected};

        [[nodiscard]] auto accepted() const noexcept -> bool
        {
            return function_object &&
                   guidance_result != RuntimeFinalWorldGuidanceBypassResult::Rejected &&
                   combat_basics_result != RuntimeTutorialPopupBypassResult::Rejected &&
                   ability_disables_result != RuntimeTutorialPopupBypassResult::Rejected;
        }
    };

    struct TutorialPatchAttempt
    {
        UFunction* function_object{};
        size_t script_size{};
        RuntimeTutorialPopupBypassResult result{RuntimeTutorialPopupBypassResult::Rejected};

        [[nodiscard]] auto accepted() const noexcept -> bool
        {
            return function_object && result != RuntimeTutorialPopupBypassResult::Rejected;
        }
    };

    auto disable_cactuar_camera_directors() -> void;

    [[nodiscard]] auto is_live_function_patch_target(UFunction* function_object) -> bool;

    [[nodiscard]] auto find_live_function(std::wstring_view function_path) -> UFunction*;

    [[nodiscard]] auto find_constructed_or_live_function(UObject* object, std::wstring_view path)
        -> UFunction*;

    [[nodiscard]] auto tutorial_popup_result_name(RuntimeTutorialPopupBypassResult result) noexcept
        -> std::wstring_view;

    [[nodiscard]] auto
    olympus_tutorial_result_name(RuntimeOlympusTutorialBypassResult result) noexcept
        -> std::wstring_view;

    [[nodiscard]] auto guidance_result_name(RuntimeFinalWorldGuidanceBypassResult result) noexcept
        -> std::wstring_view;

    [[nodiscard]] auto try_patch_final_world_function(std::wstring_view function_path,
                                                      UObject* object = nullptr)
        -> FinalWorldPatchAttempt;

    [[nodiscard]] auto try_patch_tutorial_function_object(UFunction* function_object,
                                                          TutorialPatcher patcher)
        -> TutorialPatchAttempt;

    [[nodiscard]] auto try_patch_tutorial_function(std::wstring_view function_path,
                                                   TutorialPatcher patcher) -> TutorialPatchAttempt;
} // namespace kh3ap::tutorial_patch
