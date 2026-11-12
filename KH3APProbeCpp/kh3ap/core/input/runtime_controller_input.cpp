#include <polyhook2/Detour/x64Detour.hpp>
#include "kh3ap/core/input/runtime_controller_input.h"
#include "kh3ap/features/player/characters/character_profile.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include <memory>
#include <algorithm>
#include "kh3ap/core/input/runtime_controller_input_profile.h"
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/FScriptArray.hpp>
#include <Unreal/NameTypes.hpp>
#include <Unreal/Property/FStructProperty.hpp>
#include <array>
#include <cmath>

namespace kh3ap
{
    namespace
    {
        RuntimeControllerInputPolicy input_policy;
        constexpr auto steam_input_key_rva = 0x4436010;
        constexpr auto egs_input_key_rva = 0x49e1a90;
        constexpr auto steam_player_controller_vtable_rva = 0x6969b98;
        constexpr auto egs_player_controller_vtable_rva = 0x7bbe6e8;
        constexpr auto egs_winnie_controller_vtable_rva = 0x79f1dc8;
        constexpr auto input_key_vtable_offset = 0xd38;
        constexpr auto player_input_property_offset = 0x500;
        constexpr auto input_axis_vtable_offset = 0xd48;
        const auto input_axis_rva = character_native::rva(character_native::Site::input_axis);
        const auto player_input_axis_rva =
            character_native::rva(character_native::Site::input_axis_owner);
        constexpr auto key_state_count_offset = 0x280;
        constexpr auto key_state_free_count_offset = 0x2ac;
        constexpr auto key_state_hash_size_offset = 0x2c0;
        constexpr auto max_key_state_entries = 65536;
        constexpr auto key_state_data_offset = 0x278;
        constexpr auto key_state_external_hash_offset = 0x2b8;
        constexpr auto key_state_inline_hash_offset = 0x2b0;
        constexpr auto key_state_entry_bytes = 0xf8;
        constexpr auto key_state_hash_next_offset = 0xf0;
        constexpr auto controller_input_sample_rate = 60.0f;
        constexpr auto controller_rotation_pi = 3.14159265358979323846f;
        constexpr auto controller_rotation_half_turn_degrees = 180.0f;
        constexpr auto key_state_raw_value_offset = 0x18;
        constexpr auto key_state_processed_value_offset =
            key_state_raw_value_offset + 3 * sizeof(float);
        constexpr auto key_state_button_flags_offset = 0x34;
        constexpr uint32_t key_state_down_flags = 0x03;
        // Five frame-event arrays followed by five pending-event arrays.
        constexpr auto key_state_events_offset = 0x40;
        constexpr auto key_state_event_arrays = 10;
        constexpr auto key_state_raw_accumulator_offset = 0xe0;
        constexpr auto key_state_sample_count_offset = 0xec;
        constexpr auto native_fkey_bytes = 24;
        constexpr auto native_rotator_bytes = 12;
    } // namespace

    using namespace RC::Unreal;
    using namespace controller_input;
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    static thread_local bool synthetic_axis{};
#endif

    auto configure_runtime_controller_input(RuntimeControllerInputPolicy policy) -> void
    {
        input_policy = policy;
    }

    namespace
    {
        auto alternate_controller(UObject* controller) -> bool
        {
            return input_policy.alternate_controller_allowed &&
                   input_policy.alternate_controller_allowed(controller);
        }

        auto input_actor_world(UObject* pawn) -> UObject*
        {
            return input_policy.actor_world ? input_policy.actor_world(pawn) : nullptr;
        }
    } // namespace

