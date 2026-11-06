#include <array>
#include <chrono>
#include <optional>
#include <utility>
#include "kh3ap/features/enemies/enemy_clear/service_impl.h"
#include "kh3ap/features/enemies/enemy_clear/support.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/core/gfx/gfx_reflection_helpers.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/bridge/serialization/json_fields.h"
#include "kh3ap/core/input/runtime_controller_input.h"
#include "kh3ap/core/objects/actor_discovery.h"
#include "kh3ap/bridge/utilities.h"

#include <Unreal/UObject.hpp>
#include <Unreal/AActor.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/UObjectGlobals.hpp>

#include <algorithm>
#include <stdexcept>
#include <cmath>
#include <cstring>
#include <sstream>

namespace kh3ap
{
    namespace
    {
        constexpr auto photo_friend_slot_count = 6;

        constexpr auto minimum_photo_fov_degrees = 10;
        constexpr auto maximum_photo_fov_degrees = 90;
    } // namespace

    using namespace RC::Unreal;
    using namespace enemy_clear_detail;

    std::vector<UObject*> RuntimeEnemyClearService::Impl::owned_components(UObject* actor,
                                                                           UClass* component_class)
    {
        Call get(actor, L"GetComponentsByClass");
        get.function->InitializeStruct(get.params.data());
        struct Cleanup
        {
            Call& call;
            ~Cleanup()
            {
                call.function->DestroyStruct(call.params.data());
            }
        } cleanup{get};
        auto* requested = get.field(L"ComponentClass");
        if (requested->GetClass().GetName() != L"ClassProperty" ||
            requested->GetElementSize() != sizeof(UClass*))
        {
            throw std::runtime_error("emblem_components_param_layout");
        }
        *requested->ContainerPtrToValuePtr<UClass*>(get.params.data()) = component_class;
        auto* result = get.field(L"ReturnValue");
        struct Array
        {
            UObject** data;
            int32_t count, capacity;
        };
        if (result->GetClass().GetName() != L"ArrayProperty" ||
            result->GetElementSize() != sizeof(Array))
        {
            throw std::runtime_error("emblem_components_array_layout");
        }
        get.invoke();
        const auto* values = result->ContainerPtrToValuePtr<Array>(get.params.data());
        if (values->count < 0 || values->count > max_enemy_inspection_values ||
            values->capacity < values->count || (values->count && !values->data))
        {
            throw std::runtime_error("emblem_components_array_invalid");
        }
        std::vector<UObject*> components;
        for (int32_t i = 0; i < values->count; ++i)
        {
            if (is_uobject_valid_fast(values->data[i]) &&
                values->data[i]->GetOuterPrivate() == actor)
            {
                components.push_back(values->data[i]);
            }
        }
        return components;
    }

    std::array<float, 3> RuntimeEnemyClearService::Impl::component_vector(UObject* component,
                                                                          const wchar_t* name)
    {
        Call query(component, name);
        auto* field = query.field(L"ReturnValue");
        if (field->GetClass().GetName() != L"StructProperty" ||
            field->GetElementSize() != sizeof(std::array<float, 3>))
        {
            throw std::runtime_error("emblem_transform_layout");
        }
        query.invoke();
        std::array<float, 3> result{};
        std::memcpy(result.data(), field->ContainerPtrToValuePtr<void>(query.params.data()),
                    sizeof(std::array<float, 3>));
        for (float v : result)
        {
            if (!std::isfinite(v))
            {
                throw std::runtime_error("emblem_transform_nonfinite");
            }
        }
        return result;
    }

