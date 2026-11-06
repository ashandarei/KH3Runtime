#include "kh02_darkside_pillar_repair.h"
#include "kh02_darkside_pillar_policy.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include <Unreal/FProperty.hpp>
#include <Unreal/NameTypes.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <Unreal/Property/FStructProperty.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UScriptStruct.hpp>
#include <Unreal/UObject.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>

namespace kh3ap
{
    using namespace RC::Unreal;

    namespace
    {
        constexpr auto forest_path = L"/Game/Maps/APKH02Mirrors/APKH02Forest.APKH02Forest";
        constexpr auto pillar_class_path =
            L"/Game/Zlueprints/Enemy/e_dw407b/e_dw407b_BigRoarPillarLight.e_dw407b_BigRoarPillarLight_C";
        constexpr auto actor_prefix = L"/Game/Zevels/dw/dw_08/umap/dw_08_enemy.dw_08_enemy";
        constexpr auto beam_path = L"/Game/Zffects/enemy/e_dw407/par/ve_dw407_lsr_bem0.ve_dw407_lsr_bem0";
        constexpr auto mark_path = L"/Game/Zffects/enemy/e_dw407/par/ve_dw407_lse_ato0.ve_dw407_lse_ato0";
        constexpr std::size_t maximum_pillars = 256;
        constexpr std::size_t maximum_modules = 128;
        constexpr std::size_t relative_location_parameter_bytes = 512;

        auto exact_class(UObject* object, const wchar_t* expected) -> bool
        {
            auto* type = is_uobject_valid_fast(object) ? object->GetClassPrivate() : nullptr;
            return is_uobject_valid_fast(type) && type->GetPathName() == expected;
        }

        auto boolean(UObject* object, const wchar_t* name) -> FBoolProperty*
        {
            auto* field = is_uobject_valid_fast(object) ? object->GetPropertyByNameInChain(name) : nullptr;
            if (!field || field->GetClass().GetName() != L"BoolProperty" || field->GetElementSize() != 1)
            {
                throw std::runtime_error("kh02_pillar_bool_layout");
            }
            return static_cast<FBoolProperty*>(field);
        }

        auto objects(UObject* object, const wchar_t* name) -> std::vector<UObject*>
        {
            auto* field = object->GetPropertyByNameInChain(name);
            if (!field || field->GetClass().GetName() != L"ArrayProperty")
            {
                throw std::runtime_error("kh02_pillar_module_array");
            }
            auto* inner = static_cast<FArrayProperty*>(field)->GetInner();

            struct Array
            {
                UObject** data;
                std::int32_t count, capacity;
            };

            if (!inner || inner->GetClass().GetName() != L"ObjectProperty" ||
                inner->GetElementSize() != sizeof(UObject*) || field->GetElementSize() != sizeof(Array))
            {
                throw std::runtime_error("kh02_pillar_module_array_element");
            }
            auto* values = field->ContainerPtrToValuePtr<Array>(object);
            if (values->count < 0 || static_cast<std::size_t>(values->count) > maximum_modules ||
                values->capacity < values->count || (values->count && !values->data))
            {
                throw std::runtime_error("kh02_pillar_module_array_bounds");
            }
            std::vector<UObject*> result;
            for (std::int32_t index = 0; index < values->count; ++index)
            {
                auto* item = values->data[index];
                if (!is_uobject_valid_fast(item))
                {
                    throw std::runtime_error("kh02_pillar_module_stale");
                }
                result.push_back(item);
            }
            return result;
        }

        auto vector(UObject* object, const wchar_t* name) -> std::array<float, 3>
        {
            auto* field = object->GetPropertyByNameInChain(name);
            if (!field || field->GetClass().GetName() != L"StructProperty" ||
                field->GetElementSize() != sizeof(float) * 3 ||
                static_cast<FStructProperty*>(field)->GetStruct()->GetName() != L"Vector")
            {
                throw std::runtime_error("kh02_pillar_vector_layout");
            }
            std::array<float, 3> value{};
            std::memcpy(value.data(), field->ContainerPtrToValuePtr<void>(object), sizeof(value));
            return value;
        }

