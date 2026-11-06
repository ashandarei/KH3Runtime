#pragma once

#include <string_view>

namespace RC::Unreal
{
    class FProperty;
    class UObject;
}

namespace kh3ap
{
    auto write_translation_transform_param(
            RC::Unreal::FProperty* property,
            void* params,
            double x,
            double y,
            double z) -> bool;

    auto try_write_object_name_or_string_property(
            RC::Unreal::UObject* object,
            std::wstring_view property_name,
            std::wstring_view value) -> bool;

}
