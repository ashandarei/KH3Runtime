#include <cassert>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <optional>
#include <sstream>
#include <string>
#include "kh3ap/features/player/controller_abilities/controller_ability_service.h"
#include "kh3ap/features/player/controller_abilities/controller_ability_refresh_state.h"

using kh3ap::RuntimeControllerAbilityEvent;
using kh3ap::is_runtime_mount_olympus_ability_context;

#define STR(value) L##value
struct UObject {
    bool valid{true}; int serial{1}; virtual ~UObject() = default;
    UObject *controller{}, *player{}, *pawn{}, *player_controller{};
};
struct AActor : UObject {
    UObject* world{};
    auto GetWorld() -> UObject* { return world; }
    static auto StaticClass() -> int { return 1; }
};
struct FWeakObjectPtr {
    UObject* object{}; int serial{};
    FWeakObjectPtr() = default;
    FWeakObjectPtr(UObject* p):object(p),serial(p?p->serial:0){}
    auto Get() const -> UObject* { return object&&object->valid&&object->serial==serial?object:nullptr; }
};
auto make_uobject_weak(UObject* p) -> FWeakObjectPtr { return p; }
auto is_uobject_valid_fast(UObject* p) -> bool { return p&&p->valid; }
auto is_uobject_a_fast(UObject* p, int) -> bool { return is_uobject_valid_fast(p)&&dynamic_cast<AActor*>(p); }
namespace character_swap {
    UObject* hero{};
    auto current_hero() -> UObject* { return hero; }
    auto owns_swap_pawn(UObject* p) -> bool { return p && p == hero; }
}
enum class MusicUpdate { Context };
enum class LogLevel { Warning, Verbose };
struct Output { template<LogLevel, class... T> static void send(T&&...){} };
struct RuntimeContextSnapshot { std::wstring hook,world,outer,object,pawn_source; };
constexpr auto s_bridge_protocol_name="test";
constexpr auto s_bridge_protocol_version=1;
namespace channels { constexpr auto runtime_map_ready=L"ready.json", runtime_map_ready_file=L"ready.json"; }
auto json_escape(std::wstring_view) -> std::string { return "value"; }
auto json_escape(const char* text) -> std::string { return text; }
auto now_iso8601_utc() -> std::string { return "now"; }
auto format_object_debug_name(UObject*) -> std::wstring { return L"object"; }
int marker_writes{};
auto write_text_file(const std::filesystem::path&, const std::string&) -> bool { ++marker_writes;return true; }

struct ContextState {
    std::optional<RuntimeContextSnapshot> snapshot;
    auto current_snapshot() -> std::optional<RuntimeContextSnapshot> { return snapshot; }
    auto context_state() -> ContextState& { return *this; }
    auto context_subsystem_state() -> ContextState& { return *this; }
};
struct Service {
    struct Config { ContextState runtime_state;std::function<void()> on_map_ready; } config;
    std::optional<RuntimeContextSnapshot> promoted;
    UObject* pawn{};
    bool bridge_available{true};
    int music_wakes{};
    std::wstring map_ready_context_key;
    FWeakObjectPtr map_ready_pawn,map_ready_world,observed_map_ready_world;
    auto read_object_property_in_chain(UObject* p, const wchar_t* property) const -> UObject* {
        if (!is_uobject_valid_fast(p)) return nullptr;
        const std::wstring_view name{property};
        if (name == L"Controller") return p->controller;
        if (name == L"Player") return p->player;
        if (name == L"Pawn") return p->pawn;
        if (name == L"PlayerController") return p->player_controller;
        return nullptr;
    }
    auto mark_trusted_current_playable_context_map_ready(std::wstring_view) { return promoted; }
    auto trusted_current_playable_cached_sora_pawn() -> UObject* { return pawn; }
    auto bridge_dir() -> std::optional<std::filesystem::path> {
        return bridge_available?std::optional{std::filesystem::path{"test"}}:std::nullopt;
    }
    auto request_music_update(MusicUpdate) -> void { ++music_wakes; }
#include "map_ready_under_test.h"
};

struct AbilityReady {
    std::atomic<uint32_t> pending_events_{};
#include "ability_ready_under_test.h"
};

