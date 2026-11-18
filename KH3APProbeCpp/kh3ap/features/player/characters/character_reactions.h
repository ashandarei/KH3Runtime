#pragma once
namespace RC::Unreal { class UObject; }
namespace kh3ap::character_reactions
{
    void graveyard_portal_jump(RC::Unreal::UObject* portal);
    void install_graveyard_events();
    void uninstall_graveyard_events();
    void capture_sora(RC::Unreal::UObject* pawn);
    void prepare_pawn(RC::Unreal::UObject* pawn);
    void rebind_scripted_coaster(RC::Unreal::UObject* outgoing, RC::Unreal::UObject* incoming);
    bool owns_command(RC::Unreal::UObject* controller, int command);
}
