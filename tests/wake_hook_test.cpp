#include "kh3ap/core/events/wake_hook.h"
#include <cassert>
#include <stdexcept>
#include <type_traits>

namespace fake
{
    RC::Unreal::UFunction function;
    RC::Unreal::UObjectGlobals::Callback callback;
    int lookups{}, registrations{}, removals{};
    bool missing{}, reject_registration{}, reject_removal{};
    auto reset() -> void
    {
        function = {};
        callback = {};
        lookups = registrations = removals = RC::Output::warnings = 0;
        missing = reject_registration = reject_removal = false;
    }
}

namespace kh3ap
{
    auto is_uobject_valid_fast(const RC::Unreal::UObject* object) noexcept -> bool { return object != nullptr; }
}

namespace RC::Unreal::UObjectGlobals
{
    auto find_function() -> UFunction*
    {
        ++fake::lookups;
        return fake::missing ? nullptr : &fake::function;
    }
    auto RegisterHook(UFunction*, Callback, Callback post, void*) -> std::pair<int, int>
    {
        ++fake::registrations;
        if (fake::reject_registration) throw std::runtime_error("registration");
        fake::callback = std::move(post);
        return {4, 5};
    }
    auto UnregisterHook(const std::wstring& path, std::pair<int, int> ids) -> void
    {
        assert(path == L"test" && ids.first == 4 && ids.second == 5);
        ++fake::removals;
        if (fake::reject_removal) throw std::runtime_error("removal");
        fake::callback = {};
    }
}

int main()
{
    using kh3ap::RuntimeNativeWakeHook;
    static_assert(!std::is_copy_constructible_v<RuntimeNativeWakeHook>);
    static_assert(!std::is_copy_assignable_v<RuntimeNativeWakeHook>);
    static_assert(!std::is_move_constructible_v<RuntimeNativeWakeHook>);
    static_assert(!std::is_move_assignable_v<RuntimeNativeWakeHook>);
    static_assert(std::is_nothrow_destructible_v<RuntimeNativeWakeHook>);

    fake::reset();
    int notifications{};
    {
        RuntimeNativeWakeHook hook;
        assert(hook.install(L"test", 4, [&] { ++notifications; }));
        assert(hook.failure_reason().empty());
        assert(hook.install(L"ignored", 1, [] {}));
        assert(fake::lookups == 1 && fake::registrations == 1);
        RC::Unreal::UObjectGlobals::Context context;
        fake::callback(context, nullptr);
        assert(notifications == 1);
    }
    assert(fake::removals == 1 && !fake::callback && RC::Output::warnings == 0);

    for (int failure = 0; failure < 4; ++failure)
    {
        fake::reset();
        fake::missing = failure == 0;
        fake::function.flags = failure == 1 ? 0 : RC::Unreal::FUNC_Native;
        fake::function.parameter_size = failure == 2 ? 5 : 4;
        fake::reject_registration = failure == 3;
        {
            RuntimeNativeWakeHook hook;
            assert(!hook.install(L"test", 4, [] {}));
            assert(!hook.failure_reason().empty());
            assert(RC::Output::warnings == 1);
            assert(!hook.install(L"test", 4, [] {}));
            assert(RC::Output::warnings == 1 && fake::lookups == 1);
        }
        assert(fake::removals == 0);
    }
    fake::reset();
    {
        RuntimeNativeWakeHook hook;
        assert(!hook.install(nullptr, 4, [] {}));
        assert(hook.failure_reason() == L"invalid_request");
        assert(fake::lookups == 0 && RC::Output::warnings == 1);
    }
    fake::reset();
    {
        RuntimeNativeWakeHook hook;
        assert(hook.install(L"test", 4, [] {}));
        fake::reject_removal = true;
    }
    assert(fake::removals == 1 && RC::Output::warnings == 1);
}
