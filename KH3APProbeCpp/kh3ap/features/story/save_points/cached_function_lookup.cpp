#include "helpers.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include <Unreal/FWeakObjectPtr.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <unordered_map>

namespace kh3ap::save_point_native
{
    using namespace RC::Unreal;
    auto try_resolve_cached_function(std::wstring_view function_path) -> UFunction*
    {
        // Revalidate serial-backed handles on the game thread. Cooked functions
        // without a weak serial are borrowed for this call and never cached raw.
        static std::unordered_map<std::wstring, FWeakObjectPtr> cache{};
        const std::wstring owned_path{function_path};
        if (const auto found = cache.find(owned_path); found != cache.end())
        {
            auto* const cached = found->second.Get();
            if (kh3ap::is_uobject_valid_fast(cached))
            {
                return static_cast<UFunction*>(cached);
            }
            cache.erase(found);
        }
        UFunction* const function_object =
            UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, owned_path.c_str());
        if (!kh3ap::is_uobject_valid_fast(function_object))
        {
            return nullptr;
        }
        FWeakObjectPtr weak_function{};
        weak_function = function_object;
        if (weak_function.Get() == function_object)
        {
            cache.emplace(owned_path, weak_function);
        }
        return function_object;
    }

} // namespace kh3ap::save_point_native
