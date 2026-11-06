#include <algorithm>
#include "treasure_runtime_helpers.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/bridge/text_helpers.h"
#include "kh3ap/bridge/utilities.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/FProperty.hpp>
#include <format>
#include "kh3ap/core/objects/uobject_weak.h"
#include <cstring>
#include <utility>
#include <cstddef>
#include <cstdint>
#include <unordered_map>

namespace kh3ap
{
    namespace
    {
        constexpr auto discovery_value_preview_bytes = 16;

        constexpr auto max_treasure_outer_depth = 8;
        constexpr auto treasure_event_log_limit = 24;
        constexpr size_t s_mesh_treasure_cache_max = 256;
        struct MeshTreasureBox
        {
            RC::Unreal::FWeakObjectPtr mesh;
            RC::Unreal::FWeakObjectPtr box;
        };
        std::unordered_map<uintptr_t, MeshTreasureBox> s_mesh_treasure_box_cache;

        auto cache_treasure_box_for_mesh(RC::Unreal::UObject* mesh_component,
                                         RC::Unreal::UObject* treasure_box) -> void
        {
            if (!mesh_component || !treasure_box)
            {
                return;
            }
            if (s_mesh_treasure_box_cache.size() >= s_mesh_treasure_cache_max)
            {
                s_mesh_treasure_box_cache.clear();
            }
            MeshTreasureBox entry{make_uobject_weak(mesh_component),
                                  make_uobject_weak(treasure_box)};
            if (entry.mesh.Get() == mesh_component && entry.box.Get() == treasure_box)
            {
                s_mesh_treasure_box_cache[reinterpret_cast<uintptr_t>(mesh_component)] =
                    std::move(entry);
            }
        }

        auto cached_treasure_box_for_mesh(RC::Unreal::UObject* mesh_component)
            -> RC::Unreal::UObject*
        {
            if (!mesh_component)
            {
                return nullptr;
            }
            const auto key = reinterpret_cast<uintptr_t>(mesh_component);
            const auto it = s_mesh_treasure_box_cache.find(key);
            if (it == s_mesh_treasure_box_cache.end())
            {
                return nullptr;
            }
            auto* const treasure_box = it->second.box.Get();
            if (it->second.mesh.Get() == mesh_component && treasure_box &&
                kh3ap::is_uobject_valid_fast(treasure_box))
            {
                return treasure_box;
            }
            s_mesh_treasure_box_cache.erase(it);
            return nullptr;
        }

        auto try_find_actor_outer_fast(RC::Unreal::UObject* object,
                                       RC::Unreal::UClass* actor_base_class) -> RC::Unreal::UObject*
        {
            if (!object || !actor_base_class)
            {
                return nullptr;
            }

            RC::Unreal::UObject* current = object;
            for (int depth = 0; current && depth < max_treasure_outer_depth; ++depth)
            {
                if (current != object && kh3ap::is_uobject_valid_fast(current) &&
                    current->IsA(actor_base_class))
                {
                    return current;
                }

                RC::Unreal::UObject* next_outer = nullptr;
                try
                {
                    next_outer = current->GetOuterPrivate();
                }
                catch (...)
                {
                    return nullptr;
                }
                if (!next_outer || next_outer == current)
                {
                    return nullptr;
                }
                current = next_outer;
            }

            return nullptr;
        }
    } // namespace

    auto should_log_treasureish_property(std::wstring_view property_name,
                                         std::wstring_view property_class) -> bool
    {
        if (property_class != STR("NameProperty") && property_class != STR("StrProperty") &&
            property_class != STR("ObjectProperty") && property_class != STR("ArrayProperty") &&
            property_class != STR("BoolProperty"))
        {
            return false;
        }

        return property_name.find(STR("Treasure")) != std::wstring_view::npos ||
               property_name.find(STR("Box")) != std::wstring_view::npos ||
               property_name.find(STR("Open")) != std::wstring_view::npos ||
               property_name.find(STR("Camera")) != std::wstring_view::npos ||
               property_name.find(STR("Mesh")) != std::wstring_view::npos ||
               property_name.find(STR("React")) != std::wstring_view::npos ||
               property_name.find(STR("Name")) != std::wstring_view::npos;
    }

