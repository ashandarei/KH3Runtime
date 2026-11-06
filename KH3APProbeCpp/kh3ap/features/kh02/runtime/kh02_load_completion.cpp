#include "kh02_load_completion.h"
#include "kh02_bootstrap_state.h"
#include "kh3ap/features/world/loading_runtime.h"
#include "kh3ap/features/player/characters/character_profile.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/bridge/utilities.h"
#include <Unreal/UClass.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <nlohmann/json.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#include <UnrealDef.hpp>
#include <stdexcept>

namespace kh3ap
{
    using namespace RC::Unreal;

    namespace
    {
        constexpr size_t delegate_allocation_offset = 0x20;
        constexpr size_t delegate_count_offset = 0x30;
        constexpr size_t delegate_get_object_slot = 0x08;
        constexpr size_t delegate_destructor_slot = 0x48;
        constexpr size_t delegate_copy_slot = 0x50;
        constexpr size_t delegate_execute_safe_slot = 0x68;
        constexpr size_t title_manager_offset = 0x548;
        constexpr size_t game_over_manager_offset = 0xd8;
        constexpr size_t manager_pause_offset = 0x288;
        constexpr uintptr_t game_over_load_completion_vtable = 0x7c2a1c0;
        constexpr uintptr_t game_over_load_completion_callback = 0x1a7f9f0;
        constexpr size_t weak_delegate_method_offset = 0x10;

        auto title_completion_vtable() -> uintptr_t
        {
            return loading_runtime::startup_store == AutoloadStore::steam ? 0x6a57a08 : 0x7c37680;
        }

        auto is_game_over(UObject* object) -> bool
        {
            auto* object_class =
                UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, L"/Script/TresGame.TresUIP_GameOver");
            return is_uobject_valid_fast(object) && is_uobject_valid_fast(object_class) && object->IsA(object_class);
        }

        auto delegate_instance(void* storage) -> void*
        {
            if (!storage || !loading_runtime::read_field<int32_t>(storage, delegate_count_offset))
            {
                return nullptr;
            }
            auto* allocation = loading_runtime::read_field<void*>(storage, delegate_allocation_offset);
            return allocation ? allocation : storage;
        }

        auto virtual_function(void* instance, size_t offset) -> uintptr_t
        {
            const auto table = loading_runtime::read_field<uintptr_t>(instance, 0);
            return loading_runtime::read_field<uintptr_t>(reinterpret_cast<void*>(table), offset);
        }

