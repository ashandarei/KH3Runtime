#pragma once
#include "kh3ap/features/story/progression/vbonus_tracking_policy.h"
namespace kh3ap {
// One native hook per setter, shared by all game-flag consumers. Register on
// the game thread; callbacks only enqueue POD names in the consumer's inbox.
void subscribe_game_flags(VBonusFlagEvents& inbox, bool integer_flags = false);
void unsubscribe_game_flags(VBonusFlagEvents& inbox);
}
