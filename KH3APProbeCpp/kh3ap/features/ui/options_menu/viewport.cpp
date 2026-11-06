#include "kh3ap/features/ui/options_menu/host.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/core/memory/memory_protection_bits.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/NameTypes.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using namespace RC;
using namespace RC::Unreal;

namespace kh3ap
{
    namespace
    {
        constexpr auto native_options_fkey_bytes = 24;
    }

    using namespace options_menu_detail;
    namespace options_menu_detail
    {
        // Machine-level layout of UE 4.17 FKey. The actual by-value FKey is
        // passed indirectly under the MSVC x64 ABI. The hook deliberately
        // accepts that hidden pointer so the original function remains the
        // sole owner responsible for destroying the parameter's TSharedPtr.
        struct RawFKey
        {
            FName key_name{};
            void* key_details_object{};
            void* key_details_controller{};
        };

        static_assert(sizeof(FName) == 8);
        static_assert(sizeof(RawFKey) == native_options_fkey_bytes);

    } // namespace options_menu_detail
    namespace
    {
        constexpr int window_class_name_capacity = 64;
        constexpr int32_t minimum_viewport_console_offset = 0x20;
        constexpr int32_t maximum_viewport_console_offset = 0x80;
        constexpr int32_t viewport_console_alignment = 8;
        auto address_is_executable(uintptr_t address) -> bool
        {
            MEMORY_BASIC_INFORMATION memory_info{};
            if (address == 0 || VirtualQuery(reinterpret_cast<const void*>(address), &memory_info,
                                             sizeof(memory_info)) != sizeof(memory_info))
            {
                return false;
            }
            if (memory_info.State != MEM_COMMIT || (memory_info.Protect & PAGE_GUARD) != 0)
            {
                return false;
            }
            const DWORD protection = memory_info.Protect & memory_protection::base_mode_mask;
            return protection == PAGE_EXECUTE || protection == PAGE_EXECUTE_READ ||
                   protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
        }