    std::array<UObject*, photo_friend_slot_count>
    RuntimeEnemyClearService::Impl::photo_friend_slots(UObject* pawn, UObject** manager_out)
    {
        auto* manager = try_read_reflected_object_property_value(pawn, L"MyFrdMgr");
        if (!is_uobject_valid_fast(manager) ||
            manager->GetClassPrivate()->GetName() != L"TresFriendComponent" ||
            manager->GetOuterPrivate() != pawn)
        {
            throw std::runtime_error("photo_friend_manager_identity");
        }
        auto* property = manager->GetPropertyByNameInChain(L"m_pFriendPawn");
        if (!property || property->GetClass().GetName() != L"WeakObjectProperty" ||
            property->GetArrayDim() != photo_friend_slot_count ||
            property->GetElementSize() != sizeof(FWeakObjectPtr))
        {
            throw std::runtime_error("photo_friend_slots_layout");
        }
        auto* values = property->ContainerPtrToValuePtr<FWeakObjectPtr>(manager);
        std::array<UObject*, photo_friend_slot_count> result{};
        for (size_t i = 0; i < result.size(); ++i)
        {
            auto* candidate = values[i].Get();
            if (is_uobject_valid_fast(candidate))
            {
                result[i] = candidate;
            }
        }
        if (manager_out)
        {
            *manager_out = manager;
        }
        return result;
    }

    void RuntimeEnemyClearService::Impl::inspect_emblems(UObject* pawn)
    {
        inspect_camera(pawn);
        auto* component_class = UObjectGlobals::StaticFindObject<UClass*>(
            nullptr, nullptr, L"/Script/TresGame.TresHiddenMickeyComponent");
        if (!is_uobject_valid_fast(component_class))
        {
            throw std::runtime_error("emblem_component_class_missing");
        }
        auto* box_class = UObjectGlobals::StaticFindObject<UClass*>(
            nullptr, nullptr, L"/Script/TresGame.TresDetectMarkerValidBoxComponent");
        if (!is_uobject_valid_fast(box_class))
        {
            throw std::runtime_error("emblem_viewing_box_class_missing");
        }
        std::ostringstream out;
        out << std::boolalpha << '[';
        bool first = true;
        for (const auto* path :
             {L"/Game/Blueprints/Gimmick/ex/g_ex_luckymark_01.g_ex_luckymark_01_C",
              L"/Game/Blueprints/Gimmick/ex/g_ex_luckymark_02.g_ex_luckymark_02_C",
              L"/Game/Blueprints/Gimmick/ex/g_ex_luckymark_03.g_ex_luckymark_03_C"})
        {
            auto* klass = find_loaded_runtime_asset_class(path);
            if (!is_uobject_valid_fast(klass))
            {
                continue;
            }
            for (auto* actor : find_live_actors_of_class(pawn, klass, 256))
            {
                if (!is_uobject_valid_fast(actor))
                {
                    continue;
                }
                std::ostringstream areas;
                areas << '[';
                bool first_area = true;
                for (auto* box : owned_components(actor, box_class))
                {
                    if (!first_area)
                    {
                        areas << ',';
                    }
                    first_area = false;
                    areas << "{\"component\":\"" << json_escape(box->GetPathName())
                          << "\",\"position\":";
                    json_position(areas, component_vector(box, L"K2_GetComponentLocation"));
                    areas << '}';
                }
                areas << ']';
                for (auto* component : owned_components(actor, component_class))
                {
                    if (!is_uobject_valid_fast(component))
                    {
                        continue;
                    }
                    const auto id = property_name(component, L"m_MarkID");
                    if (id.empty() || id == L"None")
                    {
                        continue;
                    }
                    const auto pos = component_vector(component, L"K2_GetComponentLocation");
                    const auto rot = component_vector(component, L"K2_GetComponentRotation");
                    if (!first)
                    {
                        out << ',';
                    }
                    first = false;
                    out << "{\"actor\":\"" << json_escape(actor->GetPathName())
                        << "\",\"component\":\"" << json_escape(component->GetPathName())
                        << "\",\"native_id\":\"" << json_escape(id)
                        << "\",\"recorded\":" << boolean(component, L"IsLuckyMarkRecorded")
                        << ",\"detect_valid\":" << boolean(component, L"IsDetectValid")
                        << ",\"position\":";
                    json_position(out, pos);
                    out << ",\"rotation\":";
                    json_position(out, rot);
                    out << ",\"owner_hidden\":" << property_bool(actor, L"bHidden", false)
                        << ",\"viewing_areas\":" << areas.str() << '}';
                }
            }
        }
        out << ']';
        emblems = out.str();
    }