int main() {
    Service service;
    int refreshes{};
    service.config.on_map_ready=[&]{++refreshes;};
    UObject world,other_world;
    AActor pawn,replacement;
    pawn.world=&world;replacement.world=&world;service.pawn=&pawn;
    const auto arrive=[&]{return service.write_map_ready_marker(L"area",nullptr,nullptr,true);};
    assert(!arrive()&&refreshes==0); // A UI notification before ownership is not readiness.
    service.promoted=RuntimeContextSnapshot{L"RuntimePlayablePawnContext",L"world",{},L"pawn",{}};
    assert(arrive()&&refreshes==1&&marker_writes==1);
    for(int i=0;i<1000;++i)assert(!arrive()); // ApplyMapName + UIP callback + same-world area banners.
    assert(refreshes==1&&marker_writes==1&&service.music_wakes==1);
    service.pawn=&replacement;assert(arrive()&&refreshes==2); // Retry replacement, same controller/map.
    ++replacement.serial;assert(arrive()&&refreshes==3); // UObject slot/address reuse.
    replacement.world=&other_world;assert(arrive()&&refreshes==4); // Seamless world ownership change.
    ++other_world.serial;assert(arrive()&&refreshes==5);
    service.map_ready_context_key.clear();service.map_ready_pawn={};service.map_ready_world={};
    assert(arrive()&&refreshes==6); // Explicit map lifecycle reset can reuse ownership.
    service.promoted->world=L"other_map";assert(arrive()&&refreshes==7);
    auto writes=marker_writes;
    service.bridge_available=false;++replacement.serial;
    assert(arrive()&&refreshes==8&&marker_writes==writes); // Native setup does not depend on a file bridge.
    assert(!arrive());
    replacement.valid=false;assert(!arrive()&&refreshes==8);
    service.pawn=nullptr;assert(!arrive());
    service.promoted.reset();service.config.runtime_state.snapshot=RuntimeContextSnapshot{L"WorldScript",L"map",{},L"script",{}};
    assert(!arrive()&&refreshes==8);

    // Map-name completion while Kairi is possessed must survive a later
    // return to a newly created Sora, without publishing Kairi as Sora.
    Service alternate;
    UObject map, next_map, local, controller;
    AActor kairi, sora;
    kairi.world = sora.world = &map;
    kairi.controller = sora.controller = &controller;
    controller.player = &local;
    local.player_controller = &controller;
    controller.pawn = &kairi;
    character_swap::hero = &kairi;
    assert(!alternate.map_ready_for_pawn(&kairi)); // Possession alone is not map readiness.
    assert(!alternate.write_map_ready_marker(L"map_name", nullptr, nullptr, true));
    assert(alternate.map_ready_context_key.empty());
    assert(alternate.map_ready_for_pawn(&kairi));
    assert(!alternate.map_ready_for_pawn(&sora)); // Parked Sora is not possessed.
    controller.pawn = &sora;
    character_swap::hero = nullptr;
    assert(alternate.map_ready_for_pawn(&sora));
    assert(!alternate.map_ready_for_pawn(&kairi));
    sora.world = &next_map;
    assert(!alternate.map_ready_for_pawn(&sora)); // No readiness across travel.
    sora.world = &map;
    ++map.serial;
    assert(!alternate.map_ready_for_pawn(&sora)); // No readiness across UObject reuse.
    alternate.pawn = &sora;
    alternate.write_map_ready_marker(L"map_name", nullptr, nullptr, true);
    assert(alternate.map_ready_for_pawn(&sora));
    local.player_controller = nullptr;
    assert(!alternate.map_ready_for_pawn(&sora)); // Remote/unowned controller rejected.
    local.player_controller = &controller;
    alternate.observed_map_ready_world = {};
    assert(!alternate.map_ready_for_pawn(&sora)); // Explicit lifecycle reset.

    AbilityReady ability;
    // The producer above suppresses duplicate banners. Every actual map-ready
    // boundary must reconcile Wall Run, even when a load reuses the pawn.
    ability.request_refresh(RuntimeControllerAbilityEvent::MapReady);
    ability.request_refresh(RuntimeControllerAbilityEvent::MapReady);
    assert(ability.pending_events_.exchange(0) == 4);
    ability.request_refresh(RuntimeControllerAbilityEvent::GameAbilityChanged);
    assert(ability.pending_events_.exchange(0) == 8);
}