        auto shaft_size_module(UObject*& native_mesh) -> UObject*
        {
            auto* effect = find_loaded_runtime_asset(beam_path);
            if (!is_uobject_valid_fast(effect))
            {
                return nullptr;
            }
            if (!exact_class(effect, L"/Script/Engine.ParticleSystem"))
            {
                throw std::runtime_error("kh02_pillar_size_effect_class");
            }
            UObject* found = nullptr;
            for (auto* emitter : objects(effect, L"Emitters"))
            {
                if (!exact_class(emitter, L"/Script/Engine.ParticleSpriteEmitter") ||
                    emitter->GetOuterPrivate() != effect)
                {
                    continue;
                }
                auto* role = emitter->GetPropertyByNameInChain(L"EmitterName");
                if (!role || role->GetClass().GetName() != L"NameProperty" || role->GetElementSize() != sizeof(FName))
                {
                    throw std::runtime_error("kh02_pillar_size_emitter_role");
                }
                if (role->ContainerPtrToValuePtr<FName>(emitter)->ToString() != L"bem")
                {
                    continue;
                }
                const auto levels = objects(emitter, L"LODLevels");
                if (levels.size() != 1 || !exact_class(levels.front(), L"/Script/Engine.ParticleLODLevel") ||
                    levels.front()->GetOuterPrivate() != emitter)
                {
                    throw std::runtime_error("kh02_pillar_size_shaft_lod");
                }
                auto* mesh_type = try_read_reflected_object_property_value(levels.front(), L"TypeDataModule");
                if (!exact_class(mesh_type, L"/Script/Engine.ParticleModuleTypeDataMesh") ||
                    mesh_type->GetOuterPrivate() != effect)
                {
                    throw std::runtime_error("kh02_pillar_size_mesh_module");
                }
                native_mesh = try_read_reflected_object_property_value(mesh_type, L"Mesh");
                for (auto* module : objects(levels.front(), L"Modules"))
                {
                    if (exact_class(module, L"/Script/Engine.ParticleModuleSize"))
                    {
                        if (found || module->GetOuterPrivate() != effect ||
                            module->GetName() != L"ParticleModuleSize_24")
                        {
                            throw std::runtime_error("kh02_pillar_unique_shaft_size");
                        }
                        found = module;
                    }
                }
            }
            if (!found)
            {
                throw std::runtime_error("kh02_pillar_shaft_size_missing");
            }
            return found;
        }
    } // namespace

    auto kh02_darkside_pillar_control_transform() -> UObject*
    {
        auto* effect = find_loaded_runtime_asset(beam_path);
        if (!is_uobject_valid_fast(effect))
        {
            return nullptr;
        }
        if (!exact_class(effect, L"/Script/Engine.ParticleSystem"))
        {
            throw std::runtime_error("kh02_pillar_effect_class");
        }
        UObject* found = nullptr;
        for (auto* emitter : objects(effect, L"Emitters"))
        {
            if (!exact_class(emitter, L"/Script/Engine.ParticleSpriteEmitter") || emitter->GetOuterPrivate() != effect)
            {
                continue;
            }
            auto* role = emitter->GetPropertyByNameInChain(L"EmitterName");
            if (!role || role->GetClass().GetName() != L"NameProperty" || role->GetElementSize() != sizeof(FName))
            {
                throw std::runtime_error("kh02_pillar_emitter_role");
            }
            if (role->ContainerPtrToValuePtr<FName>(emitter)->ToString() != L"Control_lp")
            {
                continue;
            }
            const auto levels = objects(emitter, L"LODLevels");
            if (levels.size() != 1 || !exact_class(levels.front(), L"/Script/Engine.ParticleLODLevel") ||
                levels.front()->GetOuterPrivate() != emitter)
            {
                throw std::runtime_error("kh02_pillar_control_lod");
            }
            for (auto* module : objects(levels.front(), L"Modules"))
            {
                if (exact_class(module, L"/Script/Engine.ParticleModuleSQEX_EmitterTransforms"))
                {
                    if (found || module->GetOuterPrivate() != effect)
                    {
                        throw std::runtime_error("kh02_pillar_unique_control_transform");
                    }
                    found = module;
                }
            }
        }
        if (!found)
        {
            throw std::runtime_error("kh02_pillar_control_transform_missing");
        }
        return found;
    }

