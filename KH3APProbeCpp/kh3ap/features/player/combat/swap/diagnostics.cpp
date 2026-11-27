#include "swap_impl.h"
#include "swap_state.h"
#include "kh3ap/core/objects/actor_owning_world.h"
#include "kh3ap/features/player/characters/character_swap.h"
#include "kh3ap/features/player/player_ids.h"
#include "kh3ap/diagnostics/profiling/engine_hitch_trace.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include <Unreal/Property/FArrayProperty.hpp>
#include <Unreal/TArray.hpp>
#include "kh3ap/bridge/utilities.h"
#include "kh3ap/app/enums.h"
#include "kh3ap/features/grants/vbonus/vbonus_grant_call_helpers.h"
#include <algorithm>

namespace kh3ap
{
    using namespace RC::Unreal;
    using namespace combat_swap_detail;

    void CharacterCombatSwap::Impl::TestSession::release_test_input()
    {
        if (test_movement)
        {
            auto* controller = test_locomotion
                                   ? object(local_player_pawn(test_world.Get()), L"Controller")
                                   : test_controller.Get();
            if (is_uobject_valid_fast(controller) &&
                object(controller, L"Pawn") == local_player_pawn(test_world.Get()) &&
                object(object(controller, L"Player"), L"PlayerController") == controller)
            {
                runtime_controller_stick_input(controller, 0.0f, 0.0f);
            }
        }
        if (test_camera || test_rotate)
        {
            if (auto* controller = test_controller.Get(); is_uobject_valid_fast(controller))
            {
                runtime_controller_look_input(controller, 0.0f, 0.0f);
            }
        }
        if (test_held)
        {
            const bool consumed =
                test_action == L"attack" && swap.attack_key(0, L"Gamepad_FaceButton_Bottom", 1);
            if (auto* controller = test_controller.Get();
                !consumed && is_uobject_valid_fast(controller) &&
                object(object(controller, L"Player"), L"PlayerController") == controller)
            {
                runtime_controller_action_input(controller, test_action, false);
            }
            test_held = false;
        }
    }

    void CharacterCombatSwap::Impl::TestSession::test_press(UObject* pawn, const wchar_t* action,
                                                            Clock::time_point now)
    {
        auto* controller = object(pawn, L"Controller");
        if (!is_uobject_valid_fast(controller) || object(controller, L"Pawn") != pawn)
        {
            throw std::runtime_error("kh02_combat_test_controller");
        }
        test_controller = make_uobject_weak(controller);
        test_action = action;
        const bool consumed =
            test_action == L"attack" && swap.attack_key(0, L"Gamepad_FaceButton_Bottom", 0);
        if (!consumed)
        {
            runtime_controller_action_input(controller, action, true);
        }
        test_held = true;
        test_release = now + std::chrono::milliseconds(50);
        test_next = now + std::chrono::milliseconds(180);
    }

    bool CharacterCombatSwap::Impl::TestSession::start_test(bool airborne, bool expire, float phase)
    {
        auto* pawn = swap.player();
        if (test_phase || swap.queued || !swap.available(pawn) || !swap.install())
        {
            return false;
        }
        test_world = make_uobject_weak(actor_owning_world(pawn));
        runtime_controller_test_stick_lease(nullptr, false);
        test_camera = false;
        test_movement = false;
        test_rotate = false;
        test_locomotion = false;
        test_menu = false;
        test_target = kh02_current_story_world()
                          ? (player_id(pawn) == player_id_value(PlayerId::Sora)
                                 ? player_id_value(PlayerId::Aqua)
                                 : player_id_value(PlayerId::Sora))
                          : character_swap::swap_target(pawn);
        test_expected_index = expire ? 0 : airborne ? 1 : 2;
        test_air = airborne;
        test_input_mode = 0;
        test_origin = player_id(pawn);
        test_expire = expire;
        test_swing_phase = phase;
        test_jump = airborne;
        test_until = Clock::now() + std::chrono::seconds(6);
        test_next = Clock::now();
        test_status = "running";
        test_result = nullptr;
        test_phase = 1;
        return true;
    }

