#include "kh3ap/features/tutorials/tutorial_patch_helpers.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include <Unreal/UFunction.hpp>
#include <cassert>

using namespace RC::Unreal;
namespace
{
    UObject* loaded{};
    int resolves{};
} // namespace
namespace kh3ap
{
    auto find_loaded_runtime_asset(std::wstring_view) -> UObject*
    {
        ++resolves;
        return loaded;
    }
    auto is_uobject_a_fast(const UObject* object, const UClass*) noexcept -> bool
    {
        return object && object->valid && dynamic_cast<const UFunction*>(object);
    }
} // namespace kh3ap
namespace kh3ap::tutorial_patch
{
    auto is_live_function_patch_target(UFunction* function) -> bool
    {
        return function && function->valid && function->outer_valid;
    }
} // namespace kh3ap::tutorial_patch
int main()
{
    using kh3ap::tutorial_patch::find_live_function;
    constexpr auto path = L"/Game/Streamed/Script.Script_C:ExecuteUbergraph_Script";
    assert(!find_live_function(path));
    UObject wrong_type;
    loaded = &wrong_type;
    assert(!find_live_function(path));
    UFunction first;
    UFunction replacement;
    loaded = &first;
    assert(find_live_function(path) == &first);
    first.outer_valid = false;
    assert(!find_live_function(path)); // Old package is tearing down.
    loaded = nullptr;
    for (int index = 0; index < 1000; ++index)
    {
        assert(!find_live_function(path));
    }
    loaded = &replacement;
    assert(find_live_function(path) == &replacement); // No stale-pointer cache.
    replacement.valid = false;
    assert(!find_live_function(path));
    assert(resolves == 1006);
}
