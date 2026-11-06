#include "kh02_wardrobe_materials.h"
#include "kh02_wardrobe_reflection.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh02_wardrobe_assets.h"
#include <Unreal/UClass.hpp>

namespace
{
    constexpr int maximum_material_parent_depth = 16;
}

namespace kh3ap
{
    using namespace RC::Unreal;
    using namespace kh02wardrobe_detail;

    auto Kh02WardrobeMaterials::backup(UObject* pawn, UObject* original, Kh02WardrobeAssets& assets) -> UObject*
    {
        // A backup parented to the live MID would retain its pawn and map.
        // Copy its parameters onto an asset parent and a persistent outer.
        auto* parent = original;
        for (int depth = 0; depth < maximum_material_parent_depth && is_uobject_valid_fast(parent) &&
                            parent->GetClassPrivate()->GetName() == L"MaterialInstanceDynamic";
             ++depth)
        {
            parent = object_field(parent, L"Parent");
        }
        if (!is_uobject_valid_fast(parent) || parent->GetClassPrivate()->GetName() == L"MaterialInstanceDynamic")
        {
            throw std::runtime_error("Could not preserve Aqua's original outfit material");
        }
        Call instance(gameplay, L"GetGameInstance");
        instance.object(L"WorldContextObject", pawn);
        instance.run();
        Call create(L"/Script/Engine.Default__KismetMaterialLibrary", L"CreateDynamicMaterialInstance");
        create.object(L"WorldContextObject", instance.result());
        create.object(L"Parent", parent);
        create.run();
        auto* backup = create.result();
        if (!is_uobject_valid_fast(backup))
        {
            throw std::runtime_error("Could not preserve Aqua's original outfit material");
        }
        Call copy(backup, L"K2_CopyMaterialInstanceParameters");
        copy.object(L"Source", original);
        copy.run();
        assets.retain(backup);
        return backup;
    }

    void Kh02WardrobeMaterials::restore(UObject* component)
    {
        if (is_uobject_valid_fast(component))
        {
            for (const auto& material : materials)
            {
                Call current(component, L"GetMaterial");
                current.integer(L"ElementIndex", material.index);
                current.run();
                if (current.result() != material.adapted.Get() || !is_uobject_valid_fast(material.original.Get()))
                {
                    continue;
                }
                Call restore(component, L"SetMaterial");
                restore.integer(L"ElementIndex", material.index);
                restore.object(L"Material", material.original.Get());
                restore.run();
                if (auto** cached = kh02_native_material_slot(component, material.index))
                {
                    if (*cached == material.adapted.Get())
                    {
                        *cached = material.original.Get();
                    }
                }
            }
        }
        materials.clear();
    }

    void Kh02WardrobeMaterials::record(int index, UObject* original, UObject* adapted)
    {
        materials.push_back({index, make_uobject_weak(original), make_uobject_weak(adapted)});
    }

} // namespace kh3ap
