#pragma once

namespace RC::Unreal
{
    enum EFunctionFlags { FUNC_Native = 1 };
    class UObject {};
    class UFunction : public UObject
    {
    public:
        int flags{FUNC_Native};
        int parameter_size{4};
        auto GetFunctionFlags() const -> int { return flags; }
        auto GetParmsSize() const -> int { return parameter_size; }
    };
}
