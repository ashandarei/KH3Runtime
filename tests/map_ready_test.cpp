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
struct UObject { bool valid{true}; int serial{1}; virtual ~UObject() = default; };
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
enum class MusicUpdate { Context };
enum class LogLevel { Warning };
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
    FWeakObjectPtr map_ready_pawn,map_ready_world;
    auto mark_trusted_current_playable_context_map_ready(std::wstring_view) { return promoted; }
    auto trusted_current_playable_cached_sora_pawn() -> UObject* { return pawn; }
    auto bridge_dir() -> std::optional<std::filesystem::path> {
        return bridge_available?std::optional{std::filesystem::path{"test"}}:std::nullopt;
    }
    auto request_music_update(MusicUpdate) -> void { ++music_wakes; }
#include "map_ready_under_test.h"
};

struct AbilityReady {
    struct State {
        UObject* pawn{};UObject* controller{};std::wstring pawn_name;
        auto object() { return pawn; }
        auto controller_object() { return controller; }
        auto name() -> std::wstring_view { return pawn_name; }
        auto pawn_state() -> State& { return *this; }
        auto grant_state() -> State& { return *this; }
    };
    struct Config { State runtime_state; } config_;
    FWeakObjectPtr successful_pawn_,successful_controller_;
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

    AbilityReady ability;
    UObject current_pawn,current_controller,new_controller;
    ability.config_.runtime_state={&current_pawn,&current_controller,L"/Game/Levels/ts/ts_02/pawn"};
    ability.request_refresh(RuntimeControllerAbilityEvent::MapReady);
    assert(ability.pending_events_.exchange(0)==4); // Unfinished/failed early work can retry at readiness.
    ability.successful_pawn_=&current_pawn;ability.successful_controller_=&current_controller;
    ability.request_refresh(RuntimeControllerAbilityEvent::MapReady);
    assert(ability.pending_events_.exchange(0)==0); // Successful possession setup is not repeated.
    ability.request_refresh(RuntimeControllerAbilityEvent::GameAbilityChanged);
    assert(ability.pending_events_.exchange(0)==8); // Native story setters are independent.
    ability.config_.runtime_state.pawn_name=L"/Game/Levels/he/he_02/pawn";
    ability.request_refresh(RuntimeControllerAbilityEvent::MapReady);
    assert(ability.pending_events_.exchange(0)==4); // Olympus requires the late override.
    ability.config_.runtime_state.pawn_name=L"/Game/Levels/ts/ts_02/pawn";
    ability.config_.runtime_state.controller=&new_controller;
    ability.request_refresh(RuntimeControllerAbilityEvent::MapReady);
    assert(ability.pending_events_.exchange(0)==4); // Same pawn, different controller.
    ability.config_.runtime_state.controller=&current_controller;++current_pawn.serial;
    ability.request_refresh(RuntimeControllerAbilityEvent::MapReady);
    assert(ability.pending_events_.exchange(0)==4); // Weak serial reuse cannot skip new ownership.
}
