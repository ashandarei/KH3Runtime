#include "kh02_movie_surface.h"
#include "kh02_movie_texture.h"
#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#include "kh3ap/core/gfx/gfx_reflection_helpers.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/ui/embedded_movie_resource.h"
#include "kh3ap/bridge/utilities.h"
#include "kh02/movie_surface_metadata.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#include <Unreal/TArray.hpp>
#include <Unreal/FString.hpp>
#include <cstring>
#include <stdexcept>

namespace kh3ap
{
    using namespace RC::Unreal;

    namespace
    {
        constexpr GfxProbeFunctionPaths gfx_paths{L"/Script/ScaleformUI.GFxObject:GetMemberValue",
                                                  L"/Script/ScaleformUI.GFxObject:Invoke",
                                                  L"/Script/ScaleformUI.GFxObject:SetMemberValue"};

        struct Call : Kh02ReflectedCall
        {
            Call(UObject* owner, const wchar_t* method) : Kh02ReflectedCall(owner, method, "movie_surface", 128) {}
        };

        void visible(UObject* player, bool value)
        {
            auto* root = try_read_reflected_object_property_value(player, L"Root");
            if (!try_gfx_set_member_value_bool(gfx_paths, root, L"visible", value))
            {
                throw std::runtime_error("movie_surface_visibility");
            }
        }
    } // namespace

    Kh02MovieSurface::~Kh02MovieSurface()
    {
        stop();
    }

    void Kh02MovieSurface::prepare(UObject* context)
    {
        stop();
        context_ = make_uobject_weak(context);
        auto* asset_class = static_cast<UClass*>(kh02_require_loaded_object(L"/Script/ScaleformUI.SwfMovie"));
        auto* placeholder = kh02_require_loaded_object(L"/Engine/EngineResources/DefaultTexture.DefaultTexture");
        FStaticConstructObjectParameters asset_parameters(asset_class, placeholder->GetOuterPrivate());
        asset_parameters.SetFlags = static_cast<EObjectFlags>(RF_Transient | RF_Public);
        auto* asset = UObjectGlobals::StaticConstructObject(asset_parameters);
        if (!is_uobject_valid_fast(asset))
        {
            throw std::runtime_error("movie_surface_asset");
        }
        asset->SetRootSet();
        asset_ = make_uobject_weak(asset);
        auto* raw = asset->GetPropertyByNameInChain(L"RawData");
        const auto bytes = load_embedded_movie_resource(L"KH3AP_KH02_MOVIE_SURFACE", kh02movie_metadata::surface_size);
        if (!raw || !raw->IsA<FArrayProperty>() ||
            static_cast<FArrayProperty*>(raw)->GetInner()->GetElementSize() != 1 || bytes.empty())
        {
            throw std::runtime_error("movie_surface_resource");
        }
        Call create_target(kh02_require_loaded_object(L"/Script/Engine.Default__KismetRenderingLibrary"),
                           L"CreateRenderTarget2D");
        create_target.object(L"WorldContextObject", context);
        create_target.integer(L"Width", 1920);
        create_target.integer(L"Height", 1080);
        create_target.run();
        auto* target = create_target.result();
        if (!is_uobject_valid_fast(target))
        {
            throw std::runtime_error("movie_surface_target");
        }
        target->SetRootSet();
        target_ = make_uobject_weak(target);
        raw->ContainerPtrToValuePtr<TArray<uint8_t>>(asset)->Append(bytes.data(), static_cast<int32_t>(bytes.size()));
        if (!write_bool_function_param(asset->GetPropertyByNameInChain(L"bUseGFxExport"), asset, true))
        {
            throw std::runtime_error("movie_surface_asset_format");
        }
        auto* names = asset->GetPropertyByNameInChain(L"ReferencedAssetNames");
        auto* references = asset->GetPropertyByNameInChain(L"References");
        if (!names || !names->IsA<FArrayProperty>() || !references || !references->IsA<FArrayProperty>())
        {
            throw std::runtime_error("movie_surface_references");
        }
        names->ContainerPtrToValuePtr<TArray<FString>>(asset)->Add(FString(placeholder->GetPathName().c_str()));
        references->ContainerPtrToValuePtr<TArray<UObject*>>(asset)->Add(placeholder);
        auto* texture = create_kh02_movie_texture(asset);
        texture_ = make_uobject_weak(texture);
        auto* library = kh02_require_loaded_object(L"/Script/ScaleformUI.Default__GFxMoviePlayer");
        FStaticConstructObjectParameters player_parameters(library->GetClassPrivate(), asset);
        player_parameters.SetFlags = RF_Transient;
        auto* configured = UObjectGlobals::StaticConstructObject(player_parameters);
        if (!is_uobject_valid_fast(configured) ||
            !write_integral_function_param(configured->GetPropertyByNameInChain(L"Priority"), configured, priority))
        {
            throw std::runtime_error("movie_surface_player");
        }
        // Keep native subtitles (190) and pause (200) above the movie frame.
        // Configure ordering before registration, with no UMG layer.
        Call open(library, L"OpenMovie");
        open.object(L"SwfMovie", asset);
        open.object(L"MoviePlayer", configured);
        open.boolean(L"DisplayWithHudOff", true);
        open.run();
        auto* player = open.result();
        if (!is_uobject_valid_fast(player))
        {
            throw std::runtime_error("movie_surface_open");
        }
        player->SetRootSet();
        player_ = make_uobject_weak(player);
        visible(player, false);
        for (const auto* name : {L"bAllowInput", L"bAllowFocus", L"bCaptureKeyboardInput", L"bCaptureMouseInput",
                                 L"bCaptureGamepadInput", L"bCaptureTouchInput", L"bShowDefaultMouseCursor"})
        {
            if (!write_bool_function_param(player->GetPropertyByNameInChain(name), player, false))
            {
                throw std::runtime_error("movie_surface_input_flags");
            }
        }
        Call scale(player, L"SetViewScaleMode");
        scale.integer(L"ScaleMode", 2); // ExactFit, matching the full-viewport presentation.
        scale.run();
    }

