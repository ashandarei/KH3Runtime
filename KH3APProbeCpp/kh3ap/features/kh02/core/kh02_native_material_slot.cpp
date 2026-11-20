#include "kh02_native_material_slot.h"

#include <Unreal/UObject.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#include <stdexcept>

namespace kh3ap
{
    using namespace RC::Unreal;

    auto kh02_native_material_slot(UObject* mesh, int index) -> UObject**
    {
        auto* p = mesh->GetPropertyByNameInChain(L"m_OverrideMaterials");
        if (!p || !p->IsA<FArrayProperty>() || p->GetElementSize() != 16)
        {
            throw std::runtime_error("kh02_story_material_cache");
        }
        auto* inner = static_cast<FArrayProperty*>(p)->GetInner();
        if (!inner || inner->GetClass().GetName() != L"ObjectProperty" || inner->GetElementSize() != sizeof(UObject*))
        {
            throw std::runtime_error("kh02_story_material_cache_type");
        }

        struct Array
        {
            UObject** data;
            int32_t count, capacity;
        };

        auto* a = p->ContainerPtrToValuePtr<Array>(mesh);
        // The native parameter cache is initialized lazily. If absent, the game
        // will build it from the renderer's already-adapted material instances.
        if (a->count == 0 && a->capacity >= 0)
        {
            return nullptr;
        }
        if (a->count != 11 || a->capacity < a->count || !a->data || index < 0 || index >= a->count)
        {
            throw std::runtime_error("kh02_story_material_cache_bounds");
        }
        return a->data + index;
    }
} // namespace kh3ap
