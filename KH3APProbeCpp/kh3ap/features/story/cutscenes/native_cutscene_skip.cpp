#include "native_cutscene_skip.h"
#include "kh3ap/core/input/runtime_controller_input_profile.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include <Unreal/UClass.hpp>

namespace kh3ap
{
    using namespace RC::Unreal;
    namespace
    {
        auto game_instance(UObject* world) -> UObject*
        {
            if (!is_uobject_valid_fast(world) || world != kh02_current_world())
            {
                return nullptr;
            }
            auto* instance = try_read_reflected_object_property_value(world, L"OwningGameInstance");
            return is_uobject_valid_fast(instance) &&
                           instance->GetClassPrivate()->GetName() == L"TresGameInstance"
                       ? instance
                       : nullptr;
        }

        struct Profile
        {
            uintptr_t request{};
            size_t fade_pending{};
            bool supported{};
            controller_input::DeathUiProfile ui;
        };

        auto profile() -> const Profile&
        {
            static const Profile value = []
            {
                Profile p;
                const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
                if (!controller_input::supported(base, p.ui))
                {
                    return p;
                }
                using controller_input::signature;
                if (p.ui.steam)
                {
                    p.request = 0x15a82a0;
                    p.fade_pending = 0x200;
                    p.supported =
                        signature(base + p.request,
                                  {0x48, 0x8b, 0xc4, 0x55, 0x57, 0x41, 0x56, 0x48, 0x8d, 0x68, 0xa1,
                                   0x48, 0x81, 0xec, 0xa0, 0, 0, 0}) &&
                        // Native fade completion callback -> game-instance skip.
                        signature(base + 0x15a82da, {0x48, 0x8d, 0x05, 0xbf, 0x75, 0xff, 0xff}) &&
                        signature(base + 0x159f8bb, {0x48, 0x8b, 0x8b, 0x80, 0x01, 0, 0}) &&
                        signature(base + 0x159f8e8, {0xe9, 0x73, 0xf4, 0x90, 0xff});
                }
                else
                {
                    p.request = 0x18fb510;
                    p.fade_pending = 0x1c0;
                    p.supported =
                        signature(base + p.request, {0x48, 0x8b, 0xc4, 0x55, 0x57, 0x41, 0x54, 0x41,
                                                     0x56, 0x41, 0x57, 0x48, 0x8d, 0x68, 0xa1}) &&
                        signature(base + 0x18fb54e, {0x48, 0x8d, 0x05, 0x9b, 0x65, 0, 0}) &&
                        signature(base + 0x1901b0b, {0x48, 0x8b, 0x8b, 0x40, 0x01, 0, 0}) &&
                        signature(base + 0x1901b5a, {0xe9, 0xb1, 0xb9, 0x83, 0xff});
                }
                return p;
            }();
            return value;
        }
    } // namespace

    auto native_cutscene_pause_type(UObject* world) -> int
    {
        if (!profile().supported)
        {
            return -1;
        }
        auto* instance = game_instance(world);
        int type = -1;
        if (instance)
        {
            controller_input::read(reinterpret_cast<uintptr_t>(instance) + 0x8d0, type);
        }
        return type;
    }

    auto request_native_cutscene_skip(UObject* world) -> NativeCutsceneSkipResult
    {
        const auto& p = profile();
        if (!p.supported)
        {
            return NativeCutsceneSkipResult::Unsupported;
        }
        auto* instance = game_instance(world);
        if (!instance || native_cutscene_pause_type(world) != 1)
        {
            return NativeCutsceneSkipResult::Unavailable;
        }
        auto* manager = try_read_reflected_object_property_value(instance, L"m_UIManager");
        const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        if (!is_uobject_valid_fast(manager) ||
            controller_input::field<uintptr_t>(manager, 0) != base + p.ui.manager_vtable ||
            try_read_reflected_object_property_value(manager, L"m_pGameInstance") != instance)
        {
            return NativeCutsceneSkipResult::Unavailable;
        }
        controller_input::offset(manager, L"m_pGameInstance", p.ui.manager_instance);
        auto* pause = try_read_reflected_object_property_value(manager, L"m_Pause");
        // Do not take over a player's already-open menu or another fade transaction.
        if (!is_uobject_valid_fast(pause) ||
            controller_input::field<uintptr_t>(pause, 0) != base + p.ui.pause_vtable)
        {
            return NativeCutsceneSkipResult::Unavailable;
        }
        if (controller_input::field<uint8_t>(pause, 0xa1) ||
            controller_input::field<uint8_t>(manager, p.fade_pending))
        {
            return NativeCutsceneSkipResult::Busy;
        }
        // No retained owner/function pointers are used after this call. The
        // game's coordinator captures its own weak delegate for fade completion.
        using Request = void(__fastcall*)(UObject*);
        reinterpret_cast<Request>(base + p.request)(manager);
        return NativeCutsceneSkipResult::Requested;
    }
} // namespace kh3ap
