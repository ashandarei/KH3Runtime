#pragma once

#include <Mod/CppUserModBase.hpp>

#include "kh3ap/app/composition.h"

namespace kh3ap
{
    // Thin UE4SS adapter. Runtime behavior and service ownership belong to
    // RuntimeComposition rather than the DLL entrypoint or this lifecycle shell.
    class Kh3ApRuntimeMod final : public RC::CppUserModBase
    {
    public:
        Kh3ApRuntimeMod();
        ~Kh3ApRuntimeMod() override;

        auto on_unreal_init() -> void override;
        auto on_update() -> void override;

    private:
        RuntimeComposition runtime_;
    };
}
