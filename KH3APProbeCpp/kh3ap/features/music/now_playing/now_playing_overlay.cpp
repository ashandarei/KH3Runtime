#include <memory>
#include "kh3ap/features/music/now_playing/now_playing_overlay.h"
#include "kh3ap/features/music/now_playing/now_playing_movie_data.h"
#include "kh3ap/features/music/now_playing/now_playing_gfx_lifecycle.h"
#include "kh3ap/core/ui/embedded_movie_resource.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/core/gfx/gfx_reflection_helpers.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/FWeakObjectPtr.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#include <Unreal/TArray.hpp>
#include <Unreal/FString.hpp>
#include <chrono>
#include <cstring>
#include <utility>
#include <vector>
#include <DynamicOutput/DynamicOutput.hpp>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
#include "kh3ap/bridge/utilities.h"
#include <sstream>
#endif

namespace kh3ap
{
    using namespace RC::Unreal;

    namespace
    {
        constexpr auto now_playing_movie_priority = 124;
        constexpr auto now_playing_view_alignment = 5;
        constexpr auto relative_call_instruction_bytes = 5;
        constexpr auto now_playing_update_interval = std::chrono::milliseconds(100);
        constexpr auto now_playing_asset_retry_interval = std::chrono::seconds(2);
        auto call_bool(UObject* owner, const wchar_t* name, const wchar_t* argument, bool value)
            -> void
        {
            if (!is_uobject_valid_fast(owner))
            {
                return;
            }
            auto* fn = owner->GetFunctionByNameInChain(name);
            if (!is_uobject_valid_fast(fn))
            {
                return;
            }
            std::vector<std::byte> params(fn->GetParmsSize());
            if (write_bool_function_param(find_function_param_by_name(fn, argument), params.data(),
                                          value))
            {
                owner->ProcessEvent(fn, params.data());
            }
        }
        auto readable_range(const void* address, std::size_t size) -> bool
        {
            MEMORY_BASIC_INFORMATION info{};
            if (!address || VirtualQuery(address, &info, sizeof(info)) != sizeof(info) ||
                info.State != MEM_COMMIT || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
            {
                return false;
            }
            const auto start = reinterpret_cast<std::uintptr_t>(address);
            const auto begin = reinterpret_cast<std::uintptr_t>(info.BaseAddress);
            return start >= begin && start - begin <= info.RegionSize &&
                   size <= info.RegionSize - (start - begin);
        }
    } // namespace
    struct NowPlayingOverlay::Impl
    {
        NowPlayingOverlayConfig config;
        FWeakObjectPtr asset, player, root;
        std::wstring displayed;
        bool shown{};
        using Clock = std::chrono::steady_clock;
        Clock::time_point next{}, retry{};
        FWeakObjectPtr checked_member_function;
        bool native_layout_supported{}, unsupported_logged{};

