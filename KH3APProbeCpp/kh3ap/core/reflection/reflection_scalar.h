#pragma once

#include <cstdint>
#include <optional>

namespace RC::Unreal
{
    class FProperty;
}

namespace kh3ap
{
    // Scalar storage is supplied by the caller's live reflected property. Reject
    // unsupported widths instead of inventing storage or silently truncating it.
    auto write_integral_function_param(RC::Unreal::FProperty* property, void* params,
                                       uint64_t value) -> bool;
    auto write_float_function_param(RC::Unreal::FProperty* property, void* params,
                                    double value) -> bool;
    auto read_signed_param_value(const void* value_ptr, int element_size)
        -> std::optional<int64_t>;
}
