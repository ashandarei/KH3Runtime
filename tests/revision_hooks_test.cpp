#include "kh3ap/core/events/revision_hooks.h"

#include <array>
#include <cassert>
#include <type_traits>

namespace
{
    int continuations{};
    auto original() -> void
    {
        ++continuations;
    }
    auto memory_state() -> MEMORY_BASIC_INFORMATION
    {
        MEMORY_BASIC_INFORMATION result{};
        assert(VirtualQuery(reinterpret_cast<void*>(PLH::destination), &result, sizeof(result)));
        return result;
    }
} // namespace

int main()
{
    using kh3ap::RuntimeNativeRevisionHooks;
    static_assert(!std::is_copy_constructible_v<RuntimeNativeRevisionHooks>);
    static_assert(!std::is_copy_assignable_v<RuntimeNativeRevisionHooks>);
    static_assert(!std::is_move_constructible_v<RuntimeNativeRevisionHooks>);
    static_assert(!std::is_move_assignable_v<RuntimeNativeRevisionHooks>);
    const std::array<uint8_t, 1> source{0x90};
    PLH::continuation = reinterpret_cast<uint64_t>(&original);
    {
        RuntimeNativeRevisionHooks hooks;
        assert(!hooks.install(0, source));
        assert(hooks.install(reinterpret_cast<uintptr_t>(source.data()), source));
        assert(memory_state().Protect == PAGE_EXECUTE_READ);
        reinterpret_cast<void (*)()>(PLH::destination)();
        assert(hooks.revision() == 1 && continuations == 1);
    }
    assert(PLH::destroyed == 1 && memory_state().State == MEM_FREE);
    PLH::fail_hook = true;
    {
        RuntimeNativeRevisionHooks hooks;
        assert(!hooks.install(reinterpret_cast<uintptr_t>(source.data()), source));
        assert(hooks.revision() == 0);
    }
    assert(PLH::destroyed == 2 && memory_state().State == MEM_FREE);
    PLH::fail_hook = false;
    PLH::fail_unhook = true;
    {
        RuntimeNativeRevisionHooks hooks;
        assert(hooks.install(reinterpret_cast<uintptr_t>(source.data()), source));
    }
    // A failed removal must leave both the executable page and its revision
    // storage alive. Execute the production stub after the owner goes away.
    assert(PLH::destroyed == 2 && memory_state().Protect == PAGE_EXECUTE_READ);
    reinterpret_cast<void (*)()>(PLH::destination)();
    assert(continuations == 2);
}
