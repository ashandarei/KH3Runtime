#pragma once
#include <optional>

namespace RC::Unreal { class UObject; }

namespace kh3ap::character_magic
{
    inline constexpr auto sora_magic_set_path =
        L"/Game/Blueprints/Player/p_ex001/p_ex001_MagicSet.p_ex001_MagicSet";

    struct Meter
    {
        int mp{};
        bool charging{};
        float remaining{};
    };

    bool initialize();
    bool refresh_spells(RC::Unreal::UObject* pawn);
    bool owns_command(RC::Unreal::UObject* controller, int command);
    // These are called only at the existing swap/rebind lifecycle events.
    void capture(RC::Unreal::UObject* pawn);
    void restore(RC::Unreal::UObject* pawn);
    // Preserve the current gauge around native controller restriction setters.
    std::optional<Meter> read_meter(RC::Unreal::UObject* pawn);
    bool manages_meter(RC::Unreal::UObject* pawn);
    void write_meter(RC::Unreal::UObject* pawn, const Meter& meter);
}
