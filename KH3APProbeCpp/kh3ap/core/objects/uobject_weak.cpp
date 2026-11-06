#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/features/world/space/space_profile.h"
#include <cstring>

namespace kh3ap
{
    auto make_uobject_weak(RC::Unreal::UObject* object) -> RC::Unreal::FWeakObjectPtr
    {
        RC::Unreal::FWeakObjectPtr result{};
        if (!is_uobject_valid_fast(object)) return result;
        result = object;
        if (result.Get() == object) return result;

        // Reuse the EGS/Steam image identities and weak-assignment signatures
        // already verified by the space runtime. Do not modify UE4SS or the
        // object-array serial directly.
        const auto& profile = space::native_profile();
        const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        if (!base || !profile.supported ||
            std::memcmp(reinterpret_cast<const void*>(base + profile.weak_assign_rva),
                        profile.weak_assign_bytes.data(), profile.weak_assign_bytes.size()) != 0)
            return {};
        using Assign = void (*)(RC::Unreal::FWeakObjectPtr*, RC::Unreal::UObject*);
        reinterpret_cast<Assign>(base + profile.weak_assign_rva)(&result, object);
        return result.Get() == object ? result : RC::Unreal::FWeakObjectPtr{};
    }
}
