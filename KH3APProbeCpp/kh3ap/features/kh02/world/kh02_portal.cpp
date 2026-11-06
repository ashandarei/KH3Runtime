#include "kh02_portal.h"
#include "kh3ap/features/kh02/transport/kh02_asset_companion.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#include "kh3ap/features/kh02/story/kh02_arrival_presentation.h"
#include "kh3ap/features/kh02/ui/kh02_portal_loading.h"
#include "kh3ap/core/objects/actor_discovery.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/bridge/utilities.h"
#include "kh3ap/core/input/runtime_controller_input.h"
#include "kh3ap/features/player/characters/character_swap.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include <Unreal/AActor.hpp>
#include <Unreal/GameplayStatics.hpp>
#include <Unreal/TArray.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/FFrame.hpp>
#include "kh3ap/bridge/rewards/reconciliation.h"
#include <Unreal/FWeakObjectPtr.hpp>
#include <Unreal/World.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <nlohmann/json.hpp>
#include <chrono>
#include <cstring>
#define NOMINMAX
#include <Windows.h>

namespace kh3ap
{
    using namespace RC::Unreal;

    namespace
    {
        using Json = nlohmann::json;
        using Clock = std::chrono::steady_clock;
        constexpr std::size_t reflected_parameter_limit_bytes = 512;
        constexpr std::size_t maximum_notice_parameter_bytes = 256;
        constexpr std::size_t maximum_interaction_parameter_bytes = 128;
        constexpr std::size_t minimum_jump_parameter_bytes = 17;
        constexpr std::size_t maximum_jump_parameter_bytes = 24;
        constexpr auto discovery_interval = std::chrono::seconds(1);
        constexpr auto interaction_hold_duration = std::chrono::milliseconds(150);
        constexpr auto preparation_timeout = std::chrono::seconds(120);
        constexpr float notice_display_seconds = 15.0f;
        constexpr int fade_out_kind = 1;
        constexpr auto goa = L"/Game/Maps/GardenOfAssemblage/umap/GoA_01.GoA_01";
        constexpr int dark_portal_id = 8;

        struct Call : Kh02ReflectedCall
        {
            Call(UObject* owner, const wchar_t* method)
                : Kh02ReflectedCall(owner, method, "kh02_portal", reflected_parameter_limit_bytes)
            {
            }
        };

        void notice(UObject* pawn, const std::wstring& message)
        {
            auto* owner = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr,
                                                                     L"/Script/Engine.Default__KismetSystemLibrary");
            auto* fn = is_uobject_valid_fast(owner) ? owner->GetFunctionByNameInChain(L"PrintString") : nullptr;
            if (!is_uobject_valid_fast(fn) || fn->GetParmsSize() > maximum_notice_parameter_bytes)
            {
                return;
            }
            std::vector<std::byte> params(fn->GetParmsSize());
            auto* text = find_function_param_by_name(fn, L"InString");

            struct Borrowed
            {
                const wchar_t* data;
                int32_t count, capacity;
            };

            if (!text || text->GetElementSize() != sizeof(Borrowed))
            {
                return;
            }
            const Borrowed value{message.c_str(), int32_t(message.size() + 1), int32_t(message.size() + 1)};
            std::memcpy(text->ContainerPtrToValuePtr<void>(params.data()), &value, sizeof(value));
            write_object_function_param(fn, params.data(), L"WorldContextObject", pawn);
            write_bool_function_param(find_function_param_by_name(fn, L"bPrintToScreen"), params.data(), true);
            write_bool_function_param(find_function_param_by_name(fn, L"bPrintToLog"), params.data(), true);
            write_float_function_param(find_function_param_by_name(fn, L"Duration"), params.data(),
                                       notice_display_seconds);
            if (auto* color = find_function_param_by_name(fn, L"TextColor"); color && color->GetElementSize() == 16)
            {
                const float rgba[]{0.3f, 0.8f, 1.0f, 1.0f};
                std::memcpy(color->ContainerPtrToValuePtr<void>(params.data()), rgba, 16);
            }
            owner->ProcessEvent(fn, params.data());
        }

