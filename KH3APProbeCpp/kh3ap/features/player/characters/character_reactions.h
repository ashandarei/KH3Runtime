#pragma once
namespace RC::Unreal { class UObject; }
namespace kh3ap::character_reactions
{
    void capture_sora(RC::Unreal::UObject* pawn);
    void prepare_pawn(RC::Unreal::UObject* pawn);
    bool owns_command(RC::Unreal::UObject* controller, int command);
}
