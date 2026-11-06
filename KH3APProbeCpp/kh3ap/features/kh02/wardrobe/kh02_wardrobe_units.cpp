#include "kh02_wardrobe_units.h"
#include "kh02_wardrobe_reflection.h"
#include <Unreal/UClass.hpp>
#include <Unreal/UScriptStruct.hpp>
#include <Unreal/NameTypes.hpp>
#include <Unreal/Property/FStructProperty.hpp>
#include <stdexcept>

namespace kh3ap
{
    using namespace RC::Unreal;

    auto Kh02WardrobeUnitView::decode(UObject* asset) -> Kh02WardrobeUnitView
    {
        const auto [units, inner] = kh02wardrobe_detail::array_field(asset, L"m_AssetList");
        if (!inner || !inner->IsA<FStructProperty>())
        {
            throw std::runtime_error("Invalid wardrobe piece data");
        }
        auto* schema = static_cast<FStructProperty*>(inner)->GetStruct();
        if (!is_uobject_valid_fast(schema) || schema->GetName() != L"TresDecoPartsUnit")
        {
            throw std::runtime_error("Unknown wardrobe piece schema");
        }
        const int stride = inner->GetElementSize();
        if (stride <= 0)
        {
            throw std::runtime_error("Invalid wardrobe piece data");
        }
        return {units->data, units->count, static_cast<std::size_t>(stride), schema};
    }

    auto Kh02WardrobeUnitView::unit(int index) const -> void*
    {
        if (index < 0 || index >= unit_count)
        {
            throw std::out_of_range("Invalid wardrobe piece unit");
        }
        return static_cast<std::byte*>(data) + static_cast<std::size_t>(index) * stride;
    }

    auto Kh02WardrobeUnitView::attachment(int index) const -> Kh02WardrobeAttachment
    {
        auto* storage = unit(index);
        auto* asset_property = unit_schema->GetPropertyByNameInChain(L"m_Asset");
        auto* socket_property = unit_schema->GetPropertyByNameInChain(L"m_AttachSocketName");
        const auto fits = [this](FProperty* property, std::size_t width)
        {
            if (!property || property->GetElementSize() != width || property->GetOffset_Internal() < 0)
            {
                return false;
            }
            const auto offset = static_cast<std::size_t>(property->GetOffset_Internal());
            return offset <= stride && width <= stride - offset;
        };
        if (!fits(asset_property, sizeof(UClass*)) || !fits(socket_property, sizeof(FName)))
        {
            throw std::runtime_error("Invalid wardrobe attachment fields");
        }
        auto* part = *asset_property->ContainerPtrToValuePtr<UClass*>(storage);
        if (!is_uobject_a_fast(part, UClass::StaticClass()))
        {
            throw std::runtime_error("Missing wardrobe component class");
        }
        return {part, socket_property->ContainerPtrToValuePtr<FName>(storage)->ToString()};
    }
} // namespace kh3ap
