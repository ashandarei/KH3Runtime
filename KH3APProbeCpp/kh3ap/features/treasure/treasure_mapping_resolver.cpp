#include "kh3ap/features/treasure/treasure_mapping_resolver.h"

#include "kh3ap/core/text_format.h"

#include <regex>

namespace kh3ap
{
    namespace
    {
        struct ChestNameRemap
        {
            std::wstring_view world, box_type;
            int index; // -1 matches any parsed index.
            std::wstring_view level_token, actor_token, source_id, reason_suffix;
        };

        // Shipped actor names do not always match the treasure table's numbering.
        // Keep these in priority order; a remap missing from the seed falls through.
        constexpr ChestNameRemap chest_name_remaps[]{
            {L"HE", L"SBOX", 0, L"he_02_treasure", L"he_TreasureBox_0",
                L"HE_SBOX_017", L"_olympus_he02_actor_zero_remap"},
            {L"HE", L"SBOX", 0, L"he_03_treasure", L"g_he_TreasureBox_0",
                L"HE_SBOX_026", L"_olympus_he03_actor_zero_remap"},
            {L"TS", L"LBOX", 1, L"ts_01_treasure", L"g_TreasureBoxL1",
                L"TS_LBOX_002", L"_toybox_ts01_actor_l1_to_lbox002"},
            {L"MI", L"LBOX", 0, L"mi_02_treasure", L"g_mi_TreasureBoxL_0",
                L"MI_LBOX_004", L"_monstropolis_mi02_actor_zero_to_lbox004"},
            {L"MI", L"SBOX", 0, L"mi_04_treasure", L"PersistentLevel.g_mi_TreasureBox_0",
                L"MI_SBOX_019", L"_monstropolis_mi04_actor_zero_to_sbox019"},
            {L"TS", L"LBOX", 3, L"ts_01_treasure", L"g_TreasureBoxL3",
                L"TS_LBOX_001", L"_toybox_ts01_actor_l3_to_lbox001"},
            {L"TS", L"SBOX", -1, L"ts_01_treasure", L"PersistentLevel.g_ts_TreasureBox1",
                L"TS_SBOX_002", L"_toybox_ts01_actor_g_ts_treasurebox1_to_sbox002"},
            {L"TS", L"SBOX", -1, L"ts_01_treasure", L"PersistentLevel.g_ts_TreasureBox_C_1",
                L"TS_SBOX_001", L"_toybox_ts01_actor_g_ts_treasurebox_c_1_to_sbox001"},
            {L"TS", L"SBOX", 1, L"ts_02_treasure", L"PersistentLevel.g_ts_TreasureBox_1",
                L"TS_SBOX_006", L"_toybox_ts02_actor_g_ts_treasurebox_1_to_sbox006"},
            {L"KG", L"LBOX", 1, L"kg_02_treasure", L"01_g_kg_TreasureBoxL",
                L"KG_LBOX_002", L"_kg02_actor_01_to_lbox002"},
            {L"KG", L"SBOX", 2, L"kg_01_treasure", L"g_kg_TreasureBox_2",
                L"KG_SBOX_004", L"_kg01_actor_2_to_sbox004"},
            {L"KG", L"SBOX", 2, L"kg_02_treasure", L"02_g_kg_TreasureBox",
                L"KG_SBOX_001", L"_kg02_actor_02_to_sbox001"},
            {L"KG", L"SBOX", 3, L"kg_02_treasure", L"03_g_kg_TreasureBox",
                L"KG_SBOX_002", L"_kg02_actor_03_to_sbox002"},
            {L"KG", L"SBOX", 4, L"kg_02_treasure", L"04_g_kg_TreasureBox",
                L"KG_SBOX_003", L"_kg02_actor_04_to_sbox003"},
            {L"BT", L"SBOX", 2, L"bt_07_treasure", L"g_bt_TreasureBox_l_2",
                L"BT_LBOX_002", L"_scala_bt07_lower_l_2_to_lbox002"},
            {L"BT", L"SBOX", 2, L"bt_07_treasure", L"g_bt_TreasureBox_2",
                L"BT_SBOX_001", L"_scala_bt07_actor_2_to_sbox001"},
            {L"BT", L"SBOX", 5, L"bt_07_treasure", L"g_bt_TreasureBox_5",
                L"BT_SBOX_004", L"_scala_bt07_actor_5_to_sbox004"},
            {L"BT", L"SBOX", 3, L"bt_07_treasure", L"g_bt_TreasureBox_3",
                L"BT_SBOX_002", L"_scala_bt07_actor_3_to_sbox002"},
            {L"BT", L"SBOX", 4, L"bt_07_treasure", L"g_bt_TreasureBox_4",
                L"BT_SBOX_003", L"_scala_bt07_actor_4_to_sbox003"},
            {L"BT", L"SBOX", 6, L"bt_07_treasure", L"g_bt_TreasureBox_6",
                L"BT_SBOX_005", L"_scala_bt07_actor_6_to_sbox005"},
            {L"BT", L"SBOX", 7, L"bt_07_treasure", L"g_bt_TreasureBox_7",
                L"BT_SBOX_006", L"_scala_bt07_actor_7_to_sbox006"},
            {L"BT", L"SBOX", 8, L"bt_07_treasure", L"g_bt_TreasureBox_8",
                L"BT_SBOX_007", L"_scala_bt07_actor_8_to_sbox007"},
            {L"BT", L"SBOX", 2, L"bt_08_treasure", L"g_bt_TreasureBox_l_2",
                L"BT_LBOX_001", L"_scala_bt08_lower_l_2_to_lbox001"},
        };

