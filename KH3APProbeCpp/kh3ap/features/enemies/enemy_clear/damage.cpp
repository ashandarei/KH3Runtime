#include <array>
#include "kh3ap/features/enemies/enemy_clear/service_impl.h"
#include "kh3ap/features/enemies/enemy_clear/support.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/features/enemies/enemy_remap_service.h"
#include "kh3ap/features/enemies/boss_hp_setup_policy.h"
#include "kh3ap/bridge/serialization/json_fields.h"
#include "kh3ap/bridge/utilities.h"

#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/UObjectGlobals.hpp>

#include <algorithm>
#include <stdexcept>
#include <cstring>
#include <sstream>

namespace kh3ap
{
    namespace
    {
        constexpr std::array allowed_damage_percentages{10, 25, 50};

        constexpr auto max_test_experience_award = 50000;
    } // namespace

    using namespace RC::Unreal;
    using namespace enemy_clear_detail;

    void RuntimeEnemyClearService::Impl::damage(Target& target, UObject* actor, UObject* pawn)
    {
        auto* library = UObjectGlobals::StaticFindObject<UObject*>(
            nullptr, nullptr, L"/Script/Engine.Default__GameplayStatics");
        auto* damage_type = UObjectGlobals::StaticFindObject<UClass*>(
            nullptr, nullptr, L"/Script/TresGame.TresDamageType");
        if (!is_uobject_valid_fast(damage_type))
        {
            throw std::runtime_error("damage_type_unavailable");
        }
        Call c(library, L"ApplyDamage");
        auto* controller = try_read_reflected_object_property_value(pawn, L"Controller");
        const double amount =
            command == L"damage_pulse"
                ? std::max(0.0, std::min(static_cast<double>(integer(actor, L"GetMaxHitPoint")) *
                                             damage_percent / 100.0,
                                         static_cast<double>(integer(actor, L"GetHitPoint")) - 1.0))
                : static_cast<double>(std::max<int64_t>(1, integer(actor, L"GetMaxHitPoint"))) +
                      1.0;
        target.damage_requested = static_cast<float>(amount);
        if (amount <= 0)
        {
            target.requested = true;
            return;
        }
        if (!write_object_function_param(c.function, c.params.data(), L"DamagedActor", actor) ||
            !write_object_function_param(c.function, c.params.data(), L"EventInstigator",
                                         controller) ||
            !write_object_function_param(c.function, c.params.data(), L"DamageCauser", pawn) ||
            !write_float_function_param(c.field(L"BaseDamage"), c.params.data(), amount))
        {
            throw std::runtime_error("unsupported_damage_parameters");
        }
        auto* type = c.field(L"DamageTypeClass");
        if (type->GetClass().GetName() != L"ClassProperty" ||
            type->GetElementSize() != sizeof(UClass*))
        {
            throw std::runtime_error("unsupported_damage_type_parameter");
        }
        std::memcpy(type->ContainerPtrToValuePtr<void>(c.params.data()), &damage_type,
                    sizeof(damage_type));
        auto* returned = c.field(L"ReturnValue");
        if (returned->GetClass().GetName() != L"FloatProperty" ||
            returned->GetElementSize() != sizeof(float))
        {
            throw std::runtime_error("unsupported_damage_return");
        }
        target.requested = true;
        // Preserve ordinary gameplay observation, including defeat rewards.
        c.invoke(false);
        std::memcpy(&target.damage_returned,
                    returned->ContainerPtrToValuePtr<void>(c.params.data()),
                    sizeof(target.damage_returned));
    }

