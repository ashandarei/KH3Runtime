#include "kh3ap/features/enemies/enemy_clear/service_impl.h"
#include "kh3ap/features/enemies/enemy_clear/support.h"
#include "kh3ap/features/player/player_state_ids.h"
#include "kh3ap/features/player/movement/character_rails.h"
#include "kh3ap/core/gfx/gfx_reflection_helpers.h"
#include "kh3ap/core/objects/actor_owning_world.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/bridge/utilities.h"
#include "kh3ap/bridge/serialization/json_fields.h"

#include <Unreal/AActor.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>

namespace kh3ap
{
    using namespace RC::Unreal;
    using namespace enemy_clear_detail;

    namespace
    {
        constexpr GfxProbeFunctionPaths gfx_paths{L"/Script/ScaleformUI.GFxObject:GetMemberValue",
                                                 L"/Script/ScaleformUI.GFxObject:Invoke",
                                                 L"/Script/ScaleformUI.GFxObject:SetMemberValue"};
        using Point = std::array<float, 3>;

        UObject* cover_hud()
        {
            std::vector<UObject*> objects;
            UObjectGlobals::FindAllOf(L"TresUIP_HudCommand", objects);
            UObject* found = nullptr;
            for (auto* hud : objects)
            {
                if (!is_uobject_valid_fast(hud) || !is_uobject_valid_fast(
                        try_read_reflected_object_property_value(hud, L"m_pGFxRoot")) ||
                    !is_uobject_valid_fast(try_read_reflected_object_property_value(hud, L"m_pUIManager")))
                {
                    continue;
                }
                if (found)
                {
                    return nullptr;
                }
                found = hud;
            }
            return found;
        }

        std::wstring visible_text(UObject* object)
        {
            if (!is_uobject_valid_fast(object) ||
                try_gfx_get_member_value(gfx_paths, object, L"visible") != L"ASType=2, ASBoolean=true")
            {
                return {};
            }
            return try_gfx_get_member_value_string(gfx_paths, object, L"text").value_or(L"");
        }

        UObject* hud_slot(UObject* hud, const wchar_t* name, int index)
        {
            auto* property = hud->GetPropertyByNameInChain(name);
            if (!property || property->GetClass().GetName() != L"ObjectProperty" ||
                property->GetElementSize() != sizeof(UObject*) || index >= property->GetArrayDim())
            {
                return nullptr;
            }
            return *property->ContainerPtrToValuePtr<UObject*>(hud, index);
        }

        Point vector_result(Call& call, const wchar_t* name)
        {
            auto* property = call.field(name);
            Point result{};
            if (property->GetClass().GetName() != L"StructProperty" ||
                property->GetElementSize() != sizeof(result))
            {
                throw std::runtime_error("cover_bounds_layout");
            }
            std::memcpy(result.data(), property->ContainerPtrToValuePtr<void>(call.params.data()), sizeof(result));
            return result;
        }

        struct CoverBounds
        {
            std::wstring path;
            Point center;
            Point extent;
            double distance;
        };

        void add_cover(std::vector<CoverBounds>& entries, UObject* object, const Point& center,
                       const Point& extent, const Point& origin)
        {
            double distance = 0;
            for (size_t axis = 0; axis < center.size(); ++axis)
            {
                if (!std::isfinite(center[axis]) || !std::isfinite(extent[axis]) || extent[axis] <= 0)
                {
                    return;
                }
                distance += std::pow(std::max(0.0f, std::abs(center[axis] - origin[axis]) - extent[axis]), 2);
            }
            if (distance <= 4000.0 * 4000.0)
            {
                entries.push_back({object->GetPathName(), center, extent, std::sqrt(distance)});
            }
        }

