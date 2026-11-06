#include "kh02_wardrobe_pattern.h"
#include "kh02_wardrobe_reflection.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh02_wardrobe_materials.h"
#include "kh02_wardrobe_assets.h"
#include <Unreal/UScriptStruct.hpp>
#include <Unreal/NameTypes.hpp>
#include <array>
#include <cstring>

namespace
{
    constexpr int maximum_outfit_material_slots = 64;
    constexpr float color_channel_maximum = 255.f;
} // namespace

namespace kh3ap
{
    using namespace RC::Unreal;
    using namespace kh02wardrobe_detail;

    void kh02_apply_wardrobe_pattern(const Kh02WardrobePattern& request, Kh02WardrobeMaterials& materials,
                                     Kh02WardrobeAssets& assets)
    {
        auto* pawn = request.pawn;
        auto* component = request.component;
        auto* outfit_material = request.outfit_material;
        auto* schema = request.schema;
        auto* unit = request.unit;
        const auto& selected = request.selection;

        const auto property = [&](const wchar_t* name) -> FProperty*
        {
            auto* value = schema->GetPropertyByNameInChain(name);
            if (!value)
            {
                throw std::runtime_error("Missing wardrobe pattern field");
            }
            return value;
        };
        const auto target_name =
            property(L"m_ColorChangeMaterialName")->ContainerPtrToValuePtr<FName>(unit)->ToString();
        Call count(component, L"GetNumMaterials");
        count.run();
        const auto slots = read_function_int_param(count.function, count.parameters.data(), L"ReturnValue");
        if (!slots || *slots < 1 || *slots > maximum_outfit_material_slots)
        {
            throw std::runtime_error("Invalid Aqua material count");
        }
        for (int index = 0; index < *slots; ++index)
        {
            Call get(component, L"GetMaterial");
            get.integer(L"ElementIndex", index);
            get.run();
            auto* original = get.result();
            bool matching = false;
            for (auto* parent = original; is_uobject_valid_fast(parent); parent = object_field(parent, L"Parent"))
            {
                if (parent->GetName() == target_name)
                {
                    matching = true;
                    break;
                }
            }
            if (!matching)
            {
                continue;
            }
            auto* backup = materials.backup(pawn, original, assets);
            Call create(component, L"CreateDynamicMaterialInstance");
            create.integer(L"ElementIndex", index);
            create.object(L"SourceMaterial", outfit_material);
            create.run();
            auto* adapted = create.result();
            if (!is_uobject_valid_fast(adapted))
            {
                throw std::runtime_error("Could not create Aqua's wardrobe material");
            }
            materials.record(index, backup, adapted);
            if (auto** cached = kh02_native_material_slot(component, index))
            {
                *cached = adapted;
            }
            auto scalar = [&](const wchar_t* key, float value)
            {
                Call set(adapted, L"SetScalarParameterValue");
                set.name(L"ParameterName", key);
                set.number(L"Value", value);
                set.run();
            };
            auto vector = [&](const wchar_t* key, const std::array<float, 4>& value)
            {
                Call set(adapted, L"SetVectorParameterValue");
                set.name(L"ParameterName", key);
                set.numbers(L"Value", {value[0], value[1], value[2], value[3]});
                set.run();
            };
            auto* texture = *property(L"m_ColorChangePointTexture")->ContainerPtrToValuePtr<UObject*>(unit);
            Call set_texture(adapted, L"SetTextureParameterValue");
            set_texture.name(L"ParameterName", L"PatternMask");
            set_texture.object(L"Value", texture);
            set_texture.run();
            scalar(L"VisCostume", 1);
            for (const auto* field : {L"GradePow", L"3ColorModeMode", L"halfUVTil_VMove", L"halfUVTilingMode",
                                      L"LeatherMode", L"MeshTypeModeChange", L"MoveMeshArea", L"MoveNoMeshVisMaskArea"})
            {
                const auto name = L"m_" + std::wstring(field);
                auto* value = property(name.c_str());
                if (value->GetElementSize() != sizeof(float))
                {
                    throw std::runtime_error("Invalid wardrobe scalar field");
                }
                scalar(field, *value->ContainerPtrToValuePtr<float>(unit));
            }
            for (const auto* field : {L"ChangePatternColor1", L"ChangePatternColor2", L"3Color_Top"})
            {
                std::array<float, 4> color{};
                const auto name = L"m_" + std::wstring(field);
                auto* value = property(name.c_str());
                if (value->GetElementSize() != sizeof(color))
                {
                    throw std::runtime_error("Invalid wardrobe vector field");
                }
                std::memcpy(color.data(), value->ContainerPtrToValuePtr<void>(unit), sizeof(color));
                vector(field, color);
            }
            if (selected["color_mode"] != 0)
            {
                auto convert = [&](const char* key)
                {
                    const auto& rgb = selected[key];
                    return std::array<float, 4>{rgb[0].get<float>() / color_channel_maximum,
                                                rgb[1].get<float>() / color_channel_maximum,
                                                rgb[2].get<float>() / color_channel_maximum, 1};
                };
                const auto top = convert("top");
                scalar(L"BaseColorChangeMode", 1);
                scalar(L"GradeMode", selected["color_mode"] == 2 ? 1.0f : 0.0f);
                if (selected["color_mode"] == 2)
                {
                    scalar(L"GradePow", 1);
                }
                vector(L"ChangeColor", top);
                vector(L"GladeColor", convert("bottom"));
            }
        }
        if (materials.empty())
        {
            throw std::runtime_error("Aqua's outfit material was not found");
        }
    }

} // namespace kh3ap
