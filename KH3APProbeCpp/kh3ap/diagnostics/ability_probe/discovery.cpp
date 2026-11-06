#include "kh3ap/diagnostics/ability_probe/limits.h"
#include "kh3ap/diagnostics/ability_probe/ability_reflection_probe_helpers.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/bridge/utilities.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/FProperty.hpp>
#include "kh3ap/features/player/objects/object_resolver.h"
#include "kh3ap/features/player/state/sora_pawn_catalog.h"
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/Property/FStructProperty.hpp>
#include <algorithm>
#include <array>
#include <cstring>
#include <set>

namespace kh3ap
{
    using namespace ability_probe_limits;
    namespace
    {
        auto append_ability_state_probe_candidate(
                std::vector<RC::Unreal::UObject*>& candidates,
                RC::Unreal::UObject* object) -> void
        {
            if (!object || !kh3ap::is_uobject_valid_fast(object))
            {
                return;
            }
            if (std::find(candidates.begin(), candidates.end(), object) == candidates.end())
            {
                candidates.push_back(object);
            }
        }

        auto is_sora_or_player_state_probe_candidate(RC::Unreal::UObject* object) -> bool
        {
            if (!object || !kh3ap::is_uobject_valid_fast(object))
            {
                return false;
            }

            const std::wstring full_name = object->GetFullName();
            const std::wstring class_name = object->GetClassPrivate() ? object->GetClassPrivate()->GetName() : std::wstring{};
            return contains_any_wide(
                    full_name,
                    {
                            STR("Sora"),
                            STR("PlayerController"),
                            STR("PlayerState"),
                            STR("SaveGame"),
                            STR("GameInstance"),
                            STR("Equipment"),
                    }) ||
                   runtime_sora_pawn_catalog_matches_any(full_name, class_name) ||
                   contains_any_wide(
                           class_name,
                           {
                                   STR("Sora"),
                                   STR("Player"),
                                   STR("SaveGame"),
                                   STR("GameInstance"),
                                   STR("Equipment"),
                           });
        }
    }

    auto collect_ability_state_probe_candidates(
            RC::Unreal::UObject* preferred_object,
            RC::Unreal::UObject* last_world_context_object) -> std::vector<RC::Unreal::UObject*>
    {
        std::vector<RC::Unreal::UObject*> candidates{};
        auto append_if_target = [&candidates](RC::Unreal::UObject* object) {
            if (is_sora_or_player_state_probe_candidate(object))
            {
                append_ability_state_probe_candidate(candidates, object);
            }
        };

        append_if_target(preferred_object);
        append_if_target(last_world_context_object);

        const std::array<std::wstring_view, 16> fallback_class_names{
                k_runtime_sora_pawn_catalog[0].pawn_class_name,
                k_runtime_sora_pawn_catalog[1].pawn_class_name,
                k_runtime_sora_pawn_catalog[2].pawn_class_name,
                k_runtime_sora_pawn_catalog[3].pawn_class_name,
                k_runtime_sora_pawn_catalog[4].pawn_class_name,
                k_runtime_sora_pawn_catalog[5].pawn_class_name,
                k_runtime_sora_pawn_catalog[6].pawn_class_name,
                k_runtime_sora_pawn_catalog[7].pawn_class_name,
                k_runtime_sora_pawn_catalog[8].pawn_class_name,
                k_runtime_sora_pawn_catalog[9].pawn_class_name,
                STR("TresPlayerPawn"),
                STR("TresPlayerController"),
                STR("TresPlayerState"),
                STR("TresSaveGame"),
                STR("TresEquipment"),
                STR("GameInstance"),
        };
        for (std::wstring_view class_name : fallback_class_names)
        {
            std::vector<RC::Unreal::UObject*> objects{};
            RC::Unreal::UObjectGlobals::FindAllOf(class_name, objects);
            for (RC::Unreal::UObject* object : objects)
            {
                append_if_target(object);
                if (candidates.size() >= max_fallback_class_candidates)
                {
                    return candidates;
                }
            }
        }

        const size_t initial_count = candidates.size();
        for (size_t index = 0; index < initial_count && candidates.size() < max_initial_context_candidates; ++index)
        {
            append_if_target(try_read_reflected_object_property_value(candidates[index], STR("MyEquipment")));
        }

        return candidates;
    }