    void runtime_controller_action_input(UObject* controller, std::wstring_view action,
                                         bool pressed)
    {
        const wchar_t* key_name = action == L"interact"          ? L"Gamepad_FaceButton_Top"
                                  : action == L"attack"          ? L"Gamepad_FaceButton_Bottom"
                                  : action == L"jump"            ? L"Gamepad_FaceButton_Right"
                                  : action == L"guard"           ? L"Gamepad_FaceButton_Left"
                                  : action == L"menu"            ? L"Gamepad_Special_Right"
                                  : action == L"camera"          ? L"Gamepad_Special_Left"
                                  : action == L"right_shoulder"  ? L"Gamepad_RightShoulder"
                                  : action == L"left_shoulder"   ? L"Gamepad_LeftShoulder"
                                  : action == L"left_trigger"    ? L"Gamepad_LeftTrigger"
                                  : action == L"right_trigger"   ? L"Gamepad_RightTrigger"
                                  : action == L"command_up"      ? L"Gamepad_DPad_Up"
                                  : action == L"command_down"    ? L"Gamepad_DPad_Down"
                                  : action == L"command_confirm" ? L"Gamepad_FaceButton_Bottom"
                                  : action == L"command_cancel"  ? L"Gamepad_FaceButton_Right"
                                  : action == L"pooh_shoot"      ? L"LeftMouseButton"
                                  : action == L"pooh_special"    ? L"F"
                                  : action == L"pooh_left"       ? L"A"
                                  : action == L"pooh_right"      ? L"D"
                                                                 : nullptr;
        if (!key_name)
        {
            throw std::runtime_error("unsupported_controller_action");
        }
        const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        DeathUiProfile profile;
        if (!is_uobject_valid_fast(controller) || !supported(base, profile))
        {
            throw std::runtime_error("controller_input_build_mismatch");
        }
        const uintptr_t input_rva = profile.steam ? steam_input_key_rva : egs_input_key_rva;
        const uintptr_t controller_vtable =
            profile.steam ? steam_player_controller_vtable_rva : egs_player_controller_vtable_rva;
        const bool input_signature =
            profile.steam
                ? signature(base + input_rva, {0x48, 0x8b, 0xc4, 0x48, 0x89, 0x50, 0x10, 0x55,
                                               0x41, 0x56, 0x41, 0x57, 0x48, 0x8d, 0x68, 0xa9,
                                               0x48, 0x81, 0xec, 0x00, 0x01, 0x00, 0x00, 0x48})
                : signature(base + input_rva,
                            {0x48, 0x8b, 0xc4, 0x48, 0x89, 0x50, 0x10, 0x56, 0x57});
        const bool winnie =
            !profile.steam &&
            field<uintptr_t>(controller, 0) == base + egs_winnie_controller_vtable_rva &&
            controller->GetClassPrivate()->GetName() == L"TresWinniePlayerController";
        if ((field<uintptr_t>(controller, 0) != base + controller_vtable &&
             (!alternate_controller(controller) && !winnie)) ||
            field<uintptr_t>(reinterpret_cast<void*>(field<uintptr_t>(controller, 0)),
                             input_key_vtable_offset) != base + input_rva ||
            !input_signature)
        {
            throw std::runtime_error("controller_input_build_mismatch");
        }
        offset(controller, L"PlayerInput", player_input_property_offset);
        if (!is_uobject_valid_fast(
                try_read_reflected_object_property_value(controller, L"PlayerInput")))
        {
            throw std::runtime_error("player_input_unavailable");
        }

        if (input_policy.route_action && input_policy.route_action(action, key_name, pressed))
        {
            return;
        }

        // Win64 passes FKey's 24-byte by-value temporary indirectly. InputKey
        // owns its normal temporary destructor, including lazy key details.
        struct Key
        {
            FName name;
            uintptr_t details{}, shared{};
        } key{FName{key_name}};

        static_assert(sizeof(Key) == native_fkey_bytes);
        using Input = bool(__fastcall*)(UObject*, void*, int32_t, float, bool);
        reinterpret_cast<Input>(base + input_rva)(controller, &key, pressed ? 0 : 1,
                                                  pressed ? 1.0f : 0.0f, true);
    }

