#pragma once

#include "kh02_progress_policy.h"
#include <optional>

namespace kh3ap::kh02_checks
{
    struct Condition
    {
        std::string id;
        std::string flag;
        int challenge{-1};
    };

    inline auto condition(const nlohmann::json& row) -> std::optional<Condition>
    {
        if (row.value("source_table", "") != "KH02")
        {
            return std::nullopt;
        }
        Condition result;
        result.id = row.at("source_id").get<std::string>();
        const auto type = row.at("type").get<std::string>();
        if (type == "kh02_objective" && !row.contains("kh02_flag"))
        {
            result.challenge = kh02_progress::integer(row.at("kh02_challenge"), 0, 50);
            const int number = result.challenge + 1;
            const auto expected = "Objective:" + std::string(number < 10 ? "00" : "0") + std::to_string(number);
            if (result.id == expected)
            {
                return result;
            }
        }
        else if ((type == "kh02_chest" || type == "kh02_boss" ||
                  (type == "character_unlock" && result.id == "Boss:UnlockAqua")) &&
                 !row.contains("kh02_challenge"))
        {
            result.flag = row.at("kh02_flag").get<std::string>();
            if (kh02_progress::flag_key(result.flag) &&
                ((type == "kh02_boss" || type == "character_unlock") && result.id.starts_with("Boss:") ||
                 type == "kh02_chest" && result.id.starts_with("Treasure:DW_") &&
                     result.flag == "gameflow_DW_CHALLENGE_CHEST_" + result.id.substr(9)))
            {
                return result;
            }
        }
        throw std::runtime_error("Invalid KH0.2 check condition: " + result.id);
    }

    inline auto earned(const nlohmann::json& progress, const Condition& condition) -> bool
    {
        if (progress.is_null())
        {
            return false;
        }
        if (condition.challenge >= 0)
        {
            return progress.at("challenges").value(std::to_string(condition.challenge), 0) == 2;
        }
        return progress.at("flags").value(condition.flag, 0) == 1;
    }

    inline auto earned(const kh02_progress::SnapshotPtr& progress, const Condition& condition) -> bool
    {
        if (!progress)
        {
            return false;
        }
        if (condition.challenge >= 0)
        {
            const auto found = progress->challenges.find(condition.challenge);
            return found != progress->challenges.end() && found->second == 2;
        }
        return kh02_progress::flag_value(progress->flags, condition.flag) == 1;
    }

    // The second Forest Darkside has no distinct native victory bonus. Latch
    // only its original battle-end transition; restoring a later checkpoint
    // or opening the map cannot establish a kill.
    inline auto second_darkside_victory(std::wstring_view caller, std::wstring_view key, int previous, int next) -> bool
    {
        return caller.starts_with(L"/Game/Zevels/dw/dw_08/umap/dw_08_gameflow.") && key == L"gameflow_dw" &&
               previous == 340 && next == 341;
    }
} // namespace kh3ap::kh02_checks
