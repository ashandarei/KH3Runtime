#pragma once
#include <string>
#include "kh3ap/features/world/startup/starter_option_ids.h"
#include "kh3ap/features/world/startup/fast_start_policy.h"
namespace RC::Unreal { class UObject; }
namespace kh3ap {
struct StarterOptions {
    int text{starter_option_ids::default_text_option};
    int voice{starter_option_ids::default_voice_option};
    int keyboard{starter_option_ids::default_keyboard_option};
    constexpr bool valid() const {
        return text >= starter_option_ids::first_text_option &&
               text <= starter_option_ids::last_text_option &&
               (voice == starter_option_ids::first_voice_option ||
                voice == starter_option_ids::last_voice_option) &&
               keyboard >= starter_option_ids::first_keyboard_option &&
               keyboard <= starter_option_ids::last_keyboard_option;
    }
};
FastStartEligibility runtime_fast_start_eligibility(RC::Unreal::UObject* title);
// Dispatch normal setup events; never starts a game or changes a save slot.
bool runtime_starter_options_tick(RC::Unreal::UObject* title, std::string& evidence,
                                  bool notice = false, StarterOptions options = {}, bool initial_only = false, int pandora_message_id = 0);
// One-shot navigation after native system initialization. -1 leaves difficulty manual.
bool runtime_direct_new_game_tick(RC::Unreal::UObject* title, std::string& evidence,
                                  int difficulty, bool& dispatched, bool& movie_dismissed);
}
