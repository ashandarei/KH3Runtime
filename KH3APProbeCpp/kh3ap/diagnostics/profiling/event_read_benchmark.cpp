#include <span>
#include <utility>
#include "kh3ap/features/player/controller_abilities/models.h"
#include "kh3ap/diagnostics/profiling/event_read_benchmark.h"
#include "kh3ap/features/grants/delivery/item_call_helpers.h"
#include "kh3ap/features/grants/vbonus/vbonus_grant_call_helpers.h"
#include "kh3ap/features/grants/vbonus/vbonus_grant_internal.h"
#include "kh3ap/features/player/keyblades/keyblade_options_helpers.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/features/ui/map_overlay/support.h"
#include "kh3ap/bridge/utilities.h"
#include <nlohmann/json.hpp>
#include <array>
#include <chrono>
#include <stdexcept>

namespace kh3ap
{
    namespace
    {
        using Json = nlohmann::json;
        using Clock = std::chrono::steady_clock;
        template <class Read> auto measure(Read read) -> Json
        {
            constexpr int sample_limit = 12;
            constexpr auto budget = std::chrono::milliseconds(50);
            const auto started = Clock::now();
            const auto expected = read();
            const double first_us =
                std::chrono::duration<double, std::micro>(Clock::now() - started).count();
            Json samples = Json::array();
            for (int index = 0; index < sample_limit; ++index)
            {
                const auto sample_started = Clock::now();
                const auto actual = read();
                const double elapsed =
                    std::chrono::duration<double, std::micro>(Clock::now() - sample_started)
                        .count();
                if (actual != expected)
                {
                    throw std::runtime_error("event_benchmark_state_changed");
                }
                samples.push_back(elapsed);
                if (Clock::now() - started >= budget)
                {
                    break;
                }
            }
            return Json{{"first_us", first_us},
                        {"samples_us", samples},
                        {"value", expected},
                        {"unchanged", true}};
        }
    } // namespace
    auto benchmark_event_reads(RuntimeState& state, RC::Unreal::UObject* pawn) -> std::string
    {
        if (!is_uobject_valid_fast(pawn))
        {
            throw std::runtime_error("event_benchmark_requires_live_sora");
        }
        constexpr auto library = L"/Script/TresGame.Default__TresGameBlueprintLibrary";
        Json result{{"schema", "kh3ap.event_read_benchmark.v3"}, {"functions", Json::object()}};
        auto& functions = result["functions"];
        functions["keyblade_name"] = measure(
            [&]() -> Json
            {
                const auto name = read_equipped_keyblade_name(pawn);
                if (!name)
                {
                    throw std::runtime_error("event_benchmark_weapon_unavailable");
                }
                return to_utf8(*name);
            });
        functions["controller_ability_snapshot"] = measure(
            [&]() -> Json
            {
                std::array<RuntimeSoraAbilityEntryState,
                           controller_ability_detail::s_controller_abilities.size()>
                    entries{};
                if (!try_read_runtime_sora_ability_entry_states(
                        controller_ability_detail::ability_id::wall_run, pawn, entries, nullptr))
                {
                    throw std::runtime_error("event_benchmark_abilities_unavailable");
                }
                Json values = Json::array();
                for (const auto& entry : entries)
                {
                    values.push_back({{"owned", entry.owned}, {"equipped", entry.equipped}});
                }
                return values;
            });
        // Keep the preceding query patterns in diagnostics only so each build
        // compares separate and batched reads against the same live values.
        const auto ability_values = [](const auto& entries) -> Json
        {
            Json values = Json::array();
            for (const auto& entry : entries)
            {
                values.push_back({{"owned", entry.owned}, {"equipped", entry.equipped}});
            }
            return values;
        };
        constexpr std::array<uint8_t, 2> magic_links{115, 134};
        constexpr std::array<uint8_t, 5> shared_unlocks{113, 114, 115, 116, 134};
        functions["magic_links_separate"] = measure(
            [&]() -> Json
            {
                std::array<RuntimeSoraAbilityEntryState, magic_links.size()> entries{};
                for (size_t index = 0; index < entries.size(); ++index)
                {
                    if (!try_read_runtime_sora_ability_entry_state(magic_links[index], pawn,
                                                                   &entries[index]))
                    {
                        throw std::runtime_error("event_benchmark_magic_links_unavailable");
                    }
                }
                return ability_values(entries);
            });
        functions["magic_links_snapshot"] = measure(
            [&]() -> Json
            {
                std::array<RuntimeSoraAbilityEntryState, magic_links.size()> entries{};
                if (!try_read_runtime_sora_ability_entry_states(magic_links, pawn, entries))
                {
                    throw std::runtime_error("event_benchmark_magic_links_unavailable");
                }
                return ability_values(entries);
            });
        functions["shared_unlocks_separate"] = measure(
            [&]() -> Json
            {
                std::array<RuntimeSoraAbilityEntryState, shared_unlocks.size()> entries{};
                if (!try_read_runtime_sora_ability_entry_states(
                        shared_unlocks.front(), pawn,
                        std::span{entries.data(), entries.size() - 1}) ||
                    !try_read_runtime_sora_ability_entry_state(shared_unlocks.back(), pawn,
                                                               &entries.back()))
                {
                    throw std::runtime_error("event_benchmark_shared_unlocks_unavailable");
                }
                return ability_values(entries);
            });
        functions["shared_unlocks_snapshot"] = measure(
            [&]() -> Json
            {
                std::array<RuntimeSoraAbilityEntryState, shared_unlocks.size()> entries{};
                if (!try_read_runtime_sora_ability_entry_states(shared_unlocks, pawn, entries))
                {
                    throw std::runtime_error("event_benchmark_shared_unlocks_unavailable");
                }
                return ability_values(entries);
            });
        if (functions["magic_links_separate"]["value"] !=
                functions["magic_links_snapshot"]["value"] ||
            functions["shared_unlocks_separate"]["value"] !=
                functions["shared_unlocks_snapshot"]["value"])
        {
            throw std::runtime_error("event_benchmark_selection_mismatch");
        }
        functions["overlay_hud_snapshot"] = measure(
            [&]()
            {
                using namespace map_overlay_detail;
                const auto [hud, nav] = live_parts(pawn);
                auto* native = root(nav);
                auto* texture = object(nav, L"m_LoadedAsyncTexture");
                auto* canvas = object(nav, L"m_MapCanvas");
                auto* pc = object(nav, L"m_MarkerPc");
                auto* asset = object(nav, L"m_SwfMovie");
                auto* hud_root = root(hud);
                auto* host =
                    object(object(object(hud, L"m_NaviMap"), L"m_NaviMapCanvas"), L"m_ASObject");
                const std::array objects{hud, nav,   native,   texture, canvas,
                                         pc,  asset, hud_root, host};
                std::array<uintptr_t, objects.size()> identities{};
                for (size_t i = 0; i < objects.size(); ++i)
                {
                    if (!is_uobject_valid_fast(objects[i]))
                    {
                        throw std::runtime_error("event_benchmark_hud_unavailable");
                    }
                    identities[i] = reinterpret_cast<uintptr_t>(objects[i]);
                }
                return identities;
            });
        functions["quality_integer_flag"] = measure(
            [&]() -> Json
            {
                int32_t value{};
                if (!try_call_get_game_flag(
                        L"gameflow_BX", value, state,
                        L"/Script/TresGame.TresGameBlueprintLibrary:GetGameFlag", library, false,
                        nullptr))
                {
                    throw std::runtime_error("event_benchmark_integer_flag_unavailable");
                }
                return value;
            });
        functions["quality_boolean_flag"] = measure(
            [&]() -> Json
            {
                bool value{};
                if (!try_call_get_game_flag_boolean(
                        L"gameflow_CA_specialship_powerup", value, state,
                        L"/Script/TresGame.TresGameBlueprintLibrary:GetGameFlagBoolean", library,
                        false, nullptr))
                {
                    throw std::runtime_error("event_benchmark_boolean_flag_unavailable");
                }
                return value;
            });
        // Read-only evidence for whether native command setup can be skipped.
        vbonus_detail::VanillaSoraAbilityBanks banks{};
        if (!vbonus_detail::try_resolve_vanilla_sora_ability_banks(pawn, &banks, nullptr))
        {
            throw std::runtime_error("event_benchmark_magic_counts_unavailable");
        }
        Json counts = Json::array();
        for (uint8_t bonus = vbonus_detail::s_first_magic_vbonus_kind;
             bonus <= vbonus_detail::s_last_magic_vbonus_kind; ++bonus)
        {
            const auto value =
                vbonus_detail::try_read_vanilla_vbonus_count(banks.active_record, bonus);
            if (!value)
            {
                throw std::runtime_error("event_benchmark_magic_counts_unavailable");
            }
            counts.push_back(*value);
        }
        result["magic_bonus_counts"] = std::move(counts);
        return result.dump();
    }
} // namespace kh3ap
