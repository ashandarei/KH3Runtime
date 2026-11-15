#include <array>
#include <cassert>
#include <optional>
#include <string>
#include <string_view>
#include "kh3ap/features/party/party_catalog.h"
#include "kh3ap/features/party/party_pair_policy.h"

#define STR(value) L##value
using namespace kh3ap;
struct UObject
{
    bool valid = true;
    UObject* level{};
    UObject* controller{};
    UObject* pawn{};
    UObject* manager{};
    std::wstring name;
    std::optional<std::array<float, 3>> position = std::array<float, 3>{};
    bool input_ignored{}, warp_disabled{}, warp_enabled{};
    int enable_calls{}, warp_requests{};
    UObject* GetClassPrivate()
    {
        return this;
    }
    std::wstring GetName()
    {
        return name;
    }
    std::wstring GetFullName()
    {
        return name;
    }
};
struct Weak
{
    UObject* value{};
    Weak& operator=(UObject* object)
    {
        value = object;
        return *this;
    }
    UObject* Get() const
    {
        return value && value->valid ? value : nullptr;
    }
};
inline RuntimePartyMapPolicy policy = RuntimePartyMapPolicy::managed;
inline std::array<UObject*, runtime_party_member_slot_count> native_slots{};
bool is_uobject_valid_fast(UObject* object)
{
    return object && object->valid;
}
UObject* persistent_level_for(UObject* object)
{
    return is_uobject_valid_fast(object) ? object->level : nullptr;
}
RuntimePartyMapPolicy observed_party_map_policy(UObject*, int)
{
    return policy;
}
UObject* read_object_like_property(UObject* object, std::wstring_view name)
{
    if (!object)
    {
        return nullptr;
    }
    if (name == L"Controller")
    {
        return object->controller;
    }
    if (name == L"Pawn")
    {
        return object->pawn;
    }
    if (name == L"MyFrdMgr")
    {
        return object->manager;
    }
    assert(false);
    return nullptr;
}
std::optional<bool> invoke_named_bool_getter(UObject* object, std::wstring_view name)
{
    if (!object)
    {
        return {};
    }
    if (name == L"IsMoveInputIgnored")
    {
        return object->input_ignored;
    }
    assert(name == L"IsDisableFriendWarpHome");
    return object->warp_disabled;
}
bool invoke_named_single_bool_function(UObject* object, std::wstring_view name, bool value)
{
    if (name == L"SetEnableWarp")
    {
        object->warp_enabled = value;
        ++object->enable_calls;
        return true;
    }
    assert(name == L"BP_ReqFriendWarpHomeAll");
    assert(!value);
    ++object->warp_requests;
    return true;
}
auto actor_position(UObject* object)
{
    return object->position;
}
bool outside_native_follow_distance(const std::array<float, 3>& a, const std::array<float, 3>& b)
{
    float distance = 0;
    for (size_t axis = 0; axis < 3; ++axis)
    {
        distance += (a[axis] - b[axis]) * (a[axis] - b[axis]);
    }
    return distance > 3000.f * 3000.f;
}
bool read_friend_slots(UObject*, decltype(native_slots)& slots)
{
    slots = native_slots;
    return true;
}
enum class LogLevel
{
    Normal
};
struct Output
{
    template <LogLevel, class... Args> static void send(Args&&...)
    {
    }
};
struct Party
{
    struct
    {
        int runtime_state{};
    } config_;
    bool reconcile_pending_{}, ship_control_active_{}, placement_pending_{};
    Weak placement_sora_;
    std::array<Weak, 2> placement_friends_{};
    std::array<std::optional<bool>, 2> placement_battle_modes_{};
    std::array<float, 3> placement_sora_position_{};
    std::array<size_t, 2> active_choices_{19, 0}; // Xion uses stock AI.
    std::array<std::wstring, 2> active_npc_ids_{L"n_ex033", L""};
    UObject level, sora, controller, manager, xion;
    Party()
    {
        sora.level = manager.level = xion.level = &level;
        sora.controller = &controller;
        controller.pawn = &sora;
        sora.manager = &manager;
        xion.name = L"n_ex033_Pawn_C";
        xion.position = {5000, 0, 0};
        native_slots = {&xion, nullptr};
        policy = RuntimePartyMapPolicy::managed;
    }
    UObject* resolve_party_player()
    {
        return &sora;
    }
    UObject* resolve_friend_manager()
    {
        return &manager;
    }
#include "party_placement_under_test.h"
};
int main()
{
    Party p;
    assert(p.maintain_party_placement());
    assert(p.xion.warp_enabled && p.xion.enable_calls == 1 && p.sora.warp_requests == 1);
    assert(p.maintain_party_placement()); // No repeated warp without a new entry/edge.
    assert(p.xion.enable_calls == 1 && p.sora.warp_requests == 1);
    p.placement_pending_ = true;
    p.manager.warp_disabled = true;
    assert(!p.maintain_party_placement() && p.sora.warp_requests == 1);
    p.manager.warp_disabled = false;
    assert(p.maintain_party_placement() && p.sora.warp_requests == 2);
    Party vanilla;
    vanilla.active_choices_[0] = party_catalog::vanilla_choice;
    assert(vanilla.maintain_party_placement() && vanilla.xion.enable_calls == 0 &&
           vanilla.sora.warp_requests == 0);
    Party story;
    policy = RuntimePartyMapPolicy::vanilla;
    assert(story.maintain_party_placement() && story.xion.enable_calls == 0);
    Party retired;
    UObject other_level;
    retired.xion.level = &other_level;
    assert(!retired.maintain_party_placement() && retired.xion.enable_calls == 0);
    Party wrong_class;
    wrong_class.xion.name = L"story_actor";
    assert(!wrong_class.maintain_party_placement() && wrong_class.xion.enable_calls == 0);
    Party loading;
    loading.controller.input_ignored = true;
    assert(!loading.maintain_party_placement() && loading.xion.enable_calls == 0);
    Party near;
    near.xion.position = {100, 0, 0};
    assert(near.maintain_party_placement() && near.xion.warp_enabled &&
           near.sora.warp_requests == 0);
    Party delayed;
    delayed.reconcile_pending_ = true;
    assert(!delayed.maintain_party_placement() && delayed.xion.enable_calls == 0);
    delayed.reconcile_pending_ = false;
    assert(delayed.maintain_party_placement() && delayed.sora.warp_requests == 1);
}
