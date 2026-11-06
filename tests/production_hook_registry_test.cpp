#include "kh3ap/hooks/production_hook_registry.h"
#include "kh3ap/hooks/hook_phase.h"
#include "kh3ap/core/types.h"
#include <Unreal/UObjectGlobals.hpp>
#include <cassert>
#include <chrono>
#include <future>
#include <map>
#include <stdexcept>
#include <vector>

namespace fake
{
    using RC::Unreal::UObjectGlobals::Callback;
    struct Hook { RC::Unreal::UFunction* function; Callback pre, post; std::pair<int, int> ids; };
    std::vector<Hook> hooks;
    std::vector<std::pair<int, int>> removals;
    size_t fail_registration{}, fail_removal{};
    std::map<std::wstring, std::unique_ptr<RC::Unreal::UFunction>> functions;
    bool missing_function{};
    bool zero_serial{};
    RC::Unreal::UnrealScriptFunctionCallableContext context;
    auto reset() -> void
    {
        hooks.clear(); removals.clear(); functions.clear();
        RC::Unreal::UObjectArray::items.clear();
        fail_registration = fail_removal = 0; missing_function = zero_serial = false;
    }
}

namespace RC::Unreal::UObjectGlobals
{
    auto find_function(const std::wstring& path) -> UFunction*
    {
        if (fake::missing_function) return nullptr;
        auto& function = fake::functions[path];
        if (!function)
        {
            function = std::make_unique<UFunction>();
            function->index = UObjectArray::GetNumElements();
            UObjectArray::items.push_back({function.get()});
            if (fake::zero_serial) UObjectArray::items.back().serial = 0;
        }
        return function.get();
    }
    auto RegisterHook(UFunction* function, Callback pre, Callback post, void*) -> std::pair<int, int>
    {
        if (fake::fail_registration == fake::hooks.size() + 1) throw std::runtime_error("register");
        const int id = static_cast<int>(fake::hooks.size()) * 2 + 1;
        const std::pair<int, int> ids{id, id + 1};
        // Hooks may be invoked during startup. They must stay inert until the
        // entire registry is installed, including during partial failure.
        pre(fake::context, nullptr);
        fake::hooks.push_back({function, std::move(pre), std::move(post), ids});
        return ids;
    }
    auto UnregisterHook(UFunction* function, std::pair<int, int> ids) -> void
    {
        const auto& registered = fake::hooks[static_cast<size_t>(ids.first - 1) / 2];
        assert(registered.function == function && registered.ids == ids);
        fake::removals.push_back(ids);
        // Simulate a callback already copied by UE4SS at teardown time.
        registered.pre(fake::context, nullptr);
        registered.post(fake::context, nullptr);
        if (fake::fail_removal == fake::removals.size()) throw std::runtime_error("unregister");
    }
}

auto config(int& calls, int& notes) -> kh3ap::RuntimeProductionHookRegistryConfig
{
    kh3ap::RuntimeProductionHookRegistryConfig result;
    const auto handler = [&](auto&, kh3ap::HookTargetInfo& target, kh3ap::HookPhase) {
        assert(!target.function_path.empty());
        ++calls;
    };
    result.apply_map_name = result.uip_map_name_callback = result.tres_map_jump = handler;
    result.handle_starting_new_player = result.find_player_start = handler;
    result.set_pause_menu_type = result.set_special_battle_mode = handler;
    result.note_game_thread = [&](std::wstring_view source) { assert(!source.empty()); ++notes; };
    return result;
}

