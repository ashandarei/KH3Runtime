#include <array>
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#define STR(value) L##value
constexpr uint8_t swimming_state = 143;
struct Bytes : std::vector<uint8_t>
{
    using std::vector<uint8_t>::vector;
    int Num() const { return static_cast<int>(size()); }
    void Add(uint8_t value) { push_back(value); }
    void RemoveAt(int index, int, bool) { erase(begin() + index); }
};
struct UObject
{
    bool live = true;
    bool swimming{};
    bool bad_layout{};
    bool has_magic{}, swimming_magic{};
    UObject* type = this;
    UObject* query{};
    Bytes states{98};
    std::vector<UObject*> cached;
    UObject* GetClassPrivate() { return type; }
};
bool is_uobject_valid_fast(UObject* object) { return object && object->live; }
struct Weak
{
    UObject* object{};
    UObject* Get() const { return is_uobject_valid_fast(object) ? object : nullptr; }
};
struct BoolProperty
{
    bool magic{};
    void SetPropertyValueInContainer(UObject* object, bool value)
    {
        if (magic) { object->swimming_magic = value; }
        else { object->swimming = value; }
    }
};
BoolProperty flag;
BoolProperty magic_flag{true};
BoolProperty* swim_field(UObject* object) { return is_uobject_valid_fast(object) ? &flag : nullptr; }
BoolProperty* swim_magic_field(UObject* object) { return is_uobject_valid_fast(object) && object->has_magic ? &magic_flag : nullptr; }
template <typename T> Bytes* array(UObject* object, const wchar_t*)
{
    if (!is_uobject_valid_fast(object) || object->bad_layout) { throw std::runtime_error("layout"); }
    return &object->states;
}
bool write_object_like_property(UObject* object, const wchar_t*, UObject* value, bool allow_null = false)
{
    if (value ? !is_uobject_valid_fast(value) : !allow_null) { return false; }
    object->query = value;
    return true;
}
std::vector<UObject*> cached_actions(UObject* pawn)
{
    return is_uobject_valid_fast(pawn) ? pawn->cached : std::vector<UObject*>{};
}
namespace RC
{
    enum class LogLevel { Warning };
    struct Output
    {
        static inline int warnings{};
        template <LogLevel, typename... Args> static void send(Args&&...) { ++warnings; }
    };
}
struct AttackPatch { Weak definition, query; bool swimming{}, added_state{}; std::optional<bool> swimming_magic; };
struct State
{
    Weak pawn, state_component;
    std::vector<Weak> land_gates;
    std::vector<AttackPatch> actions;
    bool underwater{}, disabled{};
};
#include "party_swim_mutations_under_test.h"
struct Service
{
    bool installed_ = true, active_ = true;
    struct Work { int requests{}; void request() { ++requests; } } maintenance_;
    std::array<State, 2> swim_combat_{};
#include "party_swim_events_under_test.h"
};

int main()
{
    UObject land_query, swim_query, definition, gate, pawn, component;
    definition.query = &land_query;
    definition.states = Bytes{1, 44, 98};
    State state;
    state.pawn = {&pawn};
    state.land_gates = {{&gate}};
    state.actions = {{{&definition}, {&land_query}, false, true}};
    state.underwater = true;
    adapt(&definition, &swim_query, true, true, false);
    adapt(&definition, &swim_query, true, true, false);
    adapt_states(&gate, L"m_States", false);
    assert((definition.states == Bytes{1, 44, 98, 143}));

    // Instances created from the modified CDO must also regain their land
    // settings, including states already in the native recycle cache.
    UObject current = definition, recycled = definition;
    current.type = definition.GetClassPrivate();
    recycled.type = definition.GetClassPrivate();
    pawn.cached = {&current, &recycled};
    restore_state(state);
    for (auto* action : {&definition, &current, &recycled})
    {
        assert(!action->swimming && action->query == &land_query);
        assert((action->states == Bytes{1, 44, 98}));
    }
    assert(gate.states == Bytes{98} && !state.underwater);

    // Preserve unrestricted actions and authored swimming eligibility.
    definition.states.clear();
    adapt(&definition, &swim_query, true, false, false);
    adapt(&definition, &land_query, false, false, true);
    assert(definition.states.empty());
    definition.states = Bytes{98, 143};
    adapt(&definition, &swim_query, true, false, false);
    adapt(&definition, &land_query, false, false, true);
    assert((definition.states == Bytes{98, 143}));

    // Travel can retire the pawn before restoration; live defaults still restore.
    definition.states = Bytes{98};
    adapt(&definition, &swim_query, true, true, false);
    pawn.live = false;
    state.underwater = true;
    restore_state(state);
    assert(!definition.swimming && definition.query == &land_query && definition.states == Bytes{98});

    // One unavailable tree field cannot prevent restoring all other patches.
    gate.bad_layout = true;
    state.underwater = true;
    adapt(&definition, &swim_query, true, true, false);
    restore_state(state);
    assert(RC::Output::warnings == 1 && !definition.swimming && definition.query == &land_query);

    // Lea's four attacks have a null ground query. Restore that null on both
    // the definition and instances created while the swim query was installed.
    UObject lea, lea_pawn;
    State lea_state;
    lea_state.pawn = {&lea_pawn};
    lea_state.actions = {{{&lea}, {}, false, true}};
    lea_state.underwater = true;
    adapt(&lea, &swim_query, true, true, false);
    UObject lea_cached = lea;
    lea_cached.type = lea.GetClassPrivate();
    lea_pawn.cached = {&lea_cached};
    restore_state(lea_state);
    for (auto* action : {&lea, &lea_cached})
    {
        assert(!action->swimming && action->query == nullptr && action->states == Bytes{98});
    }
    assert(!lea_state.underwater && RC::Output::warnings == 1);

    // Aqua's spells have a separate native swim-magic flag. Preserve false
    // as a recorded value (not absence) and restore wet-created instances too.
    UObject spell, mage;
    spell.has_magic = true;
    State magic_state;
    magic_state.pawn = {&mage};
    magic_state.actions = {{{&spell}, {}, false, true, false}};
    magic_state.underwater = true;
    adapt(&spell, &swim_query, true, true, false, true);
    assert(spell.swimming && spell.swimming_magic);
    UObject cached_spell = spell;
    cached_spell.type = spell.GetClassPrivate();
    mage.cached = {&cached_spell};
    restore_state(magic_state);
    for (auto* action : {&spell, &cached_spell})
    {
        assert(!action->swimming && !action->swimming_magic && action->query == nullptr);
        assert(action->states == Bytes{98});
    }
    assert(!magic_state.underwater && RC::Output::warnings == 1);

    Service service;
    service.swim_combat_[0].state_component = {&component};
    const auto address = reinterpret_cast<uintptr_t>(&component);
    for (int index = 0; index < 10000; ++index) { service.observe_friend_state(address + 1); }
    assert(service.maintenance_.requests == 0);
    service.observe_friend_state(address);
    assert(service.maintenance_.requests == 1);
    service.swim_combat_[0].disabled = true;
    service.observe_friend_state(address);
    service.swim_combat_[0].disabled = false;
    component.live = false;
    service.observe_friend_state(address);
    service.observe_friend_state(0);
    assert(service.maintenance_.requests == 1);
    service.restore_party_swim_combat();
    assert(!service.swim_combat_[0].state_component.Get());
}
