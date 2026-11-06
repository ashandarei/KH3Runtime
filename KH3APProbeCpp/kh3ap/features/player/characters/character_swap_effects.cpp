#include "kh3ap/features/player/characters/character_swap_effects.h"
#include "kh3ap/features/player/player_animation_support.h"
#include "kh3ap/features/player/characters/kairi_idle.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/features/kh02/compatibility/kh02_aliases.h"
#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <Unreal/TArray.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
#include <nlohmann/json.hpp>
#endif
#include <algorithm>
#include <array>
#include <exception>
#include <stdexcept>
#include <string>
#include <vector>

namespace kh3ap::character_swap_effects
{
    using namespace RC::Unreal;

    namespace
    {
        using Clock = std::chrono::steady_clock;
        constexpr auto template_path = L"/Game/Effects/PC/Free_f/par/vp_ffa_sps0.vp_ffa_sps0";
        constexpr auto callback_name = L"KH3CharacterSwapEffectLoaded";
        constexpr auto preparation_timeout = std::chrono::seconds(20);
        constexpr float effect_seconds = 1.15f;
        constexpr float particle_tail_seconds = 0.5f;
        constexpr float sampling_pose_alpha = 0.0f;
        constexpr float warm_seconds = 0.15f;
        constexpr float parked_actor_z = -100000.0f;
        constexpr int always_spawn_collision = 2;
        constexpr float preview_seconds = 2.0f;
        constexpr float preview_side_offset = 150.0f;
        constexpr size_t model_slot_count = 4;
        constexpr size_t pool_size = model_slot_count * 2;
        constexpr int single_animation_mode = 1;
        constexpr uint8_t always_refresh_bones = 0;
        constexpr unsigned minimum_warm_frames = 3;
        constexpr unsigned pose_render_frames = 2;
        constexpr int load_uuid = 0x4b4b0001;

        struct Call : Kh02ReflectedCall
        {
            Call(UObject* object, const wchar_t* method)
                : Kh02ReflectedCall(object, method, "character_swap_effect", 512)
            {
            }
        };

        struct Slot
        {
            FWeakObjectPtr actor;
            FWeakObjectPtr component;
            FWeakObjectPtr mesh;
            Clock::time_point until{}, emission_until{};
            bool emitting{};
            bool prepared{};
            bool active{}, warming{};
            unsigned activation_frames{};
            float duration{};
            unsigned warm_frames{};
            FWeakObjectPtr warm_model;
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            bool capture_pending{};
            Clock::time_point capture_at{};
#endif
        };

        std::array<Slot, pool_size> slots;
        std::vector<FWeakObjectPtr> rooted;
        FWeakObjectPtr effect_template, pool_world, request_pawn;
        FWeakObjectPtr selected_class;
        std::vector<FWeakObjectPtr> warm_models;
        bool selected_warm_pending{};
        Kh02NativeAliases completion_alias;
        Clock::time_point deadline{};
        Clock::time_point selection_warm_deadline{};
        bool enabled{}, prepare_pending{}, loading{}, completion_pending{};
        uint64_t played{}, skipped{}, warmed{};
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        uint64_t last_emit_us{}, worst_emit_us{};
        Clock::time_point suppressed_until{};
        uint64_t suppressed{};
        constexpr auto capture_delay = std::chrono::milliseconds(180);
        constexpr auto capture_request_lifetime = std::chrono::seconds(5);
        Clock::time_point capture_requested_until{};
        uint64_t captures{};
        int32_t last_capture_particles{};
#endif
        std::string error;
        std::string selection_warm_result;

        UObject* object(UObject* owner, const wchar_t* name)
        {
            return is_uobject_valid_fast(owner) ? try_read_reflected_object_property_value(owner, name) : nullptr;
        }

        UObject* current_pawn()
        {
            auto* manager = object(object(kh02_current_viewport(), L"GameInstance"), L"m_UIManager");
            auto* controller = object(manager, L"m_pPlayerController");
            auto* pawn = object(controller, L"Pawn");
            return pawn && object(pawn, L"Controller") == controller && kh02_actor_world(pawn) == kh02_current_world()
                       ? pawn
                       : nullptr;
        }

