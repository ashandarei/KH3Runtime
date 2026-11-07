#include "kh3ap/features/player/characters/character_profile.h"
#include "kh3ap/features/player/characters/character_camera.h"
#include "kh3ap/features/player/characters/character_swap.h"
#include "kh3ap/features/player/movement/character_traversal.h"
#include "kh3ap/features/player/player_animation_support.h"
#include "kh3ap/core/input/options_input_ownership.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/reflection/reflected_property_cache.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include <Unreal/UClass.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <stdexcept>
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
#include <DynamicOutput/DynamicOutput.hpp>
#endif
#include <cstring>
#include <cmath>
#define NOMINMAX
#include <Windows.h>

namespace kh3ap::character_camera
{
    namespace
    {
        const uintptr_t request_camera_rva = character_native::rva(character_native::Site::request_camera);
        constexpr size_t request_camera_virtual_slot = 0x2a28;
        RuntimeOptionsInputOwnership camera_input;
        RC::Unreal::FWeakObjectPtr sora_camera_class;
        RC::Unreal::FWeakObjectPtr height_world;
        RC::Unreal::FWeakObjectPtr adjusted_camera;
        float sora_target_height{};
        ReflectedPropertyCache<64> properties;

        struct Call : Kh02ReflectedCall
        {
            Call(RC::Unreal::UObject *owner, const wchar_t *method)
                : Kh02ReflectedCall(owner, method, "character_camera", 128)
            {
            }
        };

        auto *object(RC::Unreal::UObject *owner, const wchar_t *field)
        {
            return properties.read_object(owner, field);
        }

        auto *camera_class_field(RC::Unreal::UObject *load_asset)
        {
            auto *field = properties.resolve(load_asset, L"PlayerCameraHoldCamera");
            if (!field || field->GetClass().GetName() != L"ClassProperty" ||
                field->GetElementSize() != sizeof(RC::Unreal::UObject *))
            {
                throw std::runtime_error("character_camera_load_asset_layout");
            }
            return field->ContainerPtrToValuePtr<RC::Unreal::UObject *>(load_asset);
        }

        auto *field_camera(RC::Unreal::UObject *pawn)
        {
            auto *manager = object(object(pawn, L"Controller"), L"PlayerCameraManager");
            return object(manager, L"m_CameraNormal1st");
        }

        float socket_height(RC::Unreal::UObject *pawn, RC::Unreal::UObject *camera)
        {
            Call bone(camera, L"GetTargetBoneName");
            bone.run();
            auto *returned = bone.param(L"ReturnValue");
            if (!returned || returned->GetElementSize() != sizeof(RC::Unreal::FName))
            {
                throw std::runtime_error("character_camera_target_bone_layout");
            }
            const auto name =
                returned->ContainerPtrToValuePtr<RC::Unreal::FName>(bone.parameters.data())->ToString();
            Call socket(object(pawn, L"MyMesh"), L"GetSocketLocation");
            socket.name(L"InSocketName", name.c_str());
            socket.run();
            Call location(pawn, L"K2_GetActorLocation");
            location.run();
            return socket.result_vector()[2] - location.result_vector()[2];
        }
    } // namespace

    void capture_sora(RC::Unreal::UObject *pawn, bool capture_field_height)
    {
        sora_camera_class = make_uobject_weak(*camera_class_field(object(pawn, L"m_LoadAsset")));
        height_world = {};
        if (!capture_field_height)
        {
            return;
        }
        auto *camera = field_camera(pawn);
        if (camera)
        {
            Call offset(camera, L"GetTargetOffset");
            offset.run();
            sora_target_height = socket_height(pawn, camera) + offset.result_vector()[2];
            if (std::isfinite(sora_target_height))
            {
                height_world = make_uobject_weak(kh02_actor_world(pawn));
            }
        }
    }