        auto resolve_mapping(
                std::wstring_view full_name,
                const std::unordered_map<std::wstring, std::wstring>& treasure_lookup,
                std::wstring_view prefix, std::wstring_view box_type, int box_index,
                std::wstring_view reason) -> std::optional<TreasureMappingResult>
        {
            if (prefix.empty() || box_type.empty() || box_index < 0)
            {
                return std::nullopt;
            }

            const auto make_result = [&](std::wstring source_id, std::wstring mapping_reason) -> std::optional<TreasureMappingResult> {
                if (const auto found = treasure_lookup.find(source_id); found != treasure_lookup.end())
                {
                    return TreasureMappingResult{
                            .bridge_key = found->second,
                            .mapping_reason = std::move(mapping_reason),
                            .world_code = std::wstring{prefix},
                            .source_id = std::move(source_id),
                    };
                }

                if (treasure_lookup.empty())
                {
                    return TreasureMappingResult{
                            .bridge_key = std::wstring{L"Treasure"} + std::wstring{prefix} + L":" + source_id,
                            .mapping_reason = std::wstring{reason} + L"_rough_slot_data_unavailable",
                            .world_code = std::wstring{prefix},
                            .source_id = std::move(source_id),
                    };
                }

                return std::nullopt;
            };

            for (const auto& remap : chest_name_remaps)
            {
                if (prefix != remap.world || box_type != remap.box_type ||
                    (remap.index >= 0 && box_index != remap.index) ||
                    full_name.find(remap.level_token) == std::wstring_view::npos ||
                    full_name.find(remap.actor_token) == std::wstring_view::npos)
                    continue;

                if (auto result = make_result(std::wstring{remap.source_id}, std::wstring{reason} + std::wstring{remap.reason_suffix}))
                    return result;
            }

            std::wstring source_id = std::wstring{prefix} + L"_" + std::wstring{box_type} + L"_" + zero_pad_number(box_index, 3);
            if (const auto exact = make_result(source_id, std::wstring{reason}); exact.has_value())
            {
                return exact;
            }

            if (box_index == 0)
            {
                source_id = std::wstring{prefix} + L"_" + std::wstring{box_type} + L"_001";
                if (const auto zero_based = make_result(source_id, std::wstring{reason} + L"_zero_based_first_box"); zero_based.has_value())
                {
                    return zero_based;
                }
            }

            return std::nullopt;
        }

    }