        void travel()
        {
            auto* owner = UObjectGlobals::StaticFindObject<UObject*>(
                nullptr, nullptr, L"/Script/TresGame.Default__TresGameBlueprintLibrary");
            auto* fn = is_uobject_valid_fast(owner) ? owner->GetFunctionByNameInChain(L"TresMapJump") : nullptr;
            if (!is_uobject_valid_fast(fn) || fn->GetParmsSize() < minimum_jump_parameter_bytes ||
                fn->GetParmsSize() > maximum_jump_parameter_bytes)
            {
                throw std::runtime_error("kh02_portal_jump_layout");
            }
            alignas(uint64_t) std::byte params[maximum_jump_parameter_bytes]{};
            if (!write_name_or_string_function_param(find_function_param_by_name(fn, L"MapName"), params,
                                                     L"/Game/Maps/APKH02CastleTown/APKH02CastleTown") ||
                !write_name_or_string_function_param(find_function_param_by_name(fn, L"TargetTag"), params,
                                                     L"APKH02CastleStart") ||
                !write_integral_function_param(find_function_param_by_name(fn, L"InFadeKind"), params, fade_out_kind))
            {
                throw std::runtime_error("kh02_portal_jump_parameters");
            }
            owner->ProcessEvent(fn, params);
        }

    } // namespace

    struct Kh02Portal::Impl
    {
        std::function<bool(UObject*)> before_travel;
        std::function<bool()> assets_notified;
        inline static Impl* active{};
        bool reaction_notified{};
        bool provider_started{};
        FWeakObjectPtr world, portal, pending_pawn, pressed_controller;
        Clock::time_point release_at{};
        Clock::time_point next_discovery{};
        bool pending{};
        Kh02ArrivalPresentation preparation_loading;
        Kh02PortalLoading loading_screen;
        bool loading_owned{};
        Clock::time_point preparation_deadline{};
        uint64_t requested_at_ms{}, provider_ready_at_ms{}, assets_ready_at_ms{}, travel_at_ms{};
        std::string phase = "idle", error;
        uint64_t requests{}, travels{};

        ~Impl()
        {
            if (active == this)
            {
                active = nullptr;
            }
        }

        static void complete_thunk(UObject* object, FFrame& stack, void* result)
        {
            auto* frame = reinterpret_cast<FFrame_50_AndBelow*>(&stack);
            if (frame->Code)
            {
                ++frame->Code;
            }
            const auto saved = reconciliation::view();
            const bool complete =
                active && object == active->portal.Get() && saved->ready && saved->kh02_progress_ready &&
                saved->kh02_progress &&
                kh02_progress::flag_value(saved->kh02_progress->flags, "gameflow_REBELLIS_CLEAR") != 0;
            if (result)
            {
                *static_cast<uint8_t*>(result) = complete;
            }
        }

        static void request_thunk(UObject* object, FFrame& stack, void* result)
        {
            auto* frame = reinterpret_cast<FFrame_50_AndBelow*>(&stack);
            if (frame->Code)
            {
                ++frame->Code;
            }
            const bool accepted = active && object == active->portal.Get() && !active->pending;
            if (accepted)
            {
                active->reaction_notified = true;
            }
            if (result)
            {
                *static_cast<uint8_t*>(result) = accepted;
            }
        }

        void bind_menu()
        {
            auto* actor = portal.Get();
            for (const auto* name : {L"APKH02Complete", L"APKH02RequestTravel"})
            {
                auto* function = actor->GetFunctionByNameInChain(name);
                auto* returned = is_uobject_valid_fast(function) ? function->GetReturnProperty() : nullptr;
                if (!returned || !returned->IsA<FBoolProperty>() || function->GetNumParms() != 1 ||
                    function->GetParmsSize() != 1 || function->GetReturnValueOffset() != 0)
                {
                    throw std::runtime_error("kh02_portal_menu_signature");
                }
                function->SetFuncPtr(std::wstring_view{name} == L"APKH02Complete" ? complete_thunk : request_thunk);
                function->GetFunctionFlags() |= FUNC_Native;
            }
            active = this;
        }

        auto work_pending() const -> bool
        {
            if (reaction_notified || (pressed_controller.Get() && Clock::now() >= release_at))
            {
                return true;
            }
            if (!pending)
            {
                return false;
            }
            return Clock::now() >= preparation_deadline ||
                   (phase == "preparing" && (!provider_started || kh02_provider_completion_ready())) ||
                   (phase == "loading_assets" && assets_notified && assets_notified());
        }

        auto snapshot() const -> Json
        {
            Json result = {{"phase", phase},
                           {"error", error},
                           {"portal", is_uobject_valid_fast(portal.Get()) ? to_utf8(portal.Get()->GetPathName()) : ""},
                           {"requests", requests},
                           {"travels", travels},
                           {"pending", pending},
                           {"preparation_loading_owned", loading_owned},
                           {"preparation_loading_screen", loading_screen.active()},
                           {"requested_at_ms", requested_at_ms},
                           {"provider_ready_at_ms", provider_ready_at_ms},
                           {"assets_ready_at_ms", assets_ready_at_ms},
                           {"travel_at_ms", travel_at_ms}};
            if (auto* actor = portal.Get(); is_uobject_valid_fast(actor))
            {
                Call location(actor, L"K2_GetActorLocation");
                location.run();
                result["portal_location"] = location.result_vector();
                auto* reactor = try_read_reflected_object_property_value(actor, L"TresReactor");
                Call offset(reactor, L"K2_GetComponentLocation");
                offset.run();
                result["reactor_location"] = offset.result_vector();
            }
            return result;
        }

        void release_preparation_loading(bool travel_handoff)
        {
            if (!travel_handoff)
            {
                loading_screen.close();
            }
            if (!loading_owned)
            {
                return;
            }
            preparation_loading.release_input();
            preparation_loading.reset_bindings();
            loading_owned = false;
        }

        void discover(UObject* pawn)
        {
            auto* w = kh02_current_world();
            if (is_uobject_valid_fast(pawn) && kh02_actor_world(pawn) != w)
            {
                return;
            }
            if (world.Get() != w)
            {
                release_preparation_loading(true);
                world = make_uobject_weak(w);
                portal = {};
                reaction_notified = false;
                next_discovery = {};
                if (pending)
                {
                    pending = false;
                    pending_pawn = {};
                    phase = "cancelled_world_changed";
                }
            }
            if (!is_uobject_valid_fast(w) || w->GetPathName() != goa)
            {
                return;
            }
            if (is_uobject_valid_fast(portal.Get()) || !is_uobject_valid_fast(pawn) || Clock::now() < next_discovery)
            {
                return;
            }
            next_discovery = Clock::now() + discovery_interval;
            auto* portal_type = find_loaded_runtime_asset_class(
                L"/Game/Maps/GardenOfAssemblage/uasset/Textures/Portals/Portal_Base.Portal_Base_C");
            if (!is_uobject_valid_fast(portal_type))
            {
                return;
            }
            TArray<AActor*> found;
            UGameplayStatics::GetAllActorsOfClass(pawn, portal_type, found);
            for (auto* actor : found)
            {
                if (!is_uobject_valid_fast(actor) || actor->GetPathName().find(L"Default__") != std::wstring::npos ||
                    static_cast<AActor*>(actor)->GetWorld() != w)
                {
                    continue;
                }
                auto* identifier = actor->GetPropertyByNameInChain(L"PortalID");
                if (!identifier || identifier->GetElementSize() != sizeof(int32_t) ||
                    identifier->GetClass().GetName() != L"IntProperty" ||
                    *identifier->ContainerPtrToValuePtr<int32_t>(actor) != dark_portal_id)
                {
                    continue;
                }
                if (is_uobject_valid_fast(portal.Get()))
                {
                    throw std::runtime_error("kh02_portal_multiple_in_world");
                }
                portal = make_uobject_weak(actor);
                bind_menu();
            }
        }

        void tick(UObject* pawn, bool playable)
        {
            if (is_uobject_valid_fast(pressed_controller.Get()) && Clock::now() >= release_at)
            {
                runtime_controller_action_input(pressed_controller.Get(), L"interact", false);
                pressed_controller = {};
            }
            discover(pawn);
            if (!is_uobject_valid_fast(portal.Get()))
            {
                return;
            }
            if (reaction_notified && playable)
            {
                // The menu belongs to the current alternate. The chapter must
                // enter through its normal Sora/0.2 Aqua lifecycle.
                if (!character_swap::prepare_native_story_travel())
                {
                    return;
                }
                reaction_notified = false;
                if (pending)
                {
                    return;
                }
                ++requests;
                error.clear();
                pending = true;
                provider_started = false;
                pending_pawn = make_uobject_weak(pawn);
                requested_at_ms = GetTickCount64();
                provider_ready_at_ms = 0;
                assets_ready_at_ms = 0;
                travel_at_ms = 0;
                preparation_deadline = Clock::now() + preparation_timeout;
                loading_owned = true;
                preparation_loading.hold_input(pawn);
                loading_screen.open(pawn);
                phase = "preparing";
                // Give the loading presentation a frame before requesting any
                // provider or imported asset work. Nothing starts on proximity.
                return;
            }
            if (!pending && loading_owned && phase == "error")
            {
                release_preparation_loading(false);
            }
            if (pending)
            {
                if (!is_uobject_valid_fast(pawn) || pending_pawn.Get() != pawn ||
                    static_cast<AActor*>(pawn)->GetWorld() != world.Get())
                {
                    release_preparation_loading(false);
                    pending = false;
                    phase = "cancelled_pawn_changed";
                    return;
                }
                if (Clock::now() >= preparation_deadline)
                {
                    throw std::runtime_error("kh02_portal_preparation_timeout");
                }
            }
            if (pending && phase == "preparing" && (!provider_started || kh02_provider_completion_ready()))
            {
                provider_started = true;
                if (prepare_kh02_provider())
                {
                    provider_ready_at_ms = GetTickCount64();
                    phase = "ready";
                }
            }
            if (pending && (phase == "ready" || (phase == "loading_assets" && assets_notified && assets_notified())))
            {
                // Our own loading UI blocks the ordinary gameplay-ready flag.
                // The pinned world, pawn and possession remain authoritative.
                auto* controller = try_read_reflected_object_property_value(pawn, L"Controller");
                if (!is_uobject_valid_fast(controller) ||
                    try_read_reflected_object_property_value(controller, L"Pawn") != pawn)
                {
                    throw std::runtime_error("kh02_portal_possession_changed");
                }
                if (before_travel && !before_travel(pawn))
                {
                    phase = "loading_assets";
                    return;
                }
                pending = false;
                phase = "entering";
                assets_ready_at_ms = GetTickCount64();
                ++travels;
                loading_screen.prepare_travel();
                travel();
                loading_screen.handoff();
                travel_at_ms = GetTickCount64();
                release_preparation_loading(true);
            }
        }
    };

    Kh02Portal::Kh02Portal(std::function<bool(UObject*)> before_travel, std::function<bool()> assets_notified)
        : impl(std::make_unique<Impl>())
    {
        impl->before_travel = std::move(before_travel);
        impl->assets_notified = std::move(assets_notified);
    }

    Kh02Portal::~Kh02Portal() = default;

    auto Kh02Portal::failed_preparation() const -> bool
    {
        return impl->phase == "error" || impl->phase.starts_with("cancelled");
    }

    auto Kh02Portal::work_pending() const -> bool
    {
        return impl->work_pending();
    }

    void Kh02Portal::cancel()
    {
        impl->release_preparation_loading(impl->phase == "entering");
        impl->loading_screen.close();
        impl->pending = false;
        impl->reaction_notified = false;
        impl->pending_pawn = {};
        if (auto* controller = impl->pressed_controller.Get(); is_uobject_valid_fast(controller))
        {
            runtime_controller_action_input(controller, L"interact", false);
        }
        impl->pressed_controller = {};
        impl->phase = "cancelled";
    }

    void Kh02Portal::tick(UObject* pawn, bool playable)
    {
        try
        {
            impl->tick(pawn, playable);
        }
        catch (const std::exception& e)
        {
            const bool changed = impl->error != e.what() || impl->phase != "error";
            impl->error = e.what();
            impl->phase = "error";
            impl->pending = false;
            try
            {
                impl->release_preparation_loading(false);
            }
            catch (const std::exception& cleanup)
            {
                impl->error += "; loading_cleanup:";
                impl->error += cleanup.what();
            }
            if (changed && is_uobject_valid_fast(pawn))
            {
                notice(pawn, impl->error == "kh02_entry_progress_not_ready_connect_client"
                                 ? L"Connect the KH3 client to this seed, then try Castle Town again."
                                 : L"Castle Town could not open. You can use the portal again to retry.");
            }
        }
    }

    auto Kh02Portal::inspect() const -> std::string
    {
        return impl->snapshot().dump();
    }

    void Kh02Portal::press(UObject* pawn)
    {
        impl->discover(pawn);
        if (!is_uobject_valid_fast(impl->portal.Get()) || is_uobject_valid_fast(impl->pressed_controller.Get()))
        {
            throw std::runtime_error("kh02_portal_button_unavailable");
        }
        auto* controller = try_read_reflected_object_property_value(pawn, L"Controller");
        if (!is_uobject_valid_fast(controller))
        {
            throw std::runtime_error("kh02_portal_controller");
        }
        runtime_controller_action_input(controller, L"interact", true);
        impl->pressed_controller = make_uobject_weak(controller);
        impl->release_at = Clock::now() + interaction_hold_duration;
    }

    void Kh02Portal::interact(UObject* pawn)
    {
        impl->discover(pawn);
        auto* p = impl->portal.Get();
        if (!is_uobject_valid_fast(p))
        {
            throw std::runtime_error("kh02_portal_missing");
        }
        auto* fn = p->GetFunctionByNameInChain(
            L"BndEvt__TresReactor_K2Node_ComponentBoundEvent_0_TresReactorDoCommandSignature__DelegateSignature");
        if (!is_uobject_valid_fast(fn) || fn->GetParmsSize() > maximum_interaction_parameter_bytes)
        {
            throw std::runtime_error("kh02_portal_interaction_layout");
        }
        std::vector<std::byte> params(fn->GetParmsSize());
        if (!write_object_function_param(fn, params.data(), L"ReactorComponent",
                                         try_read_reflected_object_property_value(p, L"TresReactor")) ||
            !write_object_function_param(fn, params.data(), L"CommandCauser", pawn))
        {
            throw std::runtime_error("kh02_portal_interaction_parameters");
        }
        p->ProcessEvent(fn, params.data());
    }
} // namespace kh3ap
