#include "kh3ap/features/player/player_action_commands.h"
#include <chrono>
#include "kh3ap/features/treasure/chest_commands.h"
#include "kh3ap/features/enemies/enemy_clear/service_impl.h"
#include "kh3ap/features/enemies/enemy_clear/support.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/bridge/serialization/json_fields.h"
#include "kh3ap/core/input/runtime_controller_input.h"
#include "kh3ap/features/story/minigames/minigame_reward_service.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/features/player/characters/character_profile.h"
#include "kh3ap/features/player/player_state_ids.h"
#include "kh3ap/features/player/player_ids.h"

#include <Unreal/UObject.hpp>
#include <Unreal/AActor.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <Unreal/UObjectGlobals.hpp>

#include <algorithm>
#include <stdexcept>
#include <cmath>

namespace kh3ap
{
    namespace
    {
        constexpr auto minimum_attack_combo_count = 2;
        constexpr auto pooh_sign_count = 3;

        constexpr auto combo_release_lead_ms = 50;
        constexpr auto pooh_input_expiry_margin_ms = 1000;
    } // namespace

    using namespace RC::Unreal;
    using namespace enemy_clear_detail;

    void RuntimeEnemyClearService::Impl::press_action(UObject* controller, std::wstring_view action)
    {
        runtime_controller_action_input(controller, action, true);
        input_controller = controller;
        input_action = action;
        input_dispatched = true;
        release_at = Clock::now() + enemy_clear_button_hold_duration;
    }

    void RuntimeEnemyClearService::Impl::accept_union_cross_input(const std::string& text)
    {
        auto* pawn = controlled_combat_pawn();
        auto* current_world = actor_world(pawn);
        if (!is_uobject_valid_fast(pawn) || !is_uobject_valid_fast(current_world) ||
            current_world->GetPathName() != L"/Game/Levels/kg/kg_01/kg_01.kg_01" ||
            expected_world != current_world->GetPathName() ||
            integer(pawn, L"GetStateID") != player_state_ids::gimmick_attach ||
            integer(pawn, L"GetHitPoint") <= 0)
        {
            throw std::runtime_error("union_cross_attached_player_required");
        }
        auto* library = UObjectGlobals::StaticFindObject<UObject*>(
            nullptr, nullptr, L"/Script/TresGame.Default__TresGameBlueprintLibrary");
        if (!boolean(library, L"TresUI_IsClosedLoadingScreen"))
        {
            throw std::runtime_error("union_cross_loading");
        }
        auto* actor_class = UObjectGlobals::StaticFindObject<UClass*>(
            nullptr, nullptr, L"/Script/TresGame.TresGimmickKG_UnionCrossRide");
        UObject* ride{};
        // Enumeration is scoped to the pawn's live world. UE4SS's outer-based
        // GetWorld on this streamed actor returns its template sublevel world.
        for (auto* actor : actors_of_class(pawn, actor_class))
        {
            if (property_bool(actor, L"Started", false) &&
                try_read_reflected_object_property_value(actor, L"PlayerPawn") == pawn)
            {
                if (ride)
                {
                    throw std::runtime_error("ambiguous_union_cross_ride");
                }
                ride = actor;
            }
        }
        if (!ride)
        {
            throw std::runtime_error("started_union_cross_ride_required");
        }
        auto* gameplay_statics = UObjectGlobals::StaticFindObject<UObject*>(
            nullptr, nullptr, L"/Script/Engine.Default__GameplayStatics");
        Call paused(gameplay_statics, L"IsGamePaused");
        if (!write_object_function_param(paused.function, paused.params.data(),
                                         L"WorldContextObject", pawn))
        {
            throw std::runtime_error("union_cross_pause_signature");
        }
        paused.invoke();
        auto* result = paused.field(L"ReturnValue");
        if (result->GetClass().GetName() != L"BoolProperty" ||
            static_cast<FBoolProperty*>(result)->GetPropertyValueInContainer(paused.params.data()))
        {
            throw std::runtime_error("union_cross_paused");
        }
        // Riding intentionally ignores ordinary movement. Only this explicit
        // diagnostic input uses the attached-player gate; combat/grants do not.
        sora = pawn;
        world = current_world;
        world_name = current_world->GetPathName();
        pawn_name = pawn->GetPathName();
        sora_hp_before = sora_hp_after = integer(pawn, L"GetHitPoint");
        mutation_context_ready = mutation_control_ready = true;
        const auto action = JsonFieldReader::string_field(text, "action").value_or(L"");
        if (action != L"interact")
        {
            throw std::runtime_error("union_cross_triangle_required");
        }
        union_input_ride = make_uobject_weak(ride);
        if (JsonFieldReader::int64_field(text, "count").value_or(1) > 1)
        {
            accept_attack_combo(pawn, text);
            input_action = L"interact";
            return;
        }
        accept_press_action(pawn, text);
    }

