#pragma once
#include <Unreal/Runtime.hpp>
namespace kh3ap {
auto find_function_param_by_name(RC::Unreal::UFunction*,std::wstring_view) -> RC::Unreal::FProperty*;
auto initialize_property_value_dynamic(RC::Unreal::FProperty*,void*) -> bool;
auto destroy_property_value_dynamic(RC::Unreal::FProperty*,void*) -> void;
}
