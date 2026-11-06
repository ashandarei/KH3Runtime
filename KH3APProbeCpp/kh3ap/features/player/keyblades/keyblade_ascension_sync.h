#pragma once
namespace RC::Unreal
{
    class UObject;
}
namespace kh3ap
{
    struct RuntimeKeybladeStyleState;
    struct RuntimeKeybladeStyleSanityDeps;
} // namespace kh3ap

namespace kh3ap::keyblade_options
{
    auto prepare_keyblade_style_context(RuntimeKeybladeStyleState& state, RC::Unreal::UObject* sora,
                                        RC::Unreal::UObject* controller, bool sora_trusted,
                                        const RuntimeKeybladeStyleSanityDeps& deps) -> bool;
}
