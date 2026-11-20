#include "kh3ap/core/objects/actor_owning_world.h"
#include <cassert>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

class RC::Unreal::UObject
{
public:
    UObject* world{};
    UObject* boss{};
    UObject* controller{};
    UObject* local{};
    UObject* pawn{};
    UObject* cached{};
    UObject* manager{};
    UObject* active{};
    UObject* view{};
    UObject* type{};
    std::wstring name;
    bool live = true;
    UObject* GetClassPrivate() { return type; }
    const std::wstring& GetName() const { return name; }
};
using RC::Unreal::UObject;
UObject* world{};
UObject* player{};
UObject* alternate{};
UObject* incoming{};
UObject* slam_ready{};
bool fail_swap = false;
int swaps = 0, native_calls = 0;
UObject* native_player{};
bool is_uobject_valid_fast(UObject* p) { return p && p->live; }
UObject* kh3ap::actor_owning_world(UObject* p) { return p ? p->world : nullptr; }
using kh3ap::actor_owning_world;
UObject* kh02_current_world() { return world; }
bool tornado_world(UObject* p) { return p && p == world; }
UObject* local_pawn() { return player; }
struct Weak { UObject* p{}; UObject* Get() const { return p; } } freefall_world, vacuum_action;
Weak make_uobject_weak(UObject* p) { return {p}; }
struct Property
{
    template<typename T> T* ContainerPtrToValuePtr(UObject* p) { return reinterpret_cast<T*>(&p->cached); }
};
struct Properties
{
    UObject* read_object(UObject* p, std::wstring_view name)
    {
        if (!is_uobject_valid_fast(p)) { return nullptr; }
        if (name == L"MyCharPawn") { return p->boss; }
        if (name == L"Controller" || name == L"PlayerController") { return p->controller; }
        if (name == L"Player") { return p->local; }
        if (name == L"Pawn") { return p->pawn; }
        if (name == L"m_Player") { return p->cached; }
        if (name == L"PlayerCameraManager" || name == L"m_CameraManager") { return p->manager; }
        if (name == L"m_PlayerController") { return p->controller; }
        throw std::runtime_error("unknown property");
    }
    Property* resolve_object(UObject*, const wchar_t*) { static Property p; return &p; }
} properties;
namespace kairi_ground_slam { bool ready(UObject* p) { return p && p == slam_ready; } }
namespace character_swap
{
    bool owns_swap_pawn(UObject* p) { return p && p == alternate; }
    UObject* return_to_sora_for_encounter(UObject*)
    {
        ++swaps;
        if (fail_swap) { throw std::runtime_error("swap failed"); }
        // The UI manager deliberately remains stale. The handoff returns the
        // verified LocalPlayer possession before the native action is called.
        return incoming;
    }
}
#define STR(x) L##x
namespace RC
{
    enum class LogLevel { Warning };
    struct Output { template<LogLevel, typename... T> static void send(T&&...) {} };
}
using TornadoBegin = void (*)(UObject*);
using Notify = void (*)(UObject*, bool, bool);
void native_notify(UObject*, bool, bool) {}
uintptr_t original_notify = reinterpret_cast<uintptr_t>(&native_notify);
void native_begin(UObject* action) { ++native_calls; native_player = action->boss->cached; }
uintptr_t original_tornado = reinterpret_cast<uintptr_t>(&native_begin);
uintptr_t original_vacuum = reinterpret_cast<uintptr_t>(&native_begin);
void native_end(UObject*) {}
uintptr_t original_vacuum_end = reinterpret_cast<uintptr_t>(&native_end);
int view_restores = 0;
struct ReflectedFunctionCall
{
    UObject* owner;
    std::wstring method;
    UObject* target{};
    UObject* returned{};
    ReflectedFunctionCall(UObject* p, const wchar_t* m, const char*, size_t) : owner(p), method(m) {}
    void object(const wchar_t* name, UObject* p) { assert(std::wstring_view(name) == L"NewViewTarget"); target = p; }
    void number(const wchar_t* name, float value) { assert(std::wstring_view(name) == L"BlendTime" && value == 0); }
    void run()
    {
        if (method == L"GetActiveCamera") { returned = owner->active; }
        else if (method == L"GetActiveViewCamera") { returned = owner->view; }
        else if (method == L"SetViewTargetWithBlend") { owner->manager->view = target; ++view_restores; }
        else { assert(false); }
    }
    UObject* result() { return returned; }
};
namespace character_camera
{
    struct Call : ReflectedFunctionCall
    {
        Call(UObject* owner, const wchar_t* method)
            : ReflectedFunctionCall(owner, method, "character camera", 128) {}
    };
    UObject* object(UObject* owner, const wchar_t* member) { return properties.read_object(owner, member); }
}
#include "tornado_freefall_under_test.h"