        auto native_root_ready(UObject* object) -> bool
        {
            if (!is_uobject_valid_fast(object))
            {
                return false;
            }
            auto* function = object->GetFunctionByNameInChain(L"GetMemberValue");
            if (!is_uobject_valid_fast(function) || !function->GetFuncPtr())
            {
                return false;
            }
            if (checked_member_function.Get() != function)
            {
                checked_member_function = make_uobject_weak(function);
                native_layout_supported = false;
                const auto* thunk = reinterpret_cast<const std::uint8_t*>(function->GetFuncPtr());
                constexpr auto thunk_bytes =
                    now_playing_gfx::member_call_offset + relative_call_instruction_bytes;
                if (readable_range(thunk, thunk_bytes) &&
                    now_playing_gfx::matches({thunk, thunk_bytes}, 0,
                                             now_playing_gfx::thunk_prefix) &&
                    now_playing_gfx::matches({thunk, thunk_bytes},
                                             now_playing_gfx::member_call_offset -
                                                 (now_playing_gfx::member_call.size() - 1),
                                             now_playing_gfx::member_call))
                {
                    std::int32_t displacement{};
                    std::memcpy(&displacement, thunk + now_playing_gfx::member_call_offset + 1,
                                sizeof(displacement));
                    const auto target = reinterpret_cast<std::uintptr_t>(thunk) +
                                        now_playing_gfx::member_call_offset +
                                        relative_call_instruction_bytes + displacement;
                    const auto* body = reinterpret_cast<const std::uint8_t*>(target);
                    constexpr auto body_bytes = now_playing_gfx::member_fields_offset +
                                                now_playing_gfx::member_fields.size();
                    native_layout_supported =
                        readable_range(body, body_bytes) &&
                        now_playing_gfx::matches({body, body_bytes},
                                                 now_playing_gfx::member_fields_offset,
                                                 now_playing_gfx::member_fields);
                }
            }
            if (!native_layout_supported)
            {
                if (!unsupported_logged)
                {
                    unsupported_logged = true;
                    RC::Output::send<RC::LogLevel::Warning>(
                        L"[KH3APProbeCpp] Now playing overlay disabled: unsupported native GFx "
                        L"value layout\n");
                }
                return false;
            }
            auto* klass = object->GetClassPrivate();
            constexpr auto value_end = now_playing_gfx::value_offset + sizeof(std::uintptr_t);
            if (!is_uobject_valid_fast(klass) || klass->GetPropertiesSize() < value_end ||
                !readable_range(object, value_end))
            {
                return false;
            }
            std::uintptr_t object_interface{}, value{};
            std::uint32_t type{};
            const auto* bytes = reinterpret_cast<const std::byte*>(object);
            std::memcpy(&object_interface, bytes + now_playing_gfx::interface_offset,
                        sizeof(object_interface));
            std::memcpy(&type, bytes + now_playing_gfx::type_offset, sizeof(type));
            std::memcpy(&value, bytes + now_playing_gfx::value_offset, sizeof(value));
            return now_playing_gfx::native_value_ready(object_interface, type, value);
        }
        auto live_root() -> UObject*
        {
            auto* owner = player.Get();
            auto* current = is_uobject_valid_fast(owner)
                                ? try_read_reflected_object_property_value(owner, L"Root")
                                : nullptr;
            if (!native_root_ready(current))
            {
                root = {};
                displayed.clear();
                shown = false;
                return nullptr;
            }
            root = make_uobject_weak(current);
            return current;
        }