    auto kh02_repair_darkside_pillar_origin(UObject* actor, UObject* world, const wchar_t* component_field)
        -> std::optional<bool>
    {
        const std::wstring_view field_name = component_field;
        if (field_name != L"m_PillarLightEffCmp" && field_name != L"m_LightMarkEffCmp")
        {
            throw std::runtime_error("kh02_pillar_component_role");
        }
        if (!is_uobject_valid_fast(world) || world->GetPathName() != forest_path ||
            !exact_class(actor, pillar_class_path) || !actor->GetPathName().starts_with(actor_prefix) ||
            kh02_actor_world(actor) != world)
        {
            return std::nullopt;
        }
        auto* component = try_read_reflected_object_property_value(actor, component_field);
        if (!is_uobject_valid_fast(component))
        {
            return std::nullopt;
        }
        auto* root = try_read_reflected_object_property_value(actor, L"RootComponent");
        auto* effect = try_read_reflected_object_property_value(component, L"Template");
        const auto expected_effect = field_name == L"m_PillarLightEffCmp" ? beam_path : mark_path;
        if (!exact_class(component, L"/Script/Engine.ParticleSystemComponent") ||
            component->GetOuterPrivate() != actor || !is_uobject_valid_fast(root) || root->GetOuterPrivate() != actor ||
            !is_uobject_valid_fast(effect) || effect->GetPathName() != expected_effect ||
            try_read_reflected_object_property_value(component, L"AttachParent") != root)
        {
            return false;
        }
        const auto relative = vector(component, L"RelativeLocation");
        Kh02ReflectedCall location(root, L"K2_GetComponentLocation", "kh02_pillar_parent_location", 64);
        location.run();
        const auto root_world = location.result_vector();
        if (!kh02_pillar_origin_is_duplicated(
                relative, root_world, boolean(component, L"bAbsoluteLocation")->GetPropertyValueInContainer(component)))
        {
            return false;
        }
        Kh02ReflectedCall move(component, L"K2_SetRelativeLocation", "kh02_pillar_origin",
                               relative_location_parameter_bytes);
        if (move.function->GetOuterPrivate()->GetPathName() != L"/Script/Engine.SceneComponent")
        {
            throw std::runtime_error("kh02_pillar_origin_setter_owner");
        }
        move.vector(L"NewLocation", 0, 0, 0);
        move.boolean(L"bSweep", false);
        move.boolean(L"bTeleport", false);
        move.run();
        const auto after = vector(component, L"RelativeLocation");
        if (std::any_of(after.begin(), after.end(), [](float value)
                        { return !std::isfinite(value) || std::abs(value) > kh02_pillar_origin_tolerance; }))
        {
            throw std::runtime_error("kh02_pillar_origin_readback");
        }
        return true;
    }

    Kh02DarksidePillarRepair::~Kh02DarksidePillarRepair()
    {
        reset();
    }

    void Kh02DarksidePillarRepair::watch(UObject* actor)
    {
        static const FName pillar_name{L"e_dw407b_BigRoarPillarLight_C", FNAME_Add};
        auto* type = is_uobject_valid_fast(actor) ? actor->GetClassPrivate() : nullptr;
        if (!is_uobject_valid_fast(type) || type->GetNamePrivate() != pillar_name)
        {
            return;
        }
        if (!exact_class(actor, pillar_class_path) || !actor->GetPathName().starts_with(actor_prefix))
        {
            return;
        }
        auto* world = kh02_actor_world(actor);
        if (!is_uobject_valid_fast(world) || world->GetPathName() != forest_path)
        {
            return;
        }
        if (source_world.Get() != world)
        {
            reset();
            source_world = make_uobject_weak(world);
        }
        std::erase_if(pillars, [](const Pillar& tracked) { return !is_uobject_valid_fast(tracked.actor.Get()); });
        if (source_world.Get() != world || pillars.size() >= maximum_pillars ||
            std::any_of(pillars.begin(), pillars.end(),
                        [actor](const Pillar& tracked) { return tracked.actor.Get() == actor; }))
        {
            return;
        }
        auto weak = make_uobject_weak(actor);
        if (weak.Get() == actor)
        {
            pillars.push_back({weak});
            ++observed;
        }
    }