    static uintptr_t require_axis_controller(UObject* controller)
    {
        const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        DeathUiProfile profile;
        if (!is_uobject_valid_fast(controller) || !supported(base, profile) ||
            (field<uintptr_t>(controller, 0) != base + (profile.steam
                                                            ? steam_player_controller_vtable_rva
                                                            : egs_player_controller_vtable_rva) &&
             !alternate_controller(controller)) ||
            field<uintptr_t>(reinterpret_cast<void*>(field<uintptr_t>(controller, 0)),
                             input_axis_vtable_offset) != base + input_axis_rva ||
            !character_native::verify_sites({character_native::Site::input_axis}))
        {
            throw std::runtime_error("controller_move_build_mismatch");
        }
        offset(controller, L"PlayerInput", player_input_property_offset);
        if (!is_uobject_valid_fast(
                try_read_reflected_object_property_value(controller, L"PlayerInput")))
        {
            throw std::runtime_error("player_input_unavailable");
        }
        return base;
    }

    static void send_axis(UObject* controller, uintptr_t base, const wchar_t* name, float value)
    {
        struct Key
        {
            FName name;
            uintptr_t details{}, shared{};
        } key{FName{name}};
        using Input = bool(__fastcall*)(UObject*, void*, float, float, int32_t, bool);
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        const bool previous = synthetic_axis;
        synthetic_axis = true;
        try
        {
#endif
            reinterpret_cast<Input>(base + input_axis_rva)(
                controller, &key, value, 1.0f / controller_input_sample_rate, 1, true);
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        }
        catch (...)
        {
            synthetic_axis = previous;
            throw;
        }
        synthetic_axis = previous;
#endif
    }

    void runtime_controller_look_input(UObject* controller, float x, float y)
    {
        const auto base = require_axis_controller(controller);
        send_axis(controller, base, L"Gamepad_RightX", x);
        send_axis(controller, base, L"Gamepad_RightY", y);
    }

    template <size_t Count>
    static auto controller_key_entries(UObject* controller,
                                       const std::array<const wchar_t*, Count>& names)
        -> std::array<void*, Count>
    {
        const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        DeathUiProfile profile;
        if (!is_uobject_valid_fast(controller) || !supported(base, profile) ||
            !character_native::verify_sites({character_native::Site::input_key_value,
                                             character_native::Site::input_axis_accumulation}))
        {
            throw std::runtime_error("controller_stick_state_build_mismatch");
        }
        offset(controller, L"PlayerInput", player_input_property_offset);
        auto* input = try_read_reflected_object_property_value(controller, L"PlayerInput");
        if (!is_uobject_valid_fast(input))
        {
            throw std::runtime_error("player_input_unavailable");
        }
        // Verified UPlayerInput::GetKeyValue map lookup on both storefronts. FKeyState begins
        // with RawValue, then the processed Value used by the public getter.
        // Re-injecting processed values applies the dead zone a second time.
        const int count = field<int>(input, key_state_count_offset);
        const int free_count = field<int>(input, key_state_free_count_offset);
        std::array<void*, Count> result{};
        if (count == free_count)
        {
            return result;
        }
        const int hash_size = field<int>(input, key_state_hash_size_offset);
        if (count < 0 || count > max_key_state_entries || hash_size <= 0 ||
            hash_size > max_key_state_entries || (hash_size & (hash_size - 1)))
        {
            throw std::runtime_error("controller_stick_state_map");
        }
        const auto data = field<uintptr_t>(input, key_state_data_offset);
        const auto external_hash = field<uintptr_t>(input, key_state_external_hash_offset);
        const auto hash = external_hash
                              ? external_hash
                              : reinterpret_cast<uintptr_t>(input) + key_state_inline_hash_offset;
        for (size_t key_index = 0; key_index < names.size(); ++key_index)
        {
            FName key{names[key_index]};
            uint64_t encoded{};
            static_assert(sizeof(key) == sizeof(encoded));
            std::memcpy(&encoded, &key, sizeof(encoded));
            const auto bucket =
                (static_cast<uint32_t>(encoded) + static_cast<uint32_t>(encoded >> 32)) &
                static_cast<uint32_t>(hash_size - 1);
            int index = field<int>(reinterpret_cast<void*>(hash), bucket * 4);
            for (int traversed = 0; index != -1 && traversed < count; ++traversed)
            {
                if (index < 0 || index >= count)
                {
                    throw std::runtime_error("controller_stick_state_index");
                }
                auto* entry = reinterpret_cast<void*>(data + static_cast<uintptr_t>(index) *
                                                                 key_state_entry_bytes);
                if (field<uint64_t>(entry, 0) == encoded)
                {
                    result[key_index] = entry;
                    break;
                }
                index = field<int>(entry, key_state_hash_next_offset);
            }
        }
        return result;
    }