    bool CharacterCombatSwap::Impl::TestSession::start_menu_test()
    {
        if (!kh02_current_story_world() ||
            player_id(swap.player()) != player_id_value(PlayerId::Sora) ||
            !start_test(false, false))
        {
            return false;
        }
        test_menu = true;
        test_result = {{"spell_state_seen", false},
                       {"buffered_before", swap.buffered_presses},
                       {"replayed_before", swap.replayed_presses}};
        return true;
    }

    bool CharacterCombatSwap::Impl::TestSession::start_input_test(bool swap_back)
    {
        if (!start_test(false, false, 0.05f))
        {
            return false;
        }
        test_input_mode = swap_back ? 2 : 1;
        test_expected_index = swap_back ? 2 : 1;
        return true;
    }

    bool CharacterCombatSwap::Impl::TestSession::start_camera_test()
    {
        if (!start_test(false, false))
        {
            return false;
        }
        test_camera = true;
        test_controller = make_uobject_weak(object(swap.player(), L"Controller"));
        runtime_controller_look_input(test_controller.Get(), 0.65f, 0.3f);
        test_next = Clock::now() + std::chrono::milliseconds(400);
        test_result = nlohmann::json::object();
        return true;
    }

    bool CharacterCombatSwap::Impl::TestSession::start_movement_test(bool rotate)
    {
        if (!start_test(false, false))
        {
            return false;
        }
        test_movement = true;
        test_rotate = rotate;
        test_controller = make_uobject_weak(object(swap.player(), L"Controller"));
        runtime_controller_stick_input(test_controller.Get(), 0.6f, 0.8f);
        if (test_rotate)
        {
            runtime_controller_look_input(test_controller.Get(), 0.65f, 0.1f);
        }
        test_next = Clock::now() + std::chrono::milliseconds(1000);
        test_result = nlohmann::json::object();
        return true;
    }

    auto CharacterCombatSwap::Impl::TestSession::movement_snapshot(UObject* pawn) -> nlohmann::json
    {
        const auto distance = camera_distance(pawn);
        auto* anim = object(object(pawn, L"MyMesh"), L"AnimScriptInstance");
        auto* current = state(pawn);
        return {{"velocity", vector(pawn, L"GetVelocity")},
                {"location", vector(pawn, L"K2_GetActorLocation")},
                {"stick", movement_stick(pawn)},
                {"look", runtime_controller_stick_state(object(pawn, L"Controller"), true)},
                {"camera", vector(camera_manager(pawn), L"GetCameraRotation")},
                {"state", state_id(current)},
                {"step", property_value<int>(current, L"m_nSubStep")},
                {"anim_time", animation_value(anim, L"SQEX_AnimGetCurrentTime")},
                {"anim_length", animation_value(anim, L"SQEX_AnimGetCurrentLength")},
                {"distance", {distance.current, distance.battle, distance.normal}}};
    }

    bool CharacterCombatSwap::Impl::TestSession::start_locomotion_test(int count, int warmup_ms,
                                                                       int strength_percent,
                                                                       bool release_at_swap,
                                                                       bool camera_relative,
                                                                       bool capture_gait)
    {
        if (count < 0 || count > 8 || warmup_ms < 0 || warmup_ms > 2500 || strength_percent < 1 ||
            strength_percent > 100 || (release_at_swap && count != 1) || !start_test(false, false))
        {
            return false;
        }
        test_movement = true;
        test_locomotion = true;
        test_camera_relative = camera_relative;
        runtime_controller_test_stick_lease(object(swap.player(), L"Controller"), true);
        test_release_at_swap = release_at_swap;
        test_capture_gait = capture_gait;
        test_gait_bones.clear();
        test_swaps_remaining = count;
        test_strength = strength_percent / 100.0f;
        test_started = Clock::now();
        test_next = test_started + std::chrono::milliseconds(warmup_ms);
        test_result = {{"samples", nlohmann::json::array()}, {"requests", nlohmann::json::array()}};
        return true;
    }

