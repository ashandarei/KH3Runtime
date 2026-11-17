#include "kh3ap/core/objects/actor_owning_world.h"
#include <cassert>
#include <chrono>
#include <string>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string_view>
#include "kh3ap/features/story/save_points/save_menu_commands.h"

struct UFunction;
struct UObject
{
    bool live = true;
    bool player_character = true;
    void* GetPropertyByNameInChain(const wchar_t*) { return player_character ? this : nullptr; }
    std::wstring name;
    UObject* level{};
    std::wstring full_name;
    const std::wstring& GetFullName() const { return full_name.empty() ? name : full_name; }
    const std::wstring& GetName() const { return name; }
    UObject* outer{};
    UObject* script{};
    UFunction* function{};
    int calls = 0;
    UObject* GetOuterPrivate() { return outer; }
    UFunction* GetFunctionByNameInChain(const wchar_t*) { return function; }
    void ProcessEvent(UFunction*, void*) { ++calls; }
    UObject* world{};
    UObject* controller{};
    UObject* player{};
    UObject* pawn{};
};
struct UFunction : UObject { int params_size = 0; int GetParmsSize() { return params_size; } };
bool is_uobject_valid_fast(UObject* object) { return object && object->live; }
namespace kh3ap { using ::is_uobject_valid_fast; }
struct Candidate
{
    UObject* value{};
    bool trusted = false;
    bool candidate_trusted() const { return trusted; }
    UObject* object() const { return value; }
};
struct RuntimeState
{
    Candidate candidate;
    bool battle = false;
    bool special_battle_mode_active() const { return battle; }
    RuntimeState& grant_state() { return *this; }
    Candidate& pawn_state() { return candidate; }
};
namespace character_swap
{
    UObject* hero{};
    bool pending = false;
    bool handoff_pending() { return pending; }
    UObject* current_hero() { return hero; }
}
UObject* current_world{};
UObject* story_world{};
UObject* story_player{};
UObject* kh02_current_world() { return current_world; }
UObject* kh02_current_story_world() { return story_world; }
UObject* kh02_story_player(UObject*) { return story_player; }
UObject* actor_owning_world(UObject* object) { return object ? object->world : nullptr; }
template <size_t> struct ReflectedPropertyCache
{
    UObject* read_object(UObject* object, std::wstring_view name)
    {
        if (!is_uobject_valid_fast(object)) { return nullptr; }
        if (name == L"Controller" || name == L"PlayerController") { return object->controller; }
        if (name == L"Player") { return object->player; }
        if (name == L"Pawn") { return object->pawn; }
        throw std::runtime_error("unexpected property");
    }
};
bool control_ready = true;
bool loading_closed = true;
bool game_paused = false;
bool native_call_failed = false;
bool runtime_sora_control_ready(UObject*) { return control_ready; }
struct Kh02ReflectedCall
{
    std::wstring_view method;
    Kh02ReflectedCall(const wchar_t*, const wchar_t* method, const char*, size_t) : method(method) {}
    void object(const wchar_t*, UObject*) {}
    void run() { if (native_call_failed) { throw std::runtime_error("native call failed"); } }
    bool boolean_result() { return method == L"IsGamePaused" ? game_paused : loading_closed; }
};
struct Weak
{
    UObject* value{};
    UObject* Get() const { return is_uobject_valid_fast(value) ? value : nullptr; }
    void operator=(UObject* pawn) { value = pawn; }
};
Weak make_uobject_weak(UObject* pawn) { return {pawn}; }
#include "current_player_under_test.h"

#define STR(x) L##x
using namespace kh3ap::save_menu_commands;
struct RuntimeObjectResolver
{
    static bool is_sora_ability_pawn_candidate(UObject* pawn) { return pawn->name == L"Sora"; }
};
struct RecoveryGate
{
    struct Deps { RuntimeState& runtime_state; } deps;
    struct Request { std::wstring command; } request;
    std::wstring status, reason;
    UObject* accepted{};
    void write_result(const Deps&, const Request&, const wchar_t* result_status,
                      const wchar_t* result_reason, UObject*, UObject*, bool)
    {
        status = result_status;
        reason = result_reason;
    }
    void run()
    {
        status.clear();
        reason.clear();
        accepted = nullptr;
#include "recovery_player_gate_under_test.h"
        accepted = pawn;
    }
};
enum class LogLevel { Normal, Warning };
struct Output { template <LogLevel, typename... Args> static void send(Args&&...) {} };
struct RuntimeGrantPawnState { static constexpr auto stability_delay = std::chrono::milliseconds{250}; };
UObject* persistent_level_for(UObject* pawn) { return pawn ? pawn->level : nullptr; }
bool runtime_party_requires_entry_reconcile(const std::wstring& level) { return level == L"goa"; }
bool runtime_party_requires_context_reconcile(const std::wstring& level) { return level == L"toys"; }
struct PartyEntry
{
    RuntimeState& state;
    bool installed_ = true, active_ = true, awaiting_entry_control_ = false;
    Weak goa_entry_pawn_, context_entry_pawn_;
    struct Work { void request() {} } maintenance_;
    struct Settings { bool party_randomization_enabled = false, party_reroll_every_room = false;
                      size_t party_friend_1_choice = 1, party_friend_2_choice = 2; } settings;
    static constexpr size_t vanilla_choice = 0;
    std::chrono::steady_clock::time_point next_reconcile_attempt_{};
    int requests = 0;
    UObject* resolve_party_player() { return runtime_current_player_pawn(); }
    Settings effective_settings() { return settings; }
    void refresh_ship_control() {}
    void request_reconcile(const wchar_t*, bool) { ++requests; }
#include "party_entry_under_test.h"
};

