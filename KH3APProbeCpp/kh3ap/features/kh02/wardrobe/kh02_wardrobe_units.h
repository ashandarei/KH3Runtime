#pragma once
#include <cstddef>
#include <string>

namespace RC::Unreal
{
    class UObject;
    class UClass;
    class UScriptStruct;
} // namespace RC::Unreal

namespace kh3ap
{
    struct Kh02WardrobeAttachment
    {
        RC::Unreal::UClass* part_class;
        std::wstring socket;
    };

    // Borrowed for one apply call while the source asset is retained by wardrobe assets.
    // This view owns no UObject, component, or native effect and must not cross a frame.
    class Kh02WardrobeUnitView
    {
      public:
        static auto decode(RC::Unreal::UObject* asset) -> Kh02WardrobeUnitView;

        auto count() const -> int
        {
            return unit_count;
        }

        auto schema() const -> RC::Unreal::UScriptStruct*
        {
            return unit_schema;
        }

        auto unit(int index) const -> void*;
        auto attachment(int index) const -> Kh02WardrobeAttachment;

      private:
        Kh02WardrobeUnitView(void* data, int count, std::size_t stride, RC::Unreal::UScriptStruct* schema)
            : data(data), unit_count(count), stride(stride), unit_schema(schema)
        {
        }

        void* data;
        int unit_count;
        std::size_t stride;
        RC::Unreal::UScriptStruct* unit_schema;
    };
} // namespace kh3ap