    void RuntimeEnemyClearService::Impl::accept_boss_setting(UObject* pawn, const std::string& text)
    {
        if (!config.enemy_remaps)
        {
            finish(L"error", L"boss_settings_unavailable");
            return;
        }
        const auto before = config.enemy_remaps->status();
        bool dispatched = false;
        if (command == L"set_boss_setting")
        {
            const auto enabled = JsonFieldReader::bool_field(text, "enabled");
            if (!enabled.has_value())
            {
                finish(L"error", L"boss_enabled_bool_required");
                return;
            }
            if (!before.boss_available)
            {
                finish(L"error", L"seed_has_no_boss_remaps");
                return;
            }
            dispatched = config.enemy_remaps->set_boss_enabled(*enabled);
            if (!dispatched || config.enemy_remaps->status().boss_enabled != *enabled)
            {
                finish(L"error", L"boss_setting_readback_mismatch");
                return;
            }
        }
        const auto after = config.enemy_remaps->status();
        std::ostringstream setting;
        setting
            << std::boolalpha << "{\"available\":" << after.boss_available
            << ",\"before\":" << before.boss_enabled << ",\"enabled\":" << after.boss_enabled
            << ",\"setter_dispatched\":" << dispatched
            << ",\"pending_class_loads\":" << after.boss_pending_class_loads
            << ",\"class_load_failed\":" << after.boss_class_load_failed
            << ",\"apply\":\"next_same_map_encounter_spawn\",\"current_actors_unchanged\":true}";
        boss_setting = setting.str();
        finish(L"completed", L"boss_setting_readback");
        return;
    }

    void RuntimeEnemyClearService::Impl::accept_boss_hp_setup(UObject* pawn,
                                                              const std::string& text)
    {
        const auto selected = JsonFieldReader::string_field(text, "actor").value_or(L"");
        const auto percent = JsonFieldReader::int64_field(text, "percent").value_or(0);
        if (world_name != boss_hp_setup::forest_world || selected.empty())
        {
            throw std::runtime_error("kh02_hp_setup_requires_forest_and_exact_actor");
        }
        enumerate(pawn);
        std::erase_if(targets,
                      [&](const Target& target)
                      {
                          return target.name != selected;
                      });
        if (targets.size() != 1 || targets.front().class_name != boss_hp_setup::darkside_class)
        {
            throw std::runtime_error("kh02_hp_setup_requires_imported_darkside");
        }
        auto& target = targets.front();
        auto* boss = target.actor.Get();
        if (!is_uobject_valid_fast(boss) || target.invincible || target.no_damage)
        {
            throw std::runtime_error("kh02_hp_setup_requires_damageable_boss");
        }
        // The original camera trigger arms the hands. A spawned actor
        // alone is not an active encounter, and setup must not bypass it.
        for (const auto* hand_name : {L"TresBodyCollPrimitive13", L"TresBodyCollPrimitive15"})
        {
            auto* hand = try_read_reflected_object_property_value(boss, hand_name);
            auto* reaction = is_uobject_valid_fast(hand)
                                 ? hand->GetPropertyByNameInChain(L"m_BodyReactionType")
                                 : nullptr;
            if (!is_uobject_valid_fast(hand) || hand->GetOuterPrivate() != boss ||
                !property_bool(hand, L"m_bEnableDamage", false) || !reaction ||
                reaction->GetElementSize() != 1 ||
                (reaction->GetClass().GetName() != L"EnumProperty" &&
                 reaction->GetClass().GetName() != L"ByteProperty") ||
                read_unsigned_param_value(reaction->ContainerPtrToValuePtr<void>(hand), 1) !=
                    boss_hp_setup::damageable_body_reaction)
            {
                throw std::runtime_error("kh02_hp_setup_requires_authored_battle_activation");
            }
        }
        target.hp_before = integer(boss, L"GetHitPoint");
        target.max_hp = integer(boss, L"GetMaxHitPoint");
        const auto desired = boss_hp_setup::target_hp(target.hp_before, target.max_hp, percent);
        if (!desired)
        {
            throw std::runtime_error("kh02_hp_setup_invalid_nonlethal_fraction");
        }
        Call setter(boss, L"SetHitPoint");
        auto* owner = setter.function->GetOuterPrivate();
        auto* parameter = setter.field(L"InHP");
        if (!is_uobject_valid_fast(owner) ||
            owner->GetPathName() != L"/Script/TresGame.TresCharPawnBase" ||
            setter.function->GetParmsSize() != sizeof(std::int32_t) ||
            parameter->GetClass().GetName() != L"IntProperty" ||
            parameter->GetElementSize() != sizeof(std::int32_t) ||
            parameter->GetOffset_Internal() != 0 ||
            !write_integral_function_param(parameter, setter.params.data(), *desired))
        {
            throw std::runtime_error("kh02_hp_setup_authoritative_setter_layout");
        }
        report(L"accepted", L"kh02_hp_phase_setup_checkpoint");
        // Recheck after the checkpoint before the one synchronous write.
        if (unix_ms() >= expires || config.playable_sora() != pawn || sora.Get() != pawn ||
            actor_world(pawn) != world.Get() || !gameplay(pawn) || target.actor.Get() != boss ||
            integer(boss, L"GetHitPoint") != target.hp_before ||
            integer(boss, L"GetMaxHitPoint") != target.max_hp)
        {
            throw std::runtime_error("kh02_hp_setup_context_changed");
        }
        setter.invoke();
        target.requested = true;
        if (target.actor.Get() != boss || config.playable_sora() != pawn ||
            actor_world(pawn) != world.Get())
        {
            throw std::runtime_error("kh02_hp_setup_identity_changed_after_setter");
        }
        target.hp_after = integer(boss, L"GetHitPoint");
        const auto maximum_after = integer(boss, L"GetMaxHitPoint");
        std::ostringstream setup;
        setup << std::boolalpha << "{\"kind\":\"nonlethal_phase_setup\",\"actor\":\""
              << json_escape(selected) << "\",\"percent_of_max\":" << percent
              << ",\"hp_before\":" << target.hp_before << ",\"hp_target\":" << *desired
              << ",\"hp_after\":" << target.hp_after << ",\"max_hp_before\":" << target.max_hp
              << ",\"max_hp_after\":" << maximum_after
              << ",\"setter_dispatched\":true,\"combat_damage_proof\":false}";
        boss_hp_setup_result = setup.str();
        if (target.hp_after != *desired || maximum_after != target.max_hp)
        {
            throw std::runtime_error("kh02_hp_setup_readback_mismatch");
        }
        finish(L"completed", L"kh02_hp_phase_setup_readback");
        return;
    }