        UClass* animation_type(UObject* mesh)
        {
            auto* field = is_uobject_valid_fast(mesh) ? mesh->GetPropertyByNameInChain(L"AnimClass") : nullptr;
            if (!field || field->GetClass().GetName() != L"ClassProperty" ||
                field->GetElementSize() != sizeof(UObject*))
            {
                return nullptr;
            }
            auto* type = *field->ContainerPtrToValuePtr<UClass*>(mesh);
            return is_uobject_valid_fast(type) ? type : nullptr;
        }

        void root(UObject* value)
        {
            if (is_uobject_valid_fast(value) && !value->HasAnyInternalFlags(EInternalObjectFlags::RootSet))
            {
                value->SetRootSet();
                rooted.push_back(make_uobject_weak(value));
            }
        }

        auto objects(UObject* owner, const wchar_t* name) -> TArray<UObject*>&
        {
            auto* field = is_uobject_valid_fast(owner) ? owner->GetPropertyByNameInChain(name) : nullptr;
            if (!field || !field->IsA<FArrayProperty>() ||
                static_cast<FArrayProperty*>(field)->GetInner()->GetElementSize() != sizeof(UObject*))
            {
                throw std::runtime_error("effect_object_array_layout");
            }
            return *field->ContainerPtrToValuePtr<TArray<UObject*>>(owner);
        }

        void boolean(UObject* owner, const wchar_t* name, bool value)
        {
            auto* field = owner->GetPropertyByNameInChain(name);
            if (!field || !field->IsA<FBoolProperty>())
            {
                std::string field_name;
                for (const auto character : std::wstring(name))
                {
                    field_name.push_back(static_cast<char>(character));
                }
                throw std::runtime_error("effect_boolean_layout:" + field_name);
            }
            static_cast<FBoolProperty*>(field)->SetPropertyValueInContainer(owner, value);
        }

        void component_visible(UObject* component, bool visible)
        {
            Call call(component, L"SetVisibility");
            call.boolean(L"bNewVisibility", visible);
            call.boolean(L"bPropagateToChildren", false);
            call.run();
        }

        void component_tick(UObject* component, bool enabled)
        {
            Call call(component, L"SetComponentTickEnabled");
            call.boolean(L"bEnabled", enabled);
            call.run();
        }

        void pose_visibility(UObject* mesh, float alpha)
        {
            Call material(mesh, L"SetScalarParameterValueOnMaterials");
            material.name(L"ParameterName", L"VisChara");
            material.number(L"ParameterValue", alpha);
            material.run();
        }

        void end_emission(Slot& slot)
        {
            if (auto* component = slot.component.Get())
            {
                Call deactivate(component, L"Deactivate");
                deactivate.run();
            }
            slot.emitting = false;
        }

        void stop(Slot& slot)
        {
            slot.active = false;
            slot.warming = false;
            slot.activation_frames = 0;
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            slot.capture_pending = false;
#endif
            end_emission(slot);
            if (auto* component = slot.component.Get())
            {
                component_visible(component, false);
                component_tick(component, false);
            }
            if (auto* mesh = slot.mesh.Get())
            {
                component_visible(mesh, false);
                component_tick(mesh, false);
            }
        }

        void clear()
        {
            std::exception_ptr cleanup_error;
            const bool same_world = pool_world.Get() == kh02_current_world();
            const auto attempt = [&cleanup_error](const auto& action)
            {
                try
                {
                    action();
                }
                catch (...)
                {
                    if (!cleanup_error)
                    {
                        cleanup_error = std::current_exception();
                    }
                }
            };
            for (auto& slot : slots)
            {
                if (same_world)
                {
                    attempt([&slot] { stop(slot); });
                    if (auto* actor = slot.actor.Get())
                    {
                        attempt(
                            [actor]
                            {
                                Call destroy(actor, L"K2_DestroyActor");
                                destroy.run();
                            });
                    }
                }
                slot = {};
            }
            for (const auto& lease : rooted)
            {
                if (auto* value = lease.Get())
                {
                    value->ClearRootSet();
                }
            }
            rooted.clear();
            effect_template = {};
            pool_world = {};
            warm_models.clear();
            loading = false;
            completion_pending = false;
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            capture_requested_until = {};
#endif
            if (cleanup_error)
            {
                std::rethrow_exception(cleanup_error);
            }
        }

        auto vector(UObject* owner, const wchar_t* method) -> std::array<float, 3>
        {
            Call call(owner, method);
            call.run();
            return call.result_vector();
        }

