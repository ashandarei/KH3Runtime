#pragma once
#include <string>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap::character_style
{
    // Game-thread swap lifecycle. Stored durations do not advance off-character.
    void capture(RC::Unreal::UObject* outgoing, int target);
    void restore(RC::Unreal::UObject* incoming);
    bool pending();
    void cancel();
    void reset();

#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    std::string inspect(RC::Unreal::UObject* pawn);
    bool offer_for_test(RC::Unreal::UObject* pawn, int command);
#endif
} // namespace kh3ap::character_style
