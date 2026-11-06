#include "kh02_preview_render_lease.h"
#include "kh02_preview_reflection.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include <Unreal/UClass.hpp>
#include <Unreal/UObjectGlobals.hpp>

namespace kh3ap
{
    using namespace kh02preview_detail;

    void Kh02PreviewRenderLease::close(bool destroy_actor)
    {
        if (auto* manager = scene_manager.Get(); destroy_actor && is_uobject_valid_fast(manager))
        {
            Call camp(manager, L"EnableCampRenderMode");
            camp.boolean(L"IsEnable", true);
            camp.run();
        }
        scene_manager = {};
        if (auto* widget = image.Get(); is_uobject_valid_fast(widget))
        {
            Call remove(widget, L"RemoveFromParent");
            remove.run();
            widget->ClearRootSet();
        }
        image = {};
        if (auto* capture = actor.Get(); destroy_actor && is_uobject_valid_fast(capture))
        {
            Call destroy(capture, L"K2_DestroyActor");
            destroy.run();
        }
        actor = {};
        for (const auto& weak : lights)
        {
            if (auto* light = weak.Get(); destroy_actor && is_uobject_valid_fast(light))
            {
                Call destroy(light, L"K2_DestroyActor");
                destroy.run();
            }
        }
        lights.clear();
        if (auto* texture = target.Get(); is_uobject_valid_fast(texture))
        {
            texture->ClearRootSet();
        }
        target = {};
        hero = {};
    }