    UObject* RuntimeEnemyClearService::Impl::held_camera(UObject* pawn)
    {
        auto* queue = try_read_reflected_object_property_value(pawn, L"MyStateComp");
        auto* state = try_read_reflected_object_property_value(queue, L"m_pCurrentState");
        if (!is_uobject_valid_fast(state) ||
            state->GetClassPrivate()->GetName() != L"TresPlayerState_HoldCamera")
        {
            return nullptr;
        }
        auto* camera = try_read_reflected_object_property_value(state, L"m_pCamera");
        return is_uobject_valid_fast(camera) && actor_world(camera) == actor_world(pawn) ? camera
                                                                                         : nullptr;
    }

    void RuntimeEnemyClearService::Impl::detach_photo_attachment()
    {
        auto* pawn = photo_pawn.Get();
        auto* parent = photo_attachment.Get();
        if (is_uobject_valid_fast(pawn) && is_uobject_valid_fast(parent))
        {
            auto* root = try_read_reflected_object_property_value(pawn, L"RootComponent");
            if (try_read_reflected_object_property_value(root, L"AttachParent") == parent)
            {
                Call detach(pawn, L"K2_DetachFromActor");
                for (const auto* rule : {L"LocationRule", L"RotationRule", L"ScaleRule"})
                {
                    if (!write_integral_function_param(detach.field(rule), detach.params.data(), 1))
                    {
                        throw std::runtime_error("photo_detach_rule_layout");
                    }
                }
                detach.invoke();
                if (try_read_reflected_object_property_value(root, L"AttachParent") == parent)
                {
                    throw std::runtime_error("photo_detach_not_observed");
                }
            }
        }
        photo_attachment = nullptr;
    }

    void RuntimeEnemyClearService::Impl::attach_photo_marker(UObject* pawn, std::wstring_view path)
    {
        auto* klass = UObjectGlobals::StaticFindObject<UClass*>(
            nullptr, nullptr, L"/Script/TresGame.TresHiddenMickeyComponent");
        if (!is_uobject_valid_fast(klass))
        {
            throw std::runtime_error("photo_follow_component_class_missing");
        }
        UObject* marker = nullptr;
        UObject* marker_owner = nullptr;
        // A streamed package can also retain a template world with the
        // same object path. Resolve from current-world actors, as inspect
        // does, rather than trusting a global full-path lookup.
        for (const auto* asset :
             {L"/Game/Blueprints/Gimmick/ex/g_ex_luckymark_01.g_ex_luckymark_01_C",
              L"/Game/Blueprints/Gimmick/ex/g_ex_luckymark_02.g_ex_luckymark_02_C",
              L"/Game/Blueprints/Gimmick/ex/g_ex_luckymark_03.g_ex_luckymark_03_C"})
        {
            auto* actor_class = find_loaded_runtime_asset_class(asset);
            if (!is_uobject_valid_fast(actor_class))
            {
                continue;
            }
            for (auto* actor : find_live_actors_of_class(pawn, actor_class, 256))
            {
                for (auto* candidate : owned_components(actor, klass))
                {
                    if (candidate->GetPathName() == path)
                    {
                        if (marker)
                        {
                            throw std::runtime_error("ambiguous_photo_follow_marker");
                        }
                        marker = candidate;
                        marker_owner = actor;
                    }
                }
            }
        }
        // Streamed levels have their own outer UWorld; OwningWorld is the
        // authoritative live world shared with the persistent-level pawn.
        auto* marker_level =
            is_uobject_valid_fast(marker_owner) ? marker_owner->GetOuterPrivate() : nullptr;
        auto* pawn_level = pawn->GetOuterPrivate();
        auto* marker_world = try_read_reflected_object_property_value(marker_level, L"OwningWorld");
        auto* pawn_world = try_read_reflected_object_property_value(pawn_level, L"OwningWorld");
        if (!is_uobject_a_fast(marker, klass) || marker->GetPathName() != path ||
            marker->GetOuterPrivate() != marker_owner || !is_uobject_valid_fast(marker_world) ||
            marker_world != pawn_world)
        {
            throw std::runtime_error("photo_follow_marker_identity");
        }
        const auto id = property_name(marker, L"m_MarkID");
        if (id != L"80" && id != L"31")
        {
            throw std::runtime_error("photo_follow_requires_tram_or_rowboat_marker");
        }
        if (photo_attachment.Get() == marker)
        {
            return;
        }
        detach_photo_attachment();
        auto* root = try_read_reflected_object_property_value(pawn, L"RootComponent");
        if (!is_uobject_valid_fast(root) ||
            try_read_reflected_object_property_value(root, L"AttachParent"))
        {
            throw std::runtime_error("photo_follow_requires_unattached_sora");
        }
        Call attach(pawn, L"K2_AttachToComponent");
        if (!write_object_function_param(attach.function, attach.params.data(), L"Parent",
                                         marker) ||
            !write_name_or_string_function_param(attach.field(L"SocketName"), attach.params.data(),
                                                 L"None") ||
            !write_bool_function_param(attach.field(L"bWeldSimulatedBodies"), attach.params.data(),
                                       false))
        {
            throw std::runtime_error("photo_attach_parameter_layout");
        }
        for (const auto* rule : {L"LocationRule", L"RotationRule", L"ScaleRule"})
        {
            if (!write_integral_function_param(attach.field(rule), attach.params.data(), 1))
            {
                throw std::runtime_error("photo_attach_rule_layout");
            }
        }
        attach.invoke();
        if (try_read_reflected_object_property_value(root, L"AttachParent") != marker)
        {
            throw std::runtime_error("photo_attachment_not_observed");
        }
        photo_attachment = marker;
    }

