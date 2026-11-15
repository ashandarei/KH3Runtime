#pragma once

#include <optional>

namespace RC::Unreal { class UObject; }
namespace kh3ap
{
    struct SharedSoraProgression
    {
        int experience{};
        int level{};
    };
    // Reads lane zero's saved EXP and the loaded Sora curve, never the
    // alternate's mirrored base parameters. No gameplay or save mutation.
    auto read_shared_sora_progression(RC::Unreal::UObject* pawn)
        -> std::optional<SharedSoraProgression>;
}