        auto game_window_is_foreground() -> bool
        {
            const HWND foreground = GetForegroundWindow();
            DWORD process_id{};
            if (!foreground || !GetWindowThreadProcessId(foreground, &process_id) ||
                process_id != GetCurrentProcessId())
            {
                return false;
            }
            // The UE4SS console is another window in this process, not game focus.
            wchar_t class_name[window_class_name_capacity]{};
            return GetClassNameW(foreground, class_name, window_class_name_capacity) != 0 &&
                   std::wstring_view{class_name} == L"UnrealWindow";
        }
    } // namespace
    RuntimeOptionsMenuService::Impl::~Impl()
    {
        Impl* expected = this;
        s_input_key_owner_.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel);
        if (viewport_lost_focus_detour_)
        {
            viewport_lost_focus_detour_->unHook();
        }
        if (viewport_input_axis_detour_)
        {
            viewport_input_axis_detour_->unHook();
        }
        if (viewport_input_key_detour_)
        {
            if (viewport_input_key_hooked_)
            {
                viewport_input_key_detour_->unHook();
            }
            viewport_input_key_detour_.reset();
        }
        s_original_input_key_.store(0, std::memory_order_release);
    }

    auto RuntimeOptionsMenuService::Impl::block_background_controller_input() const -> bool
    {
        return !config_.background_controller_input_allowed ||
               !config_.background_controller_input_allowed();
    }

    auto RuntimeOptionsMenuService::Impl::dispatch_key(void* client, void* viewport,
                                                       int32_t controller, RawFKey* key,
                                                       int32_t event, float amount, bool gamepad)
        -> bool
    {
        const auto original = reinterpret_cast<ViewportInputKeyFn>(s_original_input_key_.load());
        const bool settings_key =
            gamepad &&
            (event == input_event_released || !block_background_controller_input() ||
             game_window_is_foreground()) &&
            settings_shortcut_.route(controller, key->key_name.ToString(), event);
        const bool menu_consumed = route_viewport_key(key->key_name, controller, event, gamepad);
        // Finish gameplay-owned press cycles even if a menu opened before release.
        const bool gameplay_consumed =
            (!menu_consumed || event == input_event_released) && config_.gameplay_key &&
            config_.gameplay_key(controller, key->key_name.ToString(), event, gamepad);
        const bool consumed = menu_consumed || gameplay_consumed;
        if (consumed)
        {
            key->key_name = FName{};
        }
        else if (settings_key)
        {
            key->key_name = FName{STR("Escape"), FNAME_Add};
            gamepad = false;
        }
        return original(client, viewport, controller, key, event, consumed ? 0.0F : amount,
                        gamepad) ||
               consumed;
    }

    auto RuntimeOptionsMenuService::Impl::viewport_input_axis_hook(void* client, void* viewport,
                                                                   int32_t controller, RawFKey* key,
                                                                   float delta, float delta_time,
                                                                   int32_t samples, bool gamepad)
        -> bool
    {
        auto original = reinterpret_cast<ViewportInputAxisFn>(s_original_input_axis_);
        Impl* const owner = s_input_key_owner_.load(std::memory_order_acquire);
        if (owner && key && gamepad)
        {
            if (owner->block_background_controller_input() && !game_window_is_foreground())
            {
                owner->release_background_controller_input(client, viewport);
            }
            delta = owner->controller_focus_.axis({controller, key->key_name.ToString()}, delta,
                                                  game_window_is_foreground(),
                                                  owner->block_background_controller_input());
        }
        const bool handled =
            original(client, viewport, controller, key, delta, delta_time, samples, gamepad);
        if (owner && key && !gamepad && delta != 0.0F && owner->config_.pointer_motion)
        {
            const auto name = key->key_name.ToString();
            if (name == L"MouseX" || name == L"MouseY")
            {
                owner->config_.pointer_motion();
            }
        }
        return handled;
    }

    auto RuntimeOptionsMenuService::Impl::release_background_controller_input(void* client,
                                                                              void* viewport)
        -> void
    {
        controller_focus_.lose_focus(
            [&](const RuntimeControllerFocus::Key& identity)
            {
                RawFKey key{FName{identity.second.c_str(), FNAME_Add}};
                dispatch_key(client, viewport, identity.first, &key, input_event_released, 0.0F,
                             true);
            },
            [&](const RuntimeControllerFocus::Key& identity)
            {
                RawFKey key{FName{identity.second.c_str(), FNAME_Add}};
                reinterpret_cast<ViewportInputAxisFn>(s_original_input_axis_)(
                    client, viewport, identity.first, &key, 0.0F, 0.0F, 1, true);
            },
            block_background_controller_input());
    }

    auto RuntimeOptionsMenuService::Impl::viewport_lost_focus_hook(void* client, void* viewport)
        -> void
    {
        Impl* const owner = s_input_key_owner_.load(std::memory_order_acquire);
        if (owner)
        {
            owner->release_background_controller_input(client, viewport);
        }
        reinterpret_cast<ViewportLostFocusFn>(s_original_lost_focus_)(client, viewport);
    }

    auto RuntimeOptionsMenuService::Impl::viewport_input_key_hook(
        void* viewport_client, void* viewport, int32_t controller_id, RawFKey* key,
        int32_t event_type, float amount_depressed, bool gamepad) -> bool
    {
        const uintptr_t original_address = s_original_input_key_.load(std::memory_order_acquire);
        if (original_address == 0)
        {
            return false;
        }
        const auto original = reinterpret_cast<ViewportInputKeyFn>(original_address);

        bool consumed = false;
        Impl* const owner = s_input_key_owner_.load(std::memory_order_acquire);
        if (owner && key)
        {
            try
            {
                if (gamepad && owner->controller_focus_installed_)
                {
                    if (owner->block_background_controller_input() && !game_window_is_foreground())
                    {
                        owner->release_background_controller_input(viewport_client, viewport);
                    }
                    const auto route = owner->controller_focus_.button(
                        {controller_id, key->key_name.ToString()}, event_type,
                        game_window_is_foreground(), owner->block_background_controller_input());
                    consumed = route == RuntimeControllerFocus::Route::consume;
                    if (route == RuntimeControllerFocus::Route::release)
                    {
                        event_type = input_event_released;
                        amount_depressed = 0.0F;
                    }
                }
            }
            catch (...)
            {
                consumed = gamepad && owner->block_background_controller_input() &&
                           !game_window_is_foreground();
            }
            // Never catch around an original native call and then forward the same
            // indirect FKey again: the first call already owns its destruction.
            if (!consumed)
            {
                return owner->dispatch_key(viewport_client, viewport, controller_id, key,
                                           event_type, amount_depressed, gamepad);
            }
        }
        if (!consumed)
        {
            return original(viewport_client, viewport, controller_id, key, event_type,
                            amount_depressed, gamepad);
        }

        // The original MSVC function owns destruction of the indirect
        // by-value FKey parameter. Forward the same event for NAME_None so
        // that ownership is discharged exactly once without allowing the
        // stock Config controller to observe any part of the consumed
        // button's press/repeat/release lifecycle.
        key->key_name = FName{};
        (void)original(viewport_client, viewport, controller_id, key, event_type, 0.0F, gamepad);
        return true;
    }

    auto RuntimeOptionsMenuService::Impl::find_active_viewport_client() -> UObject*
    {
        std::vector<UObject*> candidates{};
        UObjectGlobals::FindAllOf(STR("GameViewportClient"), candidates);
        for (UObject* const candidate : candidates)
        {
            if (!candidate || !kh3ap::is_uobject_valid_fast(candidate))
            {
                continue;
            }
            const std::wstring full_name = candidate->GetFullName();
            if (full_name.find(STR("Default__")) == std::wstring::npos)
            {
                return candidate;
            }
        }
        return nullptr;
    }

    auto RuntimeOptionsMenuService::Impl::viewport_console_property_offset(UObject* viewport_client)
        -> std::optional<int32_t>
    {
        if (!viewport_client || !kh3ap::is_uobject_valid_fast(viewport_client))
        {
            return std::nullopt;
        }
        for (UStruct* owner = viewport_client->GetClassPrivate(); owner;
             owner = owner->GetSuperStruct())
        {
            for (FProperty* const property : owner->ForEachProperty())
            {
                if (property && property->GetName() == STR("ViewportConsole"))
                {
                    return property->GetOffset_Internal();
                }
            }
        }
        return std::nullopt;
    }

    auto RuntimeOptionsMenuService::Impl::try_install_viewport_input_hook() -> bool
    {
        if (viewport_input_key_hooked_)
        {
            return true;
        }
        if (viewport_input_hook_failed_)
        {
            return false;
        }

        UObject* const viewport_client = find_active_viewport_client();
        if (!viewport_client)
        {
            return false;
        }
        settings_shortcut_.bind_viewport(viewport_client);
        const auto console_offset = viewport_console_property_offset(viewport_client);
        if (!console_offset.has_value() || *console_offset < minimum_viewport_console_offset ||
            *console_offset > maximum_viewport_console_offset ||
            (*console_offset % viewport_console_alignment) != 0)
        {
            viewport_input_hook_failed_ = true;
            Output::send<LogLevel::Error>(
                STR("[KH3APProbeCpp] Runtime options viewport input hook failed "
                    "reason=viewport_console_offset offset={}\n"),
                console_offset.value_or(-1));
            return false;
        }

        // UScriptViewportClient contributes FCommonViewportClient and
        // UGameViewportClient then contributes FExec. Both are empty
        // polymorphic secondary bases under MSVC, immediately before the
        // class's first reflected property. InputKey belongs to the first
        // of those two vfptrs.
        const size_t secondary_vfptr_offset =
            static_cast<size_t>(*console_offset) - (2 * sizeof(void*));
        auto*** const secondary_vfptr_location = reinterpret_cast<void***>(
            reinterpret_cast<std::byte*>(viewport_client) + secondary_vfptr_offset);
        void** const secondary_vtable = *secondary_vfptr_location;
        const uintptr_t target =
            secondary_vtable
                ? reinterpret_cast<uintptr_t>(secondary_vtable[viewport_input_key_vtable_slot])
                : 0;
        const HMODULE game_module = GetModuleHandleW(nullptr);
        MEMORY_BASIC_INFORMATION target_memory{};
        const bool target_in_game_module =
            target != 0 &&
            VirtualQuery(reinterpret_cast<const void*>(target), &target_memory,
                         sizeof(target_memory)) == sizeof(target_memory) &&
            target_memory.AllocationBase == game_module;
        if (!secondary_vtable || !address_is_executable(target) || !target_in_game_module)
        {
            viewport_input_hook_failed_ = true;
            Output::send<LogLevel::Error>(
                STR("[KH3APProbeCpp] Runtime options viewport input hook failed "
                    "reason=secondary_vtable vfptr_offset=0x{:X} "
                    "target=0x{:X}\n"),
                secondary_vfptr_offset, target);
            return false;
        }

        viewport_input_key_trampoline_ = 0;
        viewport_input_key_detour_ = std::make_unique<PLH::x64Detour>(
            static_cast<uint64_t>(target),
            reinterpret_cast<uint64_t>(&Impl::viewport_input_key_hook),
            &viewport_input_key_trampoline_);
        const bool hooked = viewport_input_key_detour_->hook();
        if (!hooked || viewport_input_key_trampoline_ == 0)
        {
            if (hooked)
            {
                viewport_input_key_detour_->unHook();
            }
            viewport_input_key_detour_.reset();
            viewport_input_hook_failed_ = true;
            Output::send<LogLevel::Error>(
                STR("[KH3APProbeCpp] Runtime options viewport input hook failed "
                    "reason=polyhook target=0x{:X}\n"),
                target);
            return false;
        }

        s_original_input_key_.store(viewport_input_key_trampoline_, std::memory_order_release);
        s_input_key_owner_.store(this, std::memory_order_release);
        viewport_input_key_hooked_ = true;
        // These adjacent methods are the UE 4.17 FViewportClient ABI used above.
        // Install the focus guard as a pair: buttons alone leave sticks active.
        const auto axis_target =
            reinterpret_cast<uintptr_t>(secondary_vtable[viewport_input_axis_vtable_slot]);
        const auto focus_target =
            reinterpret_cast<uintptr_t>(secondary_vtable[viewport_lost_focus_vtable_slot]);
        const auto valid_target = [&](uintptr_t address)
        {
            MEMORY_BASIC_INFORMATION memory{};
            return address_is_executable(address) &&
                   VirtualQuery(reinterpret_cast<void*>(address), &memory, sizeof(memory)) ==
                       sizeof(memory) &&
                   memory.AllocationBase == game_module;
        };
        if (valid_target(axis_target) && valid_target(focus_target))
        {
            viewport_input_axis_detour_ = std::make_unique<PLH::x64Detour>(
                axis_target, reinterpret_cast<uint64_t>(&Impl::viewport_input_axis_hook),
                &s_original_input_axis_);
            viewport_lost_focus_detour_ = std::make_unique<PLH::x64Detour>(
                focus_target, reinterpret_cast<uint64_t>(&Impl::viewport_lost_focus_hook),
                &s_original_lost_focus_);
            if (viewport_input_axis_detour_->hook())
            {
                if (viewport_lost_focus_detour_->hook())
                {
                    controller_focus_installed_ = true;
                }
                else
                {
                    viewport_input_axis_detour_->unHook();
                }
            }
            if (!controller_focus_installed_)
            {
                viewport_input_axis_detour_.reset();
                viewport_lost_focus_detour_.reset();
            }
        }
        Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] Controller foreground guard "
                                            "installed={} axis_rva=0x{:X} lost_focus_rva=0x{:X}\n"),
                                        controller_focus_installed_,
                                        axis_target - reinterpret_cast<uintptr_t>(game_module),
                                        focus_target - reinterpret_cast<uintptr_t>(game_module));
        const uintptr_t module_base = reinterpret_cast<uintptr_t>(game_module);
        Output::send<LogLevel::Warning>(
            STR("[KH3APProbeCpp] Runtime options viewport input hook installed "
                "class={} vfptr_offset=0x{:X} input_key_slot={} "
                "target_rva=0x{:X} "
                "consume=confirm_back_scroll_navigation\n"),
            viewport_client->GetClassPrivate()->GetName(), secondary_vfptr_offset,
            viewport_input_key_vtable_slot, target - module_base);
        return true;
    }
} // namespace kh3ap