    void RuntimeEnemyClearService::Impl::restore_photo_movement()
    {
        auto* lens = photo_fov_component.Get();
        photo_fov_component = nullptr;
        if (is_uobject_valid_fast(lens))
        {
            Call reset(lens, L"SetFieldOfView");
            if (!write_float_function_param(reset.field(L"InFieldOfView"), reset.params.data(),
                                            photo_original_fov))
            {
                throw std::runtime_error("photo_fov_restore_layout");
            }
            reset.invoke();
        }
        detach_photo_attachment();
        auto* movement = photo_movement.Get();
        photo_movement = nullptr;
        photo_pawn = nullptr;
        if (!is_uobject_valid_fast(movement))
        {
            return;
        }
        Call restore(movement, L"SetComponentTickEnabled");
        if (!write_bool_function_param(restore.field(L"bEnabled"), restore.params.data(),
                                       photo_tick_enabled))
        {
            throw std::runtime_error("photo_movement_restore_layout");
        }
        restore.invoke();
        if (boolean(movement, L"IsComponentTickEnabled") != photo_tick_enabled)
        {
            throw std::runtime_error("photo_movement_restore_readback_failed");
        }
    }

    void RuntimeEnemyClearService::Impl::inspect_camera(UObject* pawn)
    {
        auto* controller = try_read_reflected_object_property_value(pawn, L"Controller");
        auto* manager =
            try_read_reflected_object_property_value(controller, L"PlayerCameraManager");
        if (!is_uobject_valid_fast(manager))
        {
            throw std::runtime_error("camera_manager_unavailable");
        }
        std::array<float, 3> pos{}, rot{};
        for (auto [name, dest] :
             {std::pair{L"GetCameraLocation", &pos}, std::pair{L"GetCameraRotation", &rot}})
        {
            Call query(manager, name);
            auto* p = query.field(L"ReturnValue");
            if (p->GetClass().GetName() != L"StructProperty" ||
                p->GetElementSize() != sizeof(std::array<float, 3>))
            {
                throw std::runtime_error("camera_snapshot_layout");
            }
            query.invoke();
            std::memcpy(dest->data(), p->ContainerPtrToValuePtr<void>(query.params.data()),
                        sizeof(std::array<float, 3>));
        }
        auto* queue = try_read_reflected_object_property_value(pawn, L"MyStateComp");
        auto* state = try_read_reflected_object_property_value(queue, L"m_pCurrentState");
        std::wstring target, photo_target;
        if (held_camera(pawn))
        {
            auto* p = state->GetPropertyByNameInChain(L"m_pCurrentLuckyMarker");
            if (p && p->GetClass().GetName() == L"WeakObjectProperty" &&
                p->GetElementSize() == sizeof(FWeakObjectPtr))
            {
                auto* mark = p->ContainerPtrToValuePtr<FWeakObjectPtr>(state)->Get();
                if (is_uobject_valid_fast(mark))
                {
                    target = property_name(mark, L"m_MarkID");
                }
            }
            p = state->GetPropertyByNameInChain(L"m_pTargetLuckyMarker");
            if (p && p->GetClass().GetName() == L"WeakObjectProperty" &&
                p->GetElementSize() == sizeof(FWeakObjectPtr))
            {
                auto* mark = p->ContainerPtrToValuePtr<FWeakObjectPtr>(state)->Get();
                if (is_uobject_valid_fast(mark))
                {
                    photo_target = property_name(mark, L"m_MarkID");
                }
            }
        }
        std::ostringstream out;
        out << std::boolalpha << "{\"hold_active\":" << (held_camera(pawn) != nullptr)
            << ",\"state\":\""
            << json_escape(is_uobject_valid_fast(state) ? state->GetClassPrivate()->GetName() : L"")
            << "\",\"target_native_id\":\"" << json_escape(target)
            << "\",\"shot_target_native_id\":\"" << json_escape(photo_target) << "\",\"position\":";
        json_position(out, pos);
        out << ",\"rotation\":";
        json_position(out, rot);
        Call fov_query(manager, L"GetFOVAngle");
        auto* fov_field = fov_query.field(L"ReturnValue");
        if (fov_field->GetClass().GetName() != L"FloatProperty" ||
            fov_field->GetElementSize() != sizeof(float))
        {
            throw std::runtime_error("camera_fov_layout");
        }
        fov_query.invoke();
        const auto fov = *fov_field->ContainerPtrToValuePtr<float>(fov_query.params.data());
        if (!std::isfinite(fov) || fov <= 0 || fov >= maximum_camera_fov_degrees)
        {
            throw std::runtime_error("camera_fov_invalid");
        }
        out << ",\"fov_degrees\":" << fov;
        // Read the native counter only on an explicit inspection. Taking a
        // photograph remains the game's normal shutter/save transaction.
        constexpr GfxProbeFunctionPaths photo_gfx{L"/Script/ScaleformUI.GFxObject:GetMemberValue",
                                                  L"/Script/ScaleformUI.GFxObject:Invoke",
                                                  L"/Script/ScaleformUI.GFxObject:SetMemberValue"};
        auto* instance =
            try_read_reflected_object_property_value(kh02_current_viewport(), L"GameInstance");
        auto* ui = try_read_reflected_object_property_value(instance, L"m_UIManager");
        auto* hud = try_read_reflected_object_property_value(ui, L"m_HudCamera");
        auto* count_text =
            try_read_reflected_object_property_value(hud, L"m_gfx_txtCamShootSetNum");
        const auto photo_count =
            held_camera(pawn) ? try_gfx_get_member_value_string(photo_gfx, count_text, L"text")
                              : std::nullopt;
        out << ",\"photo_count_text\":\"" << json_escape(photo_count.value_or(L"")) << '"';
        auto* root = try_read_reflected_object_property_value(pawn, L"RootComponent");
        auto* parent = try_read_reflected_object_property_value(root, L"AttachParent");
        out << ",\"attachment_component\":\""
            << json_escape(is_uobject_valid_fast(parent) ? parent->GetPathName() : L"")
            << "\",\"party_pawns\":[";
        const auto friends = photo_friend_slots(pawn);
        for (size_t i = 0; i < friends.size(); ++i)
        {
            if (i)
            {
                out << ',';
            }
            out << '"' << json_escape(friends[i] ? friends[i]->GetPathName() : L"") << '"';
        }
        out << "]}";
        camera_snapshot = out.str();
    }

