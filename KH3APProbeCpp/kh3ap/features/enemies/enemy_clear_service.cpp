#include "kh3ap/features/enemies/enemy_clear/service_impl.h"
#include "kh3ap/features/enemies/enemy_clear/support.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include "kh3ap/features/player/characters/character_swap.h"

#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <Unreal/UObjectGlobals.hpp>

#include <algorithm>
#include <stdexcept>
#include <cstring>
#include <utility>

namespace kh3ap
{
    using namespace RC::Unreal;
    using namespace enemy_clear_detail;

    auto runtime_damage_friend_for_test(UObject* pawn, UObject* target) -> bool
    {
        if (!is_uobject_valid_fast(pawn) || !is_uobject_valid_fast(target) || !runtime_sora_control_ready(pawn)) return false;
        auto* library = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, L"/Script/Engine.Default__GameplayStatics");
        auto* damage_type = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, L"/Script/TresGame.TresDamageType");
        if (!is_uobject_valid_fast(damage_type)) return false;
        Call c(library, L"ApplyDamage");
        auto* controller = try_read_reflected_object_property_value(pawn, L"Controller");
        const double amount = static_cast<double>(std::max<int64_t>(1, integer(target, L"GetMaxHitPoint"))) + 1.0;
        if (!write_object_function_param(c.function, c.params.data(), L"DamagedActor", target) ||
            !write_object_function_param(c.function, c.params.data(), L"EventInstigator", controller) ||
            !write_object_function_param(c.function, c.params.data(), L"DamageCauser", pawn) ||
            !write_float_function_param(c.field(L"BaseDamage"), c.params.data(), amount))
            throw std::runtime_error("unsupported_checkpoint_damage_parameters");
        auto* type = c.field(L"DamageTypeClass");
        if (type->GetClass().GetName() != L"ClassProperty" || type->GetElementSize() != sizeof(UClass*))
            throw std::runtime_error("unsupported_checkpoint_damage_type");
        std::memcpy(type->ContainerPtrToValuePtr<void>(c.params.data()), &damage_type, sizeof(damage_type));
        // Exercise ordinary HP/death processing and its observers. Do not open
        // only the presentation or bypass invulnerability/Once More rules.
        c.invoke(false);
        return true;
    }
    auto runtime_damage_sora_for_checkpoint(UObject* pawn) -> bool
    {
        return runtime_damage_friend_for_test(pawn, pawn);
    }
    RuntimeEnemyClearService::RuntimeEnemyClearService(RuntimeEnemyClearConfig config) : impl_(std::make_unique<Impl>(std::move(config))) {}
    RuntimeEnemyClearService::~RuntimeEnemyClearService() = default;
    void RuntimeEnemyClearService::tick() { impl_->tick(); }
    bool RuntimeEnemyClearService::arm_opening_protection(int seconds) {
        auto* self=Impl::instance;
        if(!self || seconds<1 || seconds>maximum_center_duration_seconds)return false;
        self->opening_protection_expires=unix_ms()+seconds*1000;
        self->opening_protection_seen=false;
        self->opening_discovery_at={};
        self->opening_controller=nullptr;self->opening_world=nullptr;
        return true;
    }
    UObject* RuntimeEnemyClearService::Impl::controlled_combat_pawn()
    {
        const auto object = [](UObject* value, const wchar_t* field) -> UObject*
        {
            return is_uobject_valid_fast(value) ? try_read_reflected_object_property_value(value, field) : nullptr;
        };
        auto* viewport = kh02_current_viewport();
        auto* manager = object(object(viewport, L"GameInstance"), L"m_UIManager");
        auto* controller = object(manager, L"m_pPlayerController");
        auto* pawn = object(controller, L"Pawn");
        auto* local_player = object(controller, L"Player");
        return is_uobject_valid_fast(pawn) && object(pawn, L"Controller") == controller &&
                       object(local_player, L"PlayerController") == controller &&
                       kh02_actor_world(pawn) == kh02_current_world()
                   ? pawn : nullptr;
    }

    UObject* RuntimeEnemyClearService::Impl::request_pawn()
    {
        // Exploration diagnostics may follow only an alternate owned by the
        // normal swap service. This does not widen AP item-grant contexts.
        if (command == L"inspect_scene" || command == L"inspect_transform" ||
            command == L"open_chest" || command == L"set_position" ||
            command == L"look_gameplay" || command == L"move_gameplay" ||
            command == L"inspect_emblems" || command == L"look_camera")
        {
            auto* pawn = controlled_combat_pawn();
            if (pawn && character_swap::current_hero() == pawn) { return pawn; }
        }
        // Combat diagnostics follow local possession, including story characters.
        // Inventory, experience, world interactions and AP grants retain their
        // existing Sora-only context rules.
        if (command == L"inspect" || command == L"damage_pulse" || command == L"sora_protection" ||
            command == L"press_action" || command == L"attack_combo" || command == L"union_cross_input" ||
            command == L"defeat_all")
        {
            return controlled_combat_pawn();
        }
        return config.playable_sora();
    }

    bool RuntimeEnemyClearService::Impl::gameplay(UObject* pawn)
    {
        mutation_context_ready = config.gameplay_ready();
        mutation_control_ready = runtime_sora_control_ready(pawn);
        if (!mutation_context_ready || !mutation_control_ready) return false;
        auto* library = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, L"/Script/TresGame.Default__TresGameBlueprintLibrary");
        if (!boolean(library, L"TresUI_IsClosedLoadingScreen")) return false;
        auto* gameplay_statics = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, L"/Script/Engine.Default__GameplayStatics");
        Call paused(gameplay_statics, L"IsGamePaused");
        if (!write_object_function_param(paused.function, paused.params.data(), L"WorldContextObject", pawn)) return false;
        paused.invoke();
        auto* result = paused.field(L"ReturnValue");
        if (result->GetClass().GetName() != L"BoolProperty") return false;
        return !static_cast<FBoolProperty*>(result)->GetPropertyValueInContainer(paused.params.data());
    }

    RuntimeEnemyClearService::Impl::Impl(RuntimeEnemyClearConfig c) : config(c), mom_barrier(c)
    {instance=this;}

    RuntimeEnemyClearService::Impl::~Impl()
    { for(auto& [id,gate]:chest_gates)try { remove_chest_notice(gate); } catch(...) {} if(notice_instance==this)notice_instance=nullptr; if(instance==this)instance=nullptr; }

}
