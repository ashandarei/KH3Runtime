#include "bad_status_runtime.h"
#include "kh3ap/core/reflection/reflected_property_cache.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/features/grants/grant_internal_call_depth.h"
#include "kh3ap/features/player/objects/object_resolver.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/AActor.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/FScriptArray.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <algorithm>
#include <cstddef>
#include <vector>

namespace kh3ap::bad_status_detail
{
    using namespace RC;
    using namespace RC::Unreal;
    namespace
    {
        constexpr std::wstring_view s_particle_system_component_class_path =
            STR("/Script/Engine.ParticleSystemComponent");
        constexpr std::wstring_view s_actor_get_components_by_class_function_path =
            STR("/Script/Engine.Actor:K2_GetComponentsByClass");
        constexpr std::wstring_view s_actor_component_deactivate_function_path =
            STR("/Script/Engine.ActorComponent:Deactivate");
        constexpr std::wstring_view s_actor_component_deactivate_function_name = STR("Deactivate");
    } // namespace
    struct BadStatusParticles::Impl
    {
        struct ActorComponentQueryCache
        {
            FWeakObjectPtr function;
            FProperty* component_class_property{};
            FProperty* return_property{};
            int params_size{};
            auto ready() const -> bool
            {
                return function.Get() && component_class_property && return_property &&
                       params_size > 0;
            }
        };
        ActorComponentQueryCache component_query;
        FWeakObjectPtr particle_component_class;
        FWeakObjectPtr deactivate_function_cache;
        ReflectedPropertyCache<8> particle_properties;

        auto invoke_no_arg_function(UObject* object, std::wstring_view function_name) -> bool
        {
            if (!object)
            {
                return false;
            }
            auto* const cached_deactivate =
                static_cast<UFunction*>(deactivate_function_cache.Get());
            UFunction* function_object =
                function_name == s_actor_component_deactivate_function_name && cached_deactivate
                    ? cached_deactivate
                    : object->GetFunctionByNameInChain(std::wstring{function_name}.c_str());
            if (!function_object)
            {
                return false;
            }
            if (function_name == s_actor_component_deactivate_function_name)
            {
                deactivate_function_cache = make_uobject_weak(function_object);
            }

            std::vector<std::byte> params(static_cast<size_t>(function_object->GetParmsSize()));
            std::fill(params.begin(), params.end(), std::byte{0});
            const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
            object->ProcessEvent(function_object, params.empty() ? nullptr : params.data());
            return true;
        }

        auto resolve_particle_system_component_class() -> UClass*
        {
            if (auto* cached = static_cast<UClass*>(particle_component_class.Get()))
            {
                return cached;
            }
            auto* const type = UObjectGlobals::StaticFindObject<UClass*>(
                nullptr, nullptr, std::wstring{s_particle_system_component_class_path}.c_str());
            particle_component_class = make_uobject_weak(type);
            return type;
        }

        auto resolve_actor_component_query_cache() -> ActorComponentQueryCache*
        {
            if (component_query.ready())
            {
                return &component_query;
            }

            UFunction* const function_object = UObjectGlobals::StaticFindObject<UFunction*>(
                nullptr, nullptr,
                std::wstring{s_actor_get_components_by_class_function_path}.c_str());
            if (!function_object)
            {
                return nullptr;
            }
            FProperty* const component_class_property =
                find_function_param_by_name(function_object, STR("ComponentClass"));
            FProperty* const return_property =
                find_function_param_by_name(function_object, STR("ReturnValue"));
            const int params_size = function_object->GetParmsSize();
            if (!component_class_property || !return_property || params_size <= 0)
            {
                return nullptr;
            }

            component_query = ActorComponentQueryCache{
                .function = make_uobject_weak(function_object),
                .component_class_property = component_class_property,
                .return_property = return_property,
                .params_size = params_size,
            };
            return component_query.ready() ? &component_query : nullptr;
        }

        auto try_read_particle_template(UObject* component) -> UObject*
        {
            if (!component)
            {
                return nullptr;
            }
            auto* const property = particle_properties.resolve(component, L"Template");
            if (property)
            {
                auto** const value = property->ContainerPtrToValuePtr<UObject*>(component);
                return value ? *value : nullptr;
            }
            return try_read_object_pointer_in_chain(component, L"Template");
        }

