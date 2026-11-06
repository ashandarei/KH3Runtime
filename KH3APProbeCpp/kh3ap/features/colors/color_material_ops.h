#pragma once

#include "kh3ap/features/colors/color_palette.h"

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace RC::Unreal
{
    class FProperty;
    class UClass;
    class UObject;
}

namespace kh3ap
{
    // The reflected-call layer the colour feature sits on: reading
    // properties off pawns and components, finding a pawn's meshes, and
    // getting colours into and out of materials.
    //
    // Shared by the character and weapon paths deliberately. Neither knows
    // anything about the other, but both need exactly these operations,
    // and every one of them encodes something learned the hard way about
    // how this game answers reflection - which property classes it lies
    // about, which lookups return empty, which calls assume a registered
    // primitive. Duplicating any of that would mean learning it twice.

    // MeshComponent is the base of both skeletal and static meshes, so a
    // part on its own component is reachable too - Rapunzel's hair is one.
    // Null if the class cannot be resolved, which callers treat as "do
    // nothing" rather than as an error.
    [[nodiscard]] auto resolve_mesh_component_class() -> RC::Unreal::UClass*;
    [[nodiscard]] auto resolve_skeletal_mesh_component_class() -> RC::Unreal::UClass*;

    // --- property reads -------------------------------------------------
    // Each checks the property's class before reinterpreting its storage.
    // An unreadable property yields null/nullopt, never a wrong value:
    // callers gate on these, and a wrong guess at a property name must
    // fail open rather than silently disable a character.

    [[nodiscard]] auto read_strong_object_property(
            RC::Unreal::UObject* object, const wchar_t* name) -> RC::Unreal::UObject*;
    [[nodiscard]] auto read_bool_property(
            RC::Unreal::UObject* object, const wchar_t* name) -> std::optional<bool>;
    [[nodiscard]] auto read_weak_object_property_is_set(
            RC::Unreal::UObject* object, const wchar_t* name) -> std::optional<bool>;
    // nullopt means "unreadable", 0 means "genuinely empty" - the two must
    // not be conflated, because unreadable must never gate an apply.
    [[nodiscard]] auto read_array_property_num(
            RC::Unreal::UObject* object, const wchar_t* name) -> std::optional<int>;
    // TArray<UObject*> only; the inner property class is verified.
    [[nodiscard]] auto read_object_array_property(
            RC::Unreal::UObject* object, const wchar_t* name) -> std::vector<RC::Unreal::UObject*>;

    // --- mesh components ------------------------------------------------

    // Everything known about one mesh component on an actor, and whether
    // it is safe to hand to the material calls below.
    struct MeshCandidate
    {
        RC::Unreal::UObject* component{};
        RC::Unreal::UObject* skeletal_mesh{};
        RC::Unreal::UObject* anim_instance{};
        std::optional<bool> registered{};
        std::optional<bool> render_state_created{};
        std::optional<bool> master_pose_set{};
        std::optional<int> override_material_slots{};
        std::optional<int> asset_material_slots{};
        bool from_mesh_property{};
        int score{};

        [[nodiscard]] auto material_slots() const -> std::optional<int>;
        // Every gate fails open: an unreadable property is treated as fine,
        // never as "not ready".
        [[nodiscard]] auto usable() const -> bool;
    };

    [[nodiscard]] auto inspect_mesh_candidate(
            RC::Unreal::UObject* component, bool from_mesh_property) -> MeshCandidate;
    auto log_mesh_candidate(
            std::wstring_view key, const MeshCandidate& candidate, std::wstring_view tag) -> void;

    // ACharacter's "Mesh" property. KH3's pawn hierarchy does not appear to
    // use that naming, so this is one candidate source among several.
    [[nodiscard]] auto try_get_character_mesh(RC::Unreal::UObject* pawn) -> RC::Unreal::UObject*;
    // Enumerates the actor's components rather than taking whichever match
    // comes first out of a pointer-hash-ordered set.
    [[nodiscard]] auto collect_mesh_components(
            RC::Unreal::UObject* actor, RC::Unreal::UClass* mesh_class) -> std::vector<RC::Unreal::UObject*>;
    // The single-result lookup. Still queried alongside the enumeration:
    // it is the one component lookup confirmed to work on these pawns, so
    // it is the floor the search must never fall below.
    [[nodiscard]] auto first_component_by_class(
            RC::Unreal::UObject* actor, RC::Unreal::UClass* mesh_class) -> RC::Unreal::UObject*;

    // --- materials ------------------------------------------------------

    [[nodiscard]] auto read_num_materials(RC::Unreal::UObject* component) -> std::optional<int>;
    [[nodiscard]] auto read_material_at(
            RC::Unreal::UObject* component, int element_index) -> RC::Unreal::UObject*;
    [[nodiscard]] auto set_material_at(
            RC::Unreal::UObject* component, int element_index, RC::Unreal::UObject* material) -> bool;
    [[nodiscard]] auto create_dynamic_material_instance(
            RC::Unreal::UObject* component, int element_index) -> RC::Unreal::UObject*;
    // Drops every dynamic override on a slot by putting back the asset it
    // shipped with. True also when the slot was never made dynamic; false
    // means the restore was attempted and did not take.
    [[nodiscard]] auto restore_shipped_material(RC::Unreal::UObject* component, int element_index) -> bool;

    // The parameter names a material overrides. Only the FName at the head
    // of each entry is decoded, because the rest of the struct's layout has
    // moved across engine versions.
    // `array_name` picks the parameter kind: VectorParameterValues by
    // default, ScalarParameterValues or TextureParameterValues otherwise.
    [[nodiscard]] auto material_parameter_names(
            RC::Unreal::UObject* material,
            std::wstring_view array_name = L"VectorParameterValues") -> std::vector<std::wstring>;
    // KH3's face materials carry "atama"-prefixed parameters (atama = head).
    [[nodiscard]] auto material_is_head(RC::Unreal::UObject* material) -> bool;

    // MeshComponent::SetVectorParameterValueOnMaterials - writes every slot
    // at once by name. A name the materials do not expose is a no-op, which
    // is what lets one table cover many shader families.
    auto set_vector_parameter_on_materials(
            RC::Unreal::UObject* component,
            std::wstring_view parameter_name,
            const DerivedColor& color,
            std::wstring_view key) -> void;
    // MaterialInstanceDynamic::SetVectorParameterValue - one slot only, and
    // it wins over the by-name pass above. `alpha` defaults to 1.0, which is
    // what a colour wants; pass it through only when writing back a packed
    // vector whose fourth channel means something.
    auto set_vector_parameter_on_mid(
            RC::Unreal::UObject* material_instance,
            std::wstring_view parameter_name,
            const DerivedColor& color,
            std::wstring_view key,
            float alpha = 1.0f) -> void;
    // The read half. A parameter the material does not expose reads back as
    // zero rather than failing.
    [[nodiscard]] auto read_vector_parameter_from_mid(
            RC::Unreal::UObject* material_instance,
            std::wstring_view parameter_name) -> std::optional<std::array<float, 4>>;
    // The scalar pair of the two above.
    auto set_scalar_parameter_on_mid(
            RC::Unreal::UObject* material_instance,
            std::wstring_view parameter_name,
            float value,
            std::wstring_view key) -> void;
    [[nodiscard]] auto read_scalar_parameter_from_mid(
            RC::Unreal::UObject* material_instance,
            std::wstring_view parameter_name) -> std::optional<float>;
}
