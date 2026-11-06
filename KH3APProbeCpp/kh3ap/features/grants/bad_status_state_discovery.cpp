#include "bad_status_runtime.h"
#include "kh3ap/core/reflection/reflected_property_cache.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/types.h"
#include "kh3ap/features/player/objects/object_resolver.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/FScriptArray.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#include <Unreal/Property/FStructProperty.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UScriptStruct.hpp>
#include <cstddef>
#include <cstring>
#include <format>
#include <optional>

namespace kh3ap::bad_status_detail
{
    using namespace RC;
    using namespace RC::Unreal;
    namespace
    {
        constexpr auto max_status_pointer_entry_bytes = 32;
        constexpr auto max_bad_status_states = 512;
        constexpr auto max_status_array_entry_bytes = 128;
    } // namespace

    auto find_direct_player_bad_status_spec(std::wstring_view runtime_token)
        -> const DirectPlayerBadStatusSpec*
    {
        for (const DirectPlayerBadStatusSpec& spec : s_direct_player_bad_status_specs)
        {
            if (runtime_token == spec.runtime_token || runtime_token == spec.short_token ||
                runtime_token == spec.name)
            {
                return &spec;
            }
        }
        return nullptr;
    }

    auto try_read_object_pointer_in_chain(UObject* owner, const wchar_t* property_name) -> UObject*
    {
        if (!owner || !kh3ap::is_uobject_valid_fast(owner) || !property_name)
        {
            return nullptr;
        }
        FProperty* const property = owner->GetPropertyByNameInChain(property_name);
        void* const value_ptr = owner->GetValuePtrByPropertyNameInChain(property_name);
        if (!property || !value_ptr || property->GetClass().GetName() != STR("ObjectProperty") ||
            property->GetElementSize() < static_cast<int>(sizeof(UObject*)))
        {
            return nullptr;
        }

        UObject* value = nullptr;
        std::memcpy(&value, value_ptr, sizeof(value));
        return value && kh3ap::is_uobject_valid_fast(value) ? value : nullptr;
    }

    struct BadStatusStateDiscovery::Impl
    {
        struct CachedStateAsset
        {
            FWeakObjectPtr state_class;
            FName state_name;
        };
        ReflectedPropertyCache<16> properties;
        std::array<CachedStateAsset, s_direct_player_bad_status_specs.size()> state_assets{};
        std::array<bool, s_direct_player_bad_status_specs.size()> state_asset_ready{};

        auto direct_player_bad_status_spec_index(const DirectPlayerBadStatusSpec& spec)
            -> std::optional<size_t>
        {
            const auto* const begin = s_direct_player_bad_status_specs.data();
            const auto* const end = begin + s_direct_player_bad_status_specs.size();
            if (&spec < begin || &spec >= end)
            {
                return std::nullopt;
            }
            return static_cast<size_t>(&spec - begin);
        }

        auto cache_player_bad_status_state_asset(const DirectPlayerBadStatusSpec& spec,
                                                 const PlayerBadStatusStateAsset& state_asset)
            -> void
        {
            const std::optional<size_t> index = direct_player_bad_status_spec_index(spec);
            if (!index.has_value())
            {
                return;
            }
            state_assets[*index] = CachedStateAsset{make_uobject_weak(state_asset.state_class),
                                                    state_asset.state_name};
            state_asset_ready[*index] =
                state_assets[*index].state_class.Get() == state_asset.state_class;
        }