    void CharacterCombatSwap::Impl::TestSession::sample_gait(UObject* pawn, UObject* anim,
                                                             nlohmann::json& sample)
    {
        auto* mesh = object(pawn, L"MyMesh");
        const int role = player_id(pawn);
        auto found = test_gait_bones.find(role);
        if (found == test_gait_bones.end())
        {
            Call sockets(mesh, L"GetAllSocketNames");
            sockets.run();
            auto* returned = sockets.param(L"ReturnValue");
            if (!returned || !returned->IsA<FArrayProperty>() ||
                static_cast<FArrayProperty*>(returned)->GetInner()->GetElementSize() !=
                    sizeof(FName))
            {
                throw std::runtime_error("locomotion_gait_socket_layout");
            }
            const auto& names =
                *returned->ContainerPtrToValuePtr<TArray<FName>>(sockets.parameters.data());
            if (names.Num() < 0 || names.Num() > 4096)
            {
                throw std::runtime_error("locomotion_gait_socket_count");
            }
            std::vector<std::wstring> bones;
            auto all_names = nlohmann::json::array();
            for (const auto& name : names)
            {
                const auto text = name.ToString();
                all_names.push_back(to_utf8(text));
                // The playable KH3 skeletons name the ankle/toe pairs
                // ashi1/ashi2. Resolve only names actually on this mesh.
                if (text == L"L_ashi1" || text == L"R_ashi1" || text == L"L_ashi2" ||
                    text == L"R_ashi2")
                {
                    bones.push_back(text);
                }
            }
            test_result["gait_bone_names"][std::to_string(role)] = std::move(all_names);
            found = test_gait_bones.emplace(role, std::move(bones)).first;
        }
        sample["anim_play_rate"] = animation_value(anim, L"SQEX_AnimGetCurrentPlayRate");
        sample["mesh_anim_rate_scale"] = property_value<float>(mesh, L"GlobalAnimRateScale");
        sample["feet"] = nlohmann::json::object();
        for (const auto& bone : found->second)
        {
            Call position(mesh, L"GetSocketLocation");
            position.name(L"InSocketName", bone.c_str());
            position.run();
            sample["feet"][to_utf8(bone)] = position.result_vector();
        }
    }