UObject* try_read_object_property_object(UObject* object, const wchar_t*) { return object->script; }
std::wstring format_object_debug_name(UObject* object) { return object ? object->GetFullName() : L"null"; }
struct RuntimeGrantInternalCallDepth { struct Scope {}; };
struct GoaAutosave
{
    static constexpr auto goa_autosave_settle_interval = std::chrono::seconds{2};
    static constexpr int max_autosave_outer_depth = 8;
    static constexpr std::wstring_view goa_01_map_path = L"/Game/Maps/GardenOfAssemblage/umap/GoA_01";
    bool pending = false;
    int32_t pending_slot = -1, last_autosave_slot = -1;
    std::wstring pending_path;
    std::chrono::steady_clock::time_point pending_detected_at{}, last_autosave_at{};
#include "goa_autosave_under_test.h"
};

int main()
{
    UObject world, old_world, sora, kairi, aqua, npc, controller, local, other_controller;
    current_world = &world;
    npc.player_character = false;
    for (auto* pawn : {&sora, &kairi, &aqua, &npc})
    {
        pawn->world = &world;
        pawn->controller = &controller;
    }
    controller.world = &world;
    controller.player = &local;
    local.controller = &controller;
    controller.pawn = &sora;
    RuntimeState state{{&sora, true}};
    assert(runtime_current_player_pawn() == nullptr); // Grant cache cannot seed gameplay.
    assert(runtime_observe_current_player(&sora));
    const auto initial_revision = runtime_current_player_revision();
    assert(runtime_observe_current_player(&sora));
    assert(runtime_current_player_revision() == initial_revision);
    assert(runtime_current_player_pawn() == &sora);
    assert(runtime_current_player_pawn() == &sora);
    assert(runtime_ready_player_pawn() == &sora);
    // A still-trusted outgoing Sora cannot be used after local possession changes.
    controller.pawn = &kairi;
    character_swap::pending = true;
    assert(runtime_current_player_pawn() == nullptr);
    assert(!runtime_observe_current_player(&kairi));
    character_swap::pending = false;
    character_swap::hero = &kairi;
    state.candidate.trusted = false;
    assert(runtime_current_player_pawn() == &kairi);
    assert(runtime_ready_player_pawn() == &kairi);
    assert(state.candidate.value == &sora && !state.candidate.trusted); // Grant context stays separate.
    const auto alternate_revision = runtime_current_player_revision();
    assert(alternate_revision > initial_revision);
    assert(!runtime_observe_current_player(&npc));
    assert(runtime_current_player_pawn() == &kairi);
    assert(runtime_current_player_revision() == alternate_revision);
    // LocalPlayer survives controller replacement; consumers follow the new possession.
    other_controller.world = &world;
    other_controller.player = &local;
    other_controller.pawn = &kairi;
    local.controller = &other_controller;
    kairi.controller = &other_controller;
    assert(runtime_current_player_pawn() == &kairi);
    assert(runtime_current_player_revision() > alternate_revision);
    local.live = false;
    assert(runtime_current_player_pawn() == nullptr);
    local.live = true;
    local.controller = &controller;
    kairi.controller = &controller;
    other_controller.pawn = nullptr;
    assert(runtime_current_player_pawn() == &kairi);
    // All flag write variants use the possessed character, not cached Sora.
    RecoveryGate recovery{{state}, {L"set_game_flag"}};
    for (const auto* command : {L"set_game_flag", L"set_game_flag_boolean", L"set_game_flag_label", L"set_world_map_progress"})
    {
        recovery.request.command = command;
        recovery.run();
        assert(recovery.accepted == &kairi && recovery.reason.empty());
    }
    recovery.request.command = L"set_game_flag";
    state.battle = true;
    recovery.run();
    assert(!recovery.accepted && recovery.reason == L"special_battle_mode_active");
    state.battle = false;
    game_paused = true;
    recovery.run();
    assert(!recovery.accepted && recovery.reason == L"missing_ready_player_pawn");
    game_paused = false;
    character_swap::pending = true;
    assert(runtime_current_player_pawn() == nullptr);
    recovery.run();
    assert(!recovery.accepted && recovery.reason == L"missing_ready_player_pawn");
    character_swap::pending = false;
    kairi.world = &old_world;
    assert(runtime_current_player_pawn() == nullptr);
    kairi.world = &world;
    controller.world = &old_world;
    assert(runtime_current_player_pawn() == nullptr);
    controller.world = &world;
    local.controller = &other_controller;
    assert(runtime_current_player_pawn() == nullptr);
    local.controller = &controller;
    controller.pawn = &npc;
    assert(runtime_current_player_pawn() == nullptr); // A named hero is insufficient without possession.
    controller.pawn = &kairi;
    kairi.live = false;
    assert(runtime_current_player_pawn() == nullptr);
    kairi.live = true;
    game_paused = true;
    assert(runtime_current_player_pawn() == &kairi); // Music/save events still have an owner.
    assert(runtime_ready_player_pawn() == nullptr); // Spawn work waits.
    game_paused = false;
    loading_closed = false;
    assert(runtime_ready_player_pawn() == nullptr);
    loading_closed = true;
    control_ready = false;
    assert(runtime_ready_player_pawn() == nullptr);
    control_ready = true;
    native_call_failed = true;
    assert(runtime_ready_player_pawn() == nullptr);
    native_call_failed = false;
    // KH0.2 presentation ownership also obeys local possession and current world.
    character_swap::hero = nullptr;
    story_world = &world;
    story_player = &aqua;
    controller.pawn = &aqua;
    assert(runtime_current_player_pawn() == &aqua);
    assert(runtime_ready_player_pawn() == &aqua);
    recovery.run();
    assert(recovery.accepted == &aqua);
    story_player = nullptr;
    state.candidate.trusted = true;
    assert(runtime_current_player_pawn() == &aqua); // Identity does not depend on story caches.
    controller.pawn = nullptr;
    assert(runtime_current_player_pawn() == nullptr); // No Sora fallback after unpossession.
    runtime_clear_current_player();
    controller.pawn = &kairi;
    assert(runtime_current_player_pawn() == nullptr); // Travel invalidation requires a new event.
    assert(runtime_observe_current_player(&kairi));
    story_world = nullptr;
    character_swap::hero = &kairi;
    controller.pawn = &kairi;
    state.candidate.trusted = false;
    UObject goa, toys, field;
    goa.name = L"goa"; toys.name = L"toys"; field.name = L"field";
    kairi.level = &goa;
    PartyEntry party{state};
    party.observe_goa_map_ready();
    assert(party.requests == 1 && party.goa_entry_pawn_.Get() == &kairi);
    const auto first_deadline = party.next_reconcile_attempt_;
    party.observe_goa_map_ready();
    assert(party.requests == 1 && party.next_reconcile_attempt_ == first_deadline);
    kairi.level = &field;
    party.observe_sora_context_ready();
    assert(party.requests == 1);
    kairi.level = &toys;
    party.observe_sora_context_ready();
    assert(party.requests == 2 && party.awaiting_entry_control_);
    party.observe_sora_context_ready();
    assert(party.requests == 2);
    character_swap::pending = true;
    PartyEntry during_swap{state};
    during_swap.observe_sora_context_ready();
    during_swap.observe_goa_map_ready();
    assert(during_swap.requests == 0);

    character_swap::pending = false;
    UObject level, script;
    UFunction autosave_function;
    level.name = L"PersistentLevel";
    level.full_name = std::wstring{GoaAutosave::goa_01_map_path} + L":PersistentLevel";
    level.script = &script;
    script.function = &autosave_function;
    kairi.outer = &level;
    kairi.full_name = level.full_name + L".Kairi";
    GoaAutosave autosave{};
    autosave.queue_after_manual_save(L"runtime-event-only", 99);
    assert(script.calls == 0 && autosave.pending);
    autosave.process_pending();
    assert(script.calls == 1 && !autosave.pending);
    autosave.queue_after_manual_save(L"runtime-event-only", 99);
    autosave.process_pending();
    assert(script.calls == 1); // Duplicate completion is suppressed.
    GoaAutosave rejected{};
    rejected.queue_after_manual_save(L"runtime-event-only", 0);
    assert(!rejected.pending); // Autosaves never recursively trigger themselves.
    kairi.full_name = L"/Game/Levels/mi/mi_03/mi_03.Kairi";
    rejected.create_after_manual_save(L"runtime-event-only", 98);
    assert(script.calls == 1); // Another map cannot dispatch GoA's wrapper.
    kairi.full_name = level.full_name + L".Kairi";
    controller.pawn = &npc;
    rejected.create_after_manual_save(L"runtime-event-only", 98);
    assert(script.calls == 1); // Outgoing player and stale grant candidates are rejected.
    controller.pawn = &kairi;
    autosave_function.params_size = 4;
    rejected.create_after_manual_save(L"runtime-event-only", 98);
    assert(script.calls == 1); // Only the exact no-argument wrapper is callable.

}