    auto append_ability_discovery_candidate(
            std::vector<RC::Unreal::UObject*>& candidates,
            RC::Unreal::UObject* object) -> void
    {
        if (!object || !kh3ap::is_uobject_valid_fast(object))
        {
            return;
        }
        if (std::find(candidates.begin(), candidates.end(), object) == candidates.end())
        {
            candidates.push_back(object);
        }
    }

    auto is_live_ability_owner_candidate(RC::Unreal::UObject* object) -> bool
    {
        if (!object || !kh3ap::is_uobject_valid_fast(object))
        {
            return false;
        }

        const std::wstring object_name = object->GetFullName();
        const std::wstring class_name = object->GetClassPrivate() ? object->GetClassPrivate()->GetName() : std::wstring{};
        return contains_any_wide(
                       object_name,
                       {
                               STR("Sora"),
                               STR("Player"),
                               STR("Save"),
                               STR("Equip"),
                               STR("Ability"),
                               STR("EventPersistance"),
                       }) ||
               runtime_sora_pawn_catalog_matches_any(object_name, class_name) ||
               contains_any_wide(
                       class_name,
                       {
                               STR("Sora"),
                               STR("Player"),
                               STR("Save"),
                               STR("Equip"),
                               STR("Ability"),
                               STR("GameInstance"),
                               STR("EventPersistance"),
                       });
    }

    auto should_probe_live_ability_owner_property(
            std::wstring_view property_name,
            std::wstring_view property_class) -> bool
    {
        if (property_class != STR("NameProperty") && property_class != STR("StrProperty") &&
            property_class != STR("ObjectProperty") && property_class != STR("ArrayProperty") &&
            property_class != STR("MapProperty") && property_class != STR("BoolProperty") &&
            property_class != STR("ByteProperty") && property_class != STR("EnumProperty") &&
            property_class != STR("IntProperty") && property_class != STR("UInt32Property") &&
            property_class != STR("Int64Property") && property_class != STR("UInt64Property") &&
            property_class != STR("StructProperty"))
        {
            return false;
        }

        return contains_any_wide(
                property_name,
                {
                        STR("Ability"),
                        STR("Abilities"),
                        STR("Bonus"),
                        STR("Equip"),
                        STR("Skill"),
                        STR("Command"),
                        STR("Action"),
                        STR("Form"),
                        STR("Have"),
                        STR("Use"),
                        STR("Enable"),
                        STR("Sora"),
                        STR("Player"),
                });
    }

    auto is_equipmentish_ability_owner_object(RC::Unreal::UObject* object) -> bool
    {
        if (!object || !kh3ap::is_uobject_valid_fast(object))
        {
            return false;
        }

        const std::wstring object_name = object->GetFullName();
        const std::wstring class_name = object->GetClassPrivate() ? object->GetClassPrivate()->GetName() : std::wstring{};
        return contains_any_wide(object_name, {STR("Equipment"), STR("Ability"), STR("TresEquipment")}) ||
               contains_any_wide(class_name, {STR("Equipment"), STR("Ability"), STR("TresEquipment")});
    }

    auto should_probe_live_equipmentish_property(
            std::wstring_view property_name,
            std::wstring_view property_class) -> bool
    {
        if (property_class == STR("ArrayProperty") || property_class == STR("MapProperty") || property_class == STR("ObjectProperty"))
        {
            return true;
        }

        return should_probe_live_ability_owner_property(property_name, property_class);
    }