        auto try_find_recycled_player_bad_status_state_asset(UObject* state_component,
                                                             const DirectPlayerBadStatusSpec& spec,
                                                             PlayerBadStatusStateAsset* state_asset,
                                                             std::wstring* failure_details) -> bool
        {
            FProperty* const recycle_property =
                properties.resolve(state_component, STR("m_RecycleState"));
            void* const recycle_value =
                recycle_property ? recycle_property->ContainerPtrToValuePtr<void>(state_component)
                                 : nullptr;
            if (!recycle_property || !recycle_value ||
                recycle_property->GetClass().GetName() != STR("ArrayProperty"))
            {
                if (failure_details)
                {
                    *failure_details = STR("direct_player_bad_status_recycle_array_unavailable");
                }
                return false;
            }

            auto* const array_property = static_cast<FArrayProperty*>(recycle_property);
            FProperty* const inner_property = const_cast<FProperty*>(array_property->GetInner());
            const int element_size = inner_property ? inner_property->GetElementSize() : 0;
            if (!inner_property || inner_property->GetClass().GetName() != STR("ObjectProperty") ||
                element_size < static_cast<int>(sizeof(UObject*)) ||
                element_size > max_status_pointer_entry_bytes)
            {
                if (failure_details)
                {
                    *failure_details = STR("direct_player_bad_status_recycle_array_layout_invalid");
                }
                return false;
            }

            auto* const recycle_states = static_cast<FScriptArray*>(recycle_value);
            const int state_count = recycle_states->Num();
            if (state_count < 0 || state_count > max_bad_status_states ||
                (state_count > 0 && !recycle_states->GetData()))
            {
                if (failure_details)
                {
                    *failure_details =
                        std::format(STR("direct_player_bad_status_recycle_array_count_invalid:{}"),
                                    state_count);
                }
                return false;
            }

            const auto* const data = static_cast<const std::byte*>(recycle_states->GetData());
            for (int index = 0; index < state_count; ++index)
            {
                UObject* prototype = nullptr;
                std::memcpy(&prototype,
                            data + (static_cast<size_t>(index) * static_cast<size_t>(element_size)),
                            sizeof(prototype));
                if (!prototype || !kh3ap::is_uobject_valid_fast(prototype))
                {
                    continue;
                }

                FProperty* const id_property =
                    prototype->GetPropertyByNameInChain(STR("MyStateID"));
                void* const id_value =
                    prototype->GetValuePtrByPropertyNameInChain(STR("MyStateID"));
                uint8_t state_id = 0;
                if (!id_property || !id_value || id_property->GetElementSize() < 1)
                {
                    continue;
                }
                std::memcpy(&state_id, id_value, sizeof(state_id));
                if (state_id != spec.state_id)
                {
                    continue;
                }

                UClass* const state_class = prototype->GetClassPrivate();
                if (!state_class || !kh3ap::is_uobject_valid_fast(state_class))
                {
                    continue;
                }
                FName state_name{};
                FProperty* const name_property =
                    prototype->GetPropertyByNameInChain(STR("MyStateName"));
                void* const name_value =
                    prototype->GetValuePtrByPropertyNameInChain(STR("MyStateName"));
                if (name_property && name_value &&
                    name_property->GetClass().GetName() == STR("NameProperty") &&
                    name_property->GetElementSize() >= static_cast<int>(sizeof(FName)))
                {
                    std::memcpy(&state_name, name_value, sizeof(state_name));
                }

                if (state_asset)
                {
                    *state_asset = PlayerBadStatusStateAsset{
                        .state_class = state_class,
                        .state_name = state_name,
                    };
                }
                cache_player_bad_status_state_asset(spec, PlayerBadStatusStateAsset{
                                                              .state_class = state_class,
                                                              .state_name = state_name,
                                                          });
                Output::send<LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] Direct player bad status resolved from Sora recycle pool "
                        "status={} state_id={} prototype={} class={} recycle_count={}\n"),
                    spec.name, static_cast<int>(spec.state_id), format_object_debug_name(prototype),
                    state_class->GetName(), state_count);
                return true;
            }

            if (failure_details)
            {
                *failure_details = std::format(
                    STR("direct_player_bad_status_recycled_state_not_found:state_id={}:count={}"),
                    static_cast<int>(spec.state_id), state_count);
            }
            return false;
        }

        auto try_find_player_bad_status_state_asset(UObject* state_component,
                                                    const DirectPlayerBadStatusSpec& spec,
                                                    PlayerBadStatusStateAsset* state_asset,
                                                    std::wstring* failure_details) -> bool
        {
            if (state_asset)
            {
                *state_asset = PlayerBadStatusStateAsset{};
            }

            const std::optional<size_t> spec_index = direct_player_bad_status_spec_index(spec);
            if (spec_index.has_value() && state_asset_ready[*spec_index])
            {
                const CachedStateAsset& cached_asset = state_assets[*spec_index];
                if (auto* state_class = static_cast<UClass*>(cached_asset.state_class.Get()))
                {
                    if (state_asset)
                    {
                        *state_asset =
                            PlayerBadStatusStateAsset{state_class, cached_asset.state_name};
                    }
                    return true;
                }
                state_asset_ready[*spec_index] = false;
            }

            FProperty* const state_asset_property =
                properties.resolve(state_component, STR("MyStateAsset"));
            UObject** const state_set_value =
                state_asset_property
                    ? state_asset_property->ContainerPtrToValuePtr<UObject*>(state_component)
                    : nullptr;
            UObject* const state_set = state_set_value ? *state_set_value : nullptr;
            if (!state_set || !kh3ap::is_uobject_valid_fast(state_set))
            {
                if (spec.state_class_path.empty() || spec.state_class_name.empty())
                {
                    if (try_find_recycled_player_bad_status_state_asset(
                            state_component, spec, state_asset, failure_details))
                    {
                        return true;
                    }
                    return false;
                }
                UClass* const state_class = UObjectGlobals::StaticFindObject<UClass*>(
                    nullptr, nullptr, std::wstring{spec.state_class_path}.c_str());
                if (!state_class || !kh3ap::is_uobject_valid_fast(state_class) ||
                    state_class->GetName() != spec.state_class_name)
                {
                    if (failure_details)
                    {
                        *failure_details = STR("direct_player_bad_status_state_class_unavailable");
                    }
                    return false;
                }
                if (state_asset)
                {
                    *state_asset = PlayerBadStatusStateAsset{
                        .state_class = state_class,
                        .state_name = FName{},
                    };
                }
                cache_player_bad_status_state_asset(spec, PlayerBadStatusStateAsset{
                                                              .state_class = state_class,
                                                              .state_name = FName{},
                                                          });
                return true;
            }

            FProperty* const states_property = properties.resolve(state_set, STR("MyStates"));
            void* const states_value =
                states_property ? states_property->ContainerPtrToValuePtr<void>(state_set)
                                : nullptr;
            if (!states_property || !states_value ||
                states_property->GetClass().GetName() != STR("ArrayProperty"))
            {
                if (failure_details)
                {
                    *failure_details = STR("direct_player_bad_status_state_array_unavailable");
                }
                return false;
            }

            auto* const array_property = static_cast<FArrayProperty*>(states_property);
            FProperty* const inner_property = const_cast<FProperty*>(array_property->GetInner());
            if (!inner_property || inner_property->GetClass().GetName() != STR("StructProperty"))
            {
                if (failure_details)
                {
                    *failure_details = STR("direct_player_bad_status_state_array_inner_not_struct");
                }
                return false;
            }

            auto* const struct_property = static_cast<FStructProperty*>(inner_property);
            UScriptStruct* const state_asset_struct = struct_property->GetStruct();
            if (!state_asset_struct || state_asset_struct->GetName() != STR("TresStateAsset"))
            {
                if (failure_details)
                {
                    *failure_details =
                        STR("direct_player_bad_status_state_asset_struct_unavailable");
                }
                return false;
            }

            FProperty* state_class_property = nullptr;
            FProperty* state_name_property = nullptr;
            FProperty* state_id_property = nullptr;
            for (FProperty* const field : state_asset_struct->ForEachPropertyInChain())
            {
                if (!field)
                {
                    continue;
                }
                if (field->GetName() == STR("MyStateClass"))
                {
                    state_class_property = field;
                }
                else if (field->GetName() == STR("MyStateName"))
                {
                    state_name_property = field;
                }
                else if (field->GetName() == STR("MyStateID"))
                {
                    state_id_property = field;
                }
            }

            const int element_size = inner_property->GetElementSize();
            const int class_offset =
                state_class_property ? state_class_property->GetOffset_Internal() : -1;
            const int name_offset =
                state_name_property ? state_name_property->GetOffset_Internal() : -1;
            const int id_offset = state_id_property ? state_id_property->GetOffset_Internal() : -1;
            if (!state_class_property || !state_name_property || !state_id_property ||
                state_class_property->GetClass().GetName() != STR("ClassProperty") ||
                state_name_property->GetClass().GetName() != STR("NameProperty") ||
                (state_id_property->GetClass().GetName() != STR("ByteProperty") &&
                 state_id_property->GetClass().GetName() != STR("EnumProperty")) ||
                element_size <= 0 || element_size > max_status_array_entry_bytes ||
                class_offset < 0 ||
                class_offset + static_cast<int>(sizeof(UClass*)) > element_size ||
                name_offset < 0 || name_offset + static_cast<int>(sizeof(FName)) > element_size ||
                id_offset < 0 || id_offset >= element_size)
            {
                if (failure_details)
                {
                    *failure_details = STR("direct_player_bad_status_state_asset_layout_invalid");
                }
                return false;
            }

            ArrayHeader states_header{};
            std::memcpy(&states_header, states_value, sizeof(states_header));
            if (!states_header.data_ptr || states_header.num <= 0 ||
                states_header.num > max_bad_status_states || states_header.max < states_header.num)
            {
                if (failure_details)
                {
                    *failure_details = std::format(
                        STR("direct_player_bad_status_state_asset_header_invalid:num={}:max={}"),
                        states_header.num, states_header.max);
                }
                return false;
            }

            const auto* const base =
                reinterpret_cast<const std::byte*>(static_cast<uintptr_t>(states_header.data_ptr));
            for (int index = 0; index < states_header.num; ++index)
            {
                const std::byte* const entry =
                    base + (static_cast<size_t>(index) * static_cast<size_t>(element_size));
                uint8_t state_id = 0;
                std::memcpy(&state_id, entry + id_offset, sizeof(state_id));
                if (state_id != spec.state_id)
                {
                    continue;
                }

                UClass* state_class = nullptr;
                FName state_name{};
                std::memcpy(&state_class, entry + class_offset, sizeof(state_class));
                std::memcpy(&state_name, entry + name_offset, sizeof(state_name));
                if (!state_class || !kh3ap::is_uobject_valid_fast(state_class) ||
                    (!spec.state_class_name.empty() &&
                     state_class->GetName() != spec.state_class_name))
                {
                    if (failure_details)
                    {
                        *failure_details =
                            std::format(STR("direct_player_bad_status_state_class_mismatch:"
                                            "expected={}:actual={}"),
                                        spec.state_class_name.empty() ? STR("<live-state-asset>")
                                                                      : spec.state_class_name,
                                        state_class && kh3ap::is_uobject_valid_fast(state_class)
                                            ? state_class->GetName()
                                            : STR("<invalid>"));
                    }
                    return false;
                }

                if (state_asset)
                {
                    *state_asset = PlayerBadStatusStateAsset{
                        .state_class = state_class,
                        .state_name = state_name,
                    };
                }
                cache_player_bad_status_state_asset(spec, PlayerBadStatusStateAsset{
                                                              .state_class = state_class,
                                                              .state_name = state_name,
                                                          });
                return true;
            }

            if (failure_details)
            {
                *failure_details = STR("direct_player_bad_status_state_asset_not_found");
            }
            return false;
        }
    };

    BadStatusStateDiscovery::BadStatusStateDiscovery() : impl(std::make_unique<Impl>())
    {
    }
    BadStatusStateDiscovery::~BadStatusStateDiscovery() = default;

    auto BadStatusStateDiscovery::find_asset(UObject* component,
                                             const DirectPlayerBadStatusSpec& spec,
                                             PlayerBadStatusStateAsset* asset,
                                             std::wstring* failure) -> bool
    {
        return impl->try_find_player_bad_status_state_asset(component, spec, asset, failure);
    }

    auto BadStatusStateDiscovery::prewarm_context(UObject* pawn, UObject* component,
                                                  UObject* current) -> LiveBadStatusContext
    {
        if (!is_uobject_valid_fast(pawn))
        {
            return {};
        }
        if (!is_uobject_valid_fast(component))
        {
            component = impl->properties.read_object(pawn, L"MyStateComp");
        }
        if (!is_uobject_valid_fast(component))
        {
            return {};
        }
        impl->properties.resolve(component, L"m_pCurrentState");
        impl->properties.resolve(component, L"MyStateAsset");
        impl->properties.resolve(component, L"m_RecycleState");
        if (!is_uobject_valid_fast(current))
        {
            current = impl->properties.read_object(component, L"m_pCurrentState");
        }
        return {component, current};
    }
} // namespace kh3ap::bad_status_detail
