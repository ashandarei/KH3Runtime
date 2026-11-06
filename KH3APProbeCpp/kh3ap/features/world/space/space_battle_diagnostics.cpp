#include <array>
#include <chrono>
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
#include "kh3ap/features/world/space/space_service_impl.h"
#include "kh3ap/features/world/space/space_profile.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/bridge/transport/bridge_io_helpers.h"
#include "kh3ap/bridge/serialization/json_fields.h"
#include "kh3ap/bridge/utilities.h"
#include <Unreal/AActor.hpp>
#include <Unreal/GameplayStatics.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UActorComponent.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/FProperty.hpp>
#include <algorithm>
#include <cstring>
#include <sstream>
#include <stdexcept>

namespace kh3ap
{
    using namespace RC::Unreal;
    namespace
    {
        constexpr auto flight_damage_vtable_offset = 0xd38;
        constexpr auto point_damage_vtable_offset = 0x570;
        constexpr auto egs_point_damage_handler_rva = 0x1252540;
        constexpr auto egs_point_damage_type_id_rva = 0x124fd90;
        constexpr auto egs_point_damage_event_vtable_rva = 0x797d228;
        constexpr auto point_damage_type_id_vtable_offset = sizeof(void*);
        constexpr auto point_damage_event_bytes = 0xd0;
        constexpr auto point_damage_type_offset = 8;
        constexpr auto point_damage_amount_offset = 0x10;
        constexpr auto point_damage_shot_direction_offset = 0x14;
        constexpr auto point_damage_hit_flags_offset = 0x20;
        constexpr auto point_damage_hit_location_offset = 0x2c;
        constexpr auto point_damage_hit_impact_point_offset = 0x38;
        constexpr auto point_damage_hit_normal_offset = 0x44;
        constexpr auto point_damage_hit_impact_normal_offset = 0x50;
        constexpr auto point_damage_hit_trace_start_offset = 0x5c;
        constexpr auto point_damage_hit_trace_end_offset = 0x68;
        constexpr auto point_damage_hit_item_offset = 0x78;
        constexpr auto point_damage_hit_unset_index_offset = 0xa0;
        constexpr auto point_damage_hit_actor_offset = 0x84;
        constexpr auto point_damage_hit_component_offset = 0x8c;
        constexpr auto colossus_damage_hp_divisor = 4.f;
        constexpr auto pulse_required_battle_result = 2;