    void RuntimeEnemyClearService::Impl::accept_damage(UObject* pawn, const std::string& text)
    {
        if (command == L"damage_pulse")
        {
            damage_percent = JsonFieldReader::number_field(text, "percent").value_or(0);
            if (std::find(allowed_damage_percentages.begin(), allowed_damage_percentages.end(),
                          damage_percent) == allowed_damage_percentages.end())
            {
                throw std::runtime_error("damage_percent_must_be_10_25_or_50");
            }
        }
        enumerate(pawn);
        const auto selected = JsonFieldReader::string_field(text, "actor").value_or(L"");
        if (!selected.empty())
        {
            std::erase_if(targets,
                          [&](const Target& t)
                          {
                              return t.name != selected;
                          });
            if (targets.size() != 1)
            {
                throw std::runtime_error("selected_enemy_not_present");
            }
        }
        report(L"accepted",
               command == L"damage_pulse" ? L"nonlethal_damage_checkpoint" : L"defeat_checkpoint");
        waiting = true;
        return;
    }

    void RuntimeEnemyClearService::Impl::accept_experience(UObject* pawn, const std::string& text)
    {
        experience_before = experience_after = inspect_sora_experience(pawn);
        if (command == L"award_experience")
        {
            const auto amount = JsonFieldReader::int64_field(text, "amount").value_or(-1);
            if (amount < 1 || amount > max_test_experience_award)
            {
                throw std::runtime_error("experience_amount_out_of_range");
            }
            experience_requested = static_cast<int32_t>(amount);
            report(L"accepted", L"experience_checkpoint");
            experience_dispatched = true;
            award_sora_experience(pawn, experience_requested);
            experience_after = inspect_sora_experience(pawn);
            if (experience_after.total_exp <= experience_before.total_exp ||
                experience_after.level < experience_before.level)
            {
                throw std::runtime_error("experience_award_no_progress");
            }
        }
        finish(L"completed", command == L"award_experience" ? L"native_experience_increase_observed"
                                                            : L"experience_snapshot");
        return;
    }

} // namespace kh3ap