int main()
{
    using kh3ap::RuntimeProductionHookRegistry;
    using namespace std::chrono_literals;
    int calls{}, notes{};
    fake::Callback retained;
    {
        RuntimeProductionHookRegistry registry{config(calls, notes)};
        registry.install(); registry.install();
        assert(registry.installed() && fake::hooks.size() == 7 && calls == 0);
        for (const auto& hook : fake::hooks) { hook.pre(fake::context, nullptr); hook.post(fake::context, nullptr); }
        assert(calls == 14 && notes == 2);
        retained = fake::hooks.front().pre;
        registry.uninstall(); registry.uninstall();
        assert(!registry.installed() && fake::removals.size() == 7 && calls == 14);
        for (size_t i = 0; i < 7; ++i) assert(fake::removals[i] == fake::hooks[6 - i].ids);
        registry.install();
        assert(registry.installed() && fake::hooks.size() == 14);
        retained(fake::context, nullptr);
        assert(calls == 14);
        fake::hooks.back().post(fake::context, nullptr);
        assert(calls == 15);
    }
    retained(fake::context, nullptr);
    assert(calls == 15 && fake::removals.size() == 14);

    fake::reset();
    {
        // KH3 native UFunctions have serial zero: UE4SS's weak Get returns
        // null despite a live function. Every registered hook must still run.
        fake::zero_serial = true;
        int zero_calls{}, zero_notes{};
        RuntimeProductionHookRegistry registry{config(zero_calls, zero_notes)};
        registry.install();
        for (const auto& hook : fake::hooks) { hook.pre(fake::context, nullptr); hook.post(fake::context, nullptr); }
        assert(zero_calls == 14 && zero_notes == 2);
        // A changed serial invalidates even the zero-serial path guard.
        ++RC::Unreal::UObjectArray::items[fake::hooks.front().function->index].serial;
        fake::hooks.front().pre(fake::context, nullptr);
        assert(zero_calls == 14);
        registry.uninstall();
        assert(fake::removals.size() == 6);
    }

    for (size_t fail = 1; fail <= 7; ++fail)
    {
        fake::reset(); fake::fail_registration = fail;
        RuntimeProductionHookRegistry registry{config(calls, notes)};
        bool threw{};
        try { registry.install(); } catch (const std::runtime_error&) { threw = true; }
        assert(threw && !registry.installed() && fake::hooks.size() == fail - 1);
        assert(fake::removals.size() == fail - 1 && calls == 15);
        for (const auto& hook : fake::hooks) hook.pre(fake::context, nullptr);
        assert(calls == 15);
        fake::fail_registration = 0;
        registry.install();
        assert(registry.installed());
    }
    fake::reset();
    {
        RuntimeProductionHookRegistry registry{config(calls, notes)};
        registry.install();
        fake::fail_removal = 3;
        retained = fake::hooks.front().pre;
        registry.uninstall();
        assert(fake::removals.size() == 7);
    }
    retained(fake::context, nullptr);
    assert(calls == 15);

    fake::reset();
    {
        RuntimeProductionHookRegistry registry{config(calls, notes)};
        fake::missing_function = true;
        bool threw{};
        try { registry.install(); } catch (const std::runtime_error&) { threw = true; }
        assert(threw && !registry.installed() && fake::hooks.empty());
    }
    fake::reset();
    {
        RuntimeProductionHookRegistry registry{config(calls, notes)};
        registry.install(); retained = fake::hooks.front().pre;
        // Same memory / object slot with a new serial must not receive the
        // expired function's unregister operation.
        ++RC::Unreal::UObjectArray::items[fake::hooks.front().function->index].serial;
        registry.uninstall();
        assert(fake::removals.size() == 6);
        retained(fake::context, nullptr); assert(calls == 15);
    }
    fake::reset();
    {
        RuntimeProductionHookRegistry a{config(calls, notes)};
        a.install(); retained = fake::hooks.front().pre;
        RuntimeProductionHookRegistry b{config(calls, notes)};
        b.install(); const auto replaced = fake::hooks[7].pre;
        b = std::move(a);
        assert(!a.installed() && b.installed() && fake::removals.size() == 7);
        replaced(fake::context, nullptr); assert(calls == 15);
        retained(fake::context, nullptr); assert(calls == 16);
    }
    retained(fake::context, nullptr); assert(calls == 16);

    fake::reset();
    {
        auto handlers = config(calls, notes);
        std::promise<void> entered, release, shutting_down;
        auto can_finish = release.get_future().share();
        handlers.apply_map_name = [&](auto&, auto&, auto) { entered.set_value(); can_finish.wait(); ++calls; };
        RuntimeProductionHookRegistry registry{std::move(handlers)};
        registry.install();
        const auto callback = fake::hooks.front().pre;
        auto executing = std::async(std::launch::async, [&] { callback(fake::context, nullptr); });
        entered.get_future().wait();
        auto stopped = std::async(std::launch::async, [&] { shutting_down.set_value(); registry.uninstall(); });
        shutting_down.get_future().wait();
        assert(stopped.wait_for(50ms) == std::future_status::timeout);
        release.set_value(); executing.get(); stopped.get();
        callback(fake::context, nullptr);
        assert(calls == 17);
    }
    fake::reset();
    {
        auto handlers = config(calls, notes);
        handlers.apply_map_name = [](auto&, auto&, auto) { throw std::runtime_error("callback"); };
        RuntimeProductionHookRegistry registry{std::move(handlers)};
        registry.install(); fake::hooks.front().pre(fake::context, nullptr);
        registry.uninstall(); assert(fake::removals.size() == 7);
    }
}