    static auto controller_stick_entries(UObject* controller, bool right_stick)
        -> std::array<void*, 2>
    {
        return controller_key_entries(controller,
                                      right_stick ? std::array{L"Gamepad_RightX", L"Gamepad_RightY"}
                                                  : std::array{L"Gamepad_LeftX", L"Gamepad_LeftY"});
    }

    auto runtime_controller_capture_stick(UObject* controller, bool right_stick)
        -> RuntimeControllerStickState
    {
        const auto entries = controller_stick_entries(controller, right_stick);
        RuntimeControllerStickState result;
        for (size_t axis = 0; axis < entries.size(); ++axis)
        {
            if (entries[axis])
            {
                result.raw[axis] = field<float>(entries[axis], key_state_raw_value_offset);
                result.committed_raw[axis] = result.raw[axis];
                result.processed[axis] =
                    field<float>(entries[axis], key_state_processed_value_offset);
                result.pending_samples[axis] =
                    field<uint8_t>(entries[axis], key_state_sample_count_offset) != 0;
                if (result.pending_samples[axis])
                {
                    // A device change may have arrived after the last input
                    // pass. Carry it too, especially a pending stick release.
                    result.raw[axis] =
                        field<float>(entries[axis], key_state_raw_accumulator_offset);
                }
            }
        }
        return result;
    }

    auto runtime_controller_stick_state(UObject* controller, bool right_stick)
        -> std::array<float, 2>
    {
        const auto entries = controller_stick_entries(controller, right_stick);
        std::array<float, 2> result{};
        for (size_t axis = 0; axis < entries.size(); ++axis)
        {
            if (entries[axis])
            {
                result[axis] = field<float>(entries[axis], key_state_raw_value_offset);
            }
        }
        return result;
    }

    namespace
    {
        using PlayerInputAxis = bool(__fastcall*)(UObject*, void*, float, float, int32_t, bool);
        uint64_t original_player_input_axis{};
        std::unique_ptr<PLH::x64Detour> axis_handoff_hook;
        FWeakObjectPtr seeded_input, seeded_controller;
        std::array<bool, 4> seeded_axes{};
        std::array<float, 4> seeded_values{};
        bool axis_handoff_pending{}, seeding_axes{};
        DWORD axis_handoff_thread{};
        constexpr std::array axis_names{L"Gamepad_LeftX", L"Gamepad_LeftY", L"Gamepad_RightX",
                                        L"Gamepad_RightY"};
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        FWeakObjectPtr test_axis_player, test_axis_world;
        bool test_axis_owned{};
#endif

