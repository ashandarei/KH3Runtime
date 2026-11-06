#include <array>
#include <chrono>
#include "kh3ap/core/platform/game_build.h"
#include "kh3ap/features/enemies/enemy_clear/service_impl.h"
#include "kh3ap/features/enemies/enemy_clear/support.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/bridge/serialization/json_fields.h"

#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <Unreal/Property/FStructProperty.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UScriptStruct.hpp>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <stdexcept>
#include <cmath>
#include <cstring>

namespace kh3ap
{
    namespace
    {
        constexpr auto boss_test_center_getter_site_rva = 0x24c54e5;
        constexpr auto boss_test_center_offset = 0xc80;

        constexpr auto max_test_center_horizontal_distance = 5000;
        constexpr auto max_test_center_vertical_distance = 100;
    } // namespace

    using namespace RC::Unreal;
    using namespace enemy_clear_detail;

    std::array<float, 3> RuntimeEnemyClearService::Impl::read_test_center(UObject* boss)
    {
        if (!is_uobject_valid_fast(boss) || !is_uobject_valid_fast(boss->GetClassPrivate()) ||
            boss->GetClassPrivate()->GetPathName() !=
                L"/Game/Blueprints/Enemy/e_ex353/e_ex353_Pawn.e_ex353_Pawn_C" ||
            !boss->GetPathName().starts_with(
                L"/Game/Levels/fz/fz_01/umap/fz_01_enemy_03.fz_01_enemy_03:PersistentLevel."))
        {
            throw std::runtime_error("center_test_requires_marshmallow_data_xemnas");
        }
        const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        constexpr std::array<uint8_t, 24> code{0xf2, 0x0f, 0x10, 0x81, 0x80, 0x0c, 0x00, 0x00,
                                               0x8b, 0x81, 0x88, 0x0c, 0x00, 0x00, 0xf2, 0x41,
                                               0x0f, 0x11, 0x00, 0x41, 0x89, 0x40, 0x08, 0xc3};
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || nt->Signature != IMAGE_NT_SIGNATURE ||
            !kh3ap::native_build::egs.matches(nt->FileHeader.TimeDateStamp,
                                              nt->OptionalHeader.SizeOfImage) ||
            std::memcmp(reinterpret_cast<void*>(base + boss_test_center_getter_site_rva),
                        code.data(), code.size()))
        {
            throw std::runtime_error("unsupported_center_getter_image");
        }
        Call get(boss, L"GetCenterLocation");
        auto* result = get.field(L"ReturnValue");
        if (get.function->GetParmsSize() != sizeof(std::array<float, 3>) ||
            result->GetOffset_Internal() != 0 || !result->IsA<FStructProperty>() ||
            result->GetElementSize() != sizeof(std::array<float, 3>) ||
            static_cast<FStructProperty*>(result)->GetStruct()->GetName() != L"Vector")
        {
            throw std::runtime_error("unsupported_center_getter_layout");
        }
        get.invoke();
        std::array<float, 3> value{}, storage{};
        std::memcpy(value.data(), get.params.data(), sizeof(std::array<float, 3>));
        std::memcpy(storage.data(), reinterpret_cast<std::byte*>(boss) + boss_test_center_offset,
                    sizeof(std::array<float, 3>));
        if (value != storage || std::any_of(value.begin(), value.end(),
                                            [](float v)
                                            {
                                                return !std::isfinite(v);
                                            }))
        {
            throw std::runtime_error("center_getter_storage_mismatch");
        }
        return value;
    }

    void RuntimeEnemyClearService::Impl::restore_test_center()
    {
        if (auto* boss = centered_boss.Get(); is_uobject_valid_fast(boss))
        {
            (void)read_test_center(boss);
            std::memcpy(reinterpret_cast<std::byte*>(boss) + boss_test_center_offset,
                        center_original.data(), sizeof(std::array<float, 3>));
            center_after = read_test_center(boss);
            if (center_after != center_original)
            {
                throw std::runtime_error("center_restore_not_observed");
            }
        }
        centered_boss = nullptr;
        center_world = nullptr;
        center_sora = nullptr;
        center_expires = 0;
    }

    void RuntimeEnemyClearService::Impl::tick_test_center()
    {
        if (!center_expires)
        {
            return;
        }
        auto* boss = centered_boss.Get();
        auto* pawn = config.playable_sora();
        if (unix_ms() >= center_expires || !is_uobject_valid_fast(boss) ||
            !is_uobject_valid_fast(pawn) || pawn != center_sora.Get() ||
            actor_world(pawn) != center_world.Get())
        {
            restore_test_center();
        }
    }

    void RuntimeEnemyClearService::Impl::set_gigas_position(UObject* pawn)
    {
        Call accompany(pawn, L"GetSpecificActionAccompanyPawn");
        auto* id = accompany.field(L"ID");
        if (id->GetClass().GetName() != L"IntProperty" || id->GetElementSize() != sizeof(int32_t))
        {
            throw std::runtime_error("gigas_accompany_parameter_layout");
        }
        *id->ContainerPtrToValuePtr<int32_t>(accompany.params.data()) = 0;
        accompany.invoke();
        auto* returned = accompany.field(L"ReturnValue");
        if (returned->GetClass().GetName() != L"ObjectProperty" ||
            returned->GetElementSize() != sizeof(UObject*))
        {
            throw std::runtime_error("gigas_accompany_return_layout");
        }
        auto* gigas = *returned->ContainerPtrToValuePtr<UObject*>(accompany.params.data());
        auto* klass = UObjectGlobals::StaticFindObject<UClass*>(
            nullptr, nullptr, L"/Script/TresGame.TresVehiclePawnGigas");
        if (!is_uobject_valid_fast(gigas) || !is_uobject_valid_fast(klass) || !gigas->IsA(klass))
        {
            throw std::runtime_error("sora_is_not_riding_gigas");
        }
        Call rotation(gigas, L"K2_GetActorRotation");
        rotation.invoke();
        auto* rotation_result = rotation.field(L"ReturnValue");
        Call teleport(gigas, L"TeleportGigas");
        auto* location = teleport.field(L"InLocation");
        auto* facing = teleport.field(L"InRotation");
        const auto layout = [](FProperty* p, const wchar_t* type)
        {
            return p->GetClass().GetName() == L"StructProperty" &&
                   p->GetElementSize() == sizeof(std::array<float, 3>) &&
                   static_cast<FStructProperty*>(p)->GetStruct()->GetName() == type;
        };
        if (!layout(location, L"Vector") || !layout(facing, L"Rotator") ||
            !layout(rotation_result, L"Rotator"))
        {
            throw std::runtime_error("gigas_teleport_layout");
        }
        std::memcpy(location->ContainerPtrToValuePtr<void>(teleport.params.data()),
                    requested_position.data(), sizeof(std::array<float, 3>));
        std::memcpy(facing->ContainerPtrToValuePtr<void>(teleport.params.data()),
                    rotation_result->ContainerPtrToValuePtr<void>(rotation.params.data()),
                    sizeof(std::array<float, 3>));
        position_before = position(gigas);
        teleport.invoke(false);
        position_immediate = position_after = position(gigas);
        double distance_sq = 0;
        for (size_t i = 0; i < 3; ++i)
        {
            distance_sq += std::pow(position_immediate[i] - requested_position[i], 2.0);
        }
        position_dispatched = distance_sq <= 1.0;
        if (!position_dispatched)
        {
            throw std::runtime_error("gigas_position_not_observed");
        }
    }

    void RuntimeEnemyClearService::Impl::set_position(UObject* pawn)
    {
        Call c(pawn, L"K2_SetActorLocation");
        auto* vector = c.field(L"NewLocation");
        if (vector->GetClass().GetName() != L"StructProperty" ||
            vector->GetElementSize() != sizeof(std::array<float, 3>) ||
            static_cast<FStructProperty*>(vector)->GetStruct()->GetName() != L"Vector")
        {
            throw std::runtime_error("unsupported_vector_layout");
        }
        std::memcpy(vector->ContainerPtrToValuePtr<void>(c.params.data()),
                    requested_position.data(), sizeof(std::array<float, 3>));
        if (!write_bool_function_param(c.field(L"bSweep"), c.params.data(), false) ||
            !write_bool_function_param(c.field(L"bTeleport"), c.params.data(), true))
        {
            throw std::runtime_error("unsupported_position_parameters");
        }
        auto* returned = c.field(L"ReturnValue");
        if (returned->GetClass().GetName() != L"BoolProperty")
        {
            throw std::runtime_error("unsupported_position_return");
        }
        // This is teleportation; intentional overlap updates activate normal
        // map triggers. No event-completion flags or collision settings change.
        c.invoke(false);
        position_dispatched =
            static_cast<FBoolProperty*>(returned)->GetPropertyValueInContainer(c.params.data());
        position_immediate = position_after = position(pawn);
        if (!position_dispatched)
        {
            throw std::runtime_error("native_position_rejected");
        }
        double distance_sq = 0;
        for (size_t i = 0; i < 3; ++i)
        {
            distance_sq += std::pow(position_immediate[i] - requested_position[i], 2.0);
        }
        if (distance_sq > 1.0)
        {
            throw std::runtime_error("position_write_not_observed");
        }
    }

    void RuntimeEnemyClearService::Impl::accept_inspect_transform(UObject* pawn,
                                                                  const std::string& text)
    {
        actor_rotation = rotation(pawn);
        for (const auto component : position_before)
        {
            if (!std::isfinite(component))
            {
                throw std::runtime_error("nonfinite_actor_position");
            }
        }
        transform_captured = true;
        finish(L"completed", L"transform_snapshot");
        return;
    }

    void RuntimeEnemyClearService::Impl::accept_boss_center(UObject* pawn, const std::string& text)
    {
        const auto seconds = JsonFieldReader::int64_field(text, "seconds").value_or(-1);
        if (seconds < 0 || seconds > maximum_center_duration_seconds)
        {
            throw std::runtime_error("center_seconds_out_of_range");
        }
        if (!seconds)
        {
            if (!center_expires)
            {
                throw std::runtime_error("no_active_center_test");
            }
            center_before = read_test_center(centered_boss.Get());
            report(L"accepted", L"center_restore_checkpoint");
            restore_test_center();
            finish(L"completed", L"center_readback");
            return;
        }
        const auto selected = JsonFieldReader::string_field(text, "actor").value_or(L"");
        if (selected.empty())
        {
            throw std::runtime_error("center_requires_exact_actor");
        }
        enumerate(pawn);
        std::erase_if(targets,
                      [&](const Target& t)
                      {
                          return t.name != selected;
                      });
        if (targets.size() != 1 || targets[0].hp_before <= 0)
        {
            throw std::runtime_error("selected_enemy_not_present");
        }
        auto* boss = targets[0].actor.Get();
        if (center_expires && centered_boss.Get() != boss)
        {
            throw std::runtime_error("restore_previous_center_first");
        }
        const auto before = read_test_center(boss);
        std::array<float, 3> desired{};
        size_t i = 0;
        for (const char* axis : {"x", "y", "z"})
        {
            const auto value = JsonFieldReader::number_field(text, axis);
            if (!value || !std::isfinite(*value) || std::abs(*value) > max_enemy_request_coordinate)
            {
                throw std::runtime_error("invalid_center_vector");
            }
            desired[i++] = static_cast<float>(*value);
        }
        const auto origin = center_expires ? center_original : before;
        if (std::hypot(desired[0] - origin[0], desired[1] - origin[1]) >
                max_test_center_horizontal_distance ||
            std::abs(desired[2] - origin[2]) > max_test_center_vertical_distance)
        {
            throw std::runtime_error("center_shift_exceeds_test_bounds");
        }
        center_before = before;
        center_actor = selected;
        report(L"accepted", L"center_checkpoint");
        // Compare Sora's world with the same context on later ticks;
        // the boss may belong to a streamed enemy sublevel.
        if (!center_expires)
        {
            center_original = before;
            centered_boss = boss;
            center_world = actor_world(pawn);
            center_sora = pawn;
        }
        center_expires = unix_ms() + std::chrono::duration_cast<std::chrono::milliseconds>(
                                         std::chrono::seconds(seconds))
                                         .count();
        std::memcpy(reinterpret_cast<std::byte*>(boss) + boss_test_center_offset, desired.data(),
                    sizeof(std::array<float, 3>));
        center_after = read_test_center(boss);
        if (center_after != desired)
        {
            throw std::runtime_error("center_change_not_observed");
        }
        finish(L"completed", L"center_readback");
        return;
    }

} // namespace kh3ap
