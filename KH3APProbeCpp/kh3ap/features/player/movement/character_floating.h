#pragma once
namespace RC::Unreal { class UObject; }
namespace kh3ap::character_floating
{
    bool initialize();
    bool allows_swap(RC::Unreal::UObject* pawn);
}