    auto log_object_treasureish_properties(RC::Unreal::UObject* object, std::wstring_view source)
        -> void
    {
        if (!object || !kh3ap::is_uobject_valid_fast(object))
        {
            return;
        }

        RC::Unreal::UClass* object_class = object->GetClassPrivate();
        if (!object_class)
        {
            return;
        }

        int logged_count = 0;
        for (RC::Unreal::FProperty* property : object_class->ForEachPropertyInChain())
        {
            if (!property)
            {
                continue;
            }

            const std::wstring property_name = property->GetName();
            const std::wstring property_class = property->GetClass().GetName();
            if (!should_log_treasureish_property(property_name, property_class))
            {
                continue;
            }

            const int offset = property->GetOffset_Internal();
            const int element_size = property->GetElementSize();
            if (offset < 0 || element_size <= 0)
            {
                continue;
            }

            const auto* object_bytes = reinterpret_cast<const std::byte*>(object);
            const void* value_ptr = object_bytes + offset;
            std::wstring extra_value = STR("<none>");

            if (property_class == STR("NameProperty"))
            {
                const auto value = try_read_object_name_property(object, property_name.c_str());
                if (value.has_value())
                {
                    extra_value = *value;
                }
            }
            else if (property_class == STR("StrProperty"))
            {
                const auto value = try_read_object_string_property(object, property_name.c_str());
                if (value.has_value())
                {
                    extra_value = *value;
                }
            }
            else if (property_class == STR("ObjectProperty"))
            {
                if (RC::Unreal::UObject* const object_value =
                        try_read_object_property_object(object, property_name.c_str()))
                {
                    extra_value = object_value->GetFullName();
                }
            }
            else if (property_class == STR("BoolProperty"))
            {
                if (const auto value = try_read_object_bool_property(object, property_name.c_str());
                    value.has_value())
                {
                    extra_value = *value ? STR("true") : STR("false");
                }
            }
            else if (property_class == STR("ArrayProperty"))
            {
                ArrayHeader header{};
                std::memcpy(&header, value_ptr, sizeof(ArrayHeader));
                extra_value = std::format(STR("num={} max={} ptr={}"), header.num, header.max,
                                          header.data_ptr);
            }

            RC::Output::send<RC::LogLevel::Verbose>(
                STR("[KH3APProbeCpp] {} | treasure_property object={} | name={} | class={} | "
                    "offset={} | elem_size={} | raw={} | value={}\n"),
                source, object->GetFullName(), property_name, property_class, offset, element_size,
                format_bytes_hex(value_ptr, static_cast<size_t>(std::clamp(
                                                element_size, 1, discovery_value_preview_bytes))),
                extra_value);

            ++logged_count;
            if (logged_count >= treasure_event_log_limit)
            {
                break;
            }
        }
    }

    auto try_read_object_param(RC::Unreal::UFunction* function, void* params,
                               std::wstring_view param_name) -> RC::Unreal::UObject*
    {
        if (!function || !params)
        {
            return nullptr;
        }

        for (RC::Unreal::FProperty* param = function->GetFirstProperty(); param;
             param = param->HasNext() ? param->GetNextFieldAsProperty() : nullptr)
        {
            if (!param || !param->HasAnyPropertyFlags(RC::Unreal::CPF_Parm) ||
                param->GetName() != param_name ||
                param->GetClass().GetName() != STR("ObjectProperty"))
            {
                continue;
            }

            const int offset = param->GetOffset_Internal();
            const int element_size = param->GetElementSize();
            if (offset < 0 || element_size <= 0)
            {
                return nullptr;
            }

            const auto* params_bytes = static_cast<const std::byte*>(params);
            return *reinterpret_cast<RC::Unreal::UObject* const*>(params_bytes + offset);
        }

        return nullptr;
    }

    auto try_resolve_treasure_box_from_mesh_component(RC::Unreal::UObject* mesh_component,
                                                      RC::Unreal::UClass* actor_base_class)
        -> RC::Unreal::UObject*
    {
        if (!mesh_component || !kh3ap::is_uobject_valid_fast(mesh_component))
        {
            return nullptr;
        }

        if (RC::Unreal::UObject* const cached = cached_treasure_box_for_mesh(mesh_component))
        {
            return cached;
        }

        if (actor_base_class)
        {
            RC::Unreal::UObject* owner =
                try_find_actor_outer_fast(mesh_component, actor_base_class);
            if (!owner)
            {
                owner = mesh_component->GetTypedOuter(actor_base_class);
            }
            if (owner && kh3ap::is_uobject_valid_fast(owner))
            {
                if (RC::Unreal::UObject* const true_box =
                        try_read_object_property_object(owner, STR("m_pTrueTreasureBox"));
                    true_box && kh3ap::is_uobject_valid_fast(true_box))
                {
                    cache_treasure_box_for_mesh(mesh_component, true_box);
                    return true_box;
                }
                cache_treasure_box_for_mesh(mesh_component, owner);
                return owner;
            }
        }

        return nullptr;
    }

    auto try_resolve_treasure_box_from_context_object(RC::Unreal::UObject* context)
        -> RC::Unreal::UObject*
    {
        if (!context || !kh3ap::is_uobject_valid_fast(context))
        {
            return nullptr;
        }

        if (RC::Unreal::UObject* const true_box =
                try_read_object_property_object(context, STR("m_pTrueTreasureBox"));
            true_box && kh3ap::is_uobject_valid_fast(true_box))
        {
            return true_box;
        }

        if (context->GetFullName().find(STR("TreasureBox")) != std::wstring::npos)
        {
            return context;
        }

        return nullptr;
    }

} // namespace kh3ap