        bool handoff_axis(UObject* input, void* key, float value, float delta, int32_t samples,
                          bool gamepad)
        {
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            if (test_axis_owned && !synthetic_axis && GetCurrentThreadId() == axis_handoff_thread)
            {
                auto* player = test_axis_player.Get();
                auto* world = test_axis_world.Get();
                auto* controller =
                    is_uobject_valid_fast(player)
                        ? try_read_reflected_object_property_value(player, L"PlayerController")
                        : nullptr;
                auto* pawn = is_uobject_valid_fast(controller)
                                 ? try_read_reflected_object_property_value(controller, L"Pawn")
                                 : nullptr;
                if (!is_uobject_valid_fast(world) || !is_uobject_valid_fast(pawn) ||
                    input_actor_world(pawn) != world)
                {
                    test_axis_owned = false;
                }
                else if (try_read_reflected_object_property_value(controller, L"PlayerInput") ==
                         input)
                {
                    const auto name = *reinterpret_cast<FName*>(key);
                    if (name == FName{axis_names[0]} || name == FName{axis_names[1]})
                    {
                        // Keep the native owner's FKey lifetime/event handling,
                        // without adding physical noise to the owned test stick.
                        return reinterpret_cast<PlayerInputAxis>(original_player_input_axis)(
                            input, key, 0.0f, delta, 0, gamepad);
                    }
                }
            }
#endif
            if (axis_handoff_pending && !seeding_axes &&
                GetCurrentThreadId() == axis_handoff_thread)
            {
                try
                {
                    auto* controller = seeded_controller.Get();
                    auto* player =
                        is_uobject_valid_fast(controller)
                            ? try_read_reflected_object_property_value(controller, L"Player")
                            : nullptr;
                    if (!is_uobject_valid_fast(player) ||
                        try_read_reflected_object_property_value(player, L"PlayerController") !=
                            controller ||
                        try_read_reflected_object_property_value(controller, L"PlayerInput") !=
                            seeded_input.Get())
                    {
                        seeded_axes.fill(false);
                    }
                    else if (input == seeded_input.Get())
                    {
                        const auto name = *reinterpret_cast<FName*>(key);
                        for (size_t group = 0; group < 2; ++group)
                        {
                            if (!seeded_axes[group * 2] && !seeded_axes[group * 2 + 1])
                            {
                                continue;
                            }
                            const auto entries = controller_stick_entries(controller, group != 0);
                            for (size_t axis = 0; axis < entries.size(); ++axis)
                            {
                                const auto index = group * 2 + axis;
                                if (!seeded_axes[index])
                                {
                                    continue;
                                }
                                auto* entry = entries[axis];
                                const auto count =
                                    entry ? field<uint8_t>(entry, key_state_sample_count_offset)
                                          : 0;
                                if (!count)
                                {
                                    // The normal input pass already consumed the seed.
                                    seeded_axes[index] = false;
                                }
                                else if (name == FName{axis_names[index]})
                                {
                                    // A newer device sample replaces our one carried
                                    // sample, then enters the ordinary native path.
                                    const float accumulated =
                                        field<float>(entry, key_state_raw_accumulator_offset) -
                                        seeded_values[index];
                                    const auto remaining = static_cast<uint8_t>(count - 1);
                                    auto* bytes = reinterpret_cast<std::byte*>(entry);
                                    std::memcpy(bytes + key_state_raw_accumulator_offset,
                                                &accumulated, sizeof(float));
                                    std::memcpy(bytes + key_state_sample_count_offset, &remaining,
                                                sizeof(remaining));
                                    seeded_axes[index] = false;
                                }
                            }
                        }
                    }
                    axis_handoff_pending = std::any_of(seeded_axes.begin(), seeded_axes.end(),
                                                       [](bool armed)
                                                       {
                                                           return armed;
                                                       });
                }
                catch (...)
                {
                    // Device input must always reach the original owner.
                    seeded_axes.fill(false);
                    axis_handoff_pending = false;
                }
            }
            return reinterpret_cast<PlayerInputAxis>(original_player_input_axis)(
                input, key, value, delta, samples, gamepad);
        }

        void require_axis_handoff(uintptr_t base)
        {
            if (axis_handoff_hook)
            {
                return;
            }
            if (!character_native::verify_sites({character_native::Site::input_axis_owner}))
            {
                throw std::runtime_error("controller_axis_handoff_build_mismatch");
            }
            axis_handoff_thread = GetCurrentThreadId();
            auto hook = std::make_unique<PLH::x64Detour>(base + player_input_axis_rva,
                                                         reinterpret_cast<uintptr_t>(&handoff_axis),
                                                         &original_player_input_axis);
            if (!hook->hook() || !original_player_input_axis)
            {
                throw std::runtime_error("controller_axis_handoff_hook_failed");
            }
            axis_handoff_hook = std::move(hook);
        }
    } // namespace

#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    void runtime_controller_test_stick_lease(UObject* controller, bool owned)
    {
        test_axis_owned = false;
        test_axis_player = {};
        test_axis_world = {};
        if (!owned)
        {
            return;
        }
        const auto base = require_axis_controller(controller);
        require_axis_handoff(base);
        auto* player = try_read_reflected_object_property_value(controller, L"Player");
        auto* pawn = try_read_reflected_object_property_value(controller, L"Pawn");
        auto* world = input_actor_world(pawn);
        if (!is_uobject_valid_fast(player) || !is_uobject_valid_fast(world) ||
            try_read_reflected_object_property_value(player, L"PlayerController") != controller)
        {
            throw std::runtime_error("controller_test_stick_ownership");
        }
        test_axis_player = make_uobject_weak(player);
        test_axis_world = make_uobject_weak(world);
        test_axis_owned = true;
    }
#endif