    void Kh02DarksidePillarRepair::tick(UObject* world)
    {
        if (!is_uobject_valid_fast(source_world.Get()))
        {
            reset();
            return;
        }
        if (!is_uobject_valid_fast(world) || source_world.Get() != world || world->GetPathName() != forest_path)
        {
            reset();
            return;
        }
        if (parent_prepared && !is_uobject_valid_fast(control_transform.Get()))
        {
            parent_prepared = false;
            prepare_failed = false;
        }
        if (!parent_prepared && !prepare_failed)
        {
            try
            {
                if (auto* module = kh02_darkside_pillar_control_transform())
                {
                    control_transform = make_uobject_weak(module);
                    if (control_transform.Get() != module)
                    {
                        throw std::runtime_error("kh02_pillar_control_weak");
                    }
                    auto* enabled = boolean(module, L"bEnabled");
                    original_enabled = enabled->GetPropertyValueInContainer(module);
                    enabled->SetPropertyValueInContainer(module, false);
                    if (enabled->GetPropertyValueInContainer(module))
                    {
                        throw std::runtime_error("kh02_pillar_control_readback");
                    }
                    parent_prepared = true;
                }
            }
            catch (const std::exception& failure)
            {
                prepare_failed = true;
                error = failure.what();
            }
        }
        if (!shaft_size.ready() && !prepare_failed)
        {
            try
            {
                UObject* native_mesh = nullptr;
                if (auto* module = shaft_size_module(native_mesh))
                {
                    shaft_size.prepare(module, native_mesh);
                }
            }
            catch (const std::exception& failure)
            {
                prepare_failed = true;
                error = failure.what();
            }
        }
        for (auto& tracked : pillars)
        {
            auto* actor = tracked.actor.Get();
            if (!is_uobject_valid_fast(actor))
            {
                continue;
            }
            try
            {
                for (auto [field, done] : {std::pair{L"m_PillarLightEffCmp", &tracked.beam_done},
                                           std::pair{L"m_LightMarkEffCmp", &tracked.mark_done}})
                {
                    if (!*done)
                    {
                        if (auto outcome = kh02_repair_darkside_pillar_origin(actor, world, field))
                        {
                            *done = true;
                            repaired += *outcome ? 1 : 0;
                        }
                    }
                }
            }
            catch (const std::exception& failure)
            {
                tracked.beam_done = true;
                tracked.mark_done = true;
                error = failure.what();
            }
        }
        std::erase_if(
            pillars, [](const Pillar& tracked)
            { return (tracked.beam_done && tracked.mark_done) || !is_uobject_valid_fast(tracked.actor.Get()); });
    }

    void Kh02DarksidePillarRepair::reset() noexcept
    {
        try
        {
            if (auto* module = control_transform.Get(); parent_prepared && is_uobject_valid_fast(module))
            {
                auto* enabled = boolean(module, L"bEnabled");
                if (!enabled->GetPropertyValueInContainer(module))
                {
                    enabled->SetPropertyValueInContainer(module, original_enabled);
                }
            }
        }
        catch (...)
        {
        }
        shaft_size.reset();
        pillars.clear();
        source_world = nullptr;
        control_transform = nullptr;
        parent_prepared = false;
        prepare_failed = false;
    }

    auto Kh02DarksidePillarRepair::needs_frame() const -> bool
    {
        return !pillars.empty();
    }

    auto Kh02DarksidePillarRepair::snapshot() const -> nlohmann::json
    {
        return {{"tracked", pillars.size()},
                {"observed", observed},
                {"origins_repaired", repaired},
                {"parent_prepared", parent_prepared},
                {"shaft_size_prepared", shaft_size.ready()},
                {"error", error}};
    }
} // namespace kh3ap
