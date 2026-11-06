"""Compile the production overlay bindings with only the engine boundary faked."""
from pathlib import Path
import sys

source = (Path(__file__).parent / "../KH3APProbeCpp/kh3ap/features/ui/map_overlay/gfx_bindings.h").read_text(encoding="utf-8")
start = source.index("struct BorrowedString")
end = source.index("// Only reflected metadata is retained.", start)
out = Path(sys.argv[1])
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text("""#pragma once
#include "kh3ap/features/ui/map_overlay/map_gfx_value_layout.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include <Unreal/Runtime.hpp>
#include <Unreal/FWeakObjectPtr.hpp>
#include <string>
#include <string_view>
#include <cstring>
#include <cwchar>
namespace kh3ap {
using namespace RC::Unreal;
auto find_struct_field_by_name(UScriptStruct*, std::wstring_view) -> FProperty*;
auto find_function_param_by_name(UFunction*, std::wstring_view) -> FProperty*;
auto make_uobject_weak(UObject*) -> FWeakObjectPtr;
""" + source[start:end] + "\n}\n", encoding="utf-8")