    bool RuntimeEnemyClearService::Impl::update_combo_input(Clock::time_point now)
    {
        if (!(combo_active))
        {
            return false;
        }
        auto* pawn = sora.Get();
        auto* c = combo_controller.Get();
        const bool valid = is_uobject_valid_fast(pawn) && is_uobject_valid_fast(c) &&
                           actor_world(pawn) == world.Get() && request_pawn() == pawn &&
                           try_read_reflected_object_property_value(pawn, L"Controller") == c;
        if (!valid || unix_ms() >= expires)
        {
            finish(L"error", L"attack_combo_identity_or_expiry");
            return true;
        }
        const bool union_input = command == L"union_cross_input";
        const bool ready =
            union_input ? is_uobject_valid_fast(union_input_ride.Get()) &&
                              property_bool(union_input_ride.Get(), L"Started", false) &&
                              try_read_reflected_object_property_value(union_input_ride.Get(),
                                                                       L"PlayerPawn") == pawn &&
                              integer(pawn, L"GetStateID") == player_state_ids::gimmick_attach &&
                              integer(pawn, L"GetHitPoint") > 0
                        : gameplay(pawn);
        if (!ready)
        {
            finish(L"completed", L"attack_combo_gameplay_boundary");
            return true;
        }
        if (now < combo_next)
        {
            return true;
        }
        if (combo_held)
        {
            runtime_controller_action_input(c, input_action, false);
            combo_held = false;
            ++combo_released;
            if (combo_pressed == combo_requested)
            {
                finish(L"completed", L"attack_combo_released");
                return true;
            }
            combo_next = now + std::chrono::milliseconds(combo_interval_ms - combo_release_lead_ms);
        }
        else
        {
            if (combo_pressed)
            {
                combo_max_gap_ms = std::max(
                    combo_max_gap_ms,
                    std::chrono::duration_cast<std::chrono::milliseconds>(now - combo_last_press)
                        .count());
            }
            runtime_controller_action_input(c, input_action, true);
            combo_held = true;
            ++combo_pressed;
            input_dispatched = true;
            combo_last_press = now;
            combo_next = now + combo_press_interval;
        }
        return true;
    }

    bool RuntimeEnemyClearService::Impl::update_action_input(Clock::time_point now)
    {
        if (!(input_controller.Get()))
        {
            return false;
        }
        if (waiting_dive_window)
        {
            auto* pawn = sora.Get();
            auto* controller = input_controller.Get();
            if (!is_uobject_valid_fast(pawn) || actor_world(pawn) != world.Get() ||
                try_read_reflected_object_property_value(controller, L"Pawn") != pawn ||
                unix_ms() >= expires)
            {
                waiting_dive_window = false;
                input_controller = nullptr;
                finish(L"error", L"dive_window_identity_or_expiry");
                return true;
            }
            auto* queue = try_read_reflected_object_property_value(pawn, L"MyStateComp");
            auto* state = try_read_reflected_object_property_value(queue, L"m_pCurrentState");
            if (!is_uobject_valid_fast(state))
            {
                return true;
            }
            const auto* state_bytes = reinterpret_cast<const std::byte*>(state);
            const auto* controller_bytes = reinterpret_cast<const std::byte*>(controller);
            const bool native_window =
                *reinterpret_cast<const uint8_t*>(state_bytes + 0x58) == player_state_ids::jump &&
                *reinterpret_cast<const int32_t*>(state_bytes + 0x78) == 8 &&
                (*reinterpret_cast<const uint32_t*>(state_bytes + 0x114) & 0x4000u) != 0 &&
                *reinterpret_cast<const int32_t*>(controller_bytes + 0x8bc) == 11 &&
                *reinterpret_cast<const float*>(controller_bytes + 0x8b8) > 0.0f;
            if (!native_window || !gameplay(pawn))
            {
                return true;
            }
            waiting_dive_window = false;
            report(L"accepted", L"native_dive_attack_window_open");
            press_action(controller, L"attack");
            release_at = now + enemy_clear_button_hold_duration;
            return true;
        }
        if (now < release_at)
        {
            return true;
        }
        auto* controller = input_controller.Get();
        input_controller = nullptr;
        runtime_controller_action_input(controller, input_action, false);
        input_released = true;
        finish(L"completed", command == L"open_chest" ? L"chest_interaction_dispatched"
                                                      : L"native_input_released");
        return true;
    }