        std::vector<CoverBounds> nearby_cover(UObject* pawn, const Point& origin)
        {
            std::vector<CoverBounds> entries;
            auto* current_world = actor_owning_world(pawn);
            auto* volume_class = UObjectGlobals::StaticFindObject<UClass*>(
                nullptr, nullptr, L"/Script/TresGame.TresCoverActionVolume");
            if (is_uobject_valid_fast(volume_class))
            {
                for (auto* volume : actors_of_class(pawn, volume_class))
                {
                    if (!is_uobject_valid_fast(volume) || !boolean(volume, L"GetActorEnableCollision"))
                    {
                        continue;
                    }
                    Call bounds(volume, L"GetActorBounds");
                    if (!write_bool_function_param(bounds.field(L"bOnlyCollidingComponents"), bounds.params.data(), true))
                    {
                        throw std::runtime_error("cover_volume_bounds_layout");
                    }
                    bounds.invoke();
                    add_cover(entries, volume, vector_result(bounds, L"Origin"), vector_result(bounds, L"BoxExtent"), origin);
                }
            }
            auto* kismet = UObjectGlobals::StaticFindObject<UObject*>(
                nullptr, nullptr, L"/Script/Engine.Default__KismetSystemLibrary");
            std::vector<UObject*> components;
            UObjectGlobals::FindAllOf(L"TresCoverActionComponent", components);
            for (auto* component : components)
            {
                if (!is_uobject_valid_fast(component) || !property_bool(component, L"m_bIsGimmickActive", false))
                {
                    continue;
                }
                Call owner_call(component, L"GetOwner");
                owner_call.invoke();
                auto* owner = try_read_function_object_param(owner_call.function, owner_call.params.data(), L"ReturnValue");
                if (!is_uobject_valid_fast(owner) || actor_owning_world(owner) != current_world ||
                    !boolean(owner, L"GetActorEnableCollision"))
                {
                    continue;
                }
                Call bounds(kismet, L"GetComponentBounds");
                if (!write_object_function_param(bounds.function, bounds.params.data(), L"Component", component))
                {
                    throw std::runtime_error("cover_component_bounds_layout");
                }
                bounds.invoke();
                add_cover(entries, component, vector_result(bounds, L"Origin"), vector_result(bounds, L"BoxExtent"), origin);
            }
            std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return a.distance < b.distance; });
            if (entries.size() > 24)
            {
                entries.resize(24);
            }
            return entries;
        }
    }

    bool RuntimeEnemyClearService::Impl::inspect_cover_state(UObject* pawn, std::wstring_view expected_prompt,
                                                            std::wstring_view expected_enemy)
    {
        in_native_cover = integer(pawn, L"GetStateID") == player_state_ids::cover;
        auto* controller = try_read_reflected_object_property_value(pawn, L"Controller");
        auto* queue = try_read_reflected_object_property_value(pawn, L"MyStateComp");
        auto* state = try_read_reflected_object_property_value(queue, L"m_pCurrentState");
        auto* target = in_native_cover ? try_read_reflected_object_property_value(state, L"m_pTargetComponent") : nullptr;
        auto* hud = cover_hud();
        std::wstring event_label;
        std::wstring action_label;
        if (is_uobject_valid_fast(hud))
        {
            event_label = visible_text(hud_slot(hud, L"m_pGFxActEvTopText", 0));
            action_label = visible_text(hud_slot(hud, L"m_pGFxActionCmdText", 0));
        }
        const auto& label = event_label.empty() ? action_label : event_label;
        const auto* prompt = in_native_cover && (is_uobject_valid_fast(target) || label == L"Cover Shot") ? L"cover_shot" :
                             !in_native_cover && (label == L"Take Cover" || label == L"Cover Slide") ? L"take_cover" : L"";
        const auto target_path = is_uobject_valid_fast(target) ? target->GetPathName() : L"";
        std::ostringstream out;
        out << std::boolalpha << "{\"in_cover\":" << in_native_cover << ",\"prompt\":\"" << json_escape(prompt)
            << "\",\"event_label\":\"" << json_escape(event_label) << "\",\"action_label\":\""
            << json_escape(action_label) << "\",\"target\":\"" << json_escape(target_path)
            << "\",\"playing_command\":" << integer(controller, L"BP_GetPlayActionCommand");
        if (is_uobject_valid_fast(target))
        {
            Call location(target, L"K2_GetComponentLocation");
            location.invoke();
            out << ",\"target_position\":";
            json_position(out, vector_result(location, L"ReturnValue"));
        }
        out << '}';
        cover_snapshot = out.str();
        return expected_prompt == prompt &&
               (expected_enemy.empty() || target_path.starts_with(std::wstring(expected_enemy) + L'.'));
    }

    void RuntimeEnemyClearService::Impl::accept_inspect_cover(UObject* pawn, const std::string& text)
    {
        inspect_cover_state(pawn);
        if (JsonFieldReader::bool_field(text, "observe_shot").value_or(false))
        {
            auto* type = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, character_rails::cover_shot_projectile_path);
            const auto shots = is_uobject_valid_fast(type) ? actors_of_class(pawn, type) : std::vector<AActor*>{};
            std::ostringstream out;
            out << '[';
            bool first = true;
            for (auto* shot : shots)
            {
                if (!is_uobject_valid_fast(shot))
                {
                    continue;
                }
                if (!first)
                {
                    out << ',';
                }
                first = false;
                out << '"' << json_escape(shot->GetPathName()) << '"';
            }
            out << ']';
            cover_snapshot.pop_back();
            cover_snapshot += ",\"projectiles\":" + out.str() + '}';
        }
        if (JsonFieldReader::bool_field(text, "locate").value_or(false))
        {
            const auto entries = nearby_cover(pawn, position_before);
            std::ostringstream out;
            out << '[';
            for (size_t index = 0; index < entries.size(); ++index)
            {
                const auto& entry = entries[index];
                if (index)
                {
                    out << ',';
                }
                out << "{\"path\":\"" << json_escape(entry.path) << "\",\"distance\":" << entry.distance << ",\"center\":";
                json_position(out, entry.center);
                out << ",\"extent\":";
                json_position(out, entry.extent);
                out << '}';
            }
            out << ']';
            cover_snapshot.pop_back();
            cover_snapshot += ",\"nearby\":" + out.str() + '}';
        }
        finish(L"completed", L"cover_snapshot_observed");
    }
}
