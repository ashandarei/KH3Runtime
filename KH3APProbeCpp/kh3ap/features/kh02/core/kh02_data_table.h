#pragma once

#include "kh3ap/core/reflection/reflection_core.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UScriptStruct.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/NameTypes.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>

extern "C" void* kh02_table_row_find(void*, unsigned long long);
extern "C" bool kh02_table_row_add(void*, unsigned long long, void*);
extern "C" bool kh02_table_rows_visit(void*, void (*)(unsigned long long, void*, void*), void*);

namespace kh3ap
{
    // Borrowed, call-local view of the verified native DataTable ABI. Do not
    // retain this view across frames; the owning subsystem keeps weak references.
    struct Kh02DataTableView
    {
        static constexpr std::size_t row_schema_offset = 0x40;
        static constexpr std::size_t row_map_offset = 0x48;

        RC::Unreal::UScriptStruct* schema;

        Kh02DataTableView(RC::Unreal::UObject* table, const wchar_t* expected_schema, const char* layout_error)
        {
            using namespace RC::Unreal;
            auto* property = is_uobject_valid_fast(table) ? table->GetPropertyByNameInChain(L"RowStruct") : nullptr;
            auto* row_schema = try_read_reflected_object_property_value(table, L"RowStruct");
            if (!property || property->GetOffset_Internal() != row_schema_offset ||
                property->GetElementSize() != sizeof(UObject*) ||
                !is_uobject_a_fast(row_schema, UScriptStruct::StaticClass()) ||
                row_schema->GetPathName() != expected_schema)
            {
                throw std::runtime_error(layout_error);
            }
            schema = static_cast<UScriptStruct*>(row_schema);
            row_map = reinterpret_cast<std::byte*>(table) + row_map_offset;
        }

        static uint64_t row_key(const wchar_t* name)
        {
            const RC::Unreal::FName native_name(name, RC::Unreal::FNAME_Add);
            uint64_t value;
            static_assert(sizeof(native_name) == sizeof(value));
            std::memcpy(&value, &native_name, sizeof(value));
            return value;
        }

        void* find(uint64_t name) const
        {
            return kh02_table_row_find(row_map, name);
        }

        void* find(const wchar_t* name) const
        {
            return find(row_key(name));
        }

        bool insert(const wchar_t* name, void* row) const
        {
            return insert(row_key(name), row);
        }

        bool insert(uint64_t name, void* row) const
        {
            return kh02_table_row_add(row_map, name, row);
        }

        bool visit(void (*visitor)(unsigned long long, void*, void*), void* context) const
        {
            return kh02_table_rows_visit(row_map, visitor, context);
        }

      private:
        void* row_map;
    };
} // namespace kh3ap
