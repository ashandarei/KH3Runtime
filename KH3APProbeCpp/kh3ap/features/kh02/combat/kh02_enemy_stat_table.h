#pragma once
#include "kh3ap/features/kh02/core/kh02_data_table.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UScriptStruct.hpp>
#include <Unreal/FProperty.hpp>
#include <stdexcept>

namespace kh3ap
{
    namespace kh02stats_detail
    {
        constexpr int native_scalar_field_bytes = sizeof(int32_t);
        constexpr int native_table_row_header_bytes = sizeof(void*);
    } // namespace kh02stats_detail

    struct Kh02EnemyStatTable : Kh02DataTableView
    {
        Kh02EnemyStatTable(RC::Unreal::UObject* table, const wchar_t* type)
            : Kh02DataTableView(table, type, "kh02_stats_table_schema")
        {
        }

        RC::Unreal::FProperty* field(const wchar_t* name, const wchar_t* type)
        {
            auto* property = schema->GetPropertyByNameInChain(name);
            if (!property || property->GetClass().GetName() != type ||
                property->GetElementSize() != kh02stats_detail::native_scalar_field_bytes ||
                property->GetArrayDim() != 1 ||
                property->GetOffset_Internal() < kh02stats_detail::native_table_row_header_bytes ||
                property->GetOffset_Internal() + kh02stats_detail::native_scalar_field_bytes >
                    schema->GetStructureSize())
            {
                throw std::runtime_error("kh02_stats_field_schema");
            }
            return property;
        }
    };

} // namespace kh3ap