    void CharacterCombatSwap::Impl::TestSession::tick_locomotion_test(Clock::time_point now)
    {
        if (!swap.queued && test_swaps_remaining == 0 && now >= test_next)
        {
            runtime_controller_test_stick_lease(nullptr, false);
            // InputAxis accumulates samples within a frame. Release on a
            // frame that has not already received the held test sample.
            release_test_input();
            test_phase = 0;
            test_status = "completed";
            return;
        }
        auto* pawn = local_player_pawn(test_world.Get());
        auto* controller = object(pawn, L"Controller");
        if (!is_uobject_valid_fast(pawn) ||
            !is_uobject_valid_fast(object(controller, L"PlayerInput")))
        {
            return;
        }
        // Hold either a world direction or the same physical stick axes.
        // The latter exposes camera-basis changes during possession.
        const auto pending_input = runtime_controller_capture_stick(controller, false);
        if (test_release_at_swap && test_swaps_remaining == 0 && player_id(pawn) != test_origin)
        {
            // A device zero arrives after the old held state was restored,
            // before the new controller's first completed input pass.
            runtime_controller_stick_input(controller, 0.0f, 0.0f);
            test_strength = 0.0f;
            test_release_at_swap = false;
            test_result["released_ms"] =
                std::chrono::duration<double, std::milli>(now - test_started).count();
        }
        else if (!pending_input.pending_samples[0] && !pending_input.pending_samples[1] &&
                 (test_controller.Get() == controller || !swap.input_seeded ||
                  swap.positioned.Get() != pawn))
        {
            if (test_camera_relative)
            {
                runtime_controller_stick_input(controller, 0.0f, test_strength);
            }
            else
            {
                runtime_controller_move_input(controller, test_strength, 0.0f);
            }
        }
        test_controller = make_uobject_weak(controller);
        auto* movement = object(pawn, L"MyMovement");
        auto* current = state(pawn);
        if (!is_uobject_valid_fast(movement))
        {
            return;
        }
        const bool running = is_uobject_valid_fast(current) &&
                             current->GetClassPrivate()->GetName() == L"TresPlayerState_Run";
        nlohmann::json sample = {{"velocity", vector(pawn, L"GetVelocity")},
                                 {"location", vector(pawn, L"K2_GetActorLocation")},
                                 {"stick", movement_stick(pawn)},
                                 {"state", state_id(current)},
                                 {"step", is_uobject_valid_fast(current)
                                              ? property_value<int>(current, L"m_nSubStep")
                                              : -1}};
        sample["ms"] = std::chrono::duration<double, std::milli>(now - test_started).count();
        sample["player"] = player_id(pawn);
        sample["camera"] = vector(camera_manager(pawn), L"GetCameraRotation");
        const auto view = field<CameraView>(camera_pov(camera_manager(pawn), L"CameraCache"), 0);
        sample["camera_location"] = view.location;
        sample["camera_fov"] = view.fov;
        const auto distance = camera_distance(pawn);
        sample["camera_distance"] = {distance.current, distance.battle, distance.normal};
        sample["camera_pivot"] = camera_orbit(pawn).pivot;
        sample["camera_timestamp"] = *camera_timestamp(camera_manager(pawn));
        sample["control_rotation"] = vector(controller, L"GetControlRotation");
        sample["actor_rotation"] = vector(pawn, L"K2_GetActorRotation");
        sample["run_heading"] = running ? field<float>(current, 0x8c) : 0.0f;
        sample["pending"] = swap.queued;
        sample["first_movement_updates"] = swap.first_movement_updates;
        const auto stick = runtime_controller_capture_stick(controller, false);
        sample["pending_stick"] = stick.raw;
        sample["processed_stick"] = stick.processed;
        sample["acceleration"] = property_value<std::array<float, 3>>(movement, L"Acceleration");
        sample["max_speed"] = property_value<float>(movement, L"MaxWalkSpeed");
        sample["analog"] = property_value<float>(movement, L"AnalogInputModifier");
        sample["run_elapsed"] = running ? field<float>(current, 0xb8) : 0.0f;
        sample["sprinting"] = running && field<uint8_t>(current, 0xbc) != 0;
        if (running)
        {
            const auto animation_name = field<FName>(current, 0xa0).ToString();
            sample["run_animation"] = to_utf8(animation_name);
            auto* anim = object(object(pawn, L"MyMesh"), L"AnimScriptInstance");
            Call section(anim, L"SQEX_AnimGetCurrentSectionName");
            section.name(L"InSlotName", L"FullBody");
            section.run();
            auto* section_result = section.param(L"ReturnValue");
            if (section_result->GetElementSize() != sizeof(FName))
            {
                throw std::runtime_error("locomotion_section_name_layout");
            }
            const auto section_name =
                section_result->ContainerPtrToValuePtr<FName>(section.parameters.data())
                    ->ToString();
            sample["section"] = to_utf8(section_name);
            sample["anim_length"] = animation_value(anim, L"SQEX_AnimGetCurrentLength");
            sample["anim_time"] = animation_value(anim, L"SQEX_AnimGetCurrentTime");
            if (test_capture_gait)
            {
                sample_gait(pawn, anim, sample);
            }
        }
        Call delta(L"/Script/Engine.Default__GameplayStatics", L"GetWorldDeltaSeconds");
        delta.object(L"WorldContextObject", pawn);
        delta.run();
        sample["world_delta"] =
            *delta.param(L"ReturnValue")->ContainerPtrToValuePtr<float>(delta.parameters.data());
        if (const auto frame = runtime_engine_last_frame_sample())
        {
            sample["previous_engine_frame"] = {{"begin_us", frame->begin_us},
                                               {"engine_end_us", frame->engine_end_us},
                                               {"callback_end_us", frame->callback_end_us},
                                               {"foreground", frame->foreground},
                                               {"idle", frame->idle}};
        }
        if (auto* last_update = movement->GetPropertyByNameInChain(L"LastUpdateLocation");
            last_update && last_update->GetElementSize() == sizeof(std::array<float, 3>))
        {
            sample["movement_tick"] =
                property_value<std::array<float, 3>>(movement, L"LastUpdateLocation");
        }
        test_result["samples"].push_back(std::move(sample));
        if (now < test_next || swap.queued)
        {
            return;
        }
        if (pawn == swap.player() && swap.available(pawn) && swap.request(pawn))
        {
            --test_swaps_remaining;
            test_result["requests"].push_back(
                {{"ms", std::chrono::duration<double, std::milli>(now - test_started).count()},
                 {"player", player_id(pawn)}});
            test_next = now + std::chrono::milliseconds(test_swaps_remaining ? 150 : 500);
        }
    }