    auto should_follow_live_ability_owner_object_property(
            std::wstring_view property_name,
            RC::Unreal::UObject* value) -> bool
    {
        if (!value || !kh3ap::is_uobject_valid_fast(value))
        {
            return false;
        }

        const std::wstring value_name = value->GetFullName();
        const std::wstring class_name = value->GetClassPrivate() ? value->GetClassPrivate()->GetName() : std::wstring{};
        return contains_any_wide(property_name, {STR("Ability"), STR("Equip"), STR("Player"), STR("Sora"), STR("Save"), STR("Data")}) ||
               contains_any_wide(value_name, {STR("Ability"), STR("Equip"), STR("Player"), STR("Sora"), STR("Save")}) ||
               runtime_sora_pawn_catalog_matches_any(value_name, class_name) ||
               contains_any_wide(class_name, {STR("Ability"), STR("Equip"), STR("Player"), STR("Sora"), STR("Save"), STR("Data")});
    }

    auto collect_live_ability_owner_candidates(
            RC::Unreal::UObject* preferred_object,
            RC::Unreal::UObject* last_world_context_object) -> std::vector<RC::Unreal::UObject*>
    {
        std::vector<RC::Unreal::UObject*> candidates =
                collect_ability_state_probe_candidates(preferred_object, last_world_context_object);

        const size_t first_pass_count = candidates.size();
        for (size_t index = 0; index < first_pass_count && candidates.size() < max_direct_owner_candidates; ++index)
        {
            append_ability_discovery_candidate(
                    candidates,
                    try_read_reflected_object_property_value(candidates[index], STR("MyEquipment")));
        }

        const std::array<std::wstring_view, 25> class_names{
                k_runtime_sora_pawn_catalog[0].pawn_class_name,
                k_runtime_sora_pawn_catalog[1].pawn_class_name,
                k_runtime_sora_pawn_catalog[2].pawn_class_name,
                k_runtime_sora_pawn_catalog[3].pawn_class_name,
                k_runtime_sora_pawn_catalog[4].pawn_class_name,
                k_runtime_sora_pawn_catalog[5].pawn_class_name,
                k_runtime_sora_pawn_catalog[6].pawn_class_name,
                k_runtime_sora_pawn_catalog[7].pawn_class_name,
                k_runtime_sora_pawn_catalog[8].pawn_class_name,
                k_runtime_sora_pawn_catalog[9].pawn_class_name,
                STR("p_ex001_Pawn"),
                STR("TresPlayerPawn"),
                STR("TresPlayerPawnSora"),
                STR("TresCharPawnBase"),
                STR("TresPlayerController"),
                STR("TresPlayerState"),
                STR("TresGameInstance"),
                STR("GameInstance"),
                STR("TresSaveGame"),
                STR("TresSaveData"),
                STR("TresEquipment"),
                STR("TresEquipmentComponent"),
                STR("TresAbilityData"),
                STR("TresAbilitySet"),
                STR("EventPersistanceData_C"),
        };
        for (std::wstring_view class_name : class_names)
        {
            std::vector<RC::Unreal::UObject*> objects{};
            RC::Unreal::UObjectGlobals::FindAllOf(class_name, objects);
            for (RC::Unreal::UObject* object : objects)
            {
                if (is_live_ability_owner_candidate(object))
                {
                    append_ability_discovery_candidate(candidates, object);
                }
                if (candidates.size() >= max_direct_owner_candidates)
                {
                    break;
                }
            }
            if (candidates.size() >= max_direct_owner_candidates)
            {
                break;
            }
        }

        for (size_t index = 0; index < candidates.size() && candidates.size() < max_followed_owner_candidates; ++index)
        {
            RC::Unreal::UObject* object = candidates[index];
            if (!object || !kh3ap::is_uobject_valid_fast(object))
            {
                continue;
            }
            RC::Unreal::UClass* object_class = object->GetClassPrivate();
            if (!object_class)
            {
                continue;
            }

            int followed_count = 0;
            for (RC::Unreal::FProperty* property : object_class->ForEachPropertyInChain())
            {
                if (!property || property->GetClass().GetName() != STR("ObjectProperty"))
                {
                    continue;
                }

                const int offset = property->GetOffset_Internal();
                const int element_size = property->GetElementSize();
                if (offset < 0 || element_size < static_cast<int>(sizeof(RC::Unreal::UObject*)))
                {
                    continue;
                }

                RC::Unreal::UObject* value = nullptr;
                std::memcpy(&value, reinterpret_cast<const std::byte*>(object) + offset, sizeof(RC::Unreal::UObject*));
                if (should_follow_live_ability_owner_object_property(property->GetName(), value))
                {
                    append_ability_discovery_candidate(candidates, value);
                    ++followed_count;
                    if (followed_count >= max_followed_properties || candidates.size() >= max_followed_owner_candidates)
                    {
                        break;
                    }
                }
            }
        }

        return candidates;
    }

