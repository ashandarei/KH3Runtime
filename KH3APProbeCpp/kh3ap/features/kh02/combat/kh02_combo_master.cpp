#include "kh02_combo_master.h"
#include "kh3ap/app/enums.h"
#include "kh3ap/bridge/rewards/reconciliation.h"
#include "kh3ap/core/input/runtime_player_control.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/features/grants/vbonus/vbonus_grant_call_helpers.h"
#include "kh3ap/features/player/objects/object_resolver.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include "kh3ap/features/kh02/story/kh02_story.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <limits>

namespace kh3ap
{
    namespace
    {
        constexpr auto combo_master_token = L"ETresAbilityKind::COMBO_MASTER";
        constexpr auto maximum_ability_id = std::numeric_limits<uint8_t>::max();
    } // namespace

    auto Kh02ComboMaster::update(RC::Unreal::UObject* pawn) -> bool
    {
        auto* world = kh02_current_world();
        const auto kind = kh02_world_kind(world);
        if (kind == Kh02WorldKind::unavailable)
        {
            return false;
        }
        const bool in_kh02 = kind == Kh02WorldKind::story;
        if (in_kh02 ? visit.entry_complete() : !visit.active())
        {
            return true;
        }
        if (!is_uobject_valid_fast(pawn) || kh02_actor_world(pawn) != world)
        {
            return false;
        }
        if (in_kh02 ? !Kh02Story::native_save_ready(pawn) : !runtime_sora_control_ready(pawn))
        {
            return false;
        }
        if (!in_kh02 && !RuntimeObjectResolver::is_sora_ability_pawn_candidate(pawn))
        {
            return false;
        }
        const auto receipts = reconciliation::view();
        if (!receipts->ready || !receipts->expanded)
        {
            return false;
        }
        const auto ability = try_resolve_runtime_enum_value(combo_master_token);
        if (!ability || *ability == 0 || *ability > maximum_ability_id)
        {
            return false;
        }
        const bool received_from_ap =
            std::any_of(receipts->expanded->begin(), receipts->expanded->end(),
                        [](const auto& receipt)
                        {
                            const auto& grants = receipt.second;
                            return std::any_of(grants.begin(), grants.end(), [](const auto& grant)
                                               { return grant.runtime_token == combo_master_token; });
                        });
        const bool leaving = !in_kh02 && visit.active();
        visit.update(
            in_kh02, received_from_ap,
            [&]
            {
                bool granted{};
                RuntimeSoraAbilityEntryState state;
                if (!try_grant_unique_equipped_runtime_sora_ability(static_cast<uint8_t>(*ability), pawn, &granted,
                                                                    &state, &failure))
                {
                    return false;
                }
                failure.clear();
                phase = granted ? "temporary_copy_equipped" : "already_owned";
                RC::Output::send<RC::LogLevel::Verbose>(STR("[KH3APProbeCpp] KH02 Combo Master entry phase={}\n"),
                                                        std::wstring(phase.begin(), phase.end()));
                return state.owned;
            },
            [&]
            {
                if (!try_remove_runtime_sora_durable_ability_copy(static_cast<uint8_t>(*ability), pawn, &failure))
                {
                    return false;
                }
                failure.clear();
                phase = "temporary_copy_removed";
                return true;
            });
        if (leaving && !visit.active())
        {
            if (received_from_ap)
            {
                failure.clear();
                phase = "kept_ap_received";
            }
            RC::Output::send<RC::LogLevel::Verbose>(STR("[KH3APProbeCpp] KH02 Combo Master exit phase={}\n"),
                                                    std::wstring(phase.begin(), phase.end()));
        }
        return in_kh02 ? visit.entry_complete() : !visit.active();
    }

#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    auto Kh02ComboMaster::snapshot(RC::Unreal::UObject* pawn) const -> std::string
    {
        RuntimeSoraAbilityEntryState state;
        const auto ability = try_resolve_runtime_enum_value(combo_master_token);
        const bool readable = ability && *ability > 0 && *ability <= maximum_ability_id &&
                              try_read_runtime_sora_ability_entry_state(static_cast<uint8_t>(*ability), pawn, &state);
        return nlohmann::json{{"visit_active", visit.active()}, {"phase", phase},
                              {"readable", readable},           {"owned", state.owned},
                              {"equipped", state.equipped},     {"error", std::string(failure.begin(), failure.end())}}
            .dump();
    }
#endif
} // namespace kh3ap