    bool RuntimeEnemyClearService::Impl::update_camera_input(Clock::time_point now)
    {
        if (!(camera_look))
        {
            return false;
        }
        auto* pawn = sora.Get();
        auto* controller = look_controller.Get();
        const bool valid =
            is_uobject_valid_fast(pawn) && is_uobject_valid_fast(controller) &&
            actor_world(pawn) == world.Get() &&
            try_read_reflected_object_property_value(pawn, L"Controller") == controller;
        const bool done =
            now >= look_until || unix_ms() > expires || !valid ||
            (command == L"look_camera" ? !held_camera(pawn)
                                       : (!gameplay(pawn) || held_camera(pawn) != nullptr));
        if (done)
        {
            camera_look = false;
            look_controller = nullptr;
            if (is_uobject_valid_fast(controller))
            {
                if (command == L"move_gameplay")
                {
                    runtime_controller_stick_input(controller, 0, 0);
                }
                else
                {
                    runtime_controller_look_input(controller, 0, 0);
                }
            }
            if (!valid)
            {
                finish(L"error", L"camera_axis_identity_changed");
                return true;
            }
            inspect_camera(pawn);
            finish(L"completed", L"camera_axis_released");
            return true;
        }
        if (command == L"move_gameplay")
        {
            runtime_controller_stick_input(controller, look_x, look_y);
        }
        else
        {
            runtime_controller_look_input(controller, look_x, look_y);
        }
        return true;
    }

