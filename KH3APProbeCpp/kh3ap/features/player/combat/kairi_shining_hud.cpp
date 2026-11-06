#include "kh3ap/features/player/characters/character_profile.h"
#include "kh3ap/features/player/combat/kairi_shining_hud.h"
#include <polyhook2/Detour/x64Detour.hpp>
#include "kh3ap/features/player/characters/character_swap.h"
#include "kh3ap/features/player/player_animation_support.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include "kh3ap/features/world/space/space_service.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/FString.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#include <Unreal/TArray.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
#include <nlohmann/json.hpp>
#endif
#define NOMINMAX
#include <Windows.h>
#include <intrin.h>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>

namespace kh3ap::kairi_shining_hud
{
    using namespace RC::Unreal;
    namespace
    {
        using Clock = std::chrono::steady_clock;
        constexpr int kairi_id = static_cast<int>(player_animation_support::PlayerId::Kairi);
        constexpr auto preparation_timeout = std::chrono::seconds(30);
        constexpr auto departure_timeout = std::chrono::seconds(10);

        const uintptr_t add_ui_part_rva = character_native::rva(character_native::Site::add_ui_part);
        const uintptr_t remove_ui_part_rva = character_native::rva(character_native::Site::remove_ui_part);
        const uintptr_t cockpit_remove_return_rva = character_native::rva(character_native::Site::cockpit_remove_shining_call) + 5;
        const uintptr_t initialize_shining_rva = character_native::rva(character_native::Site::initialize_shining);
        const uintptr_t gfx_goto_and_stop_rva = character_native::rva(character_native::Site::gfx_goto_and_stop);
        constexpr size_t initialize_vtable_offset = 0x238;
        constexpr size_t movie_player_offset = 0x58;
        constexpr size_t ultima_lock_gauge_offset = 0x1f0;
        constexpr size_t cockpit_manager_offset = 0x208;
        constexpr size_t cockpit_shining_offset = 0x150;
        constexpr size_t shining_cockpit_offset = 0x1c8;
        constexpr size_t ui_initialized_offset = 0xa0;
        constexpr size_t ui_active_offset = 0xa1;
        constexpr size_t shining_target_offset = 0x1d0;
        constexpr size_t maximum_ui_parts = 256;
        constexpr auto shining_class_path = L"/Script/TresGame.TresUIP_HudShining";
        using AddPart = int (*)(UObject*, UObject*);
        using RemovePart = void (*)(UObject*, UObject*);
        using Initialize = void (*)(UObject*);
        using GotoAndStop = void (*)(UObject*, const FString&);

        uintptr_t module_base{};
        uint64_t original_remove{};
        std::unique_ptr<PLH::x64Detour> remove_hook;
        bool install_attempted{};
        bool enabled{};
        bool prepare_pending{};
        bool retain_departure{};
        Clock::time_point preparation_deadline{}, departure_deadline{};
        FWeakObjectPtr cached_world, cached_manager, cached_cockpit, cached_part;
        std::string last_result{"off"};
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        uint64_t created{}, adopted{}, reused{}, retained{}, released{}, preparation_attempts{};
        int64_t last_initialize_us{};
#endif

        template <typename T>
        T& native_field(UObject* object, size_t offset)
        {
            return *reinterpret_cast<T*>(reinterpret_cast<std::byte*>(object) + offset);
        }

        UObject* object(UObject* owner, const wchar_t* name)
        {
            return is_uobject_valid_fast(owner) ? try_read_reflected_object_property_value(owner, name) : nullptr;
        }

        UObject** object_slot(UObject* owner, const wchar_t* name, size_t expected_offset)
        {
            auto* field = is_uobject_valid_fast(owner) ? owner->GetPropertyByNameInChain(name) : nullptr;
            if (!field || field->GetClass().GetName() != L"ObjectProperty" ||
                field->GetElementSize() != sizeof(UObject*) || field->GetOffset_Internal() != expected_offset)
            {
                throw std::runtime_error("shining_hud_property_layout");
            }
            return field->ContainerPtrToValuePtr<UObject*>(owner);
        }

