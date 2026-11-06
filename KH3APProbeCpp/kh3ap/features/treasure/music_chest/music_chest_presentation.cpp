#include <array>
#include <chrono>
#include <utility>
#include "kh3ap/features/treasure/chest_commands.h"
#include "kh3ap/features/treasure/music_chest/music_chest_internal.h"
#include "kh3ap/features/treasure/music_chest/music_chest_pause.h"
#include "kh3ap/features/treasure/chest_persistence_service.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/features/grants/grant_internal_call_depth.h"
#include <Unreal/AActor.hpp>
#include <Unreal/GameplayStatics.hpp>
#include <Unreal/Property/FStructProperty.hpp>
#include <Unreal/UClass.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace kh3ap
{
    using namespace RC::Unreal;
    using namespace music_chest;

    namespace
    {
        constexpr int authored_mesh_forward_offset_cm = 350;
        constexpr float chest_opening_duration_seconds = 1.25f;
        constexpr float animation_position_tolerance_seconds = 0.02f;
    } // namespace

    void RuntimeMusicChestService::Impl::retire(Gate& gate)
    {
        auto* r = gate.reactor.Get();
        auto* n = gate.notice.Get();
        if (is_uobject_valid_fast(r))
        {
            auto& list = chest_delegate(r)->InvocationList;
            for (int i = list.Num() - 1; i >= 0; --i)
            {
                if (list[i].Object.Get() == n &&
                    list[i].FunctionName.ToString() == L"OnChestChecked")
                {
                    list.RemoveAt(i);
                }
            }
            reactor_command(r, &gate.original_command);
        }
        auto* chest = gate.chest.Get();
        if (gate.spawned && is_uobject_valid_fast(chest))
        {
            Call destroy(chest, L"K2_DestroyActor");
            destroy.run();
        }
        auto* original = !gate.spawned && is_uobject_valid_fast(n)
                             ? try_read_reflected_object_property_value(n, L"OriginalMaterial")
                             : nullptr;
        if (is_uobject_valid_fast(chest) && is_uobject_valid_fast(original))
        {
            auto* mesh = try_read_reflected_object_property_value(chest, L"MyMesh");
            if (is_uobject_valid_fast(mesh))
            {
                Call c(mesh, L"SetMaterial");
                write_object_function_param(c.fn, c.data.data(), L"Material", original);
                c.run();
            }
        }
        if (is_uobject_valid_fast(n))
        {
            n->ClearRootSet();
        }
    }

    void RuntimeMusicChestService::Impl::paint(Gate& gate)
    {
        auto* mesh = try_read_reflected_object_property_value(gate.chest.Get(), L"MyMesh");
        Call get(mesh, L"GetMaterial");
        get.run();
        auto* old = try_read_function_object_param(get.fn, get.data.data(), L"ReturnValue");
        Call texture(old, L"K2_GetTextureParameterValue");
        if (!write_name_or_string_function_param(texture.field(L"ParameterName"),
                                                 texture.data.data(), L"T_Diffuse"))
        {
            throw std::runtime_error("music_diffuse_parameter");
        }
        texture.run();
        auto* diffuse =
            try_read_function_object_param(texture.fn, texture.data.data(), L"ReturnValue");
        if (!is_uobject_valid_fast(diffuse))
        {
            throw std::runtime_error("music_chest_diffuse_missing");
        }
        const auto diffuse_name = diffuse->GetNamePrivate().ToString();
        // These worlds paint the body silver or yellow and the frame blue/purple
        // or gray, so the diffuse gold detector cannot identify their trim.
        const bool monstropolis =
            diffuse_name == L"T_mi_TreasureBox_s_D" || diffuse_name == L"T_mi_TreasureBox_l_D";
        const bool use_native_mask = diffuse_name == L"T_fz_TreasureBox_s_D" ||
                                     diffuse_name == L"T_fz_TresureBox_l_D" || monstropolis;
        UObject* trim_mask{};
        if (use_native_mask)
        {
            Call mask(old, L"K2_GetTextureParameterValue");
            if (!write_name_or_string_function_param(mask.field(L"ParameterName"), mask.data.data(),
                                                     L"T_TripleMask"))
            {
                throw std::runtime_error("music_chest_trim_mask_parameter");
            }
            mask.run();
            trim_mask = try_read_function_object_param(mask.fn, mask.data.data(), L"ReturnValue");
            if (!is_uobject_valid_fast(trim_mask))
            {
                throw std::runtime_error("music_chest_trim_mask_missing");
            }
        }
        // Retain the original material for seed-switch cleanup. Mesh, animation,
        // treasure ID, collision and every reward callback remain stock.
        auto* notice = gate.notice.Get();
        auto* original = notice->GetPropertyByNameInChain(L"OriginalMaterial");
        if (!original || original->GetElementSize() != sizeof(UObject*))
        {
            throw std::runtime_error("music_original_material_storage");
        }
        // A reflected strong reference on our rooted callback owns the material;
        // no unprotected raw material pointer survives a frame or map change.
        *original->ContainerPtrToValuePtr<UObject*>(notice) = old;
        Call make(mesh, L"CreateDynamicMaterialInstance");
        if (!write_object_function_param(make.fn, make.data.data(), L"SourceMaterial",
                                         loaded(paint_path)))
        {
            throw std::runtime_error("music_material_source");
        }
        make.run();
        auto* material = try_read_function_object_param(make.fn, make.data.data(), L"ReturnValue");
        Call set(material, L"SetTextureParameterValue");
        if (!write_name_or_string_function_param(set.field(L"ParameterName"), set.data.data(),
                                                 L"NativeDiffuse") ||
            !write_object_function_param(set.fn, set.data.data(), L"Value", diffuse))
        {
            throw std::runtime_error("music_material_texture");
        }
        set.run();
        if (use_native_mask)
        {
            Call mask(material, L"SetTextureParameterValue");
            if (!write_name_or_string_function_param(mask.field(L"ParameterName"), mask.data.data(),
                                                     L"NativeTrimMask") ||
                !write_object_function_param(mask.fn, mask.data.data(), L"Value", trim_mask))
            {
                throw std::runtime_error("music_material_trim_mask");
            }
            mask.run();
            Call mode(material, L"SetScalarParameterValue");
            if (!write_name_or_string_function_param(mode.field(L"ParameterName"), mode.data.data(),
                                                     L"UseNativeTrimMask") ||
                !write_float_function_param(mode.field(L"Value"), mode.data.data(), 1.f))
            {
                throw std::runtime_error("music_material_trim_mode");
            }
            mode.run();
            if (monstropolis)
            {
                // MI's worn metal frame has a lower blue-channel mask than
                // Frozen's painted trim. Keep it entirely in the trim palette.
                Call settings(material, L"SetVectorParameterValue");
                if (!write_name_or_string_function_param(settings.field(L"ParameterName"),
                                                         settings.data.data(),
                                                         L"NativeMaskSettings"))
                {
                    throw std::runtime_error("music_material_mask_settings_parameter");
                }
                auto* p = settings.field(L"Value");
                if (!p->IsA<FStructProperty>() ||
                    p->GetElementSize() != sizeof(std::array<float, 4>))
                {
                    throw std::runtime_error("music_material_mask_settings_layout");
                }
                const std::array<float, 4> values{.1f, .45f, 2.5f, 0.f};
                std::memcpy(p->ContainerPtrToValuePtr<void>(settings.data.data()), values.data(),
                            sizeof(values));
                settings.run();
            }
        }
        const auto& a = assignments.at(gate.treasure);
        if (!a.keyblade.empty())
        {
            for (const auto& [name, color] :
                 {std::pair{L"BodyColor", a.body}, std::pair{L"TrimColor", a.trim}})
            {
                Call tint(material, L"SetVectorParameterValue");
                if (!write_name_or_string_function_param(tint.field(L"ParameterName"),
                                                         tint.data.data(), name))
                {
                    throw std::runtime_error("keyblade_chest_color_parameter");
                }
                auto* p = tint.field(L"Value");
                if (!p->IsA<FStructProperty>() ||
                    p->GetElementSize() != sizeof(std::array<float, 4>))
                {
                    throw std::runtime_error("keyblade_chest_color_layout");
                }
                std::memcpy(p->ContainerPtrToValuePtr<void>(tint.data.data()), color.data(),
                            sizeof(color));
                tint.run();
            }
        }
        gate.painted = true;
    }

    UObject* RuntimeMusicChestService::Impl::spawned_component(Gate& gate, const wchar_t* name)
    {
        auto* actor = gate.chest.Get();
        auto* result = is_uobject_valid_fast(actor)
                           ? try_read_reflected_object_property_value(actor, name)
                           : nullptr;
        if (!is_uobject_valid_fast(result))
        {
            throw std::runtime_error("spawned_chest_component");
        }
        return result;
    }

    void RuntimeMusicChestService::Impl::pose(Gate& gate, float fraction)
    {
        auto* mesh = spawned_component(gate, L"NativeChestMesh");
        auto* anim = loaded(spawned_animation_path);
        if (!anim)
        {
            throw std::runtime_error("spawned_chest_animation_missing");
        }
        auto* length = anim->GetPropertyByNameInChain(L"SequenceLength");
        if (!length || length->GetElementSize() != sizeof(float))
        {
            throw std::runtime_error("spawned_chest_animation_layout");
        }
        float duration{};
        std::memcpy(&duration, length->ContainerPtrToValuePtr<void>(anim), sizeof(duration));
        if (!std::isfinite(duration) || duration <= 0)
        {
            throw std::runtime_error("spawned_chest_animation_duration");
        }
        Call override(mesh, L"OverrideAnimationData");
        if (!write_object_function_param(override.fn, override.data.data(), L"InAnimToPlay",
                                         anim) ||
            !write_float_function_param(override.field(L"Position"), override.data.data(),
                                        duration * fraction) ||
            !write_float_function_param(override.field(L"PlayRate"), override.data.data(), 0))
        {
            throw std::runtime_error("spawned_chest_pose_layout");
        }
        override.boolean(L"bIsLooping", false);
        override.boolean(L"bIsPlaying", false);
        override.run();
        Call seek(mesh, L"SetPosition");
        if (!write_float_function_param(seek.field(L"InPos"), seek.data.data(),
                                        duration * fraction))
        {
            throw std::runtime_error("spawned_chest_seek_layout");
        }
        seek.boolean(L"bFireNotifies", false);
        seek.run();
        override.run();
        Call readback(mesh, L"GetPosition");
        readback.run();
        auto* result = readback.field(L"ReturnValue");
        if (result->GetElementSize() != sizeof(float))
        {
            throw std::runtime_error("spawned_chest_pose_result_layout");
        }
        float actual{};
        std::memcpy(&actual, result->ContainerPtrToValuePtr<void>(readback.data.data()),
                    sizeof(float));
        if (!std::isfinite(actual) ||
            std::abs(actual - duration * fraction) > animation_position_tolerance_seconds)
        {
            throw std::runtime_error("spawned_chest_pose_readback");
        }
    }

    void RuntimeMusicChestService::Impl::spawn_placed(UObject* pawn, const std::wstring& id,
                                                      const Assignment& a)
    {
        // Class and all independent assets are retained before spawning. The
        // authored mesh offset is compensated; no live floor trace is needed.
        const double angle = a.yaw * 3.141592653589793 / 180;
        const FTransform transform{
            FQuat{0, 0, std::sin(angle / 2), std::cos(angle / 2)},
            FVector{a.position[0] - authored_mesh_forward_offset_cm * std::cos(angle),
                    a.position[1] - authored_mesh_forward_offset_cm * std::sin(angle),
                    a.position[2]},
            FVector{1, 1, 1}};
        const bool persisted = native_chest_was_opened(id);
        AActor* actor{};
        {
            const RuntimeGrantInternalCallDepth::Scope scope{};
            actor = UGameplayStatics::BeginDeferredActorSpawnFromClass(
                pawn, static_cast<UClass*>(loaded(spawned_class_path)), transform,
                ESpawnActorCollisionHandlingMethod::AlwaysSpawn, nullptr);
        }
        if (!is_uobject_valid_fast(actor))
        {
            throw std::runtime_error("spawned_chest_begin_failed");
        }
        Gate gate;
        gate.spawned = true;
        gate.treasure = id;
        gate.opened = persisted;
        try
        {
            {
                const RuntimeGrantInternalCallDepth::Scope scope{};
                auto* finished = UGameplayStatics::FinishSpawningActor(actor, transform);
                if (is_uobject_valid_fast(finished))
                {
                    actor = finished;
                }
            }
            if (!is_uobject_valid_fast(actor))
            {
                throw std::runtime_error("spawned_chest_finish_failed");
            }
            auto* r = try_read_reflected_object_property_value(actor, L"MemoryReactor");
            Call off(r, L"BP_DeactivateGimmickComponent");
            off.run();
            // Allocate the actor's weak serial through the existing Blueprint
            // delegate API, then remove this inert prototype listener.
            Call establish(actor, L"BindToReactor");
            if (!write_object_function_param(establish.fn, establish.data.data(),
                                             L"ReactorComponent", r))
            {
                throw std::runtime_error("spawned_chest_bind_layout");
            }
            establish.run();
            gate.chest = actor;
            gate.reactor = r;
            if (gate.chest.Get() != actor || gate.reactor.Get() != r)
            {
                throw std::runtime_error("spawned_chest_weak_identity");
            }
            auto& list = chest_delegate(r)->InvocationList;
            for (int i = list.Num() - 1; i >= 0; --i)
            {
                if (list[i].Object.Get() == actor &&
                    list[i].FunctionName.ToString() == L"OnBarrierChecked")
                {
                    list.RemoveAt(i);
                }
            }
            auto* target = r->GetPropertyByNameInChain(L"m_CmdTargetActor");
            if (!target || target->GetElementSize() != sizeof(UObject*))
            {
                throw std::runtime_error("spawned_chest_target_layout");
            }
            *target->ContainerPtrToValuePtr<UObject*>(r) = actor;
            auto* mesh = spawned_component(gate, L"NativeChestMesh");
            Call set(mesh, L"SetSkeletalMesh");
            if (!write_object_function_param(set.fn, set.data.data(), L"NewMesh",
                                             loaded(spawned_mesh_path)))
            {
                throw std::runtime_error("spawned_chest_mesh_layout");
            }
            set.boolean(L"bReinitPose", true);
            set.run();
            if (try_read_reflected_object_property_value(mesh, L"SkeletalMesh") !=
                loaded(spawned_mesh_path))
            {
                throw std::runtime_error("spawned_chest_mesh_readback");
            }
            Call make(mesh, L"CreateDynamicMaterialInstance");
            make.run();
            auto* material =
                try_read_function_object_param(make.fn, make.data.data(), L"ReturnValue");
            Call texture(material, L"SetTextureParameterValue");
            if (!write_name_or_string_function_param(texture.field(L"ParameterName"),
                                                     texture.data.data(), L"NativeDiffuse") ||
                !write_object_function_param(texture.fn, texture.data.data(), L"Value",
                                             loaded(spawned_diffuse_path)))
            {
                throw std::runtime_error("spawned_chest_texture_layout");
            }
            texture.run();
            pose(gate, persisted ? 1.f : 0.f);
            bind(gate);
            gate.painted = true;
            const uint8_t examine = chest_command::examine;
            reactor_command(r, &examine);
            if (!persisted && !a.cleared)
            {
                Call on(r, L"BP_ActivateGimmickComponent");
                on.run();
            }
            const auto key = L"placed:" + id;
            gates.emplace(key, std::move(gate));
            watch_gates();
            if (!persisted && a.cleared)
            {
                opening_work.request(key);
            }
        }
        catch (...)
        {
            if (is_uobject_valid_fast(gate.chest.Get()))
            {
                retire(gate);
            }
            else if (is_uobject_valid_fast(actor))
            {
                Call destroy(actor, L"K2_DestroyActor");
                destroy.run();
            }
            throw;
        }
    }

    void RuntimeMusicChestService::Impl::advance_spawned(UObject* pawn)
    {
        bool changed = false;
        opening_work.process(
            [&](const std::wstring& key)
            {
                const auto found = gates.find(key);
                if (found == gates.end())
                {
                    return false;
                }
                auto& gate = found->second;
                if (!gate.spawned || gate.opened || !gate.save_error.empty() ||
                    !is_uobject_valid_fast(gate.chest.Get()))
                {
                    return false;
                }
                auto it = assignments.find(gate.treasure);
                if (it == assignments.end() || !it->second.cleared)
                {
                    return false;
                }
                if (world(pawn) != current_world.Get() || !config.gameplay_ready() ||
                    !foreground() || game_paused(pawn))
                {
                    return true;
                }
                if (!gate.opening)
                {
                    Call off(gate.reactor.Get(), L"BP_DeactivateGimmickComponent");
                    off.run();
                    gate.opening = true;
                    gate.opening_start = Clock::now();
                    changed = true;
                }
                const float t = std::clamp(
                    std::chrono::duration<float>(Clock::now() - gate.opening_start).count() /
                        chest_opening_duration_seconds,
                    0.f, 1.f);
                if (!gate.animation_finished)
                {
                    pose(gate, t);
                    gate.animation_finished = t >= 1;
                }
                if (t >= 1 && gate.save_error.empty())
                {
                    try
                    {
                        record_native_chest(gate.treasure);
                        gate.opened = true;
                        gate.opening = false;
                        gate.save_error.clear();
                        changed = true;
                    }
                    catch (const std::exception& e)
                    {
                        gate.save_error = e.what();
                        changed = true;
                    }
                }
                return !gate.opened && gate.save_error.empty();
            });
        if (changed)
        {
            publish_status();
        }
    }
} // namespace kh3ap
