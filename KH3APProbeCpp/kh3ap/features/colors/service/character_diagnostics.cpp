#include "kh3ap/features/colors/service/character.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/features/colors/color_material_ops.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/UScriptStruct.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UObject.hpp>
#include <UnrealDef.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <vector>

namespace kh3ap::color_service_detail
{
    using namespace RC;
    using namespace RC::Unreal;

#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    namespace
    {
        constexpr auto max_material_parent_depth = 8;
        constexpr auto max_diagnostic_material_slots = 12;
        // Diagnostic builds only: every parameter a master material declares,
        // with its default. UE 4.17 cooks UMaterial::Expressions (it sits
        // outside WITH_EDITORONLY_DATA), so the full list is readable at
        // runtime - not only the handful an instance overrides, which is all
        // material_parameter_names can see. Logged once per master.
        auto log_master_parameters(UObject* material) -> void
        {
            static std::vector<std::wstring> logged{};
            UObject* master = material;
            for (int depth = 0; master && depth < max_material_parent_depth &&
                                master->GetClassPrivate()->GetName() != STR("Material");
                 ++depth)
            {
                master = read_strong_object_property(master, STR("Parent"));
            }
            if (!master || !is_uobject_valid_fast(master))
            {
                return;
            }
            const std::wstring master_name = master->GetName();
            if (std::find(logged.begin(), logged.end(), master_name) != logged.end())
            {
                return;
            }
            logged.push_back(master_name);
            FProperty* const expressions = master->GetPropertyByNameInChain(STR("Expressions"));
            if (!expressions || expressions->GetClass().GetName() != STR("ArrayProperty"))
            {
                return;
            }
            const auto* const array = expressions->ContainerPtrToValuePtr<FScriptArray>(master);
            UObject* const* const items =
                array ? static_cast<UObject* const*>(array->GetData()) : nullptr;
            std::wstring vectors{};
            std::wstring scalars{};
            for (int index = 0; items && index < array->Num(); ++index)
            {
                UObject* const expression = items[index];
                if (!expression || !is_uobject_valid_fast(expression))
                {
                    continue;
                }
                const std::wstring type = expression->GetClassPrivate()->GetName();
                const bool vector = type == STR("MaterialExpressionVectorParameter");
                const bool scalar = type == STR("MaterialExpressionScalarParameter");
                if (!vector && !scalar)
                {
                    continue;
                }
                FProperty* const name = expression->GetPropertyByNameInChain(STR("ParameterName"));
                FProperty* const value = expression->GetPropertyByNameInChain(STR("DefaultValue"));
                if (!name || !value)
                {
                    continue;
                }
                const std::wstring parameter =
                    name->ContainerPtrToValuePtr<FName>(expression)->ToString();
                const float* const numbers = value->ContainerPtrToValuePtr<float>(expression);
                if (vector)
                {
                    vectors += std::format(STR("{}=({:.2f},{:.2f},{:.2f}) "), parameter, numbers[0],
                                           numbers[1], numbers[2]);
                }
                else
                {
                    scalars += std::format(STR("{}={:.2f} "), parameter, numbers[0]);
                }
            }
            Output::send<LogLevel::Normal>(
                STR("[KH3APProbeCpp] Color master material={} vectors={} scalars={}\n"),
                master_name, vectors, scalars);
        }
    } // namespace
#endif

    // Diagnostic builds only: what a Link's materials actually hold, read
    // back through the dynamic instance getter, with the component's
    // visibility. Tells "the write did not land" from "the game wrote
    // over it" from "the visible Link is some other mesh".
    auto log_link_readback(UObject* pawn, std::wstring_view character_key, std::wstring_view tag)
        -> void
    {
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        constexpr std::array<std::wstring_view, 8> probes{
            STR("ShortFurColorMulti"), STR("DeepskinColorMulti"), STR("SkinSSMulti"),
            STR("HairColor"),          STR("G_Color_Multi"),      STR("FireBody2Color"),
            STR("TategamiColor"),      STR("NunoColorMulti"),
        };
        UClass* const mesh_class = resolve_mesh_component_class();
        if (!pawn || !is_uobject_valid_fast(pawn) || !mesh_class)
        {
            return;
        }
        for (UObject* const component : collect_mesh_components(pawn, mesh_class))
        {
            const int slots =
                std::min(read_num_materials(component).value_or(0), max_diagnostic_material_slots);
            Output::send<LogLevel::Normal>(
                STR("[KH3APProbeCpp] Color link readback key={} tag={} component={} visible={} "
                    "hidden_in_game={} slots={}\n"),
                character_key, tag, component->GetName(),
                read_bool_property(component, STR("bVisible")).value_or(false),
                read_bool_property(component, STR("bHiddenInGame")).value_or(false), slots);
            for (int slot = 0; slot < slots; ++slot)
            {
                UObject* const material = read_material_at(component, slot);
                std::wstring values{};
                if (material &&
                    material->GetClassPrivate()->GetName() == STR("MaterialInstanceDynamic"))
                {
                    for (const std::wstring_view name : probes)
                    {
                        const auto value = read_vector_parameter_from_mid(material, name);
                        if (value &&
                            ((*value)[0] != 0.0f || (*value)[1] != 0.0f || (*value)[2] != 0.0f))
                        {
                            values += std::format(STR("{}=({:.2f},{:.2f},{:.2f}) "), name,
                                                  (*value)[0], (*value)[1], (*value)[2]);
                        }
                    }
                }
                UObject* const parent = read_strong_object_property(material, STR("Parent"));
                log_master_parameters(material);
                Output::send<LogLevel::Normal>(
                    STR("[KH3APProbeCpp] Color link readback key={} tag={} component={} slot={} "
                        "material={} class={} parent={} values={}\n"),
                    character_key, tag, component->GetName(), slot,
                    material ? material->GetName() : std::wstring{STR("null")},
                    material ? material->GetClassPrivate()->GetName() : std::wstring{STR("-")},
                    parent ? parent->GetName() : std::wstring{STR("-")}, values);
            }
        }
#else
        (void)pawn;
        (void)character_key;
        (void)tag;
#endif
    }
} // namespace kh3ap::color_service_detail