    void CharacterCombatSwap::Impl::TestSession::tick_movement_test(UObject* pawn,
                                                                    Clock::time_point now)
    {
        if (now < test_next)
        {
            return;
        }
        if (test_phase == 1)
        {
            test_result["before"] = movement_snapshot(pawn);
            if (!swap.request(pawn))
            {
                throw std::runtime_error("kh02_movement_test_swap");
            }
            test_phase = 2;
        }
        else if (test_phase == 2 && player_id(pawn) == test_target)
        {
            test_controller = make_uobject_weak(object(pawn, L"Controller"));
            test_result["at_swap"] = movement_snapshot(pawn);
            test_next = now + std::chrono::milliseconds(400);
            test_phase = 3;
        }
        else if (test_phase == 3)
        {
            test_result["after"] = movement_snapshot(pawn);
            release_test_input();
            test_status = "completed";
            test_phase = 0;
        }
    }

    void CharacterCombatSwap::Impl::TestSession::tick_camera_test(UObject* pawn,
                                                                  Clock::time_point now)
    {
        if (test_phase == 4 && !test_result.contains("first_frame"))
        {
            test_result["first_frame"] = vector(camera_manager(pawn), L"GetCameraRotation");
        }
        if (now < test_next)
        {
            return;
        }
        if (test_phase == 1)
        {
            release_test_input();
            test_next = now + std::chrono::milliseconds(100);
            test_phase = 2;
        }
        else if (test_phase == 2)
        {
            test_result["before"] = vector(camera_manager(pawn), L"GetCameraRotation");
            test_result["control_before"] =
                vector(object(pawn, L"Controller"), L"GetControlRotation");
            if (!swap.request(pawn))
            {
                throw std::runtime_error("kh02_camera_test_swap");
            }
            test_phase = 3;
        }
        else if (test_phase == 3 && player_id(pawn) == test_target)
        {
            test_result["created_cache"] = vector(camera_manager(pawn), L"GetCameraRotation");
            for (const auto* name :
                 {L"CameraCache", L"LastFrameCameraCache", L"ViewTarget", L"PendingViewTarget"})
            {
                const auto view = field<CameraView>(camera_pov(camera_manager(pawn), name), 0);
                test_result["creation_views"].push_back(view.rotation);
            }
            test_next = now + std::chrono::milliseconds(800);
            test_phase = 4;
        }
        else if (test_phase == 4)
        {
            test_result["after"] = vector(camera_manager(pawn), L"GetCameraRotation");
            test_result["control_after"] =
                vector(object(pawn, L"Controller"), L"GetControlRotation");
            test_controller = make_uobject_weak(object(pawn, L"Controller"));
            runtime_controller_look_input(test_controller.Get(), -0.5f, -0.3f);
            test_next = now + std::chrono::milliseconds(300);
            test_phase = 5;
        }
        else if (test_phase == 5)
        {
            release_test_input();
            test_result["after_look"] = vector(camera_manager(pawn), L"GetCameraRotation");
            test_status = "completed";
            test_phase = 0;
        }
    }

