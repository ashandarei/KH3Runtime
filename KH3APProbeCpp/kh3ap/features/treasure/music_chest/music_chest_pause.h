#pragma once

namespace RC::Unreal
{
    class UObject;
}
namespace kh3ap
{
    class RuntimeMoMAudio;
}

namespace kh3ap::music_chest
{
    void set_handoff_audio(RuntimeMoMAudio* audio);
    bool pause_active();
    void update_pause(RC::Unreal::UObject* pawn, RuntimeMoMAudio& audio);
    bool foreground();
    bool game_paused(RC::Unreal::UObject* pawn);
} // namespace kh3ap::music_chest