    void RuntimeEnemyClearService::Impl::accept_pooh_input(const std::string& text)
    {
        auto* controller = RuntimeMinigameRewardService::diagnostic_controller();
        auto* pawn = try_read_reflected_object_property_value(controller, L"Pawn");
        if (!is_uobject_valid_fast(controller) || !is_uobject_valid_fast(pawn))
        {
            throw std::runtime_error("current_pooh_puzzle_required");
        }
        sora = pawn;
        world = actor_world(pawn);
        world_name = world.Get() ? world.Get()->GetPathName() : L"";
        pawn_name = pawn->GetPathName();
        if (expected_world.empty() || expected_world != world_name)
        {
            throw std::runtime_error("pooh_world_identity_mismatch");
        }
        const auto action = JsonFieldReader::string_field(text, "action").value_or(L"");
        const auto hold = JsonFieldReader::int64_field(text, "hold_ms").value_or(-1);
        if ((action != L"pooh_shoot" && action != L"pooh_left" && action != L"pooh_right" &&
             action != L"pooh_special") ||
            hold < minimum_pooh_button_hold_ms || hold > maximum_pooh_button_hold_ms ||
            unix_ms() + hold + pooh_input_expiry_margin_ms > expires)
        {
            throw std::runtime_error("invalid_pooh_input");
        }
        const auto weak_controller = make_uobject_weak(controller);
        if (weak_controller.Get() != controller)
        {
            throw std::runtime_error("pooh_controller_identity_unavailable");
        }
        report(L"accepted", L"pooh_input_checkpoint");
        press_action(controller, action);
        input_controller = weak_controller;
        release_at = Clock::now() + std::chrono::milliseconds(hold);
        waiting = true;
        return;
    }

    void RuntimeEnemyClearService::Impl::accept_attack_combo(UObject* pawn, const std::string& text)
    {
        const auto count = JsonFieldReader::int64_field(text, "count").value_or(0);
        const auto interval = JsonFieldReader::int64_field(text, "interval_ms").value_or(0);
        if (count < minimum_attack_combo_count || count > max_attack_combo_count ||
            interval < minimum_attack_combo_interval_ms ||
            interval > maximum_attack_combo_interval_ms)
        {
            throw std::runtime_error("invalid_attack_combo_bounds");
        }
        combo_controller = try_read_reflected_object_property_value(pawn, L"Controller");
        if (!is_uobject_valid_fast(combo_controller.Get()))
        {
            throw std::runtime_error("combo_controller_unavailable");
        }
        combo_requested = static_cast<int>(count);
        combo_interval_ms = static_cast<int>(interval);
        input_action = L"attack";
        combo_active = true;
        combo_next = Clock::now();
        waiting = true;
        report(L"accepted", L"attack_combo_started");
        return;
    }

