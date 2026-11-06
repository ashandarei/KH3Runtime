#pragma once
#include "kh3ap/features/kh02/core/kh02_data_table.h"
#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UScriptStruct.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/FMemory.hpp>
#include <cstring>
#include <stdexcept>

namespace kh3ap::kh02hud_detail
{
    using namespace RC::Unreal;
    constexpr std::size_t reflected_parameter_limit_bytes = 512;
    constexpr int minimum_copied_row_bytes = 16;
    constexpr int maximum_copied_row_bytes = 256;

    struct Call : Kh02ReflectedCall
    {
        Call(const wchar_t* owner_path, const wchar_t* method)
            : Kh02ReflectedCall(owner_path, method, "kh02_hud", reflected_parameter_limit_bytes)
        {
        }

        // The HUD API accepts both FName and FString identifiers.
        void name(const wchar_t* key, const wchar_t* value)
        {
            if (!write_name_or_string_function_param(param(key), parameters.data(), value))
            {
                throw std::runtime_error("kh02_hud_name");
            }
        }

        bool result()
        {
            auto* property = param(L"ReturnValue");
            if (!property || property->GetClass().GetName() != L"BoolProperty" || property->GetElementSize() != 1)
            {
                throw std::runtime_error("kh02_hud_result");
            }
            return boolean_result();
        }
    };

    constexpr auto library = L"/Script/TresGame.Default__TresGameBlueprintLibrary";

    inline uint64_t key(const wchar_t* name)
    {
        return Kh02DataTableView::row_key(name);
    }

    struct Table : Kh02DataTableView
    {
        Table(UObject* table, const wchar_t* type) : Kh02DataTableView(table, type, "kh02_hud_table_layout") {}

        void* copy(const wchar_t* name, void* source)
        {
            if (auto* existing = find(name))
            {
                return existing;
            }
            const int size = schema->GetStructureSize();
            if (!source || size < minimum_copied_row_bytes || size > maximum_copied_row_bytes)
            {
                throw std::runtime_error("kh02_hud_row_size");
            }
            auto* row = FMemory::Malloc(size, schema->GetMinAlignment());
            std::memset(row, 0, size);
            // Preserve FTableRowBase's native vptr; CopyScriptStruct deep-copies
            // soft paths/strings so the destination table owns every allocation.
            std::memcpy(row, source, sizeof(void*));
            schema->CopyScriptStruct(row, source);
            if (!insert(name, row))
            {
                schema->DestroyStruct(row);
                FMemory::Free(row);
                throw std::runtime_error("kh02_hud_row_insert");
            }
            return row;
        }
    };

} // namespace kh3ap::kh02hud_detail
