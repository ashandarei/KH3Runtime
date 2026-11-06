#pragma once

#include <string>

namespace RC::Unreal
{
    class UClass;
    class UObject;
}

namespace kh3ap
{
    // Constant-time counterpart to UE4SS UObject::IsReal. This validates the
    // exact GUObjectArray slot and rejects pending-kill, unreachable, and
    // destroying objects without scanning the global array.
    // Invalid or stale readable pointers fail closed; the Windows build also
    // guards the initial pointer read against access violations.
    [[nodiscard]] auto is_uobject_valid_fast(
            const RC::Unreal::UObject* object) noexcept -> bool;

    // Bounded, slot-validated counterpart to UObject::IsA. In addition to
    // rejecting a stale object, this validates every UClass in the ancestry
    // chain before reading its SuperStruct pointer. This is intended for
    // runtime paths where a cached UObject may disappear between frames.
    [[nodiscard]] auto is_uobject_a_fast(
            const RC::Unreal::UObject* object,
            const RC::Unreal::UClass* target_class) noexcept -> bool;

    // Validate the class itself and its complete SuperStruct chain before
    // passing it to native actor/component enumeration. A reused live UObject
    // slot alone does not prove that a cached pointer still denotes a UClass.
    [[nodiscard]] auto is_uclass_child_of_fast(
            const RC::Unreal::UClass* object_class,
            const RC::Unreal::UClass* base_class) noexcept -> bool;

    // Additionally validates every Outer used by GetFullName and each
    // object's class, then keeps the name read behind the Windows
    // access-violation guard. This bounded work is paid only by callers that
    // actually need a full name.
    [[nodiscard]] auto try_get_uobject_full_name_fast(
            const RC::Unreal::UObject* object,
            std::wstring& full_name) noexcept -> bool;
}
