#pragma once

#include <chrono>
#include <cstdint>
#include <string_view>

namespace kh3ap
{
    inline constexpr std::string_view runtime_union_cross_slot_option =
            "fast_union_cross";
    inline constexpr uint8_t runtime_union_cross_final_mode = 2;
    inline constexpr auto runtime_union_cross_retry_interval =
            std::chrono::milliseconds(250);

    inline constexpr std::wstring_view runtime_union_cross_command_class_name =
            L"g_kg_unioncross_ride_C";
    inline constexpr std::wstring_view runtime_union_cross_blueprint_package_fragment =
            L"/Game/Blueprints/Gimmick/kg/g_kg_unioncross/g_kg_unioncross_ride.";
    inline constexpr std::wstring_view runtime_union_cross_level_package_fragment =
            L"/Game/Levels/kg/kg_01/umap/kg_01_unioncross.";
    inline constexpr std::wstring_view runtime_union_cross_default_object_fragment =
            L"Default__g_kg_unioncross_ride_C";
    inline constexpr std::wstring_view runtime_union_cross_live_instance_fragment =
            L":PersistentLevel.g_kg_unioncross_ride_";
    inline constexpr std::wstring_view runtime_union_cross_started_property_name =
            L"Started";
    inline constexpr std::wstring_view runtime_union_cross_end_ride_function_name =
            L"EndRide";
    inline constexpr std::wstring_view runtime_union_cross_level_script_class_name =
            L"kg_01_unioncross_C";
    inline constexpr std::wstring_view runtime_union_cross_notify_end_ride_function_suffix =
            L"_K2Node_ActorBoundEvent_0_NotifyEndRide__DelegateSignature";
    inline constexpr std::wstring_view runtime_union_cross_baked_notify_end_ride_function_name =
            L"BndEvt__g_kg_unioncross_ride_44_K2Node_ActorBoundEvent_0_"
            L"NotifyEndRide__DelegateSignature";

    enum class RuntimeUnionCrossObjectKind
    {
        NotTarget,
        ClassDefaultObject,
        LiveInstance,
    };

    [[nodiscard]] constexpr auto classify_runtime_union_cross_object(
            std::wstring_view class_name,
            std::wstring_view full_name) noexcept -> RuntimeUnionCrossObjectKind
    {
        if (class_name != runtime_union_cross_command_class_name)
        {
            return RuntimeUnionCrossObjectKind::NotTarget;
        }
        if (full_name.find(runtime_union_cross_blueprint_package_fragment) !=
                    std::wstring_view::npos &&
            full_name.find(runtime_union_cross_default_object_fragment) !=
                    std::wstring_view::npos)
        {
            return RuntimeUnionCrossObjectKind::ClassDefaultObject;
        }
        if (full_name.find(runtime_union_cross_level_package_fragment) !=
                    std::wstring_view::npos &&
            full_name.find(runtime_union_cross_live_instance_fragment) !=
                    std::wstring_view::npos)
        {
            return RuntimeUnionCrossObjectKind::LiveInstance;
        }
        return RuntimeUnionCrossObjectKind::NotTarget;
    }

    enum class RuntimeUnionCrossStartAction
    {
        Ignored,
        Waiting,
        Complete,
    };

    struct RuntimeUnionCrossStartTracker
    {
        uint64_t actor_identity{};
        bool completion_dispatched{};

        constexpr auto observe_actor(uint64_t identity) noexcept -> bool
        {
            if (identity == 0 || identity == actor_identity)
            {
                return false;
            }
            actor_identity = identity;
            completion_dispatched = false;
            return true;
        }

        [[nodiscard]] constexpr auto observe_started(
                uint64_t identity,
                bool started) noexcept -> RuntimeUnionCrossStartAction
        {
            observe_actor(identity);
            if (identity == 0)
            {
                return RuntimeUnionCrossStartAction::Ignored;
            }
            if (!started)
            {
                completion_dispatched = false;
                return RuntimeUnionCrossStartAction::Waiting;
            }
            if (completion_dispatched)
            {
                return RuntimeUnionCrossStartAction::Ignored;
            }
            completion_dispatched = true;
            return RuntimeUnionCrossStartAction::Complete;
        }

        constexpr auto rearm_after_failed_completion() noexcept -> void
        {
            if (!completion_dispatched)
            {
                return;
            }
            completion_dispatched = false;
        }

        constexpr auto reset() noexcept -> void
        {
            actor_identity = 0;
            completion_dispatched = false;
        }
    };
}