    void RuntimeEnemyClearService::Impl::accept_photo_fov(UObject* pawn, const std::string& text)
    {
        auto* camera = held_camera(pawn);
        const auto value = JsonFieldReader::number_field(text, "fov");
        if (!camera || photo_pawn.Get() != pawn || !photo_movement.Get() || !value ||
            !std::isfinite(*value) || *value < minimum_photo_fov_degrees ||
            *value > maximum_photo_fov_degrees)
        {
            throw std::runtime_error("photo_fov_requires_owned_camera_hold");
        }
        Call get(camera, L"GetCameraComponent");
        get.invoke();
        auto* lens =
            try_read_function_object_param(get.function, get.params.data(), L"ReturnValue");
        if (!is_uobject_valid_fast(lens) || lens->GetOuterPrivate() != camera)
        {
            throw std::runtime_error("photo_fov_component_ownership");
        }
        auto* field = lens->GetPropertyByNameInChain(L"FieldOfView");
        if (!field || field->GetClass().GetName() != L"FloatProperty" ||
            field->GetElementSize() != sizeof(float))
        {
            throw std::runtime_error("photo_fov_component_layout");
        }
        if (photo_fov_component.Get() && photo_fov_component.Get() != lens)
        {
            throw std::runtime_error("photo_fov_component_changed");
        }
        if (!photo_fov_component.Get())
        {
            photo_original_fov = *field->ContainerPtrToValuePtr<float>(lens);
            if (!std::isfinite(photo_original_fov) || photo_original_fov <= 0 ||
                photo_original_fov >= maximum_camera_fov_degrees)
            {
                throw std::runtime_error("photo_original_fov_invalid");
            }
            photo_fov_component = lens;
        }
        Call set(lens, L"SetFieldOfView");
        if (!write_float_function_param(set.field(L"InFieldOfView"), set.params.data(), *value))
        {
            throw std::runtime_error("photo_fov_set_layout");
        }
        set.invoke();
        inspect_camera(pawn);
        finish(L"completed", L"photo_component_fov_dispatched");
        return;
    }

