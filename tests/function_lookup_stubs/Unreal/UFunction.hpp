#pragma once
namespace RC::Unreal
{
    class UClass
    {
    };
    class UObject
    {
    public:
        virtual ~UObject() = default;
        bool valid{true};
        bool outer_valid{true};
        int serial{1};
    };
    class UFunction : public UObject
    {
    public:
        UFunction(bool live = true, int identity = 1)
        {
            valid = live;
            serial = identity;
        }
        static auto StaticClass() -> UClass*
        {
            static UClass type;
            return &type;
        }
    };
} // namespace RC::Unreal
