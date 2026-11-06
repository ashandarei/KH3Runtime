#include <array>
#include <optional>
#include <Unreal/FWeakObjectPtr.hpp>
#include "kh3ap/features/colors/color_material_ops.h"

#include "kh3ap/features/grants/grant_internal_call_depth.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_validity.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/AActor.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/FScriptArray.hpp>
#include <Unreal/NameTypes.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <Unreal/Property/FStructProperty.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <UnrealDef.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace kh3ap
{
    namespace
    {
        constexpr auto max_mesh_material_candidates = 32;
        constexpr auto max_material_parameter_names = 64;
        constexpr auto animated_mesh_score = 4;
        constexpr auto independent_pose_mesh_score = 4;
        constexpr auto explicit_mesh_property_score = 2;
        constexpr auto mesh_material_slot_score_cap = 8;
        constexpr auto skeletal_mesh_asset_score = 6;
        constexpr auto max_material_name_probe_index = 500000;
        constexpr auto max_material_parameter_entry_bytes = 256;
    } // namespace

    using namespace RC;
    using namespace RC::Unreal;

    namespace
    {
        // MeshComponent rather than SkeletalMeshComponent: the base of both
        // skeletal and static meshes, so a part on its own component is
        // reachable too - Rapunzel's hair is one (TresCharHairMesh1).
        constexpr std::wstring_view mesh_component_class_path = STR("/Script/Engine.MeshComponent");
        constexpr std::wstring_view skeletal_mesh_component_class_path =
            STR("/Script/Engine.SkeletalMeshComponent");
        constexpr std::wstring_view get_component_by_class_function_path =
            STR("/Script/Engine.Actor:GetComponentByClass");
        constexpr std::wstring_view get_num_materials_function_path =
            STR("/Script/Engine.PrimitiveComponent:GetNumMaterials");
        constexpr std::wstring_view get_material_function_path =
            STR("/Script/Engine.PrimitiveComponent:GetMaterial");
        constexpr std::wstring_view set_material_function_path =
            STR("/Script/Engine.PrimitiveComponent:SetMaterial");
        constexpr std::wstring_view create_dynamic_material_instance_function_path =
            STR("/Script/Engine.PrimitiveComponent:CreateDynamicMaterialInstance");
        constexpr std::wstring_view mid_set_vector_parameter_function_path =
            STR("/Script/Engine.MaterialInstanceDynamic:SetVectorParameterValue");
        constexpr std::wstring_view mid_get_vector_parameter_function_path =
            STR("/Script/Engine.MaterialInstanceDynamic:K2_GetVectorParameterValue");
        constexpr std::wstring_view mid_set_scalar_parameter_function_path =
            STR("/Script/Engine.MaterialInstanceDynamic:SetScalarParameterValue");
        constexpr std::wstring_view mid_get_scalar_parameter_function_path =
            STR("/Script/Engine.MaterialInstanceDynamic:K2_GetScalarParameterValue");
        constexpr std::wstring_view set_vector_parameter_on_materials_function_path =
            STR("/Script/Engine.MeshComponent:SetVectorParameterValueOnMaterials");

        // find_function_param_by_name/write_object_function_param only
        // accept ObjectProperty; GetComponentByClass's ComponentClass and
        // CreateSaveGameObject's SaveGameClass are ClassProperty (TSubclassOf).
        [[nodiscard]] auto write_class_function_param(UFunction* function_object, void* params,
                                                      std::wstring_view param_name,
                                                      UObject* class_value) -> bool
        {
            FProperty* const property = find_function_param_by_name(function_object, param_name);
            if (!property)
            {
                return false;
            }
            const std::wstring property_class = property->GetClass().GetName();
            if (property_class != STR("ObjectProperty") && property_class != STR("ClassProperty"))
            {
                return false;
            }
            if (UObject** const value_ptr = property->ContainerPtrToValuePtr<UObject*>(params))
            {
                *value_ptr = class_value;
                return true;
            }
            return false;
        }

        // FMaterialParameterInfo's first field is the parameter's FName, so
        // the first qword of every element of a *ParameterValues array is
        // an FName's (ComparisonIndex, Number) pair. Decoding it needs no
        // knowledge of the rest of the struct's layout (which has changed
        // across engine versions) - low32 doubles as a sanity check, since
        // a genuine FName index is never a huge number, but a float sitting
        // at that offset usually reinterprets as one.
        [[nodiscard]] auto try_decode_fname_qword(uint64_t raw) -> std::optional<std::wstring>
        {
            const uint32_t low32 = static_cast<uint32_t>(raw & 0xffffffffull);
            if (low32 == 0 || low32 >= max_material_name_probe_index)
            {
                return std::nullopt;
            }
            const std::wstring name = FName(low32, 0).ToString();
            return name.empty() ? std::nullopt : std::make_optional(name);
        }

        [[nodiscard]] auto write_vector_function_param(FProperty* property, void* params, float x,
                                                       float y, float z) -> bool
        {
            if (!property || !params || property->GetClass().GetName() != STR("StructProperty"))
            {
                return false;
            }
            void* const value_ptr = property->ContainerPtrToValuePtr<void>(params);
            if (!value_ptr ||
                property->GetElementSize() < static_cast<int>(sizeof(std::array<float, 3>)))
            {
                return false;
            }
            const float vector[3]{x, y, z};
            std::memcpy(value_ptr, vector, sizeof(vector));
            return true;
        }

    } // namespace

    auto resolve_mesh_component_class() -> UClass*
    {
        UClass* const klass = UObjectGlobals::StaticFindObject<UClass*>(
            nullptr, nullptr, mesh_component_class_path.data());
        return (klass && is_uobject_valid_fast(klass)) ? klass : nullptr;
    }

    auto resolve_skeletal_mesh_component_class() -> UClass*
    {
        UClass* const klass = UObjectGlobals::StaticFindObject<UClass*>(
            nullptr, nullptr, skeletal_mesh_component_class_path.data());
        return (klass && is_uobject_valid_fast(klass)) ? klass : nullptr;
    }

    // The two gates that decide whether a component can be handed to
    // CreateDynamicMaterialInstance, which duplicates a material and
    // touches render state - unlike the name-based setters after it, that
    // assumes a registered, render-state-initialized primitive.
    auto MeshCandidate::material_slots() const -> std::optional<int>
    {
        if (!override_material_slots && !asset_material_slots)
        {
            return std::nullopt;
        }
        return std::max(override_material_slots.value_or(0), asset_material_slots.value_or(0));
    }

    // Fails open throughout: an unreadable property yields nullopt and is
    // treated as fine, never as "not ready". Sora already applies correctly
    // through this path, and a wrong guess at a property name must not
    // silently disable her.
    auto MeshCandidate::usable() const -> bool
    {
        return component && registered.value_or(true) && render_state_created.value_or(true) &&
               material_slots().value_or(1) > 0;
    }

    // Guarded reflected reads. The shared try_read_object_*_property
    // helpers reinterpret a property's storage without checking its
    // class first, which is wrong for the pointer-like properties on a
    // skeletal mesh component: MasterPoseComponent is a
    // TWeakObjectPtr (an index/serial pair), not a UObject*.
    [[nodiscard]] auto read_strong_object_property(UObject* object, const wchar_t* name) -> UObject*
    {
        if (!object)
        {
            return nullptr;
        }
        FProperty* const property = object->GetPropertyByNameInChain(name);
        if (!property || property->GetClass().GetName() != STR("ObjectProperty"))
        {
            return nullptr;
        }
        UObject* const* const value = property->ContainerPtrToValuePtr<UObject*>(object);
        UObject* const result = value ? *value : nullptr;
        return is_uobject_valid_fast(result) ? result : nullptr;
    }

    // bRegistered/bRenderStateCreated are uint8:1 bitfields sharing a
    // byte with their neighbours, so they must be read through
    // FBoolProperty's field mask - reading the raw byte reports true
    // whenever any bit in it is set.
    [[nodiscard]] auto read_bool_property(UObject* object, const wchar_t* name)
        -> std::optional<bool>
    {
        if (!object)
        {
            return std::nullopt;
        }
        FProperty* const property = object->GetPropertyByNameInChain(name);
        if (!property || property->GetClass().GetName() != STR("BoolProperty"))
        {
            return std::nullopt;
        }
        return static_cast<FBoolProperty*>(property)->GetPropertyValueInContainer(object);
    }

    // FWeakObjectPtr is (int32 ObjectIndex, int32 ObjectSerialNumber),
    // both zero when unset. Only set/unset matters here, so read the
    // pair directly rather than resolving the weak reference.
    [[nodiscard]] auto read_weak_object_property_is_set(UObject* object, const wchar_t* name)
        -> std::optional<bool>
    {
        if (!object)
        {
            return std::nullopt;
        }
        FProperty* const property = object->GetPropertyByNameInChain(name);
        if (!property || property->GetClass().GetName() != STR("WeakObjectProperty") ||
            property->GetElementSize() < static_cast<int>(sizeof(FWeakObjectPtr)))
        {
            return std::nullopt;
        }
        const int32_t* const value = property->ContainerPtrToValuePtr<int32_t>(object);
        if (!value)
        {
            return std::nullopt;
        }
        return value[0] != 0 || value[1] != 0;
    }

    // Distinguishes "property unreadable" (nullopt) from "array is
    // genuinely empty" (0) - the two must not be conflated, because
    // unreadable must never gate the apply.
    [[nodiscard]] auto read_array_property_num(UObject* object, const wchar_t* name)
        -> std::optional<int>
    {
        if (!object)
        {
            return std::nullopt;
        }
        FProperty* const property = object->GetPropertyByNameInChain(name);
        if (!property || property->GetClass().GetName() != STR("ArrayProperty"))
        {
            return std::nullopt;
        }
        FScriptArray* const array = property->ContainerPtrToValuePtr<FScriptArray>(object);
        if (!array)
        {
            return std::nullopt;
        }
        return std::max(array->Num(), 0);
    }

    // The element reader read_array_property_num deliberately stops
    // short of. Only TArray<UObject*> is accepted: the inner property
    // class is checked rather than assumed, because reinterpreting a
    // struct or weak-pointer array as raw object pointers would hand
    // ProcessEvent garbage.
    [[nodiscard]] auto read_object_array_property(UObject* object, const wchar_t* name)
        -> std::vector<UObject*>
    {
        std::vector<UObject*> result{};
        if (!object)
        {
            return result;
        }
        FProperty* const property = object->GetPropertyByNameInChain(name);
        if (!property || property->GetClass().GetName() != STR("ArrayProperty"))
        {
            return result;
        }
        FProperty* const inner = static_cast<FArrayProperty*>(property)->GetInner();
        if (!inner || inner->GetClass().GetName() != STR("ObjectProperty") ||
            inner->GetElementSize() != static_cast<int>(sizeof(UObject*)))
        {
            return result;
        }
        const FScriptArray* const array = property->ContainerPtrToValuePtr<FScriptArray>(object);
        if (!array)
        {
            return result;
        }
        const int count = array->Num();
        auto* const* const base = static_cast<UObject* const*>(array->GetData());
        if (!base || count <= 0)
        {
            return result;
        }
        // A party member carries a handful of equips at most. The cap
        // is a guard against a garbage Num() on a half-initialised
        // component, not a real limit on the data.
        for (int index = 0; index < std::min(count, max_mesh_material_candidates); ++index)
        {
            UObject* const element = base[index];
            if (element && is_uobject_valid_fast(element))
            {
                result.push_back(element);
            }
        }
        return result;
    }

    [[nodiscard]] auto inspect_mesh_candidate(UObject* component, bool from_mesh_property)
        -> MeshCandidate
    {
        MeshCandidate candidate{};
        candidate.component = component;
        candidate.from_mesh_property = from_mesh_property;
        if (!component || !is_uobject_valid_fast(component))
        {
            return candidate;
        }
        candidate.skeletal_mesh = read_strong_object_property(component, STR("SkeletalMesh"));
        candidate.anim_instance = read_strong_object_property(component, STR("AnimScriptInstance"));
        candidate.registered = read_bool_property(component, STR("bRegistered"));
        candidate.render_state_created = read_bool_property(component, STR("bRenderStateCreated"));
        candidate.master_pose_set =
            read_weak_object_property_is_set(component, STR("MasterPoseComponent"));
        candidate.override_material_slots =
            read_array_property_num(component, STR("OverrideMaterials"));
        candidate.asset_material_slots =
            read_array_property_num(candidate.skeletal_mesh, STR("Materials"));

        int score = 0;
        // The retired anim-notify trigger fired from the component
        // actually playing the idle animation and handed that exact
        // component to ApplyRandomColors - and it worked for
        // Donald/Goofy. An owning AnimInstance is the closest native
        // equivalent of that guarantee.
        if (candidate.anim_instance)
        {
            score += animated_mesh_score;
        }
        // Hats, hair and accessories slave their pose to the body mesh
        // via MasterPoseComponent; the body mesh itself has none.
        if (candidate.master_pose_set.has_value() && !*candidate.master_pose_set)
        {
            score += independent_pose_mesh_score;
        }
        if (from_mesh_property)
        {
            score += explicit_mesh_property_score;
        }
        // The body mesh carries the material slots the colors target;
        // an accessory mesh typically has one.
        score += std::min(candidate.material_slots().value_or(0), mesh_material_slot_score_cap);
        // A component with no mesh asset has nothing to color, but it
        // is not dangerous either - rank it last rather than reject it.
        if (candidate.skeletal_mesh)
        {
            score += skeletal_mesh_asset_score;
        }
        candidate.score = score;
        return candidate;
    }

    auto log_mesh_candidate(std::wstring_view character_key, const MeshCandidate& candidate,
                            std::wstring_view tag) -> void
    {
        const auto tri_state = [](const std::optional<bool>& value) -> std::wstring_view
        {
            if (!value.has_value())
            {
                return STR("unknown");
            }
            return *value ? STR("true") : STR("false");
        };
        const auto count_state = [](const std::optional<int>& value) -> std::wstring
        {
            return value.has_value() ? std::to_wstring(*value) : std::wstring{STR("unknown")};
        };
        Output::send<LogLevel::Normal>(
            STR("[KH3APProbeCpp] Color mesh candidate key={} tag={} component={} class_chain={} "
                "outer={} ") STR("skeletal_mesh={} anim_instance={} registered={} render_state={} "
                                 "master_pose_set={} ")
                STR("override_slots={} asset_slots={} score={} usable={}\n"),
            character_key, tag, format_object_debug_name(candidate.component),
            summarize_object_class_chain(candidate.component),
            format_object_outer_chain(candidate.component),
            format_object_debug_name(candidate.skeletal_mesh),
            format_object_debug_name(candidate.anim_instance), tri_state(candidate.registered),
            tri_state(candidate.render_state_created), tri_state(candidate.master_pose_set),
            count_state(candidate.override_material_slots),
            count_state(candidate.asset_material_slots), candidate.score, candidate.usable());
    }

    // ACharacter's primary skeletal mesh lives in a plain reflected
    // UPROPERTY named "Mesh" - a direct property read, not a
    // BlueprintCallable UFunction (confirmed live: StaticFindObject
    // never found "/Script/Engine.Character:GetMesh" as a UFunction at
    // all, so that attempt silently fell through every time). KH3's
    // TresNpcPawnBase hierarchy does not appear to use that naming
    // (confirmed live: "Mesh property unavailable" for Donald/Goofy),
    // so this is one candidate source among several, not the answer.
    [[nodiscard]] auto try_get_character_mesh(UObject* pawn) -> UObject*
    {
        return read_strong_object_property(pawn, STR("Mesh"));
    }

    // AActor::GetComponentByClass returns whichever match comes first
    // out of OwnedComponents, a TSet whose iteration order is pointer-
    // hash derived - so it is not a stable choice by construction, even
    // though it happens to land on TresCharMesh0 here. Enumerate the
    // full set instead and choose deterministically.
    //
    // This goes through UE4SS's typed AActor wrapper rather than
    // ProcessEvent: the reflected /Script/Engine.Actor:K2_GetComponentsByClass
    // path returned an empty array for every pawn live (component_count=0
    // for Sora, Donald and Goofy alike, while the single-result call on
    // the same pawn and class returned a real component). The one other
    // caller of that reflected path in this codebase has a typed
    // fallback that masks the same behaviour.
    [[nodiscard]] auto collect_mesh_components(UObject* pawn, UClass* mesh_class)
        -> std::vector<UObject*>
    {
        std::vector<UObject*> result{};
        if (!pawn || !mesh_class)
        {
            return result;
        }
        const auto components = [&]()
        {
            const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
            return static_cast<AActor*>(pawn)->K2_GetComponentsByClass(mesh_class);
        }();
        result.reserve(static_cast<size_t>(std::max(components.Num(), 0)));
        for (UObject* const component : components)
        {
            if (component && is_uobject_valid_fast(component))
            {
                result.push_back(component);
            }
        }
        return result;
    }

    // The single-result call the original code used. Still queried on
    // every resolve and folded into the candidate set: it is the one
    // component lookup confirmed to work on these pawns, so it is the
    // floor this function must never fall below.
    [[nodiscard]] auto first_component_by_class(UObject* pawn, UClass* mesh_class) -> UObject*
    {
        UFunction* const function = UObjectGlobals::StaticFindObject<UFunction*>(
            nullptr, nullptr, get_component_by_class_function_path.data());
        if (!function || !is_uobject_valid_fast(function) || function->GetParmsSize() <= 0)
        {
            return nullptr;
        }
        std::vector<std::byte> params(static_cast<size_t>(function->GetParmsSize()), std::byte{0});
        if (!write_class_function_param(function, params.data(), STR("ComponentClass"), mesh_class))
        {
            return nullptr;
        }
        {
            const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
            pawn->ProcessEvent(function, params.data());
        }
        FProperty* const result_property =
            find_function_param_by_name(function, STR("ReturnValue"));
        if (!result_property || result_property->GetClass().GetName() != STR("ObjectProperty"))
        {
            return nullptr;
        }
        UObject* const* const result_ptr =
            result_property->ContainerPtrToValuePtr<UObject*>(params.data());
        UObject* const mesh = result_ptr ? *result_ptr : nullptr;
        return is_uobject_valid_fast(mesh) ? mesh : nullptr;
    }

    [[nodiscard]] auto read_num_materials(UObject* component) -> std::optional<int>
    {
        UFunction* const function = UObjectGlobals::StaticFindObject<UFunction*>(
            nullptr, nullptr, get_num_materials_function_path.data());
        if (!function || !is_uobject_valid_fast(function) || function->GetParmsSize() <= 0)
        {
            return std::nullopt;
        }
        std::vector<std::byte> params(static_cast<size_t>(function->GetParmsSize()), std::byte{0});
        {
            const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
            component->ProcessEvent(function, params.data());
        }
        const std::optional<int64_t> value =
            read_function_int_param(function, params.data(), STR("ReturnValue"));
        if (!value)
        {
            return std::nullopt;
        }
        return static_cast<int>(*value);
    }

    [[nodiscard]] auto read_material_at(UObject* component, int element_index) -> UObject*
    {
        UFunction* const function = UObjectGlobals::StaticFindObject<UFunction*>(
            nullptr, nullptr, get_material_function_path.data());
        if (!function || !is_uobject_valid_fast(function) || function->GetParmsSize() <= 0)
        {
            return nullptr;
        }
        std::vector<std::byte> params(static_cast<size_t>(function->GetParmsSize()), std::byte{0});
        FProperty* const index_property =
            find_function_param_by_name(function, STR("ElementIndex"));
        if (!index_property || !write_integral_function_param(index_property, params.data(),
                                                              static_cast<uint64_t>(element_index)))
        {
            return nullptr;
        }
        {
            const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
            component->ProcessEvent(function, params.data());
        }
        return try_read_function_object_param(function, params.data(), STR("ReturnValue"));
    }

    [[nodiscard]] auto set_material_at(UObject* component, int element_index, UObject* material)
        -> bool
    {
        UFunction* const function = UObjectGlobals::StaticFindObject<UFunction*>(
            nullptr, nullptr, set_material_function_path.data());
        if (!function || !is_uobject_valid_fast(function) || function->GetParmsSize() <= 0)
        {
            return false;
        }
        std::vector<std::byte> params(static_cast<size_t>(function->GetParmsSize()), std::byte{0});
        FProperty* const index_property =
            find_function_param_by_name(function, STR("ElementIndex"));
        if (!index_property ||
            !write_integral_function_param(index_property, params.data(),
                                           static_cast<uint64_t>(element_index)) ||
            !write_object_function_param(function, params.data(), STR("Material"), material))
        {
            return false;
        }
        const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
        component->ProcessEvent(function, params.data());
        return true;
    }

    [[nodiscard]] auto create_dynamic_material_instance(UObject* component, int element_index)
        -> UObject*
    {
        UFunction* const function = UObjectGlobals::StaticFindObject<UFunction*>(
            nullptr, nullptr, create_dynamic_material_instance_function_path.data());
        if (!function || !is_uobject_valid_fast(function) || function->GetParmsSize() <= 0)
        {
            return nullptr;
        }
        std::vector<std::byte> params(static_cast<size_t>(function->GetParmsSize()), std::byte{0});
        FProperty* const index_property =
            find_function_param_by_name(function, STR("ElementIndex"));
        if (!index_property || !write_integral_function_param(index_property, params.data(),
                                                              static_cast<uint64_t>(element_index)))
        {
            return nullptr;
        }
        {
            const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
            component->ProcessEvent(function, params.data());
        }
        return try_read_function_object_param(function, params.data(), STR("ReturnValue"));
    }

    // Drops every dynamic override on a slot by putting the asset it
    // shipped with back. No cache is needed: a slot holding a
    // MaterialInstanceDynamic has the asset it was created from as its
    // Parent, so setting that back restores the slot in one call.
    // true when the slot is left holding the asset it shipped with -
    // including the case where it was never made dynamic in the first
    // place. false means the restore was attempted and did not take,
    // which callers count: a silently failing SetMaterial would leave
    // every slot holding its old overrides while reporting success.
    [[nodiscard]] auto restore_shipped_material(UObject* component, int element_index) -> bool
    {
        UObject* const current = read_material_at(component, element_index);
        if (!current)
        {
            return true;
        }
        UClass* const klass = current->GetClassPrivate();
        if (!klass || klass->GetName() != STR("MaterialInstanceDynamic"))
        {
            return true;
        }
        UObject* const parent = read_strong_object_property(current, STR("Parent"));
        if (!parent)
        {
            return false;
        }
        return set_material_at(component, element_index, parent);
    }

    // Reads back the parameter names a material instance overrides. Used
    // to recognise a head material: KH3's face materials carry
    // "atama"-prefixed parameters (atama_mtr_x, atama_WP - atama is
    // Japanese for head), on Toy Sora and on Sora's own model alike.
    [[nodiscard]] auto material_parameter_names(UObject* material, std::wstring_view array_name)
        -> std::vector<std::wstring>
    {
        std::vector<std::wstring> names{};
        if (!material)
        {
            return names;
        }
        FProperty* const property =
            material->GetPropertyByNameInChain(std::wstring{array_name}.c_str());
        if (!property || property->GetClass().GetName() != STR("ArrayProperty"))
        {
            return names;
        }
        auto* const array_property = static_cast<FArrayProperty*>(property);
        FProperty* const inner = array_property->GetInner();
        const FScriptArray* const array = property->ContainerPtrToValuePtr<FScriptArray>(material);
        if (!inner || !array)
        {
            return names;
        }
        const int count = array->Num();
        const int element_size = inner->GetElementSize();
        const auto* const base = static_cast<const std::byte*>(array->GetData());
        if (!base || count <= 0 || element_size <= 0 ||
            element_size > max_material_parameter_entry_bytes)
        {
            return names;
        }
        for (int index = 0; index < std::min(count, max_material_parameter_names); ++index)
        {
            uint64_t first_qword = 0;
            std::memcpy(&first_qword,
                        base + (static_cast<size_t>(index) * static_cast<size_t>(element_size)),
                        std::min(static_cast<size_t>(element_size), sizeof(uint64_t)));
            if (const std::optional<std::wstring> name = try_decode_fname_qword(first_qword))
            {
                names.push_back(*name);
            }
        }
        return names;
    }

    [[nodiscard]] auto material_is_head(UObject* material) -> bool
    {
        for (const std::wstring& name : material_parameter_names(material))
        {
            if (name.starts_with(STR("atama")))
            {
                return true;
            }
        }
        return false;
    }

    // MeshComponent::SetVectorParameterValueOnMaterials(FName, FVector)
    auto set_vector_parameter_on_materials(UObject* component, std::wstring_view parameter_name,
                                           const DerivedColor& color,
                                           std::wstring_view character_key) -> void
    {
        UFunction* const function = UObjectGlobals::StaticFindObject<UFunction*>(
            nullptr, nullptr, set_vector_parameter_on_materials_function_path.data());
        if (!function || !is_uobject_valid_fast(function) || function->GetParmsSize() <= 0)
        {
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] Color materials setter unresolved key={} param={}\n"),
                character_key, parameter_name);
            return;
        }
        std::vector<std::byte> params(static_cast<size_t>(function->GetParmsSize()), std::byte{0});
        FProperty* const name_property =
            find_function_param_by_name(function, STR("ParameterName"));
        FProperty* const value_property =
            find_function_param_by_name(function, STR("ParameterValue"));
        if (!name_property ||
            !write_name_or_string_function_param(name_property, params.data(), parameter_name) ||
            !value_property ||
            !write_vector_function_param(value_property, params.data(), color.r, color.g, color.b))
        {
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] Color materials setter bind failed key={} param={}\n"),
                character_key, parameter_name);
            return;
        }
        const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
        component->ProcessEvent(function, params.data());
    }

    // MaterialInstanceDynamic::SetVectorParameterValue(FName, FLinearColor)
    // `alpha` defaults to 1.0, which is what a colour wants and what
    // every caller but one passes. The exception is the metallic
    // relaxation, which writes back a packed (metallic, specular,
    // roughness) vector whose fourth channel the shipped assets leave
    // at 0 - carrying it through keeps that write to the one channel it
    // means to change.
    auto set_vector_parameter_on_mid(UObject* material_instance, std::wstring_view parameter_name,
                                     const DerivedColor& color, std::wstring_view character_key,
                                     float alpha) -> void
    {
        UFunction* const function = UObjectGlobals::StaticFindObject<UFunction*>(
            nullptr, nullptr, mid_set_vector_parameter_function_path.data());
        if (!function || !is_uobject_valid_fast(function) || function->GetParmsSize() <= 0)
        {
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] Color MID setter unresolved key={} param={}\n"), character_key,
                parameter_name);
            return;
        }
        std::vector<std::byte> params(static_cast<size_t>(function->GetParmsSize()), std::byte{0});
        FProperty* const name_property =
            find_function_param_by_name(function, STR("ParameterName"));
        FProperty* const value_property = find_function_param_by_name(function, STR("Value"));
        if (!name_property ||
            !write_name_or_string_function_param(name_property, params.data(), parameter_name) ||
            !value_property ||
            !write_linear_color_function_param(value_property, params.data(), color.r, color.g,
                                               color.b, alpha))
        {
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] Color MID setter bind failed key={} param={}\n"),
                character_key, parameter_name);
            return;
        }
        const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
        material_instance->ProcessEvent(function, params.data());
    }

    // MaterialInstanceDynamic::K2_GetVectorParameterValue(FName) ->
    // FLinearColor. The read half of the MID setter, and the only
    // trustworthy way to get a parameter's VALUE back out.
    //
    // material_parameter_names deliberately decodes only the FName at
    // the head of each VectorParameterValues element, because the rest
    // of FVectorParameterValue's layout has moved across engine
    // versions. Going through the reflected getter sidesteps the layout
    // question entirely.
    //
    // A parameter the material does not expose reads back as zero
    // rather than failing, which callers here rely on: it is
    // indistinguishable from a zone that is genuinely non-metallic, and
    // both mean "leave it alone".
    [[nodiscard]] auto read_vector_parameter_from_mid(UObject* material_instance,
                                                      std::wstring_view parameter_name)
        -> std::optional<std::array<float, 4>>
    {
        UFunction* const function = UObjectGlobals::StaticFindObject<UFunction*>(
            nullptr, nullptr, mid_get_vector_parameter_function_path.data());
        if (!function || !is_uobject_valid_fast(function) || function->GetParmsSize() <= 0)
        {
            return std::nullopt;
        }
        std::vector<std::byte> params(static_cast<size_t>(function->GetParmsSize()), std::byte{0});
        FProperty* const name_property =
            find_function_param_by_name(function, STR("ParameterName"));
        if (!name_property ||
            !write_name_or_string_function_param(name_property, params.data(), parameter_name))
        {
            return std::nullopt;
        }
        {
            const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
            material_instance->ProcessEvent(function, params.data());
        }
        FProperty* const result = find_function_param_by_name(function, STR("ReturnValue"));
        if (!result || result->GetClass().GetName() != STR("StructProperty") ||
            result->GetElementSize() < static_cast<int>(sizeof(std::array<float, 4>)))
        {
            return std::nullopt;
        }
        const float* const value = result->ContainerPtrToValuePtr<float>(params.data());
        if (!value)
        {
            return std::nullopt;
        }
        return std::array<float, 4>{value[0], value[1], value[2], value[3]};
    }

    auto set_scalar_parameter_on_mid(UObject* material_instance, std::wstring_view parameter_name,
                                     float value, std::wstring_view key) -> void
    {
        UFunction* const function = UObjectGlobals::StaticFindObject<UFunction*>(
            nullptr, nullptr, mid_set_scalar_parameter_function_path.data());
        if (!function || !is_uobject_valid_fast(function) || function->GetParmsSize() <= 0)
        {
            return;
        }
        std::vector<std::byte> params(static_cast<size_t>(function->GetParmsSize()), std::byte{0});
        FProperty* const name_property =
            find_function_param_by_name(function, STR("ParameterName"));
        FProperty* const value_property = find_function_param_by_name(function, STR("Value"));
        if (!name_property ||
            !write_name_or_string_function_param(name_property, params.data(), parameter_name) ||
            !value_property || !write_float_function_param(value_property, params.data(), value))
        {
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] Color MID scalar setter bind failed key={} param={}\n"), key,
                parameter_name);
            return;
        }
        const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
        material_instance->ProcessEvent(function, params.data());
    }

    [[nodiscard]] auto read_scalar_parameter_from_mid(UObject* material_instance,
                                                      std::wstring_view parameter_name)
        -> std::optional<float>
    {
        UFunction* const function = UObjectGlobals::StaticFindObject<UFunction*>(
            nullptr, nullptr, mid_get_scalar_parameter_function_path.data());
        if (!function || !is_uobject_valid_fast(function) || function->GetParmsSize() <= 0)
        {
            return std::nullopt;
        }
        std::vector<std::byte> params(static_cast<size_t>(function->GetParmsSize()), std::byte{0});
        FProperty* const name_property =
            find_function_param_by_name(function, STR("ParameterName"));
        if (!name_property ||
            !write_name_or_string_function_param(name_property, params.data(), parameter_name))
        {
            return std::nullopt;
        }
        {
            const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
            material_instance->ProcessEvent(function, params.data());
        }
        FProperty* const result = find_function_param_by_name(function, STR("ReturnValue"));
        if (!result || result->GetClass().GetName() != STR("FloatProperty"))
        {
            return std::nullopt;
        }
        return *result->ContainerPtrToValuePtr<float>(params.data());
    }
} // namespace kh3ap