        bool contains_part(UObject* manager, UObject* part)
        {
            auto* field = manager->GetPropertyByNameInChain(L"m_UIPartsAry");
            if (!field || !field->IsA<FArrayProperty>())
            {
                return false;
            }
            auto* inner = static_cast<FArrayProperty*>(field)->GetInner();
            if (!inner || inner->GetClass().GetName() != L"ObjectProperty" ||
                inner->GetElementSize() != sizeof(UObject*))
            {
                return false;
            }
            const auto& parts = *field->ContainerPtrToValuePtr<TArray<UObject*>>(manager);
            if (parts.Num() < 0 || parts.Num() > maximum_ui_parts)
            {
                return false;
            }
            for (auto* candidate : parts)
            {
                if (candidate == part)
                {
                    return true;
                }
            }
            return false;
        }

        UObject* current_manager()
        {
            return object(object(kh02_current_viewport(), L"GameInstance"), L"m_UIManager");
        }

        UObject* current_cockpit(UObject* manager)
        {
            return object(object(manager, L"m_ActiveHUD"), L"TresCockpit");
        }

        void forget()
        {
            cached_part = {};
            cached_cockpit = {};
            cached_manager = {};
            cached_world = {};
            retain_departure = false;
        }

        void release()
        {
            auto* part = cached_part.Get();
            auto* manager = cached_manager.Get();
            auto* cockpit = cached_cockpit.Get();
            forget();
            if (!part)
            {
                return;
            }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            ++released;
#endif
            // An attached widget belongs to the native cockpit again. It will
            // perform its own reset/removal on the return to Sora or teardown.
            if (cockpit && object(cockpit, L"m_pHudShining") == part)
            {
                return;
            }
            if (manager && original_remove && contains_part(manager, part))
            {
                native_field<UObject*>(part, shining_cockpit_offset) = nullptr;
                reinterpret_cast<RemovePart>(original_remove)(manager, part);
            }
        }

        void remove_part(UObject* manager, UObject* part)
        {
            if (part && part == cached_part.Get())
            {
                auto* cockpit = cached_cockpit.Get();
                // Retain only the exact native mode-change removal from our
                // authorized Kairi -> Sora swap. The caller has already hidden
                // the movie and reset its target/counter; it clears its pointer
                // after this call. All other removals keep native semantics.
                const bool keep = enabled && retain_departure &&
                    reinterpret_cast<uintptr_t>(_ReturnAddress()) == module_base + cockpit_remove_return_rva &&
                    Clock::now() < departure_deadline && cockpit && manager == cached_manager.Get() &&
                    cached_world.Get() == kh02_current_world() && current_cockpit(manager) == cockpit &&
                    object(cockpit, L"m_pHudShining") == part &&
                    !native_field<uint8_t>(part, ui_active_offset) &&
                    !native_field<FWeakObjectPtr>(part, shining_target_offset).Get();
                retain_departure = false;
                if (keep)
                {
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
                    ++retained;
#endif
                    last_result = "standby";
                    return;
                }
                forget();
                last_result = "native_release";
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
                ++released;
#endif
            }
            reinterpret_cast<RemovePart>(original_remove)(manager, part);
        }

        bool install()
        {
            if (install_attempted)
            {
                return remove_hook && original_remove;
            }
            install_attempted = true;
            module_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
            if (!character_native::verify_sites({character_native::Site::add_ui_part, character_native::Site::remove_ui_part, character_native::Site::initialize_shining, character_native::Site::gfx_goto_and_stop, character_native::Site::cockpit_remove_shining_call}))
            {
                return false;
            }
            remove_hook = std::make_unique<PLH::x64Detour>(module_base + remove_ui_part_rva,
                reinterpret_cast<uintptr_t>(&remove_part), &original_remove);
            if (!remove_hook->hook() || !original_remove)
            {
                remove_hook.reset();
                original_remove = 0;
                return false;
            }
            return true;
        }