    auto is_ability_array_needle_candidate(RC::Unreal::UObject* object) -> bool
    {
        if (!object || !kh3ap::is_uobject_valid_fast(object))
        {
            return false;
        }

        const std::wstring object_name = object->GetFullName();
        const std::wstring class_name = object->GetClassPrivate() ? object->GetClassPrivate()->GetName() : std::wstring{};
        return contains_any_wide(
                       object_name,
                       {
                               STR("Sora"),
                               STR("Player"),
                               STR("GameInstance"),
                               STR("EventPersistance"),
                               STR("Randomizer"),
                               STR("ChrInit"),
                               STR("Equipment"),
                               STR("Ability"),
                               STR("Persistance"),
                       }) ||
               runtime_sora_pawn_catalog_matches_any(object_name, class_name) ||
               contains_any_wide(
                       class_name,
                       {
                               STR("Sora"),
                               STR("Player"),
                               STR("GameInstance"),
                               STR("EventPersistance"),
                               STR("Randomizer"),
                               STR("ChrInit"),
                               STR("Equipment"),
                               STR("Ability"),
                               STR("Persistance"),
                       });
    }

    auto collect_ability_array_needle_candidates(
            RC::Unreal::UObject* preferred_object,
            RC::Unreal::UObject* last_world_context_object) -> std::vector<RC::Unreal::UObject*>
    {
        std::vector<RC::Unreal::UObject*> candidates{};
        auto append_if_interesting = [&candidates](RC::Unreal::UObject* object) {
            if (is_ability_array_needle_candidate(object))
            {
                append_ability_discovery_candidate(candidates, object);
            }
        };

        append_if_interesting(preferred_object);
        append_if_interesting(last_world_context_object);

        for (RC::Unreal::UObject* object : collect_ability_state_probe_candidates(preferred_object, last_world_context_object))
        {
            append_if_interesting(object);
        }

        const std::array<std::wstring_view, 26> class_names{{
                k_runtime_sora_pawn_catalog[0].pawn_class_name,
                k_runtime_sora_pawn_catalog[1].pawn_class_name,
                k_runtime_sora_pawn_catalog[2].pawn_class_name,
                k_runtime_sora_pawn_catalog[3].pawn_class_name,
                k_runtime_sora_pawn_catalog[4].pawn_class_name,
                k_runtime_sora_pawn_catalog[5].pawn_class_name,
                k_runtime_sora_pawn_catalog[6].pawn_class_name,
                k_runtime_sora_pawn_catalog[7].pawn_class_name,
                k_runtime_sora_pawn_catalog[8].pawn_class_name,
                k_runtime_sora_pawn_catalog[9].pawn_class_name,
                STR("p_ex001_Pawn"),
                STR("TresPlayerController"),
                STR("TresPlayerControllerBase"),
                STR("TresPlayerState"),
                STR("TresGameInstance"),
                STR("GameInstance"),
                STR("TresChrInitData"),
                STR("TresEquipmentComponent"),
                STR("TresFormAbilitySet"),
                STR("TresEquipKeybladeData"),
                STR("EventPersistanceData_C"),
                STR("RetryPersistanceData_C"),
                STR("LevelUpPersistanceData_C"),
                STR("RandomizerData_C"),
                STR("RandomizerEventData_C"),
                STR("Utilities_C"),
        }};

        for (std::wstring_view class_name : class_names)
        {
            std::vector<RC::Unreal::UObject*> objects{};
            RC::Unreal::UObjectGlobals::FindAllOf(class_name, objects);
            for (RC::Unreal::UObject* object : objects)
            {
                append_if_interesting(object);
                if (candidates.size() >= max_direct_owner_candidates)
                {
                    return candidates;
                }
            }
        }

        return candidates;
    }