    void match_field_height(RC::Unreal::UObject *pawn)
    {
        using namespace RC::Unreal;
        auto *id = properties.resolve(pawn, L"m_PlayerUniqueID");
        if (!id || id->GetElementSize() != sizeof(uint8_t) ||
            *id->ContainerPtrToValuePtr<uint8_t>(pawn) !=
                static_cast<uint8_t>(player_animation_support::PlayerId::Roxas) ||
            !character_swap::owns_swap_pawn(pawn) || !height_world.Get() ||
            kh02_actor_world(pawn) != height_world.Get())
        {
            return;
        }
        auto *camera = field_camera(pawn);
        if (!camera || adjusted_camera.Get() == camera)
        {
            return;
        }
        Call offset(camera, L"GetTargetOffset");
        offset.run();
        auto target_offset = offset.result_vector();
        const auto original_height = socket_height(pawn, camera);
        target_offset[2] = sora_target_height - original_height;
        if (!std::isfinite(target_offset[2]))
        {
            throw std::runtime_error("character_camera_target_height_invalid");
        }
        // Keep the native camera socket, orbit and collision behavior. Only
        // this Roxas camera compensates for its socket's different height.
        Call adjust(camera, L"BP_SetTargetOffset");
        adjust.vector(L"TargetOffset", target_offset[0], target_offset[1], target_offset[2]);
        adjust.number(L"Time", 0.0f);
        adjust.run();
        adjusted_camera = make_uobject_weak(camera);
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        RC::Output::send<RC::LogLevel::Normal>(
            STR("[KH3APProbeCpp] CharacterCameraHeight player={} sora_target={} socket={} "
                "offset={}\n"),
            player_id_value(PlayerId::Roxas), sora_target_height, original_height,
            target_offset[2]);
#endif
    }

    bool prepare(RC::Unreal::UObject *pawn)
    {
        using namespace RC::Unreal;
        if (!character_swap::owns_swap_pawn(pawn))
        {
            return true;
        }
        auto *original = object(pawn, L"m_LoadAsset");
        auto **current = camera_class_field(original);
        if (is_uobject_valid_fast(*current))
        {
            return true;
        }
        auto *source = sora_camera_class.Get();
        if (!source)
        {
            return false;
        }
        // Keep authored camera choices and all other load-asset fields. Only
        // this pawn receives the missing configured phone-camera class.
        FStaticConstructObjectParameters parameters(original->GetClassPrivate(), pawn);
        parameters.Template = original;
        parameters.SetFlags = RF_Transient;
        auto *private_asset = UObjectGlobals::StaticConstructObject(parameters);
        if (!is_uobject_valid_fast(private_asset))
        {
            throw std::runtime_error("character_camera_load_asset_create");
        }
        *camera_class_field(private_asset) = source;
        auto *field = properties.resolve(pawn, L"m_LoadAsset");
        if (!field || field->GetClass().GetName() != L"ObjectProperty" ||
            field->GetElementSize() != sizeof(UObject *))
        {
            throw std::runtime_error("character_camera_pawn_load_asset_layout");
        }
        *field->ContainerPtrToValuePtr<UObject *>(pawn) = private_asset;
        return true;
    }

    bool route(int controller, std::wstring_view key, int event, bool gamepad)
    {
        if (!gamepad || key != L"Gamepad_Special_Left")
        {
            return false;
        }
        return camera_input.route(controller, std::wstring(key), event, [&] {
            if (event != 0)
            {
                return true;
            }
            auto *pawn = character_swap::current_hero();
            if (!character_swap::owns_swap_pawn(pawn) || !character_traversal::ready(pawn) ||
                !player_animation_support::supported_skeleton_layout() || !prepare(pawn))
            {
                return false;
            }
            const auto image = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
            const auto method = (*reinterpret_cast<uintptr_t **>(
                pawn))[request_camera_virtual_slot / sizeof(uintptr_t)];
            if (method != image + request_camera_rva ||
                !character_native::verify_sites({character_native::Site::request_camera}))
            {
                return false;
            }
            // The alternate controllers omit Sora's camera binding. Use the
            // shared owning request, retaining its story unlock, pause, battle,
            // movement, input and temporary stop-camera eligibility checks.
            const bool accepted = reinterpret_cast<bool (*)(RC::Unreal::UObject *)>(method)(pawn);
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            RC::Output::send<RC::LogLevel::Normal>(
                STR("[KH3APProbeCpp] Character camera request pawn={} accepted={}\n"),
                pawn->GetName(), accepted);
#endif
            return accepted;
        });
    }
} // namespace kh3ap::character_camera