    void CharacterCombatSwap::Impl::TestSession::tick_menu_test(UObject* pawn,
                                                                Clock::time_point now)
    {
        if (swap.queued || now < test_next)
        {
            return;
        }
        if (test_phase == 1)
        {
            if (!swap.available(pawn) || !swap.request(pawn))
            {
                throw std::runtime_error("kh02_menu_test_swap");
            }
            test_phase = 2;
        }
        else if (test_phase == 2 && player_id(pawn) == test_target && swap.available(pawn))
        {
            test_result["mp_before"] = property_value<int>(pawn, L"m_MagicPoint");
            test_press(pawn, L"command_down", now);
            test_phase = 3;
        }
        else if (test_phase == 3 && !test_held)
        {
            // One physical confirm, routed through the real swap buffer.
            // It must open Magic without confirming a spell on a retry.
            test_press(pawn, L"attack", now);
            test_next = now + std::chrono::milliseconds(100);
            test_phase = 4;
        }
        else if (test_phase == 4)
        {
            auto* current = state(pawn);
            if (is_uobject_valid_fast(current) &&
                current->GetClassPrivate()->GetName() == L"TresPlayerState_FireMagic")
            {
                test_result["spell_state_seen"] = true;
            }
            if (now < test_release + std::chrono::milliseconds(1000))
            {
                return;
            }
            const int mp = property_value<int>(pawn, L"m_MagicPoint");
            test_result["mp_after"] = mp;
            test_result["buffered_after"] = swap.buffered_presses;
            test_result["replayed_after"] = swap.replayed_presses;
            test_status = test_result["spell_state_seen"].get<bool>() ||
                                  mp < test_result["mp_before"].get<int>()
                              ? "failed_unrequested_cast"
                              : "completed";
            test_phase = 0;
        }
    }

    void CharacterCombatSwap::Impl::TestSession::tick_test(Clock::time_point now)
    {
        if (!test_phase)
        {
            return;
        }
        if (test_held && now >= test_release)
        {
            release_test_input();
        }
        if (now > test_until || test_world.Get() != swap_context_world())
        {
            runtime_controller_test_stick_lease(nullptr, false);
            release_test_input();
            test_status = "failed_context_or_timeout";
            test_phase = 0;
            return;
        }
        auto* pawn = swap.player();
        if (test_menu)
        {
            tick_menu_test(pawn, now);
            return;
        }
        if (!test_camera && test_phase == 4 && !test_held)
        {
            test_status = "completed";
            test_phase = 0;
            return;
        }
        if (test_locomotion)
        {
            tick_locomotion_test(now);
            return;
        }
        if (swap.queued || !swap.available(pawn))
        {
            return;
        }
        if (test_camera)
        {
            tick_camera_test(pawn, now);
            return;
        }
        if (test_movement)
        {
            tick_movement_test(pawn, now);
            return;
        }
        if (test_phase == 1)
        {
            auto* current = state(pawn);
            const int wanted = test_air || test_input_mode ? 0 : 1;
            if (attack_state(current, pawn) && field<int>(current, combo_index_offset) == wanted &&
                !field<uint8_t>(current, combo_finisher_offset) &&
                (field<uint8_t>(current, combo_air_offset) != 0) == test_air)
            {
                auto* anim = object(object(pawn, L"MyMesh"), L"AnimScriptInstance");
                const float length = animation_value(anim, L"SQEX_AnimGetCurrentLength");
                if (length > 0.0f &&
                    animation_value(anim, L"SQEX_AnimGetCurrentTime") / length < test_swing_phase)
                {
                    return;
                }
                release_test_input();
                if (!swap.request(pawn))
                {
                    test_status = "failed_swap_request";
                    test_phase = 0;
                    return;
                }
                test_phase = 2;
                test_next = now + (test_expire ? std::chrono::milliseconds(1200)
                                               : std::chrono::milliseconds(100));
                if (test_input_mode)
                {
                    // One press immediately after requesting the swap,
                    // before the new controller is ready. No retries.
                    test_press(pawn, L"attack", now);
                    test_phase = test_input_mode == 2 ? 5 : 3;
                }
                return;
            }
            if (!test_held && now >= test_next)
            {
                test_press(pawn, test_jump ? L"jump" : L"attack", now);
                if (test_jump)
                {
                    test_jump = false;
                    test_next = now + std::chrono::milliseconds(100);
                }
            }
        }
        else if (test_phase == 2 && player_id(pawn) == test_target && now >= test_next)
        {
            if (test_expire && now <= swap.combo_until + std::chrono::milliseconds(100))
            {
                return;
            }
            if (!test_expire && !test_input_mode && attack_state(state(pawn), pawn))
            {
                auto* anim = object(object(pawn, L"MyMesh"), L"AnimScriptInstance");
                const float length = animation_value(anim, L"SQEX_AnimGetCurrentLength");
                if (length > 0.0f &&
                    animation_value(anim, L"SQEX_AnimGetCurrentTime") / length < 0.55f)
                {
                    return;
                }
            }
            test_press(pawn, L"attack", now);
            test_phase = 3;
        }
        else if (test_phase == 3 && !swap.selections.empty())
        {
            const auto& selected = swap.selections.back();
            if (selected["player"] == test_target && !selected.value("resumed_swing", false))
            {
                if (test_input_mode == 2 && selected["index"].get<int>() < test_expected_index &&
                    swap.attack_buffer.front())
                {
                    return;
                }
                test_result = selected;
                if (selected["index"] != test_expected_index ||
                    (test_expire && selected["handoff"] != false) || selected["air"] != test_air)
                {
                    release_test_input();
                    test_status = "failed_selection";
                    test_phase = 0;
                    return;
                }
                test_phase = 4;
            }
        }
        else if (test_phase == 5 && player_id(pawn) == test_target)
        {
            release_test_input();
            if (!swap.request(pawn))
            {
                throw std::runtime_error("kh02_input_test_swap_back");
            }
            test_target = test_origin;
            test_press(pawn, L"attack", now);
            test_phase = 3;
        }
    }