    void runtime_controller_restore_stick(UObject* controller, bool right_stick,
                                          const RuntimeControllerStickState& state)
    {
        for (size_t axis = 0; axis < state.raw.size(); ++axis)
        {
            if (!std::isfinite(state.raw[axis]) || !std::isfinite(state.processed[axis]))
            {
                throw std::runtime_error("controller_stick_restore_value");
            }
        }
        const auto base = require_axis_controller(controller);
        require_axis_handoff(base);
        auto* input = try_read_reflected_object_property_value(controller, L"PlayerInput");
        if (seeded_input.Get() != input)
        {
            seeded_axes.fill(false);
            axis_handoff_pending = false;
            seeded_input = make_uobject_weak(input);
            seeded_controller = make_uobject_weak(controller);
        }
        for (size_t axis = 0; axis < state.raw.size(); ++axis)
        {
            auto entries = controller_stick_entries(controller, right_stick);
            const auto index = (right_stick ? 2 : 0) + axis;
            const bool pending =
                entries[axis] && field<uint8_t>(entries[axis], key_state_sample_count_offset) != 0;
            float raw = pending ? field<float>(entries[axis], key_state_raw_accumulator_offset)
                                : state.raw[axis];
            if (!pending)
            {
                seeding_axes = true;
                try
                {
                    send_axis(controller, base, axis_names[index], raw);
                }
                catch (...)
                {
                    seeding_axes = false;
                    throw;
                }
                seeding_axes = false;
                seeded_values[index] = raw;
                // A zero seed still counts as a pending native sample;
                // retire it too before a newer direction arrives.
                seeded_axes[index] = true;
            }
            // A key insertion can reallocate the map. Resolve again, and carry
            // both current values for Run's immediate entry update. Newer
            // pending device events remain authoritative for the next pass.
            entries = controller_stick_entries(controller, right_stick);
            if (!entries[axis])
            {
                throw std::runtime_error("controller_stick_restore_state");
            }
            auto* bytes = reinterpret_cast<std::byte*>(entries[axis]);
            std::memcpy(bytes + key_state_raw_value_offset, &raw, sizeof(float));
            std::memcpy(bytes + key_state_processed_value_offset, &state.processed[axis],
                        sizeof(float));
        }
        axis_handoff_pending = std::any_of(seeded_axes.begin(), seeded_axes.end(),
                                           [](bool armed)
                                           {
                                               return armed;
                                           });
    }

    void runtime_controller_stick_input(UObject* controller, float right, float forward)
    {
        const auto base = require_axis_controller(controller);
        send_axis(controller, base, L"Gamepad_LeftX", right);
        send_axis(controller, base, L"Gamepad_LeftY", forward);
    }

