#include "kh3ap/features/party/party_map_policy.h"
#include <cassert>
#include <chrono>
#include <cstdint>
#include <map>
#include <string>

struct UObject
{
    bool live = true;
    UObject* outer{};
    UObject* type{};
    UObject* level{};
    std::wstring name;
    std::map<std::wstring, UObject*> fields;
    UObject* GetOuterPrivate() { return outer; }
    UObject* GetClassPrivate() { return type; }
    const std::wstring& GetName() { return name; }
    const std::wstring& GetFullName() { return name; }
};
using UClass = UObject;
bool is_uobject_valid_fast(UObject* value) { return value && value->live; }
UObject* read_object_like_property(UObject* value, const wchar_t* name)
{
    if (!is_uobject_valid_fast(value)) { return nullptr; }
    const auto found = value->fields.find(name);
    return found == value->fields.end() ? nullptr : found->second;
}
UObject* persistent_level_for(UObject* pawn) { return pawn->level; }
struct Weak
{
    UObject* value{};
    UObject* Get() const { return is_uobject_valid_fast(value) ? value : nullptr; }
    void operator=(UObject* pawn) { value = pawn; }
};
enum class LogLevel { Normal };
struct Output { template<LogLevel, class... T> static void send(T&&...) {} };
#define STR(value) L##value
using kh3ap::runtime_party_is_ship_control_state;

struct PartyService
{
    bool installed_ = true, active_ = true, ship_control_active_ = false;
    Weak ship_control_sora_;
    UObject* controlled{};
    int reconciles = 0, friend_events = 0;
    std::wstring reason;
    struct Maintenance { void request() {} } maintenance_;
    std::chrono::steady_clock::time_point next_reconcile_attempt_{};
    UObject* resolve_party_player() { return controlled; }
    void request_reconcile(const wchar_t* why, bool reroll)
    {
        assert(!reroll); // Boarding preserves the saved/randomized selection.
        ++reconciles;
        reason = why;
    }
    void observe_friend_state(uintptr_t) { ++friend_events; }
    #include "party_ship_events_under_test.h"
};

int main()
{
    UObject level, ship_type, cinematic_type, idle_type;
    level.name = L"Level /Game/Levels/ca/ca_02/ca_02.ca_02:PersistentLevel";
    ship_type.name = L"TresPlayerState_SpecialShip";
    cinematic_type.name = L"TresCharState_Cinematic";
    idle_type.name = L"TresPlayerState_Idle";
    // No cached Sora participates. Each controlled character's component is
    // enough to restore its party after boarding, including ordinary Sora.
    for (const auto* name : {L"Kairi", L"Aqua", L"Riku", L"Roxas", L"Sora"})
    {
        UObject pawn, component, state, retired, retired_component;
        pawn.name = name;
        pawn.level = &level;
        pawn.fields[L"MyStateComp"] = &component;
        component.outer = &pawn;
        component.fields[L"m_pCurrentState"] = &state;
        state.fields[L"MyCharPawn"] = &pawn;
        state.type = &ship_type;
        retired_component.outer = &retired;
        PartyService service;
        service.controlled = &pawn;
        const auto event = reinterpret_cast<uintptr_t>(&component);
        service.observe_state_write(event);
        assert(service.ship_control_active_ && service.reconciles == 1);

        state.type = &cinematic_type;
        service.observe_state_write(event);
        assert(service.ship_control_active_ && service.reconciles == 1);

        state.type = &idle_type;
        service.observe_state_write(reinterpret_cast<uintptr_t>(&retired_component));
        assert(service.ship_control_active_); // NPC/retired Sora cannot release it.
        component.live = false;
        service.observe_state_write(event);
        component.live = true;
        component.fields[L"m_pCurrentState"] = nullptr;
        service.observe_state_write(event);
        assert(service.ship_control_active_); // Incomplete entry must wait.
        component.fields[L"m_pCurrentState"] = &state;
        state.fields[L"MyCharPawn"] = &retired;
        service.observe_state_write(event);
        assert(service.ship_control_active_);
        state.fields[L"MyCharPawn"] = &pawn;
        service.observe_state_write(event);
        assert(!service.ship_control_active_ && service.reconciles == 2);
        assert(service.reason == L"Caribbean helm exited");
        service.observe_state_write(event);
        assert(service.reconciles == 2); // Repeated events do not reroll/respawn.
        assert(service.friend_events == 7); // Existing companion wake path survives.

        state.type = &ship_type;
        pawn.fields[L"MyStateComp"] = &retired_component;
        service.observe_state_write(event);
        assert(!service.ship_control_active_); // Replaced component is ignored.
        pawn.fields[L"MyStateComp"] = &component;
        service.controlled = nullptr;
        service.observe_state_write(event);
        assert(!service.ship_control_active_); // Lost possession/world ownership.
        service.controlled = &pawn;
        service.active_ = false;
        service.observe_state_write(event);
        service.active_ = true;
        service.installed_ = false;
        service.observe_state_write(event);
        service.observe_state_write(0);
        assert(service.reconciles == 2);
    }
}
