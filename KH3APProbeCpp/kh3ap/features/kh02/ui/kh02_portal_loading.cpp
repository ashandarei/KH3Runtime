#include "kh02_portal_loading.h"
#include "kh3ap/features/kh02/core/kh02_profile.h"
#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#include "kh3ap/features/world/loading_runtime.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <stdexcept>

namespace kh3ap
{
    using namespace RC::Unreal;
    using namespace loading_runtime;

    namespace
    {

        auto image() -> uintptr_t
        {
            const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
            using kh02_native::Site;
            if (!supported_image(base) ||
                !kh02_native::verify_sites({Site::manual_loading_open, Site::manual_loading_flags}))
            {
                throw std::runtime_error("portal_loading_unsupported_native_screen");
            }
            return base;
        }

        auto loading_definition(uintptr_t base) -> const void*
        {
            // The loading operation retains its last native artwork/text row.
            // Borrow it within this tick; the engine owns its FString/FTexts.
            void* operation{};
            const auto transition = loading_transition(base, false);
            if (!transition.complete || transition.layout_mismatch ||
                !read_native(base + native_rva(loading_operation_rva), operation) ||
                read_field<uintptr_t>(operation, 0) != base + native_rva(0x7c40fa0) ||
                read_field<uint8_t>(operation, 0x73, 0xff) != 0)
            {
                throw std::runtime_error("portal_loading_definition_owner");
            }
            const auto* definition = static_cast<const std::byte*>(operation) + 0x20;
            const auto* artwork = read_field<const wchar_t*>(definition, 0x30);
            const auto length = read_field<int32_t>(definition, 0x38, -1);
            const auto capacity = read_field<int32_t>(definition, 0x3c, -1);
            wchar_t terminator{};
            if (!artwork || length < 2 || length > 512 || capacity < length || capacity > 4096 ||
                !read_native(reinterpret_cast<uintptr_t>(artwork + length - 1), terminator) || terminator != 0)
            {
                throw std::runtime_error("portal_loading_definition_artwork");
            }
            return definition;
        }
    } // namespace

    Kh02PortalLoading::~Kh02PortalLoading()
    {
        try
        {
            close();
        }
        catch (...)
        {
        }
    }

    void Kh02PortalLoading::open(UObject* pawn)
    {
        close();
        const auto base = image();
        auto* manager = owned_save_manager(pawn);
        auto* controller = try_read_reflected_object_property_value(pawn, L"Controller");
        if (!is_uobject_valid_fast(manager) || !is_uobject_valid_fast(controller) ||
            manager->GetClassPrivate()->GetName() != L"TresUIManager" ||
            try_read_reflected_object_property_value(manager, L"m_pPlayerController") != controller ||
            try_read_reflected_object_property_value(controller, L"Pawn") != pawn)
        {
            throw std::runtime_error("portal_loading_owner");
        }
        const auto screen = loading_screen_snapshot(base, manager);
        if (!screen.known || screen.layout_mismatch || !screen.closed || !is_uobject_valid_fast(screen.object) ||
            !is_uobject_valid_fast(try_read_reflected_object_property_value(screen.object, L"m_pMoviePlayer")))
        {
            throw std::runtime_error("portal_loading_native_screen_unavailable");
        }
        const auto* definition = loading_definition(base);
        native_screen = make_uobject_weak(screen.object);
        native_manager = make_uobject_weak(manager);
        const auto open = base + kh02_native::rva(kh02_native::Site::manual_loading_open);
        if (kh02_native::build() == kh02_native::Build::Steam)
        {
            reinterpret_cast<void (*)(UObject*, const void*, bool)>(open)(screen.object, definition, false);
        }
        else
        {
            reinterpret_cast<void (*)(UObject*, const void*)>(open)(screen.object, definition);
        }
        const auto opened = loading_screen_snapshot(base, manager);
        if (!opened.known || !opened.open || opened.closed || opened.mode != 1 || opened.object != screen.object)
        {
            throw std::runtime_error("portal_loading_open_not_observed");
        }
    }

    void Kh02PortalLoading::prepare_travel()
    {
        Kh02ReflectedCall type(L"/Script/TresGame.Default__TresGameBlueprintLibrary", L"TresUI_SetLoadingScreenType",
                               "portal_loading", 8);
        type.integer(L"Type", 1);
        type.run();
        travel_prepared = true;
    }

    void Kh02PortalLoading::handoff()
    {
        travel_prepared = false;
        native_screen = {};
        native_manager = {};
    }

    void Kh02PortalLoading::close()
    {
        if (travel_prepared)
        {
            Kh02ReflectedCall type(L"/Script/TresGame.Default__TresGameBlueprintLibrary",
                                   L"TresUI_SetLoadingScreenType", "portal_loading", 8);
            type.integer(L"Type", 0);
            type.run();
            travel_prepared = false;
        }
        auto* screen = native_screen.Get();
        auto* manager = native_manager.Get();
        if (is_uobject_valid_fast(screen) && is_uobject_valid_fast(manager))
        {
            const auto base = image();
            const auto current = loading_screen_snapshot(base, manager);
            if (!current.known || current.layout_mismatch || current.object != screen)
            {
                throw std::runtime_error("portal_loading_cleanup_owner_changed");
            }
            if (!current.closed)
            {
                reinterpret_cast<void (*)(UObject*, bool)>(base + native_rva(close_loading_screen_rva))(screen, true);
            }
        }
        native_screen = {};
        native_manager = {};
    }

    auto Kh02PortalLoading::active() const -> bool
    {
        return is_uobject_valid_fast(native_screen.Get());
    }
} // namespace kh3ap
