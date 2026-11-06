#include "bad_status_runtime.h"
#include "kh3ap/core/reflection/reflected_property_cache.h"
#include <Unreal/FProperty.hpp>
#include <Unreal/FScriptArray.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UObject.hpp>
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <format>

namespace kh3ap::bad_status_detail
{
    using namespace RC;
    using namespace RC::Unreal;
    namespace
    {
        constexpr auto max_status_pointer_entry_bytes = 32;
        constexpr auto max_previous_bad_status_states = 64;
    } // namespace
    struct BadStatusStateQueue::Impl
    {
        ReflectedPropertyCache<8> properties;
        FWeakObjectPtr layout_class;
        FProperty* id_property{};
        FProperty* name_property{};
        FProperty* pawn_property{};
        bool layout_ready{};

        auto resolve_player_state_field_layout(UObject* player_state) -> bool
        {
            if (layout_ready &&
                layout_class.Get() == (player_state ? player_state->GetClassPrivate() : nullptr))
            {
                return true;
            }
            if (!player_state)
            {
                return false;
            }

            id_property = properties.resolve(player_state, STR("MyStateID"));
            name_property = properties.resolve(player_state, STR("MyStateName"));
            pawn_property = properties.resolve(player_state, STR("MyCharPawn"));
            layout_ready = id_property && name_property && pawn_property &&
                           id_property->GetElementSize() >= static_cast<int>(sizeof(uint8_t)) &&
                           name_property->GetClass().GetName() == STR("NameProperty") &&
                           name_property->GetElementSize() >= static_cast<int>(sizeof(FName)) &&
                           pawn_property->GetClass().GetName() == STR("ObjectProperty") &&
                           pawn_property->GetElementSize() >= static_cast<int>(sizeof(UObject*));
            layout_class = make_uobject_weak(player_state->GetClassPrivate());
            return layout_ready;
        }

        auto try_write_player_bad_status_state_fields(UObject* bad_status_state, UObject* sora_pawn,
                                                      const DirectPlayerBadStatusSpec& spec,
                                                      const PlayerBadStatusStateAsset& state_asset,
                                                      std::wstring* failure_details) -> bool
        {
            if (!bad_status_state || !sora_pawn)
            {
                return false;
            }

            if (!resolve_player_state_field_layout(bad_status_state))
            {
                if (failure_details)
                {
                    *failure_details = STR("direct_player_bad_status_state_field_layout_invalid");
                }
                return false;
            }
            void* const id_value =
                id_property ? id_property->ContainerPtrToValuePtr<void>(bad_status_state) : nullptr;
            void* const name_value =
                name_property ? name_property->ContainerPtrToValuePtr<void>(bad_status_state)
                              : nullptr;
            void* const pawn_value =
                pawn_property ? pawn_property->ContainerPtrToValuePtr<void>(bad_status_state)
                              : nullptr;
            if (!id_value || !name_value || !pawn_value)
            {
                if (failure_details)
                {
                    *failure_details = STR("direct_player_bad_status_state_field_layout_invalid");
                }
                return false;
            }

            std::memcpy(id_value, &spec.state_id, sizeof(spec.state_id));
            std::memcpy(name_value, &state_asset.state_name, sizeof(state_asset.state_name));
            std::memcpy(pawn_value, &sora_pawn, sizeof(sora_pawn));
            return true;
        }

        auto try_prepend_player_bad_status_state(UObject* state_component,
                                                 UObject* bad_status_state, int* queue_count_before,
                                                 int* queue_count_after,
                                                 std::wstring* failure_details) -> bool
        {
            FProperty* const queue_property =
                properties.resolve(state_component, STR("m_StateQueue"));
            void* const queue_value =
                queue_property ? queue_property->ContainerPtrToValuePtr<void>(state_component)
                               : nullptr;
            if (!queue_property || !queue_value ||
                queue_property->GetClass().GetName() != STR("ArrayProperty"))
            {
                if (failure_details)
                {
                    *failure_details = STR("direct_player_bad_status_state_queue_unavailable");
                }
                return false;
            }

            auto* const array_property = static_cast<FArrayProperty*>(queue_property);
            FProperty* const inner_property = const_cast<FProperty*>(array_property->GetInner());
            const int element_size = inner_property ? inner_property->GetElementSize() : 0;
            const int element_alignment =
                inner_property ? std::max(inner_property->GetMinAlignment(), 1) : 1;
            if (!inner_property || inner_property->GetClass().GetName() != STR("ObjectProperty") ||
                element_size < static_cast<int>(sizeof(UObject*)) ||
                element_size > max_status_pointer_entry_bytes)
            {
                if (failure_details)
                {
                    *failure_details = STR("direct_player_bad_status_state_queue_layout_invalid");
                }
                return false;
            }

            auto* const queue = static_cast<FScriptArray*>(queue_value);
            const int old_count = queue->Num();
            if (old_count < 0 || old_count > max_previous_bad_status_states)
            {
                if (failure_details)
                {
                    *failure_details = std::format(
                        STR("direct_player_bad_status_state_queue_count_invalid:{}"), old_count);
                }
                return false;
            }

            const int new_index =
                queue->Add(1, element_size, static_cast<uint32_t>(element_alignment));
            if (new_index != old_count || queue->Num() != old_count + 1 || !queue->GetData())
            {
                if (failure_details)
                {
                    *failure_details = STR("direct_player_bad_status_state_queue_add_failed");
                }
                return false;
            }

            auto* const data = static_cast<std::byte*>(queue->GetData());
            if (old_count > 0)
            {
                std::memmove(data + element_size, data,
                             static_cast<size_t>(old_count) * static_cast<size_t>(element_size));
            }
            std::memset(data, 0, static_cast<size_t>(element_size));
            std::memcpy(data, &bad_status_state, sizeof(bad_status_state));

            if (queue_count_before)
            {
                *queue_count_before = old_count;
            }
            if (queue_count_after)
            {
                *queue_count_after = queue->Num();
            }
            return true;
        }
    };
    BadStatusStateQueue::BadStatusStateQueue() : impl(std::make_unique<Impl>())
    {
    }
    BadStatusStateQueue::~BadStatusStateQueue() = default;

    auto BadStatusStateQueue::property(UObject* component) -> FProperty*
    {
        return impl->properties.resolve(component, L"m_StateQueue");
    }
    auto BadStatusStateQueue::prewarm_fields(UObject* current) -> void
    {
        if (is_uobject_valid_fast(current))
        {
            impl->resolve_player_state_field_layout(current);
        }
    }
    auto BadStatusStateQueue::write_fields(UObject* state, UObject* pawn,
                                           const DirectPlayerBadStatusSpec& spec,
                                           const PlayerBadStatusStateAsset& asset,
                                           std::wstring* failure) -> bool
    {
        return impl->try_write_player_bad_status_state_fields(state, pawn, spec, asset, failure);
    }
    auto BadStatusStateQueue::prepend(UObject* component, UObject* state, int* before, int* after,
                                      std::wstring* failure) -> bool
    {
        return impl->try_prepend_player_bad_status_state(component, state, before, after, failure);
    }
} // namespace kh3ap::bad_status_detail