    auto TreasureMappingResolver::infer_from_actor_name(
            std::wstring_view full_name,
            const std::unordered_map<std::wstring, std::wstring>& treasure_lookup)
            -> std::optional<TreasureMappingResult>
    {
        if (full_name.empty()) return std::nullopt;
        const auto validate_mapping = [&](std::wstring_view prefix, std::wstring_view box_type,
                                          int box_index, std::wstring_view reason) {
            return resolve_mapping(full_name, treasure_lookup, prefix, box_type, box_index, reason);
        };

        std::match_results<std::wstring_view::const_iterator> match;
        const auto search = [&](const std::wregex& pattern) {
            return std::regex_search(full_name.begin(), full_name.end(), match, pattern);
        };

        if (full_name.find(L"TreasureBox") != std::wstring_view::npos &&
            (full_name.find(L"/Game/Levels/ew/") != std::wstring_view::npos ||
             full_name.find(L"/Game/Maps/ew/") != std::wstring_view::npos ||
             full_name.find(L"ew_02_treasure") != std::wstring_view::npos))
        {
            if (const auto final_world = validate_mapping(L"EW", L"LBOX", 1, L"validated_final_world_single_large_chest"); final_world.has_value())
            {
                return final_world;
            }
        }

        // The Leviathan ship chest uses a generic special-ship actor name with no CA/SBOX token.
        // Depending on how the ship level is loaded, the actor can be owned by any ca_* map.
        if (full_name.find(L"SpecialShip_TreasureBox_C") != std::wstring_view::npos &&
            (full_name.find(L"/Game/Levels/ca/ca_") != std::wstring_view::npos ||
             full_name.find(L"/Game/Maps/ca/ca_") != std::wstring_view::npos))
        {
            if (const auto leviathan = validate_mapping(L"CA", L"SBOX", 25, L"validated_caribbean_special_ship_actor"); leviathan.has_value())
            {
                return leviathan;
            }
        }

        static const std::wregex explicit_source_pattern(LR"(g_([a-z][a-z0-9]+)_TreasureBox_([A-Z][A-Z0-9]+)_([LS]BOX)_(\d+))");
        if (search(explicit_source_pattern))
        {
            const std::wstring prefix = uppercase_ascii(match[1].str());
            if (prefix == match[2].str())
                return validate_mapping(prefix, match[3].str(), std::stoi(match[4].str()), L"validated_actor_explicit_source_id");
        }

        static const std::wregex treasure_suffix_pattern(LR"(g_([a-z][a-z0-9]+)_Treasure([LS])BOX_(\d+))");
        if (search(treasure_suffix_pattern))
        {
            const std::wstring prefix = uppercase_ascii(match[1].str());
            const std::wstring box_type = match[2].str() == L"L" ? L"LBOX" : L"SBOX";
            return validate_mapping(prefix, box_type, std::stoi(match[3].str()), L"validated_actor_treasure_box_suffix");
        }

        static const std::wregex compact_box_pattern(LR"([.:]([a-z][a-z0-9]+)_Treasure([LlSs])Box_(\d+))");
        if (search(compact_box_pattern))
        {
            const std::wstring prefix = uppercase_ascii(match[1].str());
            const std::wstring box_type = (match[2].str() == L"L" || match[2].str() == L"l") ? L"LBOX" : L"SBOX";
            return validate_mapping(prefix, box_type, std::stoi(match[3].str()), L"validated_actor_compact_box_suffix");
        }

        static const std::wregex compact_small_pattern(LR"([.:]([a-z][a-z0-9]+)_TreasureBox_(\d+))");
        if (search(compact_small_pattern))
        {
            const std::wstring prefix = uppercase_ascii(match[1].str());
            return validate_mapping(prefix, L"SBOX", std::stoi(match[2].str()), L"validated_actor_compact_small_suffix");
        }

        static const std::wregex prefixed_large_instance_pattern(LR"([.:](\d+)_g_([a-z][a-z0-9]+)_TreasureBoxL\b)");
        if (search(prefixed_large_instance_pattern))
        {
            const std::wstring prefix = uppercase_ascii(match[2].str());
            return validate_mapping(prefix, L"LBOX", std::stoi(match[1].str()), L"validated_actor_prefixed_instance_map");
        }

        static const std::wregex prefixed_small_instance_pattern(LR"([.:](\d+)_g_([a-z][a-z0-9]+)_TreasureBox\b)");
        if (search(prefixed_small_instance_pattern))
        {
            const std::wstring prefix = uppercase_ascii(match[2].str());
            return validate_mapping(prefix, L"SBOX", std::stoi(match[1].str()), L"validated_actor_prefixed_instance_map");
        }

        static const std::wregex class_world_pattern(LR"(^g_([a-z][a-z0-9]+)_TreasureBoxL?_C)");
        if (search(class_world_pattern))
        {
            const std::wstring prefix = uppercase_ascii(match[1].str());

            static const std::wregex class_large_area_pattern(LR"([.:]g_TreasureBoxL_[A-Za-z0-9]+_(\d+))");
            if (search(class_large_area_pattern))
            {
                return validate_mapping(prefix, L"LBOX", std::stoi(match[1].str()), L"validated_actor_class_world_area_large_suffix");
            }

            static const std::wregex class_large_pattern(LR"([.:]g_TreasureBoxL_?(\d+))");
            if (search(class_large_pattern))
            {
                return validate_mapping(prefix, L"LBOX", std::stoi(match[1].str()), L"validated_actor_class_world_instance_suffix");
            }

            static const std::wregex class_small_area_pattern(LR"([.:]g_TreasureBox_[A-Za-z0-9]+_(\d+))");
            if (search(class_small_area_pattern))
            {
                return validate_mapping(prefix, L"SBOX", std::stoi(match[1].str()), L"validated_actor_class_world_area_small_suffix");
            }

            static const std::wregex class_small_pattern(LR"([.:]g_TreasureBox_?(\d+))");
            if (search(class_small_pattern))
            {
                return validate_mapping(prefix, L"SBOX", std::stoi(match[1].str()), L"validated_actor_class_world_instance_suffix");
            }
        }

        static const std::wregex large_instance_pattern(LR"(g_([a-z][a-z0-9]+)_TreasureBoxL_?(\d+))");
        if (search(large_instance_pattern))
        {
            const std::wstring prefix = uppercase_ascii(match[1].str());
            return validate_mapping(prefix, L"LBOX", std::stoi(match[2].str()), L"validated_actor_instance_map");
        }

        static const std::wregex large_area_instance_pattern(LR"(g_([a-z][a-z0-9]+)_TreasureBoxL_[A-Za-z0-9]+_(\d+))");
        if (search(large_area_instance_pattern))
        {
            const std::wstring prefix = uppercase_ascii(match[1].str());
            return validate_mapping(prefix, L"LBOX", std::stoi(match[2].str()), L"validated_actor_instance_area_map");
        }

        static const std::wregex small_instance_pattern(LR"(g_([a-z][a-z0-9]+)_TreasureBox_?(\d+))");
        if (search(small_instance_pattern))
        {
            const std::wstring prefix = uppercase_ascii(match[1].str());
            return validate_mapping(prefix, L"SBOX", std::stoi(match[2].str()), L"validated_actor_instance_map");
        }

        static const std::wregex small_area_instance_pattern(LR"(g_([a-z][a-z0-9]+)_TreasureBox_[A-Za-z0-9]+_(\d+))");
        if (search(small_area_instance_pattern))
        {
            const std::wstring prefix = uppercase_ascii(match[1].str());
            return validate_mapping(prefix, L"SBOX", std::stoi(match[2].str()), L"validated_actor_instance_area_map");
        }

        return std::nullopt;
    }
}
