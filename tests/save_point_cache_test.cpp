#include <cassert>
#include <string>
#include <string_view>
#include <unordered_map>

#include "kh3ap/features/story/save_points/helpers.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include <Unreal/UObjectGlobals.hpp>

using namespace RC::Unreal;
namespace kh3ap
{
    auto is_uobject_valid_fast(const UObject* function) noexcept -> bool
    {
        return function && function->valid;
    }
} // namespace kh3ap
int main()
{
    using kh3ap::save_point_native::try_resolve_cached_function;
    UFunction first;
    UObjectGlobals::current = &first;
    assert(try_resolve_cached_function(L"stable") == &first);
    assert(try_resolve_cached_function(L"stable") == &first);
    assert(UObjectGlobals::resolves == 1);
    ++first.serial; // Same address, different UObject identity.
    assert(try_resolve_cached_function(L"stable") == &first);
    assert(UObjectGlobals::resolves == 2);
    first.valid = false;
    UFunction replacement;
    UObjectGlobals::current = &replacement;
    assert(try_resolve_cached_function(L"stable") == &replacement);
    assert(try_resolve_cached_function(L"stable") == &replacement);
    assert(UObjectGlobals::resolves == 3);
    UObjectGlobals::current = nullptr;
    assert(!try_resolve_cached_function(L"late"));
    UObjectGlobals::current = &replacement;
    assert(try_resolve_cached_function(L"late") == &replacement);
    assert(UObjectGlobals::resolves == 5);
    UFunction cooked{true, 0};
    UObjectGlobals::current = &cooked;
    assert(try_resolve_cached_function(L"cooked") == &cooked);
    assert(try_resolve_cached_function(L"cooked") == &cooked);
    assert(UObjectGlobals::resolves == 7); // No unprotected raw fallback.
}