        ~Impl()
        {
            call_bool(player.Get(), L"Close", L"Unload", true);
            if (auto* object = asset.Get(); is_uobject_valid_fast(object))
            {
                object->ClearRootSet();
            }
        }
        auto open() -> bool
        {
            RC::Output::send<RC::LogLevel::Normal>(L"[KH3APProbeCpp] Now playing movie opening\n");
            auto* self = UObjectGlobals::StaticFindObject<UObject*>(
                nullptr, nullptr, L"/Script/ScaleformUI.Default__GFxMoviePlayer");
            if (!is_uobject_valid_fast(self))
            {
                return false;
            }
            auto* fn = self->GetFunctionByNameInChain(L"OpenMovie");
            if (!is_uobject_valid_fast(fn))
            {
                return false;
            }
            auto* movie_asset = asset.Get();
            if (!is_uobject_valid_fast(movie_asset))
            {
                auto* font =
                    find_loaded_runtime_asset(L"/Game/UI/Scaleform/91_font/TresFont.TresFont");
                if (!is_uobject_valid_fast(font))
                {
                    return false;
                }
                auto* klass = find_loaded_runtime_asset_class(L"/Script/ScaleformUI.SwfMovie");
                if (!is_uobject_valid_fast(klass))
                {
                    return false;
                }
                const auto bytes = load_embedded_movie_resource(L"KH3AP_NOW_PLAYING_MOVIE",
                                                                now_playing_movie_size);
                if (bytes.empty())
                {
                    return false;
                }
                FStaticConstructObjectParameters construct(klass, font->GetOuterPrivate());
                construct.SetFlags = static_cast<EObjectFlags>(RF_Transient | RF_Public);
                movie_asset = UObjectGlobals::StaticConstructObject(construct);
                if (!is_uobject_valid_fast(movie_asset))
                {
                    return false;
                }
                auto* raw = movie_asset->GetPropertyByNameInChain(L"RawData");
                if (!raw || raw->GetClass().GetName() != L"ArrayProperty" ||
                    static_cast<FArrayProperty*>(raw)->GetInner()->GetElementSize() != 1)
                {
                    return false;
                }
                raw->ContainerPtrToValuePtr<TArray<uint8_t>>(movie_asset)
                    ->Append(bytes.data(), static_cast<int32_t>(bytes.size()));
                write_bool_function_param(movie_asset->GetPropertyByNameInChain(L"bUseGFxExport"),
                                          movie_asset, true);
                auto* names = movie_asset->GetPropertyByNameInChain(L"ReferencedAssetNames");
                auto* references = movie_asset->GetPropertyByNameInChain(L"UserReferences");
                if (!names || names->GetClass().GetName() != L"ArrayProperty" ||
                    static_cast<FArrayProperty*>(names)->GetInner()->GetClass().GetName() !=
                        L"StrProperty" ||
                    !references || references->GetClass().GetName() != L"ArrayProperty" ||
                    static_cast<FArrayProperty*>(references)->GetInner()->GetClass().GetName() !=
                        L"ObjectProperty")
                {
                    return false;
                }
                names->ContainerPtrToValuePtr<TArray<FString>>(movie_asset)
                    ->Add(FString(L"/Game/UI/Scaleform/91_font/TresFont.TresFont"));
                references->ContainerPtrToValuePtr<TArray<UObject*>>(movie_asset)->Add(font);
                movie_asset->SetRootSet();
                asset = make_uobject_weak(movie_asset);
            }
            FStaticConstructObjectParameters construct(self->GetClassPrivate(),
                                                       movie_asset->GetOuterPrivate());
            construct.SetFlags = RF_Transient;
            auto* configured = UObjectGlobals::StaticConstructObject(construct);
            if (!is_uobject_valid_fast(configured) ||
                !write_integral_function_param(configured->GetPropertyByNameInChain(L"Priority"),
                                               configured, now_playing_movie_priority))
            {
                return false;
            }
            // This independent movie owns no world objects and survives travel.
            // Its visibility follows the current runtime UI state instead.
            write_bool_function_param(configured->GetPropertyByNameInChain(L"bCloseOnLevelChange"),
                                      configured, false);
            std::vector<std::byte> params(fn->GetParmsSize());
            if (!write_object_function_param(fn, params.data(), L"SwfMovie", movie_asset) ||
                !write_object_function_param(fn, params.data(), L"MoviePlayer", configured))
            {
                return false;
            }
            write_bool_function_param(find_function_param_by_name(fn, L"DisplayWithHudOff"),
                                      params.data(), true);
            self->ProcessEvent(fn, params.data());
            auto* opened = try_read_function_object_param(fn, params.data(), L"ReturnValue");
            if (!is_uobject_valid_fast(opened))
            {
                return false;
            }
            player = make_uobject_weak(opened);
            for (const auto* field :
                 {L"bAllowInput", L"bAllowFocus", L"bCaptureKeyboardInput", L"bCaptureMouseInput",
                  L"bCaptureGamepadInput", L"bCaptureTouchInput", L"bShowDefaultMouseCursor",
                  L"bCloseOnLevelChange"})
            {
                write_bool_function_param(opened->GetPropertyByNameInChain(field), opened, false);
            }
            call_bool(opened, L"SetMovieCanReceiveInput", L"bCanReceiveInput", false);
            call_bool(opened, L"SetMovieCanReceiveFocus", L"bCanReceiveFocus", false);
            // Use the real viewport bounds, including ultrawide, just as the
            // independent map movie does. AS3 anchors to stage.stageWidth.
            if (auto* scale = opened->GetFunctionByNameInChain(L"SetViewScaleMode");
                is_uobject_valid_fast(scale))
            {
                std::vector<std::byte> mode(scale->GetParmsSize()); // NoScale = 0
                opened->ProcessEvent(scale, mode.data());
            }
            if (auto* align = opened->GetFunctionByNameInChain(L"SetAlignment");
                is_uobject_valid_fast(align))
            {
                std::vector<std::byte> mode(align->GetParmsSize());
                if (write_integral_function_param(find_function_param_by_name(align, L"Align"),
                                                  mode.data(), now_playing_view_alignment))
                {
                    opened->ProcessEvent(align, mode.data());
                }
            }
            auto* movie_root = try_read_reflected_object_property_value(opened, L"Root");
            root = make_uobject_weak(movie_root);
            displayed.clear();
            shown = false;
            RC::Output::send<RC::LogLevel::Normal>(L"[KH3APProbeCpp] Now playing movie bound={}\n",
                                                   is_uobject_valid_fast(movie_root));
            return is_uobject_valid_fast(movie_root);
        }
        auto tick() -> void
        {
            const auto now = Clock::now();
            if (now < next)
            {
                return;
            }
            next = now + now_playing_update_interval;
            const auto& title = config.title();
            const bool visible = config.visible() && !title.empty();
            auto* movie_root = live_root();
            if (!is_uobject_valid_fast(movie_root))
            {
                if (!visible || now < retry)
                {
                    return;
                }
                if (unsupported_logged)
                {
                    return;
                }
                retry = now + now_playing_asset_retry_interval;
                call_bool(player.Get(), L"Close", L"Unload", true);
                player = {};
                root = {};
                if (!open())
                {
                    return;
                }
                movie_root = root.Get();
                return; // Allow the movie's document constructor to initialize.
            }
            if (try_gfx_get_member_value_number(config.gfx, movie_root, L"ready") != 1.0)
            {
                return;
            }
            if (!title.empty() && title != displayed &&
                try_gfx_set_member_value_string(config.gfx, movie_root, L"songTitle", title))
            {
                displayed = title;
            }
            if (visible != shown &&
                try_gfx_set_member_value_bool(config.gfx, movie_root, L"active", visible))
            {
                shown = visible;
            }
        }
    };
    NowPlayingOverlay::NowPlayingOverlay(NowPlayingOverlayConfig config)
        : impl_(std::make_unique<Impl>())
    {
        impl_->config = std::move(config);
    }
    NowPlayingOverlay::~NowPlayingOverlay() = default;
    auto NowPlayingOverlay::tick() -> void
    {
        impl_->tick();
    }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    auto NowPlayingOverlay::test_snapshot() -> std::string
    {
        auto* root = impl_->live_root();
        std::ostringstream out;
        out << std::boolalpha << "{\"bound\":" << is_uobject_valid_fast(root)
            << ",\"visible\":" << impl_->shown << ",\"title\":\"" << json_escape(impl_->displayed)
            << '"';
        if (is_uobject_valid_fast(root))
        {
            for (const auto* name :
                 {L"scrollOffset", L"scrollOverflow", L"titleWindowWidth", L"panelVisible",
                  L"ready", L"displayScale", L"windowScale", L"backgroundOpacity", L"panelX",
                  L"panelY", L"viewportWidth", L"viewportHeight"})
            {
                out << ",\"" << json_escape(name) << "\":"
                    << try_gfx_get_member_value_number(impl_->config.gfx, root, name).value_or(-1);
            }
            out << ",\"renderedTitle\":\""
                << json_escape(
                       try_gfx_get_member_value_string(impl_->config.gfx, root, L"renderedTitle")
                           .value_or(L""))
                << '"';
        }
        out << '}';
        return out.str();
    }
#endif
} // namespace kh3ap
