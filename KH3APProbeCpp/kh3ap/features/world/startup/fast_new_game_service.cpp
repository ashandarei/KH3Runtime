#include "kh3ap/features/world/startup/fast_new_game_service.h"
#include "kh3ap/features/world/startup/starter_options.h"
#include "kh3ap/features/world/startup/starter_option_ids.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/bridge/serialization/json_fields.h"
#include "kh3ap/bridge/utilities.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <vector>
#include <stdexcept>
namespace kh3ap {
    namespace
    {
        constexpr int unselected_difficulty = starter_option_ids::unselected_difficulty;
        constexpr int maximum_difficulty = starter_option_ids::maximum_difficulty;
        constexpr int first_text_option = starter_option_ids::first_text_option;
        constexpr int last_text_option = starter_option_ids::last_text_option;
        constexpr int first_voice_option = starter_option_ids::first_voice_option;
        constexpr int last_voice_option = starter_option_ids::last_voice_option;
        constexpr int first_keyboard_option = starter_option_ids::first_keyboard_option;
        constexpr int last_keyboard_option = starter_option_ids::last_keyboard_option;

        constexpr auto fast_new_game_retry_interval = std::chrono::seconds(1);
    }

void RuntimeFastNewGameService::tick(bool initialized, bool title_active) {
    if (!initialized || !title_active || new_game_dispatched_ || existing_saves_) return;
    const auto now = std::chrono::steady_clock::now();
    if (now >= next_poll_) {
        next_poll_ = now + fast_new_game_retry_interval;
        const auto bridge = bridge_();
        if (bridge != last_bridge_) {
            last_bridge_ = bridge;
            failed_config_.clear();
            last_evidence_.clear();
        }
        content_ = bridge ? read_text_file(*bridge / "fast_new_game_settings.json") : std::nullopt;
    }
    const auto& bridge = last_bridge_;
    const auto& content = content_;
    if (!bridge) return;
    if (!content || *content == failed_config_) return;
    if (JsonFieldReader::int64_field(*content,"schema_version") != 1) return;
    const bool setup=JsonFieldReader::bool_field(*content,"enabled")==true;
    const bool direct=JsonFieldReader::bool_field(*content,"direct_new_game")==true;
    if (!setup && !direct) return;
    const auto difficulty =
        JsonFieldReader::int64_field(*content, "difficulty").value_or(unselected_difficulty);
    if (difficulty < unselected_difficulty || difficulty > maximum_difficulty)
    {
        return;
    }
    const auto text = JsonFieldReader::int64_field(*content,"text");
    const auto voice = JsonFieldReader::int64_field(*content,"voice");
    const auto keyboard = JsonFieldReader::int64_field(*content,"keyboard");
    if (!text || !voice || !keyboard || *text < first_text_option || *text > last_text_option ||
        (*voice != first_voice_option && *voice != last_voice_option) ||
        *keyboard < first_keyboard_option || *keyboard > last_keyboard_option)
    {
        return;
    }
    const StarterOptions options{static_cast<int>(*text),static_cast<int>(*voice),static_cast<int>(*keyboard)};
    try {
        // File reads and object discovery stay throttled, but the startup
        // guard runs every update so it precedes the one-frame notice enqueue.
        auto* title = title_.Get();
        if (!is_uobject_valid_fast(title)) {
        if (now < next_title_search_) return;
        next_title_search_ = now + fast_new_game_retry_interval;
        std::vector<RC::Unreal::UObject*> titles;
        RC::Unreal::UObjectGlobals::FindAllOf(STR("TresHUDTitle"),titles);
        title = nullptr;
        for (auto* candidate : titles) {
            std::wstring name;
            if (!is_uobject_valid_fast(candidate) || !try_get_uobject_full_name_fast(candidate,name) ||
                name.find(L"Default__") != std::wstring::npos || name.find(L"PersistentLevel") == std::wstring::npos) continue;
            if (title) throw std::runtime_error("ambiguous_setup_title");
            title = candidate;
        }
        if (!title) return;
        title_ = title;
        }
        // Gate all automation before setup, notice bits, movies or New Game.
        // Recheck an empty catalog each tick in case native loading completes
        // later. Once any save appears, leave this launch entirely manual.
        const auto eligibility=runtime_fast_start_eligibility(title);
        if (eligibility==FastStartEligibility::waiting) return;
        if (eligibility==FastStartEligibility::existing) {
            existing_saves_=true;
            write_text_file(*bridge / "fast_new_game_status.json",
                "{\"status\":\"skipped\",\"reason\":\"existing_game_saves\"}");
            return;
        }
        std::string evidence;
        bool selected=false;
        if (direct) {
            selected=runtime_direct_new_game_tick(title,evidence,static_cast<int>(difficulty),
                                                  new_game_dispatched_,movie_dismissed_);
            if (new_game_dispatched_) {
                const auto observed = JsonFieldReader::int64_field(evidence, "difficulty")
                                          .value_or(unselected_difficulty);
                if (observed>=0 && observed<=3) dispatched_difficulty_=static_cast<int>(observed);
            }
        }
        if (!selected && setup && now >= next_setup_) {
            next_setup_ = now + fast_new_game_retry_interval;
            selected=runtime_starter_options_tick(title,evidence,false,options,true);
        }
        if (selected && evidence != last_evidence_) {
            last_evidence_ = evidence;
            write_text_file(*bridge / "fast_new_game_status.json",
                "{\"status\":\"selected\",\"evidence\":" + evidence + "}");
        }
    } catch (const std::exception& error) {
        // Fail closed until preferences change; leave the normal menus usable.
        failed_config_ = *content;
        const std::string reason = error.what();
        write_text_file(*bridge / "fast_new_game_status.json",
            "{\"status\":\"error\",\"reason\":\"" + json_escape(std::wstring(reason.begin(),reason.end())) + "\"}");
    }
}
}