    auto should_probe_ability_scalar_or_map_property(std::wstring_view property_name) -> bool
    {
        return contains_any_wide(
                property_name,
                {
                        STR("Ability"),
                        STR("Abilities"),
                        STR("Equip"),
                        STR("Skill"),
                        STR("Debug"),
                        STR("Have"),
                        STR("Enable"),
                        STR("Player"),
                        STR("Sora"),
                });
    }

    auto should_skip_heavy_live_owner_probe_for_reason(std::wstring_view reason) -> bool
    {
        return reason.find(STR("post_runtime_grant")) != std::wstring_view::npos ||
               reason.find(STR("post_manual_runtime_grant")) != std::wstring_view::npos;
    }

    auto should_log_targeted_sora_state_property(
            std::wstring_view property_name,
            std::wstring_view property_class) -> bool
    {
        if (property_class != STR("ObjectProperty") &&
            property_class != STR("ArrayProperty") &&
            property_class != STR("BoolProperty") &&
            property_class != STR("ByteProperty") &&
            property_class != STR("EnumProperty") &&
            property_class != STR("IntProperty") &&
            property_class != STR("UInt32Property") &&
            property_class != STR("Int64Property") &&
            property_class != STR("UInt64Property") &&
            property_class != STR("NameProperty") &&
            property_class != STR("StrProperty"))
        {
            return false;
        }

        return contains_any_wide(
                property_name,
                {
                        STR("Ability"),
                        STR("Abilities"),
                        STR("Action"),
                        STR("Bonus"),
                        STR("Command"),
                        STR("Equip"),
                        STR("Form"),
                        STR("Have"),
                        STR("Skill"),
                        STR("Use"),
                        STR("Enable"),
                        STR("Player"),
                        STR("Sora"),
                });
    }

    auto should_follow_ability_discovery_object_property(
            std::wstring_view property_name,
            RC::Unreal::UObject* value) -> bool
    {
        if (!value || !kh3ap::is_uobject_valid_fast(value))
        {
            return false;
        }

        const std::wstring value_name = value->GetFullName();
        const std::wstring class_name = value->GetClassPrivate() ? value->GetClassPrivate()->GetName() : std::wstring{};
        return contains_any_wide(property_name, {STR("Ability"), STR("Bonus"), STR("Equip"), STR("Save"), STR("Player"), STR("Data")}) ||
               contains_any_wide(value_name, {STR("Ability"), STR("Bonus"), STR("Equip"), STR("Save"), STR("Player"), STR("Sora")}) ||
               runtime_sora_pawn_catalog_matches_any(value_name, class_name) ||
               contains_any_wide(class_name, {STR("Ability"), STR("Bonus"), STR("Equip"), STR("Save"), STR("Player"), STR("Data")});
    }