    void Kh02MovieSurface::show()
    {
        auto* player = player_.Get();
        auto* target = target_.Get();
        Call bind(player, L"SetExternalTexture");
        bind.string(L"Resource", L"KH02Frame");
        bind.object(L"Texture", target);
        bind.run();
        if (!bind.boolean_result())
        {
            throw std::runtime_error("movie_surface_external_texture");
        }
        visible(player, true);
    }

    void Kh02MovieSurface::upload(std::span<const uint8_t> pixels)
    {
        upload_kh02_movie_texture(texture_.Get(), pixels);
        auto* rendering = kh02_require_loaded_object(L"/Script/Engine.Default__KismetRenderingLibrary");
        Call begin(rendering, L"BeginDrawCanvasToRenderTarget");
        begin.object(L"WorldContextObject", context_.Get());
        begin.object(L"TextureRenderTarget", target_.Get());
        begin.run();
        auto* canvas = try_read_function_object_param(begin.function, begin.parameters.data(), L"Canvas");
        Call draw(canvas, L"K2_DrawTexture");
        draw.object(L"RenderTexture", texture_.Get());
        draw.numbers(L"ScreenPosition", {0, 0});
        draw.numbers(L"ScreenSize", {1920, 1080});
        draw.numbers(L"CoordinatePosition", {0, 0});
        draw.numbers(L"CoordinateSize", {1, 1});
        draw.numbers(L"RenderColor", {1, 1, 1, 1});
        draw.integer(L"BlendMode", 0);
        draw.run();
        Call end(rendering, L"EndDrawCanvasToRenderTarget");
        end.object(L"WorldContextObject", context_.Get());
        auto* from = begin.param(L"Context");
        auto* to = end.param(L"Context");
        if (!from || !to || from->GetElementSize() != to->GetElementSize())
        {
            throw std::runtime_error("movie_surface_canvas_context");
        }
        std::memcpy(to->ContainerPtrToValuePtr<void>(end.parameters.data()),
                    from->ContainerPtrToValuePtr<void>(begin.parameters.data()), from->GetElementSize());
        end.run();
    }

    void Kh02MovieSurface::stop() noexcept
    {
        if (auto* player = player_.Get(); is_uobject_valid_fast(player))
        {
            try
            {
                Call close(player, L"Close");
                close.boolean(L"Unload", true);
                close.run();
            }
            catch (...)
            {
            }
            player->ClearRootSet();
        }
        for (auto weak : {texture_, target_, asset_})
        {
            if (auto* object = weak.Get(); is_uobject_valid_fast(object))
            {
                object->ClearRootSet();
            }
        }
        player_ = {};
        texture_ = {};
        asset_ = {};
        target_ = {};
        context_ = {};
    }

    auto Kh02MovieSurface::texture() const -> UObject*
    {
        return texture_.Get();
    }
} // namespace kh3ap
