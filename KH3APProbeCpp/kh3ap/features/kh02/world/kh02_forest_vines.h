#pragma once

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    // Apply the source family's damage filter before its BeginPlay binding.
    void kh02_configure_forest_vine_damage(RC::Unreal::UObject* hits);
} // namespace kh3ap