    void runtime_controller_clear_parked_input(UObject* controller)
    {
        if (!is_uobject_valid_fast(controller) ||
            try_read_reflected_object_property_value(controller, L"Player") ||
            try_read_reflected_object_property_value(controller, L"Pawn"))
        {
            throw std::runtime_error("controller_input_clear_requires_parked_controller");
        }
        auto* input = try_read_reflected_object_property_value(controller, L"PlayerInput");
        if (!is_uobject_valid_fast(input) || input->GetOuterPrivate() != controller)
        {
            throw std::runtime_error("parked_controller_input_owner_mismatch");
        }

        const auto clear_values = [](std::byte* entry)
        {
            constexpr std::array<float, 3> zero{};
            constexpr uint8_t no_samples{};
            std::memcpy(entry + key_state_raw_value_offset, zero.data(), sizeof(zero));
            std::memcpy(entry + key_state_processed_value_offset, zero.data(), sizeof(zero));
            std::memcpy(entry + key_state_raw_accumulator_offset, zero.data(), sizeof(zero));
            std::memcpy(entry + key_state_sample_count_offset, &no_samples, sizeof(no_samples));
        };
        for (auto* entry : controller_key_entries(controller, axis_names))
        {
            if (entry)
            {
                clear_values(static_cast<std::byte*>(entry));
            }
        }

        // Releases received during the swap go to the new controller. The
        // parked one must discard both its held state and any queued repeats.
        constexpr std::array navigation_keys{
            L"Gamepad_DPad_Up", L"Gamepad_DPad_Down", L"Gamepad_DPad_Left", L"Gamepad_DPad_Right",
            L"Gamepad_LeftStick_Up", L"Gamepad_LeftStick_Down", L"Gamepad_LeftStick_Left",
            L"Gamepad_LeftStick_Right", L"Up", L"Down", L"Left", L"Right",
        };
        static_assert(key_state_events_offset + key_state_event_arrays * sizeof(FScriptArray) ==
                      key_state_raw_accumulator_offset);
        for (auto* entry : controller_key_entries(controller, navigation_keys))
        {
            if (!entry)
            {
                continue;
            }
            auto* bytes = static_cast<std::byte*>(entry);
            clear_values(bytes);
            const uint32_t flags = field<uint32_t>(entry, key_state_button_flags_offset) &
                                   ~key_state_down_flags;
            std::memcpy(bytes + key_state_button_flags_offset, &flags, sizeof(flags));
            for (size_t index = 0; index < key_state_event_arrays; ++index)
            {
                auto* events = reinterpret_cast<FScriptArray*>(
                    bytes + key_state_events_offset + index * sizeof(FScriptArray));
                events->Empty(events->Max(), sizeof(uint32_t), alignof(uint32_t));
            }
        }
        if (seeded_controller.Get() == controller)
        {
            seeded_axes.fill(false);
            axis_handoff_pending = false;
            seeded_controller = {};
            seeded_input = {};
        }
    }

    void runtime_controller_move_input(UObject* controller, float x, float y)
    {
        if (!is_uobject_valid_fast(controller))
        {
            throw std::runtime_error("controller_move_unavailable");
        }
        // KH3's field camera can rotate independently of ControlRotation.
        // Left-stick movement is relative to the rendered gameplay camera.
        float right{}, forward{};
        if (x != 0 || y != 0)
        {
            auto* camera =
                try_read_reflected_object_property_value(controller, L"PlayerCameraManager");
            if (!is_uobject_valid_fast(camera))
            {
                throw std::runtime_error("controller_move_camera_unavailable");
            }
            auto* rotation = camera->GetFunctionByNameInChain(L"GetCameraRotation");
            auto* returned = is_uobject_valid_fast(rotation)
                                 ? find_function_param_by_name(rotation, L"ReturnValue")
                                 : nullptr;
            if (!returned || !returned->IsA<FStructProperty>() ||
                returned->GetElementSize() != native_rotator_bytes ||
                rotation->GetParmsSize() != native_rotator_bytes)
            {
                throw std::runtime_error("controller_move_rotation_layout");
            }
            std::array<float, 3> angles{};
            camera->ProcessEvent(rotation, angles.data());
            const auto yaw =
                angles[1] * controller_rotation_pi / controller_rotation_half_turn_degrees;
            right = -std::sin(yaw) * x + std::cos(yaw) * y;
            forward = std::cos(yaw) * x + std::sin(yaw) * y;
        }
        runtime_controller_stick_input(controller, right, forward);
    }
} // namespace kh3ap