        UObject* animation(UObject* pawn)
        {
            auto* instance = object(object(pawn, L"MyMesh"), L"AnimScriptInstance");
            if (!instance)
            {
                return nullptr;
            }
            Call name(pawn, L"BP_AnimGetLastPlayAnimName");
            name.name(L"InSlotName", L"FullBody");
            name.run();
            auto* result = name.param(L"ReturnValue");
            if (result->GetElementSize() != sizeof(FName))
            {
                throw std::runtime_error("effect_animation_name_layout");
            }
            const auto motion = kairi_idle::effective_animation_name(
                instance, *result->ContainerPtrToValuePtr<FName>(name.parameters.data()));
            for (auto* set : objects(instance, L"MyAnimSets"))
            {
                auto* field = is_uobject_valid_fast(set) ? set->GetPropertyByNameInChain(L"Anims") : nullptr;
                if (!field || !field->IsA<FArrayProperty>() ||
                    static_cast<FArrayProperty*>(field)->GetInner()->GetElementSize() !=
                        sizeof(player_animation_support::AnimationEntry))
                {
                    continue;
                }
                for (const auto& entry :
                     *field->ContainerPtrToValuePtr<TArray<player_animation_support::AnimationEntry>>(set))
                {
                    if (entry.name == motion && is_uobject_valid_fast(entry.asset))
                    {
                        return entry.asset;
                    }
                }
            }
            return nullptr;
        }

        void move_component(UObject* component, const std::array<float, 3>& location,
                            const std::array<float, 3>& rotation)
        {
            Call transform(component, L"K2_SetWorldLocationAndRotation");
            transform.vector(L"NewLocation", location[0], location[1], location[2]);
            transform.vector(L"NewRotation", rotation[0], rotation[1], rotation[2]);
            transform.boolean(L"bSweep", false);
            transform.boolean(L"bTeleport", true);
            transform.run();
        }

        UObject* attach_particle(UObject* mesh, UObject* asset, UObject* owner)
        {
            Call spawn(kh02_require_loaded_object(L"/Script/Engine.Default__GameplayStatics"), L"SpawnEmitterAttached");
            spawn.object(L"EmitterTemplate", asset);
            spawn.object(L"AttachToComponent", mesh);
            spawn.name(L"AttachPointName", L"None");
            spawn.integer(L"LocationType", 0);
            spawn.boolean(L"bAutoDestroy", false);
            spawn.object(L"in_Owner", owner);
            spawn.run();
            auto* component = spawn.result();
            if (!is_uobject_valid_fast(component))
            {
                throw std::runtime_error("sparkle_component_create");
            }
            return component;
        }

        bool play_pose(Slot& slot, UObject* source_mesh, UObject* sequence, float phase,
                       const std::array<float, 3>& source_location, const std::array<float, 3>& rotation, bool warming,
                       float duration)
        {
            auto* mesh = slot.mesh.Get();
            auto* model = object(source_mesh, L"SkeletalMesh");
            if (!mesh || !model || !sequence)
            {
                return false;
            }
            if (object(mesh, L"SkeletalMesh") != model)
            {
                slot.prepared = false;
                objects(mesh, L"OverrideMaterials").Empty();
                Call set_mesh(mesh, L"SetSkeletalMesh");
                set_mesh.object(L"NewMesh", model);
                set_mesh.boolean(L"bReinitPose", true);
                set_mesh.run();
                pose_visibility(mesh, sampling_pose_alpha);
            }
            Call mode(mesh, L"SetAnimationMode");
            mode.integer(L"InAnimationMode", single_animation_mode);
            mode.run();
            Call animation(mesh, L"SetAnimation");
            animation.object(L"NewAnimToPlay", sequence);
            animation.run();
            Call position(mesh, L"SetPosition");
            position.number(L"InPos", phase);
            position.boolean(L"bFireNotifies", false);
            position.run();
            auto location = source_location;
            move_component(mesh, location, rotation);
            component_visible(mesh, true);
            component_tick(mesh, true);
            if (!slot.component.Get())
            {
                slot.component = make_uobject_weak(attach_particle(mesh, effect_template.Get(), slot.actor.Get()));
            }
            end_emission(slot);
            component_visible(slot.component.Get(), false);
            const auto now = Clock::now();
            slot.until = now + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<float>(duration));
            slot.emission_until = slot.until - std::chrono::duration_cast<Clock::duration>(std::chrono::duration<float>(
                                                   warming ? 0.0f : particle_tail_seconds));
            slot.active = true;
            slot.warming = warming;
            // Vertex-spawn particles need the newly revealed frozen pose to
            // reach the render thread first. Only presentation waits here;
            // the character factory and controller handoff continue immediately.
            slot.activation_frames = pose_render_frames;
            slot.duration = duration;
            slot.warm_model = make_uobject_weak(model);
            slot.warm_frames = 0;
            return true;
        }