    void Kh02PreviewRenderLease::prepare(UObject* pawn, UObject* preview_material)
    {
        constexpr auto gameplay = L"/Script/Engine.Default__GameplayStatics";
        Call state(gameplay, L"GetGameState");
        state.object(L"WorldContextObject", pawn);
        state.run();
        Call manager(state.result(), L"GetSceneManager");
        manager.run();
        scene_manager = make_uobject_weak(manager.result());
        // Camp mode excludes the live pawn and its wardrobe attachments.
        // Keep the native menu pause, but render those objects for this pane.
        // Closing the pane restores the camp renderer before returning to it.
        Call camp(manager.result(), L"EnableCampRenderMode");
        camp.boolean(L"IsEnable", false);
        camp.run();
        Call create(L"/Script/Engine.Default__KismetRenderingLibrary", L"CreateRenderTarget2D");
        create.object(L"WorldContextObject", pawn);
        create.integer(L"Width", preview_texture_size_pixels);
        create.integer(L"Height", preview_texture_size_pixels);
        create.run();
        auto* texture = create.result();
        if (!is_uobject_valid_fast(texture))
        {
            throw std::runtime_error("Could not create Aqua preview texture");
        }
        texture->SetRootSet();
        target = make_uobject_weak(texture);

        Call begin(gameplay, L"BeginDeferredActorSpawnFromClass");
        begin.object(L"WorldContextObject", pawn);
        // KH3's menu actor supplies the capture's native visibility/update
        // context. A generic SceneCapture2D remains blank in the paused camp.
        begin.object(L"ActorClass", kh02_require_loaded_object(L"/Script/TresGame.TresUIActor"));
        begin.integer(L"CollisionHandlingOverride", always_spawn_collision_handling);
        begin.spawn_transform();
        begin.run();
        auto* capture = begin.result();
        if (!is_uobject_valid_fast(capture))
        {
            throw std::runtime_error("Could not create Aqua preview camera");
        }
        actor = make_uobject_weak(capture);
        Call finish(gameplay, L"FinishSpawningActor");
        finish.object(L"Actor", capture);
        finish.spawn_transform();
        finish.run();
        auto* component = object_field(capture, L"SceneCaptureComponent2D");
        if (!is_uobject_valid_fast(component))
        {
            throw std::runtime_error("Missing Aqua preview capture component");
        }
        auto* texture_property = component->GetPropertyByNameInChain(L"TextureTarget");
        if (!texture_property || texture_property->GetElementSize() != sizeof(UObject*))
        {
            throw std::runtime_error("Invalid Aqua preview texture field");
        }
        *texture_property->ContainerPtrToValuePtr<UObject*>(component) = texture;
        integer_field(component, L"PrimitiveRenderMode", show_only_primitive_render_mode);
        integer_field(component, L"CaptureSource", scene_color_depth_capture_source);
        integer_field(component, L"ProjectionType", orthographic_projection);
        auto* width = component->GetPropertyByNameInChain(L"OrthoWidth");
        if (!width || width->GetElementSize() != sizeof(float))
        {
            throw std::runtime_error("Invalid Aqua preview framing field");
        }
        *width->ContainerPtrToValuePtr<float>(component) = orthographic_width_cm;
        write_bool_function_param(component->GetPropertyByNameInChain(L"bCaptureEveryFrame"), component, false);
        write_bool_function_param(component->GetPropertyByNameInChain(L"bCaptureOnMovement"), component, false);
        write_bool_function_param(component->GetPropertyByNameInChain(L"bSQEXCampRenderMode"), component, false);
        auto* source_mesh = object_field(pawn, L"MyMesh");
        auto* preview_mesh = object_field(capture, L"MeshComponent");
        Call mesh(preview_mesh, L"SetSkeletalMesh");
        mesh.object(L"NewMesh", object_field(source_mesh, L"SkeletalMesh"));
        mesh.boolean(L"bReinitPose", true);
        mesh.run();
        Call pose(preview_mesh, L"SetMasterPoseComponent");
        pose.object(L"NewMasterBoneComponent", source_mesh);
        pose.run();
        Call link(capture, L"SetLinkMeshComponent");
        link.object(L"LinkMeshComponent", source_mesh);
        link.run();
        for (const auto& lighting : preview_lights)
        {
            Call spawn_light(gameplay, L"BeginDeferredActorSpawnFromClass");
            spawn_light.object(L"WorldContextObject", pawn);
            spawn_light.object(L"ActorClass", kh02_require_loaded_object(L"/Script/Engine.DirectionalLight"));
            spawn_light.integer(L"CollisionHandlingOverride", always_spawn_collision_handling);
            spawn_light.spawn_transform();
            spawn_light.run();
            auto* light = spawn_light.result();
            if (!is_uobject_valid_fast(light))
            {
                throw std::runtime_error("Could not create Aqua preview light");
            }
            lights.push_back(make_uobject_weak(light));
            auto* light_component = object_field(light, L"LightComponent");
            integer_field(light_component, L"Mobility", movable_component_mobility);
            Call finish_light(gameplay, L"FinishSpawningActor");
            finish_light.object(L"Actor", light);
            finish_light.spawn_transform();
            finish_light.run();
            Call intensity(light_component, L"SetIntensity");
            intensity.number(L"NewIntensity", lighting.intensity);
            intensity.run();
            Call shadows(light_component, L"SetCastShadows");
            shadows.boolean(L"bNewValue", false);
            shadows.run();
        }

        std::vector<UObject*> candidates;
        UObjectGlobals::FindAllOf(L"W_ProofUI_C", candidates);
        UObject* canvas = nullptr;
        UObject* tree = nullptr;
        for (auto* candidate : candidates)
        {
            if (!is_uobject_valid_fast(candidate) || candidate->HasAnyFlags(RF_ClassDefaultObject))
            {
                continue;
            }
            Call visible(candidate, L"IsInViewport");
            visible.run();
            if (visible.boolean_result())
            {
                tree = object_field(candidate, L"WidgetTree");
                canvas = object_field(tree, L"RootWidget");
                break;
            }
        }
        if (!is_uobject_valid_fast(canvas) || canvas->GetClassPrivate()->GetName() != L"CanvasPanel")
        {
            throw std::runtime_error("Missing Aqua preview widget host");
        }
        FStaticConstructObjectParameters construct(
            static_cast<UClass*>(kh02_require_loaded_object(L"/Script/UMG.Image")), tree);
        construct.SetFlags = RF_Transient;
        auto* widget = UObjectGlobals::StaticConstructObject(construct);
        if (!is_uobject_valid_fast(widget))
        {
            throw std::runtime_error("Could not create Aqua preview image");
        }
        widget->SetRootSet();
        image = make_uobject_weak(widget);

        Call brush(widget, L"SetBrushFromMaterial");
        brush.object(L"Material", preview_material);
        brush.run();
        Call dynamic(widget, L"GetDynamicMaterial");
        dynamic.run();
        Call bind_texture(dynamic.result(), L"SetTextureParameterValue");
        bind_texture.name(L"ParameterName", L"Preview");
        bind_texture.object(L"Value", texture);
        bind_texture.run();

        Call visibility(widget, L"SetVisibility");
        visibility.integer(L"InVisibility", hit_test_invisible_visibility);
        visibility.run();
        Call add(canvas, L"AddChildToCanvas");
        add.object(L"Content", widget);
        add.run();
        auto* slot = add.result();
        Call anchors(slot, L"SetAnchors");
        anchors.numbers(L"InAnchors", {.30f, .14f, .71f, .87f});
        anchors.run();
        Call offsets(slot, L"SetOffsets");
        offsets.numbers(L"InOffset", {0, 0, 0, 0});
        offsets.run();
        Call order(slot, L"SetZOrder");
        order.integer(L"InZOrder", preview_z_order);
        order.run();
        hero = make_uobject_weak(pawn);
    }

    auto Kh02PreviewRenderLease::ready_for(UObject* pawn) const -> bool
    {
        return hero.Get() == pawn && is_uobject_valid_fast(actor.Get()) && is_uobject_valid_fast(image.Get());
    }
} // namespace kh3ap
