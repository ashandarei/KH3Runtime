#pragma once
// Test object registry: changing serial simulates destruction and reuse of
// both the allocation address and object-array slot.
namespace RC::Unreal
{
    class UObject
    {
    public:
        int serial{1};
        bool alive{true};
    };
}
