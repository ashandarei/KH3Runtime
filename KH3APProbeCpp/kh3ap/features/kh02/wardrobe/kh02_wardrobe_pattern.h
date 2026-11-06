#pragma once
#include <nlohmann/json.hpp>

namespace RC::Unreal
{
    class UObject;
    class UScriptStruct;
} // namespace RC::Unreal

namespace kh3ap
{
    class Kh02WardrobeMaterials;
    class Kh02WardrobeAssets;

    // All members are borrowed for one synchronous material update.
    struct Kh02WardrobePattern
    {
        RC::Unreal::UObject* pawn;
        RC::Unreal::UObject* component;
        RC::Unreal::UObject* outfit_material;
        RC::Unreal::UScriptStruct* schema;
        void* unit;
        const nlohmann::json& selection;
    };

    void kh02_apply_wardrobe_pattern(const Kh02WardrobePattern& request, Kh02WardrobeMaterials& materials,
                                     Kh02WardrobeAssets& assets);
} // namespace kh3ap