    void RuntimeEnemyClearService::Impl::accept_press_action(UObject* pawn, const std::string& text)
    {
        const auto action = JsonFieldReader::string_field(text, "action").value_or(L"");
        if (action == L"tornado_dive_window")
        {
            using character_native::Site;
            auto* id = pawn->GetPropertyByNameInChain(L"m_PlayerUniqueID");
            if (character_native::build() == character_native::Build::Unsupported ||
                !character_native::verify_sites({Site::dive_attack_window}) ||
                world_name != L"/Game/Levels/he/he_06/he_06.HE_06" || !id ||
                id->GetElementSize() != sizeof(uint8_t) ||
                !is_standard_sora_player_id(*id->ContainerPtrToValuePtr<uint8_t>(pawn)))
            {
                throw std::runtime_error("tornado_dive_window_requires_supported_sora");
            }
            input_controller = try_read_reflected_object_property_value(pawn, L"Controller");
            waiting_dive_window = true;
            input_action = L"attack";
            waiting = true;
            report(L"accepted", L"waiting_for_native_dive_attack_window");
            return;
        }
        const auto duration = JsonFieldReader::int64_field(text, "duration_ms")
                                  .value_or(enemy_clear_button_hold_duration.count());
        // A full shotlock charge can outlast the short movement/button route.
        // Keep this longer diagnostic hold specific to R1; the same ownership,
        // expiry and world-change paths still release it normally.
        const auto maximum_duration =
            action == L"right_shoulder" ? maximum_shotlock_button_hold_ms : maximum_stick_hold_ms;
        if (duration < minimum_stick_hold_ms || duration > maximum_duration)
        {
            throw std::runtime_error("action_hold_duration_out_of_range");
        }
        report(L"accepted", L"input_checkpoint");
        press_action(try_read_reflected_object_property_value(pawn, L"Controller"), action);
        release_at = Clock::now() + std::chrono::milliseconds(duration);
        waiting = true;
        return;
    }

    void RuntimeEnemyClearService::Impl::accept_pooh_sign(UObject* pawn, const std::string& text)
    {
        if (world_name != L"/Game/Levels/po/po_01/po_01.po_01")
        {
            throw std::runtime_error("pooh_field_required");
        }
        const auto index = JsonFieldReader::int64_field(text, "sign").value_or(0);
        if (index < 1 || index > pooh_sign_count)
        {
            throw std::runtime_error("invalid_pooh_sign");
        }
        const auto name = L"g_po_SignBoard_0" + std::to_wstring(index) + L"_C";
        auto* actor_class =
            UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, L"/Script/Engine.Actor");
        UObject* sign = nullptr;
        for (auto* candidate : actors_of_class(pawn, actor_class))
        {
            if (is_uobject_valid_fast(candidate) && candidate->GetClassPrivate()->GetName() == name)
            {
                if (sign)
                {
                    throw std::runtime_error("ambiguous_pooh_sign");
                }
                sign = candidate;
            }
        }
        if (!sign || property_bool(sign, L"bHidden", false) ||
            !boolean(sign, L"GetActorEnableCollision"))
        {
            throw std::runtime_error("pooh_sign_unavailable");
        }
        const auto pos = position(sign);
        double distance = 0;
        for (size_t i = 0; i < 3; ++i)
        {
            distance += std::pow(pos[i] - position_before[i], 2.0);
        }
        if (distance > chest_interaction_radius * chest_interaction_radius)
        {
            throw std::runtime_error("pooh_sign_out_of_range");
        }
        auto* reactor = try_read_reflected_object_property_value(sign, L"TresReactor");
        if (!is_uobject_valid_fast(reactor) || !boolean(reactor, L"BP_IsGimmickComponentActive"))
        {
            throw std::runtime_error("pooh_sign_reactor_inactive");
        }
        auto* property = reactor->GetPropertyByNameInChain(L"m_Command");
        if (!property || property->GetElementSize() != 1)
        {
            throw std::runtime_error("pooh_sign_command_layout");
        }
        if (*property->ContainerPtrToValuePtr<uint8_t>(reactor) != chest_command::examine)
        {
            throw std::runtime_error("pooh_sign_requires_check_reactor");
        }
        // Reactor CHECK=4 maps to the controller's distinct command CHECK=79.
        constexpr int action = player_action_command_value(PlayerActionCommand::CHECK);
        auto* controller = try_read_reflected_object_property_value(pawn, L"Controller");
        Call set(controller, L"BP_NotifySetActionCommand1");
        if (!write_integral_function_param(set.field(L"inCommand"), set.params.data(), action) ||
            !write_float_function_param(set.field(L"inTimer"), set.params.data(), 1.0) ||
            !write_object_function_param(set.function, set.params.data(), L"inActor", sign) ||
            !write_object_function_param(set.function, set.params.data(), L"InComponent", reactor))
        {
            throw std::runtime_error("pooh_sign_action_layout");
        }
        interaction_actor = sign->GetPathName();
        report(L"accepted", L"pooh_sign_input_checkpoint");
        set.invoke();
        press_action(controller, L"interact");
        interaction_dispatched = true;
        waiting = true;
        return;
    }

} // namespace kh3ap