        auto reject_completion(uintptr_t base, void* source, UObject* request_manager, UObject* bound_object,
                               UObject* resolved_owner, const char* reason) -> UObject*
        {
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            const auto describe = [](UObject* object)
            {
                const bool valid = is_uobject_valid_fast(object);
                nlohmann::json value{{"pointer", reinterpret_cast<uintptr_t>(object)}, {"valid", valid}};
                if (valid)
                {
                    std::wstring full_name;
                    if (try_get_uobject_full_name_fast(object, full_name))
                    {
                        value["full_name"] = to_utf8(full_name);
                    }
                    auto* object_class = object->GetClassPrivate();
                    if (is_uobject_valid_fast(object_class))
                    {
                        const auto class_name = object_class->GetName();
                        value["class"] = to_utf8(class_name);
                    }
                }
                return value;
            };
            auto* instance = delegate_instance(source);
            const auto table = loading_runtime::read_field<uintptr_t>(instance, 0);
            auto* capture = loading_runtime::read_field<UObject*>(instance, sizeof(uintptr_t));
            const auto get_object = virtual_function(instance, delegate_get_object_slot);
            nlohmann::json evidence{
                {"reason", reason},
                {"storage", reinterpret_cast<uintptr_t>(source)},
                {"allocation", loading_runtime::read_field<uintptr_t>(source, delegate_allocation_offset)},
                {"units", loading_runtime::read_field<int32_t>(source, delegate_count_offset)},
                {"instance", reinterpret_cast<uintptr_t>(instance)},
                {"vtable", table},
                {"vtable_rva", table >= base ? table - base : 0},
                {"get_object_rva", get_object >= base ? get_object - base : 0},
                {"bound_object", describe(bound_object)},
                {"capture", describe(capture)},
                {"resolved_owner", describe(resolved_owner)},
                {"request_manager", describe(request_manager)}};
            // This raw capture layout is inspected only for the exact native
            // Continue functor. No unknown delegate gains a new dispatch path.
            if (table == base + title_completion_vtable() && is_uobject_valid_fast(capture))
            {
                evidence["captured_title_manager"] =
                    describe(loading_runtime::read_field<UObject*>(capture, title_manager_offset));
            }
            const auto serialized = evidence.dump();
            RC::Output::send<RC::LogLevel::Warning>(STR("[KH3APProbeCpp] KH02 native load completion rejected {}\n"),
                                                    std::wstring(serialized.begin(), serialized.end()));
#endif
            return nullptr;
        }

    } // namespace

    Kh02LoadCompletion::~Kh02LoadCompletion()
    {
        clear();
    }

    auto Kh02LoadCompletion::supported(uintptr_t image) -> bool
    {
        using character_native::Site;
        if (!character_native::verify_sites({Site::load_title_delegate_copy, Site::load_title_delegate_execute,
                Site::load_game_over_delegate_copy, Site::load_game_over_delegate_execute,
                Site::load_game_over_completion, Site::load_delegate_free}))
        {
            return false;
        }
        const bool steam = loading_runtime::startup_store == AutoloadStore::steam;
        const auto title_table = image + title_completion_vtable();
        const auto game_over_table = image + (steam ? 0x6a43d48 : game_over_load_completion_vtable);
        const auto matches_slot = [&](uintptr_t table, size_t offset, Site site)
        {
            return loading_runtime::read_field<uintptr_t>(reinterpret_cast<void*>(table), offset) ==
                image + character_native::rva(site);
        };
        return matches_slot(title_table, delegate_copy_slot, Site::load_title_delegate_copy) &&
            matches_slot(title_table, delegate_execute_safe_slot, Site::load_title_delegate_execute) &&
            matches_slot(game_over_table, delegate_copy_slot, Site::load_game_over_delegate_copy) &&
            matches_slot(game_over_table, delegate_execute_safe_slot, Site::load_game_over_delegate_execute);
    }

    void Kh02LoadCompletion::clear()
    {
        if (auto* instance = delegate_instance(storage.data()))
        {
            using Destroy = void(__fastcall*)(void*, uint32_t);
            reinterpret_cast<Destroy>(virtual_function(instance, delegate_destructor_slot))(instance, 0);
        }
        if (auto* allocation = loading_runtime::read_field<void*>(storage.data(), delegate_allocation_offset))
        {
            using Free = void(__fastcall*)(void*);
            reinterpret_cast<Free>(base + loading_runtime::native_rva(loading_runtime::free_rva))(allocation);
        }
        storage = {};
    }

    void Kh02LoadCompletion::copy(uintptr_t image, void* source)
    {
        clear();
        base = image;
        auto* instance = delegate_instance(source);
        if (!instance)
        {
            throw std::runtime_error("kh02_bootstrap_missing_completion");
        }
        using Copy = void(__fastcall*)(void*, void*);
        reinterpret_cast<Copy>(virtual_function(instance, delegate_copy_slot))(instance, storage.data());
    }

    void Kh02LoadCompletion::fail_without_load(int32_t slot)
    {
        if (auto* instance = delegate_instance(storage.data()))
        {
            // Use the native unavailable-load failure branch. Result 1
            // is success and must never be synthesized by this gate.
            const Kh02UnavailableLoadCompletion failure{slot};
            using Execute = bool(__fastcall*)(void*, int32_t, int32_t, const void*);
            reinterpret_cast<Execute>(virtual_function(instance, delegate_execute_safe_slot))(
                instance, failure.result, failure.slot, failure.metadata);
        }
    }

    auto Kh02LoadCompletion::resolve_owner(uintptr_t base, void* source, UObject* request_manager) -> UObject*
    {
        auto* instance = delegate_instance(source);
        if (!instance)
        {
            return reject_completion(base, source, request_manager, nullptr, nullptr, "missing_instance");
        }
        using GetObject = UObject*(__fastcall*)(void*);
        auto* bound_object =
            reinterpret_cast<GetObject>(virtual_function(instance, delegate_get_object_slot))(instance);
        auto* result = bound_object;
        if (!is_uobject_valid_fast(result))
        {
            // Continue owns an inspected one-pointer functor capture. Do not
            // interpret captures of any other functor as an object reference.
            if (loading_runtime::startup_store == AutoloadStore::unsupported ||
                loading_runtime::read_field<uintptr_t>(instance, 0) != base + title_completion_vtable())
            {
                return reject_completion(base, source, request_manager, bound_object, result, "unknown_functor");
            }
            result = loading_runtime::read_field<UObject*>(instance, sizeof(uintptr_t));
        }
        if (!is_uobject_valid_fast(result) || !is_uobject_valid_fast(result->GetClassPrivate()))
        {
            return reject_completion(base, source, request_manager, bound_object, result, "invalid_owner");
        }
        auto* title_class =
            UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, L"/Script/TresGame.TresHUDTitle");
        if (is_uobject_valid_fast(title_class) && result->IsA(title_class))
        {
            if (loading_runtime::read_field<UObject*>(result, title_manager_offset) == request_manager)
            {
                return result;
            }
            return reject_completion(base, source, request_manager, bound_object, result, "title_manager_mismatch");
        }
        auto* save_load_class =
            UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, L"/Script/TresGame.TresUIP_SaveLoad");
        if (is_uobject_valid_fast(save_load_class) && result->IsA(save_load_class) &&
            try_read_reflected_object_property_value(result, L"m_pUIManager") == request_manager)
        {
            return result;
        }
        if (is_game_over(result))
        {
            auto* pause = try_read_reflected_object_property_value(result, L"m_Owner");
            auto* pause_class =
                UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, L"/Script/TresGame.TresUIP_Pause");
            const bool steam = loading_runtime::startup_store == AutoloadStore::steam;
            const bool inspected_delegate =
                loading_runtime::read_field<uintptr_t>(instance, 0) ==
                    base + (steam ? 0x6a43d48 : game_over_load_completion_vtable) &&
                loading_runtime::read_field<uintptr_t>(instance, weak_delegate_method_offset) ==
                    base + (steam ? 0x1706d30 : game_over_load_completion_callback);
            if (inspected_delegate && is_uobject_valid_fast(pause) && is_uobject_valid_fast(pause_class) &&
                pause->IsA(pause_class) && loading_runtime::property_offset(result, L"m_Owner", 0xd0) &&
                loading_runtime::property_offset(pause, L"m_GameOver", 0x7b0) &&
                try_read_reflected_object_property_value(pause, L"m_GameOver") == result &&
                loading_runtime::read_field<UObject*>(result, game_over_manager_offset) == request_manager &&
                loading_runtime::read_field<UObject*>(request_manager, steam ? 0x2c8 : manager_pause_offset) == pause)
            {
                return result;
            }
            return reject_completion(base, source, request_manager, bound_object, result, "game_over_owner_mismatch");
        }
        return reject_completion(base, source, request_manager, bound_object, result,
                                 "owner_class_or_manager_mismatch");
    }

    auto Kh02LoadCompletion::is_game_over_owner(UObject* object) -> bool
    {
        return is_game_over(object);
    }
} // namespace kh3ap
