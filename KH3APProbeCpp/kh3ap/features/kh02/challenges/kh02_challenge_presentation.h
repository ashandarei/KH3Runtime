#pragma once
#include "kh02_challenge_log.h"
#include <map>

namespace kh3ap
{
    auto kh02_challenge_progress(int number, bool active, const std::map<int, int>& counts) -> std::wstring;
    auto kh02_challenge_description(int number) -> std::wstring;
    auto kh02_challenge_help(int number, bool active, const std::map<int, int>& counts) -> std::wstring;
    auto kh02_challenge_rows(const std::map<int, int>& states, const std::map<int, int>& counts)
        -> std::vector<Kh02ChallengeRow>;
} // namespace kh3ap
