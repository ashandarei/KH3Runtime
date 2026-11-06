#include "kh02_wardrobe_accessories.h"
#include "kh02_wardrobe_reflection.h"
#include "kh3ap/core/objects/uobject_weak.h"

namespace
{
    constexpr int always_spawn_collision_handling = 1;
    constexpr int snap_to_target_rule = 2;
} // namespace

namespace kh3ap
{
    using namespace RC::Unreal;
    using namespace kh02wardrobe_detail;

    void Kh02WardrobeAccessories::attach(UObject* pawn, UObject* component, UObject* prototype,
                                         const std::wstring& socket)
    {
        auto* skeletal_mesh = object_field(prototype, L"SkeletalMesh");
        if (!is_uobject_valid_fast(skeletal_mesh))
        {
            throw std::runtime_error("Wardrobe piece has no skeletal mesh");
        }
        Call begin(gameplay, L"BeginDeferredActorSpawnFromClass");
        begin.object(L"WorldContextObject", pawn);
        begin.object(L"ActorClass", kh02_require_loaded_object(L"/Script/Engine.SkeletalMeshActor"));
        begin.integer(L"CollisionHandlingOverride", always_spawn_collision_handling);
        begin.spawn_transform();
        begin.run();
        auto* actor = begin.result();
        if (!is_uobject_valid_fast(actor))
        {
            throw std::runtime_error("Could not create a wardrobe piece");
        }
        accessories.push_back(make_uobject_weak(actor));
        Call finish(gameplay, L"FinishSpawningActor");
        finish.object(L"Actor", actor);
        finish.spawn_transform();
        finish.run();
        auto* accessory_mesh = object_field(actor, L"SkeletalMeshComponent");
        Call set_mesh(accessory_mesh, L"SetSkeletalMesh");
        set_mesh.object(L"NewMesh", skeletal_mesh);
        set_mesh.boolean(L"bReinitPose", true);
        set_mesh.run();
        const auto [overrides, inner] = array_field(prototype, L"OverrideMaterials");
        if (!inner || inner->GetElementSize() != sizeof(UObject*))
        {
            throw std::runtime_error("Invalid wardrobe material overrides");
        }
        for (int index = 0; index < overrides->count; ++index)
        {
            auto* material = static_cast<UObject**>(overrides->data)[index];
            if (is_uobject_valid_fast(material))
            {
                Call set_material(accessory_mesh, L"SetMaterial");
                set_material.integer(L"ElementIndex", index);
                set_material.object(L"Material", material);
                set_material.run();
            }
        }
        Call owner(actor, L"SetOwner");
        owner.object(L"NewOwner", pawn);
        owner.run();
        Call collision(actor, L"SetActorEnableCollision");
        collision.boolean(L"bNewActorEnableCollision", false);
        collision.run();
        Call attach(actor, L"K2_AttachToComponent");
        attach.object(L"Parent", component);
        attach.name(L"SocketName", socket.c_str());
        attach.integer(L"LocationRule", snap_to_target_rule);
        attach.integer(L"RotationRule", snap_to_target_rule);
        attach.integer(L"ScaleRule", snap_to_target_rule);
        attach.boolean(L"bWeldSimulatedBodies", false);
        attach.run();
        Call parent(actor, L"GetAttachParentActor");
        parent.run();
        if (parent.result() != pawn)
        {
            throw std::runtime_error("Could not attach a wardrobe piece to Aqua");
        }
    }

    void Kh02WardrobeAccessories::clear()
    {
        for (auto& weak : accessories)
        {
            if (auto* actor = weak.Get(); is_uobject_valid_fast(actor))
            {
                Call destroy(actor, L"K2_DestroyActor");
                destroy.run();
            }
        }
        accessories.clear();
    }

} // namespace kh3ap