        bool prepare()
        {
            auto* world = kh02_current_world();
            auto* manager = current_manager();
            auto* controller = object(manager, L"m_pPlayerController");
            auto* pawn = object(controller, L"Pawn");
            auto* cockpit = current_cockpit(manager);
            if (!is_uobject_valid_fast(world) || !is_uobject_valid_fast(pawn) || !is_uobject_valid_fast(cockpit) ||
                kh02_current_story_world() || RuntimeSpaceService::is_space_active() ||
                kh02_actor_world(pawn) != world || object(pawn, L"Controller") != controller ||
                object(object(controller, L"Player"), L"PlayerController") != controller)
            {
                return false;
            }
            if (cockpit->GetClassPrivate()->GetName() != L"TresCockpit" ||
                native_field<UObject*>(cockpit, cockpit_manager_offset) != manager)
            {
                throw std::runtime_error("shining_hud_cockpit_layout");
            }
            if (cached_part.Get() && cached_world.Get() == world && cached_manager.Get() == manager &&
                cached_cockpit.Get() == cockpit && contains_part(manager, cached_part.Get()))
            {
                return true;
            }
            release();
            auto** attached = object_slot(cockpit, L"m_pHudShining", cockpit_shining_offset);
            auto* part = *attached;
            const bool existing = is_uobject_valid_fast(part);
            // Do not borrow a stock story-fight widget. Only adopt one created
            // by our swap if selection raced the initial prewarm.
            if (part && (!existing || character_swap::current_hero() != pawn))
            {
                return false;
            }
            auto* type = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, shining_class_path);
            // TresCockpit already lives in the native transient package. The
            // UE4SS long-object-name resolver does not accept package-only paths.
            auto* outer = cockpit->GetOuterPrivate();
            if (!is_uobject_valid_fast(type) || !is_uobject_valid_fast(outer))
            {
                return false;
            }
            if (!existing)
            {
                FStaticConstructObjectParameters parameters(type, outer);
                parameters.SetFlags = RF_Transient;
                part = UObjectGlobals::StaticConstructObject(parameters);
            }
            if (!is_uobject_valid_fast(part) || part->GetClassPrivate() != type)
            {
                throw std::runtime_error("shining_hud_class_layout");
            }
            const auto* vtable = *reinterpret_cast<const uintptr_t* const*>(part);
            if (vtable[initialize_vtable_offset / sizeof(uintptr_t)] != module_base + initialize_shining_rva)
            {
                throw std::runtime_error("shining_hud_initialize_layout");
            }
            object_slot(part, L"m_pMoviePlayer", movie_player_offset);
            object_slot(part, L"m_UltimaLockGauge", ultima_lock_gauge_offset);
            cached_world = make_uobject_weak(world);
            cached_manager = make_uobject_weak(manager);
            cached_cockpit = make_uobject_weak(cockpit);
            cached_part = make_uobject_weak(part);
            if (!cached_world.Get() || !cached_manager.Get() || !cached_cockpit.Get() || !cached_part.Get())
            {
                throw std::runtime_error("shining_hud_weak_reference");
            }
            if (!existing)
            {
                // The manager's reflected list owns the GC reference. Never
                // root a UI object or re-add an already registered widget.
                reinterpret_cast<AddPart>(module_base + add_ui_part_rva)(manager, part);
                if (!contains_part(manager, part))
                {
                    throw std::runtime_error("shining_hud_registration");
                }
                native_field<UObject*>(part, shining_cockpit_offset) = cockpit;
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
                const auto started = Clock::now();
#endif
                reinterpret_cast<Initialize>(module_base + initialize_shining_rva)(part);
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
                last_initialize_us = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - started).count();
                ++created;
#endif
                native_field<uint8_t>(part, ui_active_offset) = 0;
                if (auto* lock_set = object(part, L"m_UltimaLockSet"))
                {
                    const FString off_label{L"off"};
                    reinterpret_cast<GotoAndStop>(module_base + gfx_goto_and_stop_rva)(lock_set, off_label);
                }
            }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            else
            {
                ++adopted;
            }
#endif
            if (!native_field<uint8_t>(part, ui_initialized_offset) || !object(part, L"m_pMoviePlayer") ||
                !object(part, L"m_UltimaLockGauge"))
            {
                throw std::runtime_error("shining_hud_movie_not_ready");
            }
            last_result = existing ? "attached" : "standby";
            return true;
        }

        void fail(const std::exception& error)
        {
            prepare_pending = false;
            release();
            last_result = error.what();
            RC::Output::send<RC::LogLevel::Warning>(
                STR("[KH3APProbeCpp] Kairi Shining HUD cache: {}\n"),
                std::wstring(last_result.begin(), last_result.end()));
        }

        void request_preparation()
        {
            prepare_pending = enabled;
            preparation_deadline = Clock::now() + preparation_timeout;
        }
    }

    void select(int player_id)
    {
        const bool selected = player_id == kairi_id;
        if (enabled == selected)
        {
            return;
        }
        enabled = selected;
        release();
        last_result = enabled ? "pending" : "off";
        request_preparation();
    }

    bool preparation_pending()
    {
        return prepare_pending;
    }

    void notify_player_started()
    {
        // Existing map/player-start notifications rearm a bounded request.
        // A same-cockpit handoff keeps its movie and stops on the first check.
        request_preparation();
    }

    void notify_map_loading()
    {
        prepare_pending = false;
        release();
        last_result = enabled ? "map_loading" : "off";
    }

    void before_swap(UObject* outgoing_pawn, int target_id)
    {
        try
        {
            retain_departure = false;
            auto* part = cached_part.Get();
            auto* manager = cached_manager.Get();
            auto* cockpit = cached_cockpit.Get();
            if (enabled && !part)
            {
                // A native UI replacement may retire the cache without a map
                // change. This explicit swap event can adopt its replacement.
                request_preparation();
            }
            if (!enabled || !part || !manager || !cockpit || cached_world.Get() != kh02_actor_world(outgoing_pawn) ||
                cached_world.Get() != kh02_current_world() || manager != current_manager() ||
                cockpit != current_cockpit(manager) || !contains_part(manager, part))
            {
                return;
            }
            auto** attached = object_slot(cockpit, L"m_pHudShining", cockpit_shining_offset);
            if (target_id == kairi_id && !*attached)
            {
                *attached = part;
                last_result = "attached";
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
                ++reused;
#endif
            }
            else if (target_id != kairi_id && *attached == part && character_swap::current_hero() == outgoing_pawn)
            {
                retain_departure = true;
                departure_deadline = Clock::now() + departure_timeout;
            }
        }
        catch (const std::exception& error)
        {
            fail(error);
        }
    }

    void tick(Clock::time_point now)
    {
        if (!prepare_pending)
        {
            return;
        }
        try
        {
            if (now >= preparation_deadline)
            {
                throw std::runtime_error("shining_hud_preparation_timeout");
            }
            if (!install())
            {
                prepare_pending = false;
                last_result = "unsupported_build_or_hook";
                return;
            }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            ++preparation_attempts;
#endif
            prepare_pending = !prepare();
        }
        catch (const std::exception& error)
        {
            fail(error);
        }
    }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    nlohmann::json snapshot()
    {
        auto* part = cached_part.Get();
        auto* cockpit = cached_cockpit.Get();
        return {{"enabled", enabled}, {"pending", prepare_pending}, {"result", last_result},
                {"ready", part && cached_manager.Get() && cockpit && cached_world.Get()},
                {"attached", part && cockpit && object(cockpit, L"m_pHudShining") == part},
                {"created", created}, {"adopted", adopted}, {"reused", reused}, {"retained", retained},
                {"released", released}, {"preparation_attempts", preparation_attempts},
                {"last_initialize_us", last_initialize_us}};
    }
#endif
}