    auto collect_ability_discovery_candidates(
            RC::Unreal::UObject* preferred_object,
            RC::Unreal::UObject* last_world_context_object) -> std::vector<RC::Unreal::UObject*>
    {
        std::vector<RC::Unreal::UObject*> candidates = collect_ability_state_probe_candidates(preferred_object, last_world_context_object);

        const std::array<std::wstring_view, 31> class_names{
                STR("TresAbility"),
                STR("TresAbilityData"),
                STR("TresAbilitySet"),
                STR("TresAbilityManager"),
                STR("TresFormAbilitySet"),
                STR("TresBonus"),
                STR("TresBonusData"),
                STR("TresPlayerData"),
                STR("TresPlayerState"),
                STR("TresPlayerController"),
                STR("TresEquipmentComponent"),
                STR("TresEquipment"),
                STR("TresSaveGame"),
                STR("TresSaveData"),
                STR("TresGameInstance"),
                STR("GameInstance"),
                k_runtime_sora_pawn_catalog[0].pawn_class_name,
                k_runtime_sora_pawn_catalog[1].pawn_class_name,
                k_runtime_sora_pawn_catalog[2].pawn_class_name,
                k_runtime_sora_pawn_catalog[3].pawn_class_name,
                k_runtime_sora_pawn_catalog[4].pawn_class_name,
                k_runtime_sora_pawn_catalog[5].pawn_class_name,
                k_runtime_sora_pawn_catalog[6].pawn_class_name,
                k_runtime_sora_pawn_catalog[7].pawn_class_name,
                k_runtime_sora_pawn_catalog[8].pawn_class_name,
                k_runtime_sora_pawn_catalog[9].pawn_class_name,
                STR("p_ex001_FormAbility"),
                STR("EventPersistanceData_C"),
                STR("Utilities_C"),
                STR("RandomizerData_C"),
                STR("RandomizerEventData_C"),
        };
        for (std::wstring_view class_name : class_names)
        {
            std::vector<RC::Unreal::UObject*> objects{};
            RC::Unreal::UObjectGlobals::FindAllOf(class_name, objects);
            for (RC::Unreal::UObject* object : objects)
            {
                append_ability_discovery_candidate(candidates, object);
                if (candidates.size() >= max_direct_owner_candidates)
                {
                    break;
                }
            }
            if (candidates.size() >= max_direct_owner_candidates)
            {
                break;
            }
        }

        for (size_t index = 0; index < candidates.size() && candidates.size() < max_followed_owner_candidates; ++index)
        {
            RC::Unreal::UObject* object = candidates[index];
            if (!object || !kh3ap::is_uobject_valid_fast(object))
            {
                continue;
            }
            RC::Unreal::UClass* object_class = object->GetClassPrivate();
            if (!object_class)
            {
                continue;
            }

            int followed_count = 0;
            for (RC::Unreal::FProperty* property : object_class->ForEachPropertyInChain())
            {
                if (!property || property->GetClass().GetName() != STR("ObjectProperty"))
                {
                    continue;
                }

                const int offset = property->GetOffset_Internal();
                const int element_size = property->GetElementSize();
                if (offset < 0 || element_size < static_cast<int>(sizeof(RC::Unreal::UObject*)))
                {
                    continue;
                }

                const auto* object_bytes = reinterpret_cast<const std::byte*>(object);
                RC::Unreal::UObject* value = nullptr;
                std::memcpy(&value, object_bytes + offset, sizeof(RC::Unreal::UObject*));
                if (should_follow_ability_discovery_object_property(property->GetName(), value))
                {
                    append_ability_discovery_candidate(candidates, value);
                    ++followed_count;
                    if (followed_count >= max_followed_properties || candidates.size() >= max_followed_owner_candidates)
                    {
                        break;
                    }
                }
            }
        }

        return candidates;
    }

    auto should_probe_ability_state_property(
            std::wstring_view property_name,
            std::wstring_view property_class) -> bool
    {
        if (property_class != STR("NameProperty") && property_class != STR("StrProperty") &&
            property_class != STR("ObjectProperty") && property_class != STR("ArrayProperty") &&
            property_class != STR("MapProperty") && property_class != STR("BoolProperty") &&
            property_class != STR("ByteProperty") && property_class != STR("EnumProperty") &&
            property_class != STR("IntProperty") && property_class != STR("UInt32Property") &&
            property_class != STR("Int64Property") && property_class != STR("UInt64Property") &&
            property_class != STR("StructProperty"))
        {
            return false;
        }

        return contains_any_wide(
                property_name,
                {
                        STR("Ability"),
                        STR("Abilities"),
                        STR("Bonus"),
                        STR("Equip"),
                        STR("Skill"),
                        STR("Command"),
                        STR("Action"),
                });
    }
}