        bool play(Slot& slot, UObject* pawn, bool warming, float duration, float preview_offset = 0.0f)
        {
            auto* mesh = object(pawn, L"MyMesh");
            auto* instance = object(mesh, L"AnimScriptInstance");
            auto* sequence = animation(pawn);
            if (!instance || !sequence)
            {
                return false;
            }
            Call time(instance, L"SQEX_AnimGetCurrentTime");
            time.name(L"InSlotName", L"FullBody");
            time.run();
            const auto phase = *time.param(L"ReturnValue")->ContainerPtrToValuePtr<float>(time.parameters.data());
            auto location = vector(mesh, L"K2_GetComponentLocation");
            location[1] += preview_offset;
            return play_pose(slot, mesh, sequence, phase, location, vector(mesh, L"K2_GetComponentRotation"), warming,
                             duration);
        }

        bool model_warmed(UObject* model)
        {
            return std::any_of(slots.begin(), slots.end(), [model](const auto& slot)
                               { return slot.prepared && slot.mesh.Get() && slot.warm_model.Get() == model; });
        }

        void warm_selection()
        {
            if (!selected_warm_pending || !effect_template.Get() ||
                // Only these slots are reassigned below. Sora's active sparks
                // must not starve alternate-model preparation during swaps.
                std::any_of(slots.begin() + model_slot_count, slots.end(),
                            [](const auto& slot) { return slot.active; }))
            {
                return;
            }
            if (Clock::now() >= selection_warm_deadline)
            {
                selected_warm_pending = false;
                selection_warm_result = "timeout";
                return;
            }
            auto* type = static_cast<UClass*>(selected_class.Get());
            auto* mesh = type ? object(type->GetClassDefaultObject(), L"MyMesh") : nullptr;
            auto* model = object(mesh, L"SkeletalMesh");
            if (!model)
            {
                selection_warm_result = "model_unavailable";
                return;
            }
            if (model_warmed(model))
            {
                selected_warm_pending = false;
                selection_warm_result = "ready";
                return;
            }
            auto* animation_class = animation_type(mesh);
            auto* instance = animation_class ? animation_class->GetClassDefaultObject() : nullptr;
            UObject* sequence{};
            const FName idle_name(L"IdleB", FNAME_Add);
            if (instance)
            {
                for (auto* set : objects(instance, L"MyAnimSets"))
                {
                    auto* field = is_uobject_valid_fast(set) ? set->GetPropertyByNameInChain(L"Anims") : nullptr;
                    if (!field || !field->IsA<FArrayProperty>() ||
                        static_cast<FArrayProperty*>(field)->GetInner()->GetElementSize() !=
                            sizeof(player_animation_support::AnimationEntry))
                    {
                        continue;
                    }
                    for (const auto& entry :
                         *field->ContainerPtrToValuePtr<TArray<player_animation_support::AnimationEntry>>(set))
                    {
                        if (entry.name == idle_name && is_uobject_valid_fast(entry.asset))
                        {
                            sequence = entry.asset;
                            break;
                        }
                    }
                    if (sequence)
                    {
                        break;
                    }
                }
            }
            if (!sequence)
            {
                selection_warm_result = "idle_unavailable";
                return;
            }
            root(type);
            root(model);
            root(sequence);
            // Selection can change after several factory swaps have retired
            // the pawn that originally requested this world's particle asset.
            auto* pawn = current_pawn();
            auto* current_mesh = object(pawn, L"MyMesh");
            if (!current_mesh)
            {
                selection_warm_result = "context_unavailable";
                return;
            }
            const auto location = vector(current_mesh, L"K2_GetComponentLocation");
            const auto rotation = vector(current_mesh, L"K2_GetComponentRotation");
            for (size_t index = model_slot_count; index < slots.size(); ++index)
            {
                play_pose(slots[index], mesh, sequence, 0.0f, location, rotation, true, warm_seconds);
            }
            selected_warm_pending = false;
            selection_warm_result = "warming";
        }

