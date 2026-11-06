#include "kh3ap/features/player/characters/character_swap.h"
#include "kh3ap/features/grants/messages/ap_message_gate.h"

#include "kh3ap/bridge/utilities.h"

#include "kh3ap/features/kh02/story/kh02_story.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>

#include <format>
#include <sstream>

namespace kh3ap
{
    namespace
    {
        auto runtime_ap_message_has_valid_sora_pawn(RuntimeState& runtime_state) -> bool
        {
            const auto& pawn_state = runtime_state.grant_state().pawn_state();
            // Pawn discovery performs the expensive UObject validation once and
            // records that result in candidate_trusted(). Repeating IsReal and
            // GetFullName here walks UE4SS's global object array every 500 ms,
            // which is visible as a heartbeat hitch in object-heavy maps.
            return pawn_state.object() &&
                   pawn_state.candidate_trusted();
        }
    }

    auto extend_runtime_ap_message_quiet_window(
            RuntimeApMessageGateState& state,
            std::wstring_view source,
            std::chrono::milliseconds quiet_window) -> void
    {
        const auto quiet_until = std::chrono::steady_clock::now() + quiet_window;
        if (quiet_until <= state.quiet_until &&
            state.quiet_source == source)
        {
            return;
        }

        state.quiet_until = quiet_until;
        state.quiet_source = std::wstring{source};
    }

    auto runtime_ap_message_defer_reason(
            RuntimeApMessageGateState& state,
            RuntimeState& runtime_state,
            bool pending_request_file_exists) -> std::wstring
    {
        if (!pending_request_file_exists)
        {
            return {};
        }

        const auto now = std::chrono::steady_clock::now();
        if (state.quiet_until != std::chrono::steady_clock::time_point{} &&
            now < state.quiet_until)
        {
            return state.quiet_source.empty()
                    ? std::wstring{STR("waiting_for_runtime_ap_message_quiet_window")}
                    : std::format(STR("waiting_for_runtime_ap_message_quiet_window:{}"), state.quiet_source);
        }

        const auto blocking_ui = runtime_state.ui_diagnostic_state().blocking_ui_state().snapshot();
        if (blocking_ui.active)
        {
            return blocking_ui.context.empty()
                    ? std::wstring{STR("waiting_for_blocking_ui_clear")}
                    : std::format(STR("waiting_for_blocking_ui_clear:{}"), blocking_ui.context);
        }

        // The Sora cache can still contain the departed hero during a swap.
        // While Aqua is possessed, her live presentation gate is authoritative.
        if (character_swap::current_hero())
        {
            return character_swap::reward_context() ? std::wstring{} : L"waiting_for_ready_alternate";
        }
        auto* hero = Kh02Story::current_hero();
        if (hero && hero->GetClassPrivate()->GetName() == L"p_ex002_Pawn_C")
        {
            return persistent_player_reward_context() ? std::wstring{} : L"waiting_for_ready_kh02_aqua";
        }
        if (!runtime_ap_message_has_valid_sora_pawn(runtime_state))
        {
            return STR("waiting_for_valid_sora_pawn");
        }

        return {};
    }

    auto maybe_log_runtime_ap_message_deferred(
            RuntimeApMessageGateState& state,
            const std::optional<std::filesystem::path>& bridge_dir,
            std::wstring_view reason,
            std::wstring_view build_stamp,
            std::wstring_view runtime_context) -> void
    {
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        if (state.last_defer_reason == reason)
        {
            return;
        }
        if (!bridge_dir.has_value())
        {
            return;
        }

        state.last_defer_reason = std::wstring{reason};
        std::ostringstream json;
        json << "{\"schema\":\"kh3ap.runtime_ap_message_deferred.v1\"";
        json << ",\"recorded_at_utc\":\"" << now_iso8601_utc() << "\"";
        json << ",\"build_stamp\":\"" << json_escape(build_stamp) << "\"";
        json << ",\"reason\":\"" << json_escape(reason) << "\"";
        json << ",\"runtime_context\":\"" << json_escape(runtime_context) << "\"";
        json << "}\n";
        append_text_file(*bridge_dir / "runtime_ap_message_deferred.jsonl", json.str());
#endif
    }
}
