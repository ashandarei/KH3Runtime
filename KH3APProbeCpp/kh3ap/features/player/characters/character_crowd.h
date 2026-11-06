#pragma once

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap::character_crowd
{
    // Rebind the stock crowd's weak player reference when a swap commits ownership.
    // This is an ownership event, not an update/timer task.
    void rebind(RC::Unreal::UObject* pawn);
} // namespace kh3ap::character_crowd