    bool CharacterCombatSwap::give_combo_master()
    {
        if (!initialize())
        {
            return false;
        }
        auto* pawn = impl->player();
        const auto ability = try_resolve_runtime_enum_value(L"ETresAbilityKind::COMBO_MASTER");
        if (!impl->available(pawn) || player_id(pawn) != player_id_value(PlayerId::Sora) ||
            !ability || *ability > 255)
        {
            return false;
        }
        RuntimeSoraAbilityEntryState state;
        bool granted{};
        std::wstring reason;
        return try_grant_unique_equipped_runtime_sora_ability(static_cast<uint8_t>(*ability), pawn,
                                                              &granted, &state, &reason) &&
               state.owned && state.equipped;
    }

    bool CharacterCombatSwap::start_test(bool air, bool expire, float phase)
    {
        return initialize() && impl->session.start_test(air, expire, phase);
    }

    bool CharacterCombatSwap::start_camera_test()
    {
        return initialize() && impl->session.start_camera_test();
    }

    bool CharacterCombatSwap::start_input_test(bool swap_back)
    {
        return initialize() && impl->session.start_input_test(swap_back);
    }

    bool CharacterCombatSwap::start_menu_test()
    {
        return initialize() && impl->session.start_menu_test();
    }

    bool CharacterCombatSwap::start_movement_test(bool rotate)
    {
        return initialize() && impl->session.start_movement_test(rotate);
    }

    bool CharacterCombatSwap::start_locomotion_test(int swaps, int warmup_ms, int strength_percent,
                                                    bool release_at_swap, bool camera_relative,
                                                    bool capture_gait)
    {
        return initialize() &&
               impl->session.start_locomotion_test(swaps, warmup_ms, strength_percent,
                                                   release_at_swap, camera_relative, capture_gait);
    }

    auto CharacterCombatSwap::snapshot() const -> std::string
    {
        if (!impl)
        {
            return R"({"installed":false,"pending":false,"next_index":-1,"swaps":0,"error":"","selections":[],"test_status":"idle","test_result":null,"test_input_held":false})";
        }
        return nlohmann::json{{"installed", impl->installed},
                              {"pending", impl->queued},
                              {"next_index", impl->next_index},
                              {"swaps", impl->swaps},
                              {"camera_control_ready", impl->camera_control_ready},
                              {"camera_ui_current", impl->camera_ui_current},
                              {"pose_handoff", impl->pose_handoff},
                              {"camera_deltas", impl->camera_deltas},
                              {"swing_resume", impl->swing_resume},
                              {"duplicate_hits_blocked", impl->duplicate_hits_blocked},
                              {"buffered_presses", impl->buffered_presses},
                              {"replayed_presses", impl->replayed_presses},
                              {"accepted_presses", impl->accepted_presses},
                              {"pending_attack_inputs", impl->attack_buffer.size()},
                              {"injected_attack_held", impl->attack_press_held},
                              {"error", impl->error},
                              {"selections", impl->selections},
                              {"test_status", impl->session.test_status},
                              {"test_result", impl->session.test_result},
                              {"test_input_held", impl->session.test_held}}
            .dump();
    }
} // namespace kh3ap
