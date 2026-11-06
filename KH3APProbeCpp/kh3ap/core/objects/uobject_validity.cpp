#include "kh3ap/core/objects/uobject_validity.h"

#include <Unreal/UClass.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectArray.hpp>
#include <Unreal/UStruct.hpp>

#include <cstddef>

#if defined(_MSC_VER)
#define NOMINMAX
#include <Windows.h>
#endif

namespace kh3ap
{
    namespace
    {
        constexpr size_t s_max_uobject_outer_depth = 64;
        constexpr size_t s_max_uobject_class_depth = 64;

        auto is_uobject_slot_valid_unprotected(
                const RC::Unreal::UObject* object,
                int32_t object_count) -> bool
        {
            if (!object)
            {
                return false;
            }

            const int32_t object_index = object->GetInternalIndex();
            if (object_index < 0 || object_index >= object_count)
            {
                return false;
            }

            const auto* const object_item = object->GetObjectItem();
            return object_item &&
                    object_item->GetUObject() == object &&
                    RC::Unreal::UObjectArray::IsValid(
                            const_cast<RC::Unreal::FUObjectItem*>(object_item),
                            false) &&
                    !const_cast<RC::Unreal::UObject*>(object)->HasAnyFlags(
                            static_cast<RC::Unreal::EObjectFlags>(
                                    RC::Unreal::RF_BeginDestroyed |
                                    RC::Unreal::RF_FinishDestroyed));
        }

        auto is_uobject_valid_fast_unprotected(
                const RC::Unreal::UObject* object) -> bool
        {
            try
            {
                if (!object)
                {
                    return false;
                }
                const int32_t object_count = RC::Unreal::UObjectArray::GetNumElements();
                return is_uobject_slot_valid_unprotected(object, object_count);
            }
            catch (...)
            {
                return false;
            }
        }

        auto is_uobject_a_fast_unprotected(
                const RC::Unreal::UObject* object,
                const RC::Unreal::UClass* target_class) -> bool
        {
            if (!object || !target_class)
            {
                return false;
            }

            const int32_t object_count =
                    RC::Unreal::UObjectArray::GetNumElements();
            if (!is_uobject_slot_valid_unprotected(object, object_count) ||
                !is_uobject_slot_valid_unprotected(
                        target_class,
                        object_count))
            {
                return false;
            }

            const RC::Unreal::UStruct* current = object->GetClassPrivate();
            for (size_t depth = 0;
                 current && depth < s_max_uobject_class_depth;
                 ++depth)
            {
                const auto* const current_object =
                        static_cast<const RC::Unreal::UObject*>(current);
                if (!is_uobject_slot_valid_unprotected(
                            current_object,
                            object_count))
                {
                    return false;
                }
                if (current == target_class)
                {
                    return true;
                }

                const RC::Unreal::UStruct* const next =
                        current->GetSuperStruct();
                if (next == current)
                {
                    return false;
                }
                current = next;
            }
            return false;
        }

        auto is_uclass_child_of_fast_unprotected(
                const RC::Unreal::UClass* object_class,
                const RC::Unreal::UClass* base_class) -> bool
        {
            const auto* class_type = RC::Unreal::UClass::StaticClass();
            if (!is_uobject_a_fast_unprotected(base_class, class_type)) return false;
            bool matched = false;
            const RC::Unreal::UStruct* current = object_class;
            for (size_t depth = 0;
                 current && depth < s_max_uobject_class_depth;
                 ++depth)
            {
                if (!is_uobject_a_fast_unprotected(current, class_type)) return false;
                matched = matched || current == base_class;
                current = current->GetSuperStruct();
                if (!current) return matched;
            }
            return false;
        }

        auto is_uobject_name_chain_valid_unprotected(
                const RC::Unreal::UObject* object) -> bool
        {
            if (!object)
            {
                return false;
            }

            try
            {
                const int32_t object_count =
                        RC::Unreal::UObjectArray::GetNumElements();
                const RC::Unreal::UObject* current = object;
                for (size_t depth = 0;
                     current && depth < s_max_uobject_outer_depth;
                     ++depth)
                {
                    if (!is_uobject_slot_valid_unprotected(current, object_count))
                    {
                        return false;
                    }

                    const auto* const object_class = current->GetClassPrivate();
                    if (!is_uobject_slot_valid_unprotected(
                                object_class,
                                object_count))
                    {
                        return false;
                    }

                    const RC::Unreal::UObject* const outer =
                            current->GetOuterPrivate();
                    if (!outer)
                    {
                        return true;
                    }
                    if (outer == current)
                    {
                        return false;
                    }
                    current = outer;
                }
                return false;
            }
            catch (...)
            {
                return false;
            }
        }

        auto try_get_uobject_full_name_fast_unprotected(
                const RC::Unreal::UObject* object,
                std::wstring& full_name) -> bool
        {
            try
            {
                if (!is_uobject_name_chain_valid_unprotected(object))
                {
                    return false;
                }
                full_name = object->GetFullName();
                return !full_name.empty();
            }
            catch (...)
            {
                return false;
            }
        }

#if defined(_MSC_VER)
        auto fast_uobject_validity_exception_filter(
                unsigned long exception_code) noexcept -> int
        {
            return exception_code == EXCEPTION_ACCESS_VIOLATION ||
                            exception_code == EXCEPTION_IN_PAGE_ERROR
                    ? EXCEPTION_EXECUTE_HANDLER
                    : EXCEPTION_CONTINUE_SEARCH;
        }
#endif
    }

    auto is_uobject_valid_fast(
            const RC::Unreal::UObject* object) noexcept -> bool
    {
#if defined(_MSC_VER)
        __try
        {
            return is_uobject_valid_fast_unprotected(object);
        }
        __except (fast_uobject_validity_exception_filter(GetExceptionCode()))
        {
            return false;
        }
#else
        return is_uobject_valid_fast_unprotected(object);
#endif
    }

    auto is_uobject_a_fast(
            const RC::Unreal::UObject* object,
            const RC::Unreal::UClass* target_class) noexcept -> bool
    {
#if defined(_MSC_VER)
        __try
        {
            return is_uobject_a_fast_unprotected(object, target_class);
        }
        __except (fast_uobject_validity_exception_filter(GetExceptionCode()))
        {
            return false;
        }
#else
        return is_uobject_a_fast_unprotected(object, target_class);
#endif
    }

    auto is_uclass_child_of_fast(
            const RC::Unreal::UClass* object_class,
            const RC::Unreal::UClass* base_class) noexcept -> bool
    {
#if defined(_MSC_VER)
        __try
        {
            return is_uclass_child_of_fast_unprotected(object_class, base_class);
        }
        __except (fast_uobject_validity_exception_filter(GetExceptionCode()))
        {
            return false;
        }
#else
        return is_uclass_child_of_fast_unprotected(object_class, base_class);
#endif
    }

    auto try_get_uobject_full_name_fast(
            const RC::Unreal::UObject* object,
            std::wstring& full_name) noexcept -> bool
    {
        full_name.clear();
#if defined(_MSC_VER)
        __try
        {
            return try_get_uobject_full_name_fast_unprotected(
                    object,
                    full_name);
        }
        __except (fast_uobject_validity_exception_filter(GetExceptionCode()))
        {
            return false;
        }
#else
        return try_get_uobject_full_name_fast_unprotected(object, full_name);
#endif
    }
}
