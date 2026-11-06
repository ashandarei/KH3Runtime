#pragma once
namespace RC::Unreal { class UObject; }
namespace kh3ap::character_airstep
{
    bool initialize();
    void prepare(RC::Unreal::UObject* pawn, RC::Unreal::UObject* controller);
}