    void RuntimeEnemyClearService::Impl::accept_photo_party(UObject* pawn, const std::string& text)
    {
        if (!held_camera(pawn) || photo_pawn.Get() != pawn || !photo_movement.Get())
        {
            throw std::runtime_error("photo_party_requires_owned_camera_hold");
        }
        UObject* manager = nullptr;
        const auto before = photo_friend_slots(pawn, &manager);
        report(L"accepted", L"photo_party_clear_checkpoint");
        // Clear descending indices so native slot compaction cannot
        // leave a world ally behind. These are ordinary DeleteFriend
        // calls; map entry still owns its subsequent vanilla roster.
        for (int i = static_cast<int>(before.size()) - 1; i >= 0; --i)
        {
            if (before[i])
            {
                Call remove(manager, L"DeleteFriend");
                if (!write_integral_function_param(remove.field(L"inIdx"), remove.params.data(), i))
                {
                    throw std::runtime_error("photo_party_delete_layout");
                }
                remove.invoke();
            }
        }
        const auto after = photo_friend_slots(pawn);
        if (std::any_of(after.begin(), after.end(),
                        [](auto* value)
                        {
                            return value != nullptr;
                        }))
        {
            throw std::runtime_error("photo_party_slots_not_empty");
        }
        inspect_camera(pawn);
        finish(L"completed", L"all_six_photo_party_slots_empty");
        return;
    }

    void RuntimeEnemyClearService::Impl::accept_photo_position(UObject* pawn,
                                                               const std::string& text)
    {
        auto enabled = JsonFieldReader::bool_field(text, "enabled");
        if (!enabled)
        {
            throw std::runtime_error("photo_position_enabled_required");
        }
        if (!*enabled)
        {
            restore_photo_movement();
            finish(L"completed", L"photo_movement_restored");
            return;
        }
        if (!held_camera(pawn))
        {
            throw std::runtime_error("photo_position_requires_real_camera");
        }
        auto* movement = try_read_reflected_object_property_value(pawn, L"MyMovement");
        if (!is_uobject_valid_fast(movement))
        {
            throw std::runtime_error("photo_movement_missing");
        }
        if (!photo_movement.Get())
        {
            photo_tick_enabled = boolean(movement, L"IsComponentTickEnabled");
            photo_movement = movement;
            photo_pawn = pawn;
        }
        if (photo_movement.Get() != movement || photo_pawn.Get() != pawn)
        {
            throw std::runtime_error("photo_movement_identity_changed");
        }
        photo_hold_until = Clock::now() + photo_hold_timeout;
        Call stop(movement, L"StopMovementImmediately");
        stop.invoke();
        Call disable(movement, L"SetComponentTickEnabled");
        if (!write_bool_function_param(disable.field(L"bEnabled"), disable.params.data(), false))
        {
            throw std::runtime_error("photo_movement_tick_layout");
        }
        disable.invoke();
        if (boolean(movement, L"IsComponentTickEnabled"))
        {
            throw std::runtime_error("photo_movement_not_disabled");
        }
        const auto follow = JsonFieldReader::string_field(text, "follow_component").value_or(L"");
        if (follow.empty())
        {
            detach_photo_attachment();
        }
        else
        {
            attach_photo_marker(pawn, follow);
        }
        finish(L"completed", L"photo_movement_disabled_readback");
        return;
    }

    void RuntimeEnemyClearService::Impl::accept_camera_look(UObject* pawn, const std::string& text)
    {
        auto x = JsonFieldReader::number_field(text, "x");
        auto y = JsonFieldReader::number_field(text, "y");
        auto ms = JsonFieldReader::number_field(text, "duration_ms");
        if (!x || !y || !ms || !std::isfinite(*x) || !std::isfinite(*y) || !std::isfinite(*ms) ||
            std::abs(*x) > 1 || std::abs(*y) > 1 || *ms < minimum_stick_hold_ms ||
            *ms > maximum_stick_hold_ms ||
            (command == L"look_camera" ? !held_camera(pawn) : held_camera(pawn) != nullptr))
        {
            throw std::runtime_error("hold_camera_axis_request_invalid");
        }
        look_x = static_cast<float>(*x);
        look_y = static_cast<float>(*y);
        look_controller = try_read_reflected_object_property_value(pawn, L"Controller");
        look_until = Clock::now() + std::chrono::milliseconds(static_cast<int>(*ms));
        camera_look = true;
        waiting = true;
        report(L"accepted", L"camera_axis_checkpoint");
        return;
    }

    void RuntimeEnemyClearService::Impl::accept_inspect_emblems(UObject* pawn,
                                                                const std::string& text)
    {
        inspect_emblems(pawn);
        finish(L"completed", L"emblem_snapshot");
        return;
    }

} // namespace kh3ap