        void loaded(UObject* pawn, FFrame&, void*)
        {
            if (loading && request_pawn.Get() == pawn)
            {
                completion_pending = true;
            }
        }

        void prepare(UObject* pawn)
        {
            auto* source = find_loaded_runtime_asset(template_path);
            if (!source || !animation(pawn))
            {
                return;
            }
            root(source);
            effect_template = make_uobject_weak(source);
            pool_world = make_uobject_weak(kh02_actor_world(pawn));
            auto* gameplay = kh02_require_loaded_object(L"/Script/Engine.Default__GameplayStatics");
            for (size_t index = 0; index < slots.size(); ++index)
            {
                auto& slot = slots[index];
                Call spawn(gameplay, L"BeginDeferredActorSpawnFromClass");
                spawn.object(L"WorldContextObject", pawn);
                spawn.object(L"ActorClass", kh02_require_loaded_object(L"/Script/Engine.SkeletalMeshActor"));
                spawn.integer(L"CollisionHandlingOverride", always_spawn_collision);
                spawn.spawn_transform(0, 0, parked_actor_z);
                spawn.run();
                auto* actor = spawn.result();
                auto* mesh = object(actor, L"SkeletalMeshComponent");
                if (!mesh)
                {
                    throw std::runtime_error("sparkle_mesh_proxy_create");
                }
                // The level owns these actors. Only their posed surface is
                // sampled: no opaque duplicate, collision, shadow or AI.
                slot.actor = make_uobject_weak(actor);
                slot.mesh = make_uobject_weak(mesh);
                boolean(mesh, L"bRenderInMainPass", true);
                boolean(mesh, L"bRenderCustomDepth", true);
                boolean(mesh, L"CastShadow", false);
                auto* update = mesh->GetPropertyByNameInChain(L"MeshComponentUpdateFlag");
                if (!update || update->GetElementSize() != sizeof(uint8_t))
                {
                    throw std::runtime_error("sparkle_pose_tick_layout");
                }
                *update->ContainerPtrToValuePtr<uint8_t>(mesh) = always_refresh_bones;
                Call collision(actor, L"SetActorEnableCollision");
                collision.boolean(L"bNewActorEnableCollision", false);
                collision.run();
                Call finish(gameplay, L"FinishSpawningActor");
                finish.object(L"Actor", actor);
                finish.spawn_transform(0, 0, parked_actor_z);
                finish.run();
                if (index < model_slot_count)
                {
                    if (!play(slot, pawn, true, warm_seconds))
                    {
                        throw std::runtime_error("sparkle_warm_pose_unavailable");
                    }
                }
                else
                {
                    component_tick(mesh, false);
                }
            }
            loading = false;
            completion_pending = false;
            prepare_pending = false;
        }

        void failure(const std::exception& exception)
        {
            error = exception.what();
            loading = false;
            completion_pending = false;
            prepare_pending = false;
            selected_warm_pending = false;
            // Failed presentation work is canceled once, never retried per frame.
            for (auto& slot : slots)
            {
                slot.active = false;
                slot.warming = false;
            }
            try
            {
                clear();
            }
            catch (const std::exception&)
            { /* Preserve the original failure. */
            }
            RC::Output::send<RC::LogLevel::Warning>(STR("[KH3APProbeCpp] Character swap effect unavailable: {}\n"),
                                                    std::wstring(error.begin(), error.end()));
        }

