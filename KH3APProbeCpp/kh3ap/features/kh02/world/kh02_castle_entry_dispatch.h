#pragma once
#include <nlohmann/json.hpp>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    void kh02_dispatch_castle_entry(RC::Unreal::UObject* script, bool first_castle_intro, bool entry_retry,
                                    bool entry_backtrack, bool arrival_owned, nlohmann::json& loading_proof);
}
