#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    inline constexpr std::string_view runtime_festival_dance_story_slot_option =
            "fast_dance_minigame";

    struct RuntimeFestivalDanceStoryPropertySpec
    {
        std::wstring_view property_name;
        int32_t vanilla_value;
        int32_t fast_value;
    };

    inline constexpr std::array<RuntimeFestivalDanceStoryPropertySpec, 3>
            runtime_festival_dance_story_property_specs{{
                    {L"Wave1ClearValue", 500, 40},
                    {L"WaveRaDanceFinishValue", 18, 2},
                    {L"MissionClearValue", 2000, 540},
            }};

    inline constexpr std::wstring_view runtime_festival_dance_story_class_name =
            L"ra_02_mission_Dance_C";
    inline constexpr std::wstring_view runtime_festival_dance_story_manager_class_name =
            L"lv_ra_FestivalDance_C";
    inline constexpr std::wstring_view runtime_festival_dance_story_package_fragment =
            L"/Game/Levels/ra/ra_02/umap/ra_02_mission_Dance.";
    inline constexpr std::wstring_view runtime_festival_dance_story_manager_instance_fragment =
            L":PersistentLevel.lv_ra_FestivalDance";

    struct RuntimeFestivalDanceStoryTimingPropertySpec
    {
        std::wstring_view property_name;
        float vanilla_value;
        float fast_value;
    };

    inline constexpr std::array<RuntimeFestivalDanceStoryTimingPropertySpec, 2>
            runtime_festival_dance_story_timing_property_specs{{
                    {L"RapunzelDanceTotalTime", 28.0F, 4.0F},
                    {L"FlynnSplineStayTime", 10.0F, 2.0F},
            }};

    enum class RuntimeFestivalDanceStoryObjectKind
    {
        NotTarget,
        ClassDefaultObject,
        LiveInstance,
    };

    [[nodiscard]] constexpr auto classify_runtime_festival_dance_story_object(
            std::wstring_view class_name,
            std::wstring_view full_name) noexcept -> RuntimeFestivalDanceStoryObjectKind
    {
        if (class_name != runtime_festival_dance_story_class_name ||
            full_name.find(runtime_festival_dance_story_package_fragment) == std::wstring_view::npos)
        {
            return RuntimeFestivalDanceStoryObjectKind::NotTarget;
        }
        if (full_name.find(L"Default__ra_02_mission_Dance_C") != std::wstring_view::npos)
        {
            return RuntimeFestivalDanceStoryObjectKind::ClassDefaultObject;
        }
        if (full_name.find(L":PersistentLevel.ra_02_mission_Dance_C_") != std::wstring_view::npos)
        {
            return RuntimeFestivalDanceStoryObjectKind::LiveInstance;
        }
        return RuntimeFestivalDanceStoryObjectKind::NotTarget;
    }

    [[nodiscard]] constexpr auto is_runtime_festival_dance_story_manager_live_object(
            std::wstring_view class_name,
            std::wstring_view full_name) noexcept -> bool
    {
        return class_name == runtime_festival_dance_story_manager_class_name &&
               full_name.find(runtime_festival_dance_story_package_fragment) !=
                       std::wstring_view::npos &&
               full_name.find(runtime_festival_dance_story_manager_instance_fragment) !=
                       std::wstring_view::npos;
    }

    struct RuntimeFestivalDanceStoryPatchProgress
    {
        bool class_default_object_patched{false};
        bool live_instance_patched{false};
        bool manager_live_instance_timing_patched{false};

        [[nodiscard]] constexpr auto contains(
                RuntimeFestivalDanceStoryObjectKind object_kind) const noexcept -> bool
        {
            switch (object_kind)
            {
            case RuntimeFestivalDanceStoryObjectKind::ClassDefaultObject:
                return class_default_object_patched;
            case RuntimeFestivalDanceStoryObjectKind::LiveInstance:
                return live_instance_patched;
            case RuntimeFestivalDanceStoryObjectKind::NotTarget:
            default:
                return false;
            }
        }

        constexpr auto record_success(
                RuntimeFestivalDanceStoryObjectKind object_kind) noexcept -> void
        {
            switch (object_kind)
            {
            case RuntimeFestivalDanceStoryObjectKind::ClassDefaultObject:
                class_default_object_patched = true;
                break;
            case RuntimeFestivalDanceStoryObjectKind::LiveInstance:
                live_instance_patched = true;
                break;
            case RuntimeFestivalDanceStoryObjectKind::NotTarget:
            default:
                break;
            }
        }

        constexpr auto record_manager_timing_success() noexcept -> void
        {
            manager_live_instance_timing_patched = true;
        }

        [[nodiscard]] constexpr auto manager_timing_complete() const noexcept -> bool
        {
            return manager_live_instance_timing_patched;
        }

        [[nodiscard]] constexpr auto complete() const noexcept -> bool
        {
            return class_default_object_patched &&
                   live_instance_patched &&
                   manager_live_instance_timing_patched;
        }
    };

    enum class RuntimeFestivalDanceStoryPropertyWriteStatus
    {
        NotAttempted,
        MissingProperty,
        WrongPropertyType,
        WrongPropertySize,
        MissingValuePointer,
        UnexpectedValue,
        AlreadySet,
        Patched,
        VerificationFailed,
    };

    struct RuntimeFestivalDanceStoryPropertyWriteResult
    {
        std::wstring_view property_name{};
        int32_t original_value{};
        int32_t requested_value{};
        RuntimeFestivalDanceStoryPropertyWriteStatus status{
                RuntimeFestivalDanceStoryPropertyWriteStatus::NotAttempted};
    };

    struct RuntimeFestivalDanceStoryObjectPatchResult
    {
        RuntimeFestivalDanceStoryObjectKind object_kind{
                RuntimeFestivalDanceStoryObjectKind::NotTarget};
        std::wstring object_name{};
        std::array<RuntimeFestivalDanceStoryPropertyWriteResult,
                   runtime_festival_dance_story_property_specs.size()>
                writes{};
        bool success{false};
        bool changed{false};
    };

    struct RuntimeFestivalDanceStoryTimingPropertyWriteResult
    {
        std::wstring_view property_name{};
        float original_value{};
        float requested_value{};
        RuntimeFestivalDanceStoryPropertyWriteStatus status{
                RuntimeFestivalDanceStoryPropertyWriteStatus::NotAttempted};
    };

    struct RuntimeFestivalDanceStoryTimingPatchResult
    {
        bool is_target{false};
        std::wstring object_name{};
        std::array<RuntimeFestivalDanceStoryTimingPropertyWriteResult,
                   runtime_festival_dance_story_timing_property_specs.size()>
                writes{};
        bool success{false};
        bool changed{false};
    };

    [[nodiscard]] auto runtime_festival_dance_story_object_kind_name(
            RuntimeFestivalDanceStoryObjectKind object_kind) noexcept -> std::wstring_view;
    [[nodiscard]] auto runtime_festival_dance_story_property_write_status_name(
            RuntimeFestivalDanceStoryPropertyWriteStatus status) noexcept -> std::wstring_view;
    [[nodiscard]] auto try_patch_runtime_festival_dance_story_object(
            RC::Unreal::UObject* object) -> RuntimeFestivalDanceStoryObjectPatchResult;
    [[nodiscard]] auto try_patch_runtime_festival_dance_story_timing_object(
            RC::Unreal::UObject* object) -> RuntimeFestivalDanceStoryTimingPatchResult;
}