int main()
{
    UObject arena, boss, action, sora, kairi, riku, controller, local;
    world = &arena;
    boss.world = world;
    action.boss = &boss;
    for (auto* p : {&sora, &kairi, &riku}) { p->world = world; p->controller = &controller; }
    controller.local = &local;
    controller.world = world;
    local.controller = &controller;
    player = alternate = slam_ready = &kairi;
    controller.pawn = &kairi;
    begin_vacuum(&action);
    assert(swaps == 0 && native_player == &kairi && vacuum_action.Get() == &action);
    end_vacuum(&action);
    native_calls = 0;
    begin_tornado(&action);
    assert(swaps == 0 && native_calls == 1 && native_player == &kairi);
    assert(freefall_world.Get() == world);
    notify_floating(&controller, true, false);
    assert(freefall_world.Get() == nullptr); // Successful slam unlocks immediately.
    freefall_world = make_uobject_weak(world);
    controller.world = nullptr;
    notify_floating(&controller, false, false);
    assert(freefall_world.Get() == world); // Ignore a controller from another world.
    controller.world = world;
    notify_floating(&controller, false, false);
    assert(freefall_world.Get() == nullptr); // Retry retires an unfinished dive.
    player = alternate = &riku;
    incoming = &sora;
    controller.pawn = &sora;
    boss.cached = nullptr;
    begin_vacuum(&action);
    assert(swaps == 1 && native_player == &sora && vacuum_action.Get() == &action);
    assert(freefall_world.Get() == nullptr); // Handoff precedes the grab/movie.
    UObject unrelated;
    end_vacuum(&unrelated);
    assert(vacuum_action.Get() == &action);
    end_vacuum(&action);
    assert(vacuum_action.Get() == nullptr); // Failed suction releases its own lock.
    swaps = 0;
    native_calls = 1;
    boss.cached = nullptr;
    begin_tornado(&action);
    assert(swaps == 1 && native_calls == 2 && native_player == &sora);
    assert(player == &riku); // Repair did not rely on the delayed UI manager.
    player = &sora;
    alternate = nullptr;
    boss.cached = nullptr;
    begin_tornado(&action);
    assert(swaps == 1 && native_player == &sora);
    controller.pawn = &riku;
    boss.cached = nullptr;
    begin_tornado(&action);
    assert(native_player == nullptr); // Reject an unpossessed/stale player.
    boss.world = nullptr;
    begin_tornado(&action);
    assert(native_calls == 5 && native_player == nullptr && swaps == 1);

    UObject manager, camera, camera_type, movie;
    manager.world = camera.world = world;
    camera_type.name = L"BP_TresCameraHe903_C";
    camera.type = &camera_type;
    camera.manager = controller.manager = &manager;
    camera.controller = &controller;
    manager.active = &camera;
    manager.view = &controller;
    controller.pawn = &sora;
    assert(restore_camera_view(&sora) && manager.view == &camera && view_restores == 1);
    assert(!restore_camera_view(&sora) && view_restores == 1); // Already correct.
    manager.view = &movie;
    assert(!restore_camera_view(&sora) && manager.view == &movie); // Native movie wins.
    manager.view = &controller;
    camera_type.name = L"BP_TresCameraNormal_ex001_C";
    assert(!restore_camera_view(&sora)); // Ordinary field cameras are unchanged.
    assert(character_camera::restore_active_view(&sora, {}) && manager.view == &camera && view_restores == 2);
    camera_type.name = L"BP_TresCameraHe903_C";
    manager.view = &controller;
    camera.manager = nullptr;
    assert(!restore_camera_view(&sora)); // Do not steal another manager's camera.
    camera.manager = &manager;
    camera.world = nullptr;
    assert(!restore_camera_view(&sora));
    camera.world = world;
    controller.pawn = &riku;
    assert(!restore_camera_view(&sora));
    assert(view_restores == 2);
}