        bool emit(UObject* pawn, float duration, float preview_offset = 0.0f)
        {
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            const bool capture_requested = Clock::now() < capture_requested_until;
            capture_requested_until = {};
#endif
            if (!enabled || !error.empty() || !is_uobject_valid_fast(pawn) || kh02_current_story_world() ||
                kh02_actor_world(pawn) != pool_world.Get())
            {
                ++skipped;
                return false;
            }
            if (!model_warmed(object(object(pawn, L"MyMesh"), L"SkeletalMesh")))
            {
                ++skipped;
                return false;
            }
            for (auto& slot : slots)
            {
                if (slot.component.Get() && !slot.active && slot.prepared &&
                    slot.warm_model.Get() == object(object(pawn, L"MyMesh"), L"SkeletalMesh"))
                {
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
                    const auto started = Clock::now();
#endif
                    if (play(slot, pawn, false, duration, preview_offset))
                    {
                        ++played;
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
                        slot.capture_pending = capture_requested;
                        last_emit_us =
                            std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - started).count();
                        worst_emit_us = std::max(worst_emit_us, last_emit_us);
#endif
                        return true;
                    }
                    break;
                }
            }
            ++skipped;
            return false;
        }
    } // namespace

    void select(bool value)
    {
        if (enabled == value)
        {
            return;
        }
        enabled = value;
        try
        {
            clear();
        }
        catch (const std::exception& exception)
        {
            failure(exception);
        }
        error.clear();
        prepare_pending = enabled;
        deadline = Clock::now() + preparation_timeout;
    }

    void notify_player_started()
    {
        if (enabled && pool_world.Get() != kh02_current_world())
        {
            try
            {
                clear();
            }
            catch (const std::exception& exception)
            {
                failure(exception);
            }
            error.clear();
            prepare_pending = true;
            selected_warm_pending = selected_class.Get() != nullptr;
            selection_warm_deadline = Clock::now() + preparation_timeout;
            deadline = Clock::now() + preparation_timeout;
        }
    }

    void notify_map_loading()
    {
        prepare_pending = false;
        selected_warm_pending = false;
        loading = false;
        request_pawn = {};
        try
        {
            clear();
        }
        catch (const std::exception& exception)
        {
            failure(exception);
        }
    }

    void selected_class_ready(UObject* type)
    {
        selected_class = make_uobject_weak(type);
        selected_warm_pending = enabled && is_uobject_valid_fast(type);
        selection_warm_deadline = Clock::now() + preparation_timeout;
        selection_warm_result = selected_warm_pending ? "pending" : "off";
    }

    void tick(Clock::time_point now)
    {
        try
        {
            bool paused{};
            if (std::any_of(slots.begin(), slots.end(), [](const auto& slot) { return slot.active && slot.warming; }))
            {
                Call pause(kh02_require_loaded_object(L"/Script/Engine.Default__GameplayStatics"), L"IsGamePaused");
                pause.object(L"WorldContextObject", kh02_current_world());
                pause.run();
                paused = pause.boolean_result();
            }
            for (auto& slot : slots)
            {
                if (!slot.active)
                {
                    continue;
                }
                if (!slot.mesh.Get() || pool_world.Get() != kh02_current_world())
                {
                    slot.active = false;
                    continue;
                }
                if (slot.activation_frames)
                {
                    if (paused || --slot.activation_frames)
                    {
                        continue;
                    }
                    auto* component = slot.component.Get();
                    if (!component)
                    {
                        stop(slot);
                        continue;
                    }
                    component_visible(component, !slot.warming);
                    component_tick(component, true);
                    Call activate(component, L"ActivateSystem");
                    activate.boolean(L"bFlagAsJustAttached", true);
                    activate.run();
                    slot.emitting = true;
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
                    slot.capture_at = now + capture_delay;
#endif
                    slot.until = now + std::chrono::duration_cast<Clock::duration>(
                                           std::chrono::duration<float>(slot.duration));
                    slot.emission_until = slot.until - std::chrono::duration_cast<Clock::duration>(
                        std::chrono::duration<float>(slot.warming ? 0.0f : particle_tail_seconds));
                }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
                // Capture inside the active burst. Bridge command round trips
                // can take longer than this effect's visible particle lifetime.
                if (slot.capture_pending && now >= slot.capture_at)
                {
                    slot.capture_pending = false;
                    if (auto* pawn = current_pawn(); pawn && slot.component.Get())
                    {
                        Call count(slot.component.Get(), L"GetNumActiveParticles");
                        count.run();
                        last_capture_particles =
                            *count.param(L"ReturnValue")->ContainerPtrToValuePtr<int32_t>(count.parameters.data());
                        Call capture(kh02_require_loaded_object(L"/Script/Engine.Default__KismetSystemLibrary"),
                                     L"ExecuteConsoleCommand");
                        capture.object(L"WorldContextObject", pawn);
                        capture.string(L"Command", L"Shot showui");
                        capture.run();
                        ++captures;
                    }
                }
#endif
                if (slot.warming)
                {
                    // A paused menu can finish loading, but cannot warm the
                    // particle render path. Finish only after live world frames.
                    if (paused)
                    {
                        slot.until = now + std::chrono::duration_cast<Clock::duration>(
                                               std::chrono::duration<float>(warm_seconds));
                        continue;
                    }
                    ++slot.warm_frames;
                    if (slot.warm_frames < minimum_warm_frames)
                    {
                        continue;
                    }
                }
                if (now >= slot.until)
                {
                    if (slot.warming)
                    {
                        ++warmed;
                        slot.prepared = true;
                        if (auto* model = slot.warm_model.Get();
                            model && std::none_of(warm_models.begin(), warm_models.end(),
                                                  [model](const auto& candidate) { return candidate.Get() == model; }))
                        {
                            warm_models.push_back(make_uobject_weak(model));
                        }
                    }
                    stop(slot);
                }
                else if (!slot.warming && slot.emitting && now >= slot.emission_until)
                {
                    end_emission(slot);
                }
            }
            warm_selection();
            if (!prepare_pending && !loading)
            {
                return;
            }
            if (!player_animation_support::supported_skeleton_layout())
            {
                throw std::runtime_error("effect_unsupported_build");
            }
            if (now >= deadline)
            {
                throw std::runtime_error("effect_preparation_timeout");
            }
            if (completion_pending)
            {
                prepare(request_pawn.Get());
                return;
            }
            if (loading)
            {
                return;
            }
            auto* pawn = current_pawn();
            if (!pawn || kh02_current_story_world() || kh02_actor_world(pawn) != kh02_current_world())
            {
                return;
            }
            request_pawn = make_uobject_weak(pawn);
            if (find_loaded_runtime_asset(template_path))
            {
                prepare(pawn);
                return;
            }
            if (completion_alias.empty())
            {
                completion_alias.add(L"/Script/Engine.Actor", callback_name,
                                     L"/Script/Engine.PrimitiveComponent:SetTranslucentSortPriority", loaded);
            }
            loading = true;
            RuntimeAssetLoadCallbackScope callback(callback_name);
            if (!request_runtime_asset_load(pawn, template_path, nullptr, load_uuid))
            {
                throw std::runtime_error("effect_load_request");
            }
        }
        catch (const std::exception& exception)
        {
            failure(exception);
        }
    }

    void departing(UObject* pawn)
    {
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        if (Clock::now() < suppressed_until)
        {
            ++suppressed;
            return;
        }
#endif
        try
        {
            emit(pawn, effect_seconds);
        }
        catch (const std::exception& exception)
        {
            failure(exception);
        }
    }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    void capture_next_for_test()
    {
        capture_requested_until = Clock::now() + capture_request_lifetime;
    }

    void suppress_for_test(std::chrono::milliseconds duration)
    {
        suppressed_until = Clock::now() + std::clamp(duration, std::chrono::milliseconds{0},
                                                    std::chrono::milliseconds{10000});
    }

    bool preview(UObject* pawn)
    {
        try
        {
            return emit(pawn, preview_seconds, preview_side_offset);
        }
        catch (const std::exception& exception)
        {
            failure(exception);
            return false;
        }
    }

    nlohmann::json snapshot()
    {
        nlohmann::json pool = nlohmann::json::array();
        for (const auto& slot : slots)
        {
            nlohmann::json entry{
                {"valid", slot.component.Get() != nullptr}, {"active", slot.active}, {"warming", slot.warming},
                {"prepared", slot.prepared}};
            if (slot.active)
            {
                if (auto* component = slot.component.Get())
                {
                    Call count(component, L"GetNumActiveParticles");
                    count.run();
                    entry["sparks"] =
                        *count.param(L"ReturnValue")->ContainerPtrToValuePtr<int32_t>(count.parameters.data());
                }
            }
            pool.push_back(std::move(entry));
        }
        return {{"enabled", enabled},
                {"suppressed", suppressed},
                {"suppression_active", Clock::now() < suppressed_until},
                {"captures", captures},
                {"last_capture_particles", last_capture_particles},
                {"pending", prepare_pending || loading},
                {"error", error},
                {"played", played},
                {"skipped", skipped},
                {"warmed", warmed},
                {"warm_models", warm_models.size()},
                {"selection_warm", selection_warm_result},
                {"pool", pool},
                {"last_emit_us", last_emit_us},
                {"worst_emit_us", worst_emit_us}};
    }
#endif
} // namespace kh3ap::character_swap_effects