        constexpr auto max_space_battle_actor_results = 512;
        constexpr auto max_colossus_components = 512;
        constexpr auto max_space_damage_parameter_bytes = 128;
        constexpr auto default_space_protection_seconds = 60;
        constexpr auto max_space_protection_seconds = 300;
        constexpr auto default_space_input_hold_ms = 100;
        constexpr auto min_space_input_hold_ms = 50;
        constexpr auto max_space_input_hold_ms = 2000;
        constexpr auto space_battle_approach_distance = 25000.0f;
        template <class T> T call(UObject* object, const wchar_t* name, T params = {})
        {
            auto* fn =
                is_uobject_valid_fast(object) ? object->GetFunctionByNameInChain(name) : nullptr;
            if (!is_uobject_valid_fast(fn) || fn->GetParmsSize() != sizeof(T))
            {
                throw std::runtime_error("battle_function_layout:" + to_utf8(name));
            }
            object->ProcessEvent(fn, &params);
            return params;
        }
        using Vector = std::array<float, 3>;
        void event(UObject* object, const wchar_t* name)
        {
            auto* fn =
                is_uobject_valid_fast(object) ? object->GetFunctionByNameInChain(name) : nullptr;
            if (!is_uobject_valid_fast(fn) || fn->GetParmsSize() != 0)
            {
                throw std::runtime_error("battle_event_layout");
            }
            object->ProcessEvent(fn, nullptr);
        }
        std::vector<AActor*> actors(UObject* pawn, const wchar_t* path)
        {
            auto* cls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, path);
            if (!is_uobject_valid_fast(cls))
            {
                throw std::runtime_error("battle_class_unavailable");
            }
            std::vector<AActor*> result;
            UGameplayStatics::GetAllActorsOfClass(pawn, cls, result);
            if (result.size() > max_space_battle_actor_results)
            {
                throw std::runtime_error("battle_actor_limit");
            }
            return result;
        }
        std::wstring path(UObject* obj)
        {
            return is_uobject_valid_fast(obj) ? obj->GetPathName() : L"";
        }
        void vec(std::ostream& out, Vector v)
        {
            out << '[' << v[0] << ',' << v[1] << ',' << v[2] << ']';
        }
        void input(UObject* controller, const std::wstring& key_name, bool pressed)
        {
            const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
            const auto& profile = space::native_profile();
            if (!profile.supported || !is_uobject_valid_fast(controller) ||
                *reinterpret_cast<uintptr_t*>(controller) !=
                    base + profile.flight_controller_vtable ||
                *reinterpret_cast<uintptr_t*>(base + profile.flight_controller_vtable +
                                              flight_damage_vtable_offset) !=
                    base + profile.flight_input_rva ||
                std::memcmp(reinterpret_cast<void*>(base + profile.flight_input_rva),
                            profile.flight_input_bytes.data(), profile.flight_input_bytes.size()))
            {
                throw std::runtime_error("flight_input_build_mismatch");
            }
            struct Key
            {
                FName name;
                uintptr_t details{}, shared{};
            } key{FName{key_name.c_str()}};
            reinterpret_cast<bool (*)(UObject*, void*, int32_t, float, bool)>(
                base + profile.flight_input_rva)(controller, &key, pressed ? 0 : 1,
                                                 pressed ? 1.f : 0.f, true);
        }
        float colossus_damage(UObject* enemy, UObject* pawn, UObject* controller, int hp)
        {
            // EGS FTresGumiShipDamageEvent, verified against reflected HitInfo layout
            // and the native TakeDamage reader. Colossus requires a hit component;
            // generic FDamageEvent leaves it null and crashes its weak-point handler.
            const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
            const std::array<uint8_t, 6> type_id{0xb8, 0x10, 0x27, 0, 0, 0xc3};
            const auto vtable = *reinterpret_cast<uintptr_t*>(enemy);
            if (*reinterpret_cast<uintptr_t*>(vtable + point_damage_vtable_offset) !=
                    base + egs_point_damage_handler_rva ||
                *reinterpret_cast<uintptr_t*>(base + egs_point_damage_event_vtable_rva +
                                              point_damage_type_id_vtable_offset) !=
                    base + egs_point_damage_type_id_rva ||
                std::memcmp(reinterpret_cast<void*>(base + egs_point_damage_type_id_rva),
                            type_id.data(), type_id.size()))
            {
                throw std::runtime_error("colossus_damage_build_mismatch");
            }
            auto* cls = UObjectGlobals::StaticFindObject<UClass*>(
                nullptr, nullptr, L"/Script/Engine.PrimitiveComponent");
            auto* damage_type = UObjectGlobals::StaticFindObject<UClass*>(
                nullptr, nullptr, L"/Script/Engine.DamageType");
            if (!is_uobject_valid_fast(cls) || !is_uobject_valid_fast(damage_type))
            {
                throw std::runtime_error("colossus_damage_classes_missing");
            }
            auto components = static_cast<AActor*>(enemy)->K2_GetComponentsByClass(cls);
            if (components.Num() > max_colossus_components)
            {
                throw std::runtime_error("colossus_component_limit");
            }
            const float amount =
                std::min(static_cast<float>(hp + 1),
                         std::max(1.f, call<int>(enemy, L"GetHPMax") / colossus_damage_hp_divisor));
            float total = 0;
            for (auto* component : components)
            {
                if (!is_uobject_valid_fast(component) ||
                    call<uint8_t>(component, L"GetCollisionEnabled") == 0)
                {
                    continue;
                }
                alignas(16) std::array<std::byte, point_damage_event_bytes> event{};
                const auto put = [&](size_t offset, const auto& value)
                {
                    std::memcpy(event.data() + offset, &value, sizeof(value));
                };
                put(0, base + egs_point_damage_event_vtable_rva);
                put(point_damage_type_offset, damage_type);
                put(point_damage_amount_offset, amount);
                put(point_damage_shot_direction_offset, Vector{1, 0, 0});
                put(point_damage_hit_flags_offset, uint8_t{1});
                const auto point = call<Vector>(component, L"K2_GetComponentLocation");
                put(point_damage_hit_location_offset, point);
                put(point_damage_hit_impact_point_offset, point);
                put(point_damage_hit_normal_offset, Vector{1, 0, 0});
                put(point_damage_hit_impact_normal_offset, Vector{1, 0, 0});
                put(point_damage_hit_trace_start_offset,
                    call<Vector>(pawn, L"K2_GetActorLocation"));
                put(point_damage_hit_trace_end_offset, point);
                put(point_damage_hit_item_offset, int32_t{-1});
                put(point_damage_hit_unset_index_offset, int32_t{-1});
                FWeakObjectPtr actor, hit;
                actor = enemy;
                hit = component;
                static_assert(sizeof(FWeakObjectPtr) == 8);
                put(point_damage_hit_actor_offset, actor);
                put(point_damage_hit_component_offset, hit);
                total += reinterpret_cast<float (*)(UObject*, float, void*, UObject*, UObject*)>(
                    base + egs_point_damage_handler_rva)(enemy, amount, event.data(), controller,
                                                         pawn);
                if (call<int>(enemy, L"GetHP") != hp || call<bool>(enemy, L"IsDeadOrDisappear"))
                {
                    break;
                }
            }
            return total;
        }
        float damage(UObject* enemy, UObject* pawn, UObject* controller, int hp, bool boss)
        {
            auto* colossus = UObjectGlobals::StaticFindObject<UClass*>(
                nullptr, nullptr, L"/Script/TresGame.TresGumiShipEnemy626Pawn");
            if (is_uobject_valid_fast(colossus) && enemy->IsA(colossus))
            {
                return colossus_damage(enemy, pawn, controller, hp);
            }
            auto* lib = UObjectGlobals::StaticFindObject<UObject*>(
                nullptr, nullptr, L"/Script/Engine.Default__GameplayStatics");
            auto* fn = is_uobject_valid_fast(lib) ? lib->GetFunctionByNameInChain(L"ApplyDamage")
                                                  : nullptr;
            if (!is_uobject_valid_fast(fn) || fn->GetParmsSize() > max_space_damage_parameter_bytes)
            {
                throw std::runtime_error("battle_damage_layout");
            }
            std::vector<std::byte> params(fn->GetParmsSize());
            // Bosses can reject a lethal hit before their HP-triggered phase change.
            // Use quarter-health pulses so native transition/death gates remain in charge.
            const auto limit =
                boss ? std::max(1.0, call<int>(enemy, L"GetHPMax") / 4.0) : 1000000.0;
            if (!write_float_function_param(find_function_param_by_name(fn, L"BaseDamage"),
                                            params.data(),
                                            std::min(limit, static_cast<double>(hp) + 1)) ||
                !write_object_function_param(fn, params.data(), L"DamagedActor", enemy) ||
                !write_object_function_param(fn, params.data(), L"EventInstigator", controller) ||
                !write_object_function_param(fn, params.data(), L"DamageCauser", pawn))
            {
                throw std::runtime_error("battle_damage_parameters");
            }
            lib->ProcessEvent(fn, params.data());
            auto* returned = find_function_param_by_name(fn, L"ReturnValue");
            if (!returned || returned->GetElementSize() != sizeof(float))
            {
                throw std::runtime_error("battle_damage_result_layout");
            }
            if (*returned->ContainerPtrToValuePtr<float>(params.data()) == 0 && hp > 1 &&
                limit >= hp && boss)
            {
                write_float_function_param(find_function_param_by_name(fn, L"BaseDamage"),
                                           params.data(), hp - 1.0);
                lib->ProcessEvent(fn, params.data());
            }
            return *returned->ContainerPtrToValuePtr<float>(params.data());
        }
    } // namespace
    void RuntimeSpaceService::Impl::poll_ship_protection(std::chrono::steady_clock::time_point now)
    {
        if (auto* controller = battle_input_controller.Get();
            controller && now >= battle_input_release)
        {
            try
            {
                input(controller, battle_input_key, false);
            }
            catch (...)
            {
            }
            battle_input_controller = nullptr;
        }
        auto* pawn = protected_ship.Get();
        if (!is_uobject_valid_fast(pawn))
        {
            protected_ship = nullptr;
            for (auto& fixture : enabled_battle_fixtures)
            {
                if (auto* actor = fixture.Get(); is_uobject_valid_fast(actor))
                {
                    try
                    {
                        event(actor, L"Disable");
                    }
                    catch (...)
                    {
                    }
                }
            }
            enabled_battle_fixtures.clear();
            return;
        }
        try
        {
            if (now >= ship_protection_deadline ||
                pawn != RuntimeSpaceService::diagnostic_flight_pawn())
            {
                call<bool>(pawn, L"SetInvincible", ship_original_invincible);
                protected_ship = nullptr;
                for (auto& fixture : enabled_battle_fixtures)
                {
                    if (auto* actor = fixture.Get(); is_uobject_valid_fast(actor))
                    {
                        event(actor, L"Disable");
                    }
                }
                enabled_battle_fixtures.clear();
            }
            else if (!call<bool>(pawn, L"IsInvincible"))
            {
                call<bool>(pawn, L"SetInvincible", true);
            }
        }
        catch (...)
        {
            protected_ship = nullptr;
        }
    }
    void RuntimeSpaceService::Impl::battle_protect(const std::string& text,
                                                   const BattleCommandContext& context)
    {

        const auto seconds = JsonFieldReader::int64_field(text, "seconds")
                                 .value_or(default_space_protection_seconds);
        if (seconds < 1 || seconds > max_space_protection_seconds)
        {
            throw std::runtime_error("protection_lease_1_to_300_seconds_required");
        }
        if (protected_ship.Get() && protected_ship.Get() != context.pawn)
        {
            throw std::runtime_error("previous_ship_lease_active");
        }
        if (!protected_ship.Get())
        {
            ship_original_invincible = call<bool>(context.pawn, L"IsInvincible");
            protected_ship = context.pawn;
        }
        ship_protection_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
        call<bool>(context.pawn, L"SetInvincible", true);
    }

    void RuntimeSpaceService::Impl::battle_restore()
    {

        ship_protection_deadline = {};
        poll_ship_protection(std::chrono::steady_clock::now());
        for (auto& fixture : enabled_battle_fixtures)
        {
            if (auto* actor = fixture.Get(); is_uobject_valid_fast(actor))
            {
                event(actor, L"Disable");
            }
        }
        enabled_battle_fixtures.clear();
    }

    void RuntimeSpaceService::Impl::battle_enable_fixture(const BattleCommandContext& context,
                                                          std::ostream& body)
    {

        if (!context.selected || context.active || !delivery_context(true) ||
            protected_ship.Get() != context.pawn)
        {
            throw std::runtime_error("protected_idle_flight_and_exact_starter_required");
        }
        const bool was_enabled = call<bool>(context.selected, L"IsEnabled");
        if (!was_enabled)
        {
            FWeakObjectPtr fixture;
            fixture = context.selected;
            enabled_battle_fixtures.push_back(fixture);
            event(context.selected, L"Enable");
        }
        body << ",\"starter_original_enabled\":" << (was_enabled ? "true" : "false")
             << ",\"starter_enabled_readback\":"
             << (call<bool>(context.selected, L"IsEnabled") ? "true" : "false");
    }

    void RuntimeSpaceService::Impl::battle_approach(const BattleCommandContext& context,
                                                    std::ostream& body)
    {

        if (!context.selected || context.active || !delivery_context(true))
        {
            throw std::runtime_error("idle_flight_and_exact_starter_required");
        }
        if (!call<bool>(context.selected, L"IsEnabled"))
        {
            throw std::runtime_error("battle_starter_disabled");
        }
        auto* trigger = object_field(context.selected, L"m_pRailShootingBattleStartTrigger");
        if (!trigger)
        {
            trigger = object_field(context.selected, L"m_pFieldBattleTriggerVolume");
        }
        if (!trigger)
        {
            throw std::runtime_error("battle_entrance_missing");
        }
        struct Warp
        {
            Vector location, rotation;
        };
        auto location = call<Vector>(trigger, L"K2_GetActorLocation");
        // Park outside the entrance for marker readback without starting
        // a replay. Coordinates come from this exact loaded trigger.
        if (context.action == L"near")
        {
            location[0] -= space_battle_approach_distance;
        }
        call<Warp>(context.pawn, L"RequestWarp",
                   {location, call<Vector>(context.selected, L"K2_GetActorRotation")});
        body << ",\"warp_target\":";
        vec(body, location);
    }

    void RuntimeSpaceService::Impl::battle_input(const std::string& text)
    {

        const auto key = JsonFieldReader::string_field(text, "key").value_or(L"");
        const auto duration = JsonFieldReader::int64_field(text, "milliseconds")
                                  .value_or(default_space_input_hold_ms);
        if (battle_input_controller.Get() || duration < min_space_input_hold_ms ||
            duration > max_space_input_hold_ms ||
            (key != L"Gamepad_FaceButton_Bottom" && key != L"Gamepad_FaceButton_Right" &&
             key != L"Gamepad_FaceButton_Top" && key != L"Gamepad_RightTrigger"))
        {
            throw std::runtime_error("bounded_flight_action_required");
        }
        input(space_controller.Get(), key, true);
        battle_input_controller = space_controller;
        battle_input_key = key;
        battle_input_release =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(duration);
    }

    void RuntimeSpaceService::Impl::apply_battle_action(const std::string& text,
                                                        const BattleCommandContext& context,
                                                        std::ostream& body)
    {
        if (context.action == L"protect")
        {
            battle_protect(text, context);
        }
        else if (context.action == L"restore")
        {
            battle_restore();
        }
        else if (context.action == L"enable_for_test")
        {
            battle_enable_fixture(context, body);
        }
        else if (context.action == L"approach" || context.action == L"near")
        {
            battle_approach(context, body);
        }
        else if (context.action == L"input")
        {
            battle_input(text);
        }
        else if (context.action != L"inspect" && context.action != L"pulse")
        {
            throw std::runtime_error("unknown_battle_test_action");
        }
        if (context.action == L"pulse" &&
            (!context.selected || context.active != context.selected || !delivery_context(false) ||
             call<int>(context.pawn, L"GetHP") <= 0 || !is_uobject_valid_fast(context.manager) ||
             *(reinterpret_cast<uint8_t*>(context.manager) +
               space::native_profile().battle_result) != pulse_required_battle_result))
        {
            throw std::runtime_error("expected_active_battle_required");
        }
    }

    void RuntimeSpaceService::Impl::append_battle_snapshot(const BattleCommandContext& context,
                                                           std::ostream& body)
    {
        body << ",\"pawn\":\"" << json_escape(path(context.pawn))
             << "\",\"hp\":" << call<int>(context.pawn, L"GetHP")
             << ",\"invincible\":" << (call<bool>(context.pawn, L"IsInvincible") ? "true" : "false")
             << ",\"protection_active\":" << (protected_ship.Get() ? "true" : "false")
             << ",\"original_invincible\":" << (ship_original_invincible ? "true" : "false")
             << ",\"active_starter\":\"" << json_escape(path(context.active)) << "\",\"position\":";
        vec(body, call<Vector>(context.pawn, L"K2_GetActorLocation"));
        if (is_uobject_valid_fast(context.manager))
        {
            body << ",\"battle_result\":"
                 << static_cast<int>(*(reinterpret_cast<uint8_t*>(context.manager) +
                                       space::native_profile().battle_result));
        }
        body << ",\"battle_markers\":";
        rewards.inspect_battle_markers(body);
        body << ",\"starters\":[";
        bool first = true;
        for (auto* starter : context.starters)
        {
            if (!first)
            {
                body << ',';
            }
            first = false;
            body << "{\"path\":\"" << json_escape(path(starter))
                 << "\",\"enabled\":" << (call<bool>(starter, L"IsEnabled") ? "true" : "false")
                 << '}';
        }
        append_battle_enemies(context, body);
        append_battle_parts(context, body);
    }

    void RuntimeSpaceService::Impl::append_battle_enemies(const BattleCommandContext& context,
                                                          std::ostream& body)
    {
        body << "],\"enemies\":[";
        bool first = true;
        for (auto* enemy : actors(context.pawn, L"/Script/TresGame.TresGumiShipEnemyPawnBase"))
        {
            if (!is_uobject_valid_fast(enemy))
            {
                continue;
            }
            auto* owner = call<UObject*>(enemy, L"GetCurrentEnemyBattleStarter");
            const auto hp = call<int>(enemy, L"GetHP");
            float applied = 0;
            bool requested = false;
            if (context.action == L"pulse" && owner == context.selected && hp > 0 &&
                !call<bool>(enemy, L"IsDeadOrDisappear") && !call<bool>(enemy, L"IsInvincible"))
            {
                applied = damage(enemy, context.pawn, space_controller.Get(), hp,
                                 call<bool>(enemy, L"IsBossEnemy"));
                requested = true;
            }
            if (!first)
            {
                body << ',';
            }
            first = false;
            body << "{\"path\":\"" << json_escape(path(enemy)) << "\",\"starter\":\""
                 << json_escape(path(owner)) << "\",\"hp_before\":" << hp
                 << ",\"hp_after\":" << call<int>(enemy, L"GetHP")
                 << ",\"invincible\":" << (call<bool>(enemy, L"IsInvincible") ? "true" : "false")
                 << ",\"dead\":" << (call<bool>(enemy, L"IsDeadOrDisappear") ? "true" : "false")
                 << ",\"damage_requested\":" << (requested ? "true" : "false")
                 << ",\"damage_returned\":" << applied << '}';
        }
    }

    void RuntimeSpaceService::Impl::append_battle_parts(const BattleCommandContext& context,
                                                        std::ostream& body)
    {
        body << "],\"parts\":[";
        bool first = true;
        // Boss cores/walls are separate character actors. Damage their actual
        // parts so native forwarding and vulnerability gates control the boss HP.
        for (auto* part : actors(context.pawn, L"/Script/TresGame.TresGumiShipEnemyPartsBase"))
        {
            if (!is_uobject_valid_fast(part))
            {
                continue;
            }
            auto* enemy = call<UObject*>(part, L"GetPartsOwnerEnemy");
            if (!is_uobject_valid_fast(enemy))
            {
                continue;
            }
            auto* owner = call<UObject*>(enemy, L"GetCurrentEnemyBattleStarter");
            if (!context.active || owner != context.active)
            {
                continue;
            }
            const auto hp = call<int>(part, L"GetHP");
            float applied = 0;
            bool requested = false;
            // Only the verified fortress parts use this generic damage path.
            // Other bosses include mechanical/effect actors with incompatible
            // damage handlers, despite sharing the same parts base class.
            const bool fortress = path(enemy).find(L".e_gm622_0_Pawn_C_") != std::wstring::npos;
            if (context.action == L"pulse" && fortress && owner == context.selected && hp > 0 &&
                !call<bool>(part, L"IsDead") && !call<bool>(part, L"IsInvincible"))
            {
                applied = damage(part, context.pawn, space_controller.Get(), hp, true);
                requested = true;
            }
            if (!first)
            {
                body << ',';
            }
            first = false;
            body << "{\"path\":\"" << json_escape(path(part)) << "\",\"enemy\":\""
                 << json_escape(path(enemy)) << "\",\"hp_before\":" << hp
                 << ",\"hp_after\":" << call<int>(part, L"GetHP")
                 << ",\"invincible\":" << (call<bool>(part, L"IsInvincible") ? "true" : "false")
                 << ",\"damage_requested\":" << (requested ? "true" : "false")
                 << ",\"damage_returned\":" << applied << '}';
        }
        body << ']';
    }

    void RuntimeSpaceService::Impl::battle_command(const std::string& text,
                                                   const std::wstring& request, std::wstring reason)
    {
        std::ostringstream body;
        const auto action = JsonFieldReader::string_field(text, "action").value_or(L"inspect");
        try
        {
            if (!reason.empty())
            {
                throw std::runtime_error("request_guard_rejected");
            }
            auto* pawn = RuntimeSpaceService::diagnostic_flight_pawn();
            if (!is_uobject_valid_fast(pawn) || current_world != world_name())
            {
                throw std::runtime_error("ready_flight_required");
            }
            const auto modes = actors(pawn, L"/Script/TresGame.TresGumiShipGameMode");
            if (modes.size() != 1)
            {
                throw std::runtime_error("unique_flight_game_mode_required");
            }
            auto* mode = modes.front();
            auto* manager = object_field(mode, L"m_pMissionMgr");
            auto* active = object_field(mode, L"m_pCurrentEnemyBattleStarter");
            const auto expected = JsonFieldReader::string_field(text, "starter").value_or(L"");
            const auto starters = actors(pawn, L"/Script/TresGame.TresGumiShipEnemyBattleStarter");
            UObject* selected = nullptr;
            for (auto* starter : starters)
            {
                if (path(starter) == expected)
                {
                    selected = starter;
                }
            }
            const BattleCommandContext context{action, pawn, manager, active, selected, starters};
            apply_battle_action(text, context, body);
            append_battle_snapshot(context, body);
        }
        catch (const std::exception& error)
        {
            if (reason.empty())
            {
                std::string e = error.what();
                reason.assign(e.begin(), e.end());
            }
        }
        std::ostringstream out;
        out << "{\"request_id\":\"" << json_escape(request)
            << "\",\"process_id\":" << GetCurrentProcessId() << ",\"status\":\""
            << (reason.empty() ? "completed" : "failed") << "\",\"reason\":\""
            << json_escape(reason) << "\",\"action\":\"" << json_escape(action) << '"'
            << (reason.empty() ? body.str() : "") << "}\n";
        write_text_file_atomic_verified(access.bridge / L"runtime_space_result.json", out.str());
    }
} // namespace kh3ap
#endif
