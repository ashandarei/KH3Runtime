#pragma once

#include "kh3ap/diagnostics/reflection/reflection_diagnostic_models.h"
#include "kh3ap/core/types.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    struct StableAbilityTraceData
    {
        std::wstring phase_name{};
        std::wstring hook_name{};
        std::wstring context_name{};
        std::wstring frame_name{};
        std::wstring params_summary{};
        std::wstring dedupe_key{};
    };

    struct AbilityArrayNeedleProbeEntry
    {
        std::wstring kind{};
        std::wstring object_name{};
        std::wstring class_name{};
        std::wstring property_name{};
        std::wstring property_class{};
        std::wstring inner_class{};
        int offset{-1};
        int element_size{-1};
        int inner_size{-1};
        int array_num{-1};
        int array_max{-1};
        std::wstring matches{};
        std::wstring value{};
    };

    struct EquipmentComponentDeepTarget
    {
        RC::Unreal::UObject* object{};
        std::wstring source_property{};
    };

    struct EquipmentComponentOffsetProbeData
    {
        std::vector<PropertyProbeEntry> entries{};
        std::vector<EquipmentComponentDeepTarget> deep_targets{};
    };

    struct LiveAbilityOwnerPropertyProbeEntry
    {
        PropertyProbeEntry entry{};
        std::wstring raw_value{};
        RC::Unreal::UObject* followed_object{};
    };

    struct LiveAbilityOwnerProbeObjectEntry
    {
        RC::Unreal::UObject* object{};
        std::wstring object_name{};
        std::wstring class_name{};
        int property_limit{24};
    };

    struct LiveAbilityOwnerProbeCandidateData
    {
        size_t candidate_count{0};
        std::vector<LiveAbilityOwnerProbeObjectEntry> objects{};
    };

    struct AbilityDiscoveryPropertyProbeEntry
    {
        PropertyProbeEntry entry{};
        std::wstring raw_value{};
    };

    struct AbilityDiscoveryObjectProbeEntry
    {
        std::wstring object_name{};
        std::wstring class_name{};
        std::vector<AbilityDiscoveryPropertyProbeEntry> properties{};
    };

    struct TargetedSoraEquipmentLinkProbeData
    {
        PropertyProbeEntry entry{};
        RC::Unreal::UObject* equipment_object{};
    };

}
