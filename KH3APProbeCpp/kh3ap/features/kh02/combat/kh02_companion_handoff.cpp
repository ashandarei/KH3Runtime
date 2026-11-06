#include "kh02_companion_handoff.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/features/party/party_helpers.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <array>
#include <vector>

namespace kh3ap
{
    using namespace RC::Unreal;

    namespace
    {
        constexpr auto companion_handoff_timeout = std::chrono::seconds(10);
        constexpr auto companion_retry_interval = std::chrono::milliseconds(100);
        constexpr size_t friend_slot_count = 6;
        constexpr size_t reflected_parameter_limit_bytes = 64;

        enum class FriendOperation
        {
            create,
            retire
        };

        auto mickey(UObject* pawn) -> bool
        {
            return is_uobject_valid_fast(pawn) && pawn->GetClassPrivate()->GetName() == L"n_dw003_Pawn_C";
        }

        auto friend_command(UObject* manager, int slot, FriendOperation operation) -> bool
        {
            if (!party_native::is_friend_component(manager))
            {
                return false;
            }
            auto* function = manager->GetFunctionByNameInChain(operation == FriendOperation::create ? L"SetFriend"
                                                                                                    : L"DeleteFriend");
            if (!is_uobject_valid_fast(function) || function->GetParmsSize() > reflected_parameter_limit_bytes)
            {
                return false;
            }
            std::vector<std::byte> params(function->GetParmsSize());
            if (!write_integral_function_param(find_function_param_by_name(function, L"inIdx"), params.data(), slot))
            {
                return false;
            }
            if (operation == FriendOperation::create &&
                !write_name_or_string_function_param(find_function_param_by_name(function, L"inNpcName"), params.data(),
                                                     L"n_dw003"))
            {
                return false;
            }
            auto* result = find_function_param_by_name(function, L"ReturnValue");
            if (!result || !result->IsA<FBoolProperty>())
            {
                return false;
            }
            manager->ProcessEvent(function, params.data());
            return static_cast<FBoolProperty*>(result)->GetPropertyValueInContainer(params.data());
        }
    } // namespace

    void Kh02CompanionHandoff::prepare(UObject* pawn, std::wstring_view world)
    {
        slot = -1;
        retired = false;
        spawn_dispatched = false;
        if (world != L"World /Game/Maps/APKH02Mirrors/APKH02Depths.APKH02Depths" &&
            world != L"World /Game/Maps/APKH02Mirrors/APKH02DestinyIslands.APKH02DestinyIslands")
        {
            return;
        }
        auto* manager = party_native::read_object_like_property(pawn, L"MyFrdMgr");
        std::array<UObject*, friend_slot_count> friends{};
        if (!party_native::read_friend_slots(manager, friends))
        {
            return;
        }
        for (int index = 0; index < static_cast<int>(friends.size()); ++index)
        {
            if (mickey(friends[index]))
            {
                previous_manager = manager;
                previous_pawn = pawn;
                previous_world = kh02_actor_world(pawn);
                source_world = world;
                slot = index;
                deadline = std::chrono::steady_clock::now() + companion_handoff_timeout;
                retry_at = {};
                phase = "waiting_for_character";
                return;
            }
        }
    }

    void Kh02CompanionHandoff::tick(const std::function<UObject*()>& player, std::wstring_view world)
    {
        const auto now = std::chrono::steady_clock::now();
        if (slot < 0 || now < retry_at)
        {
            return;
        }
        if (world != source_world || now >= deadline)
        {
            slot = -1;
            phase = "canceled_world_changed_or_timeout";
            return;
        }
        retry_at = now + companion_retry_interval;
        auto* pawn = player();
        if (!is_uobject_valid_fast(pawn))
        {
            return;
        }
        if (!previous_world.Get() || kh02_actor_world(pawn) != previous_world.Get())
        {
            slot = -1;
            phase = "canceled_world_instance_changed";
            return;
        }
        auto* controller = party_native::read_object_like_property(pawn, L"Controller");
        auto* manager = party_native::read_object_like_property(pawn, L"MyFrdMgr");
        if (pawn == previous_pawn.Get() || !controller ||
            party_native::read_object_like_property(controller, L"Pawn") != pawn ||
            !party_native::is_friend_component(manager))
        {
            return;
        }
        std::array<UObject*, friend_slot_count> friends{};
        if (!party_native::read_friend_slots(manager, friends))
        {
            return;
        }
        if (!retired)
        {
            auto* old_manager = previous_manager.Get();
            std::array<UObject*, friend_slot_count> old_friends{};
            if (old_manager != manager && party_native::read_friend_slots(old_manager, old_friends) &&
                mickey(old_friends[slot]) && !friend_command(old_manager, slot, FriendOperation::retire))
            {
                phase = "retiring_previous_companion";
                return;
            }
            retired = true;
        }
        for (auto* companion : friends)
        {
            if (mickey(companion) && party_native::read_object_like_property(companion, L"m_pFriendMan") == manager)
            {
                slot = -1;
                phase = "companion_owned_by_active_character";
                return;
            }
        }
        if (friends[slot])
        {
            slot = -1;
            phase = "canceled_slot_reassigned";
            return;
        }
        if (!spawn_dispatched)
        {
            spawn_dispatched = friend_command(manager, slot, FriendOperation::create);
            phase = spawn_dispatched ? "awaiting_companion_slot" : "companion_spawn_rejected";
        }
    }
} // namespace kh3ap
