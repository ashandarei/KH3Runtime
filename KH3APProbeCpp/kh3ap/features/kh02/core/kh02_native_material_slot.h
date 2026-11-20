#pragma once

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    auto kh02_native_material_slot(RC::Unreal::UObject* mesh, int index) -> RC::Unreal::UObject**;
}