        auto collect_actor_particle_components(UObject* actor) -> std::vector<UObject*>
        {
            std::vector<UObject*> result{};
            if (!actor)
            {
                return result;
            }
            UClass* const particle_component_class = resolve_particle_system_component_class();
            if (!particle_component_class)
            {
                return result;
            }

            ActorComponentQueryCache* const query_cache = resolve_actor_component_query_cache();
            if (!query_cache)
            {
                const auto components = [&]()
                {
                    const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
                    return static_cast<AActor*>(actor)->K2_GetComponentsByClass(
                        particle_component_class);
                }();
                result.reserve(static_cast<size_t>(std::max(components.Num(), 0)));
                for (UObject* component : components)
                {
                    if (component)
                    {
                        result.push_back(component);
                    }
                }
                return result;
            }

            std::vector<std::byte> params(static_cast<size_t>(query_cache->params_size));
            std::fill(params.begin(), params.end(), std::byte{0});
            UClass** const component_class_value =
                query_cache->component_class_property->ContainerPtrToValuePtr<UClass*>(
                    params.data());
            if (!component_class_value)
            {
                return result;
            }
            *component_class_value = particle_component_class;
            {
                const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
                actor->ProcessEvent(static_cast<UFunction*>(query_cache->function.Get()),
                                    params.data());
            }

            FScriptArray* const components =
                query_cache->return_property->ContainerPtrToValuePtr<FScriptArray>(params.data());
            if (!components)
            {
                return result;
            }
            const int component_count = std::max(components->Num(), 0);
            result.reserve(static_cast<size_t>(component_count));
            auto* const component_values = static_cast<UObject**>(components->GetData());
            if (component_values)
            {
                for (int index = 0; index < component_count; ++index)
                {
                    UObject* const component = component_values[index];
                    if (component)
                    {
                        result.push_back(component);
                    }
                }
            }
            destroy_property_value_dynamic(query_cache->return_property, components);
            return result;
        }

        auto is_bad_status_particle_component(UObject* component,
                                              std::wstring_view particle_template_path_fragment,
                                              std::wstring_view particle_template_name,
                                              std::wstring* template_name) -> bool
        {
            if (template_name)
            {
                template_name->clear();
            }
            if (!component)
            {
                return false;
            }
            UObject* const particle_template = try_read_particle_template(component);
            if (!particle_template)
            {
                return false;
            }
            const std::wstring short_template_name = particle_template->GetName();
            if (!particle_template_name.empty() && short_template_name == particle_template_name)
            {
                if (template_name)
                {
                    *template_name = short_template_name;
                }
                return true;
            }
            if (particle_template_path_fragment.empty())
            {
                return false;
            }
            const std::wstring formatted_template_name =
                format_object_debug_name(particle_template);
            if (template_name)
            {
                *template_name = formatted_template_name;
            }
            return formatted_template_name.find(particle_template_path_fragment) !=
                   std::wstring::npos;
        }
        auto prewarm() -> void
        {
            resolve_particle_system_component_class();
            resolve_actor_component_query_cache();
            if (!deactivate_function_cache.Get())
            {
                UFunction* const deactivate_function = UObjectGlobals::StaticFindObject<UFunction*>(
                    nullptr, nullptr,
                    std::wstring{s_actor_component_deactivate_function_path}.c_str());
                if (deactivate_function && kh3ap::is_uobject_valid_fast(deactivate_function))
                {
                    deactivate_function_cache = make_uobject_weak(deactivate_function);
                }
            }
        }
        auto clean(UObject* sora_pawn, const DirectPlayerBadStatusSpec& spec, UObject* effect_state)
            -> void
        {
            size_t particle_component_count = 0;
            size_t matching_component_count = 0;
            size_t deactivated_count = 0;
            if (!spec.particle_template_path_fragment.empty())
            {
                const std::vector<UObject*> current_components =
                    collect_actor_particle_components(sora_pawn);
                particle_component_count = current_components.size();
                for (UObject* component : current_components)
                {
                    std::wstring template_name{};
                    const bool matches = is_bad_status_particle_component(
                        component, spec.particle_template_path_fragment,
                        spec.particle_template_name,
                        s_enable_direct_player_bad_status_diagnostics ? &template_name : nullptr);
                    if (!matches)
                    {
                        continue;
                    }
                    ++matching_component_count;
                    const bool deactivated = invoke_no_arg_function(
                        component, s_actor_component_deactivate_function_name);
                    if (deactivated)
                    {
                        ++deactivated_count;
                    }
                    if constexpr (s_enable_direct_player_bad_status_diagnostics)
                    {
                        Output::send<LogLevel::Warning>(
                            STR("[KH3APProbeCpp] Direct player bad-status particle cleanup "
                                "status={} component={} template={} deactivated={}\n"),
                            spec.name, format_object_debug_name(component), template_name,
                            deactivated ? STR("true") : STR("false"));
                    }
                }
            }

            if constexpr (s_enable_direct_player_bad_status_diagnostics)
            {
                Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] Direct player bad-status transition completed status={} "
                        "pawn={} state={} particle_current={} matching_components={} "
                        "particle_deactivated={}\n"),
                    spec.name, format_object_debug_name(sora_pawn),
                    format_object_debug_name(effect_state), particle_component_count,
                    matching_component_count, deactivated_count);
            }
        }
    };

    BadStatusParticles::BadStatusParticles() : impl(std::make_unique<Impl>())
    {
    }
    BadStatusParticles::~BadStatusParticles() = default;
    auto BadStatusParticles::prewarm() -> void
    {
        impl->prewarm();
    }
    auto BadStatusParticles::clean(UObject* pawn, const DirectPlayerBadStatusSpec& spec,
                                   UObject* state) -> void
    {
        impl->clean(pawn, spec, state);
    }
} // namespace kh3ap::bad_status_detail
