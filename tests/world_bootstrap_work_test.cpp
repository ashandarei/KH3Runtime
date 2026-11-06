#include "kh3ap/features/story/bootstrap/world_bootstrap.h"
#include "kh3ap/features/story/bootstrap/world_bootstrap_service.h"
#include <atomic>
#include <cassert>
#include <memory>
#include <string>
#include <vector>
#define STR(value) L##value
namespace RC {
enum class LogLevel { Verbose };
struct Output {template<LogLevel,class... Args>static void send(Args&&...) {}};
namespace Unreal {
using int32=int32_t;
struct UObjectBase {};
struct UFunction {};
class UObject : public UObjectBase {
public:
    bool live=true,in_viewport=false,is_actor=false;
    int generation=1;
    UObject* controller{}; UObject* owned_pawn{};
    void ProcessEvent(UFunction*,void*) { in_viewport=false; }
};
class UClass : public UObject {};
class AActor : public UObject {
public:
    UObject* level{};
    AActor(){is_actor=true;}
    UObject* GetWorld(){return level;}
    static UClass* StaticClass(){static UClass type;return &type;}
};
struct FWeakObjectPtr {
    UObject* ptr{};int generation{};
    FWeakObjectPtr()=default;
    FWeakObjectPtr(std::nullptr_t){}
    FWeakObjectPtr(UObject* value):ptr(value),generation(value?value->generation:0){}
    UObject* Get()const{return ptr&&ptr->live&&ptr->generation==generation?ptr:nullptr;}
};
struct FUObjectDeleteListener {
    virtual ~FUObjectDeleteListener()=default;
    virtual void NotifyUObjectDeleted(const UObjectBase*,int32)=0;
};
}
}
namespace kh3ap {
using namespace RC;
using namespace RC::Unreal;
constexpr bool s_enable_runtime_hot_path_profiling=true;
int loading_reads{},shop_calls{},class_queries{},create_calls{};
bool loading=true,shop_success=true,asset_ready=true,create_success=true;
std::vector<std::unique_ptr<UObject>> widgets;
std::function<void()> during_create;
template<bool>struct RuntimeConditionalProfileScope { explicit RuntimeConditionalProfileScope(const wchar_t*){} };
bool is_uobject_valid_fast(UObject* obj){return obj&&obj->live;}
bool is_uobject_a_fast(UObject* obj,UClass*){return is_uobject_valid_fast(obj)&&obj->is_actor;}
FWeakObjectPtr make_uobject_weak(UObject* obj){return obj;}
UObject* try_read_reflected_object_property_value(UObject* object,std::wstring_view name){
    return !object?nullptr:name==L"Controller"?object->controller:object->owned_pawn;
}
UObject* live_weak_object(const FWeakObjectPtr& weak){return weak.Get();}
std::optional<bool> widget_in_viewport(UObject* obj){return obj?std::optional{obj->in_viewport}:std::nullopt;}
std::optional<bool> call_loading_screen_closed(std::wstring*){++loading_reads;return loading;}
bool call_set_shop_level(UObject*,std::wstring*){++shop_calls;return shop_success;}
UFunction* widget_remove_function(UObject*){static UFunction remove;return &remove;}
std::wstring format_object_debug_name(UObject*){return L"widget";}
bool create_and_add_proof_ui(UObject*,UClass*,UObject** result,std::wstring*){
    ++create_calls;if(!create_success)return false;
    widgets.push_back(std::make_unique<UObject>());
    *result=widgets.back().get();(*result)->in_viewport=true;
    if(during_create)during_create();return true;
}
void report_failure(std::wstring_view, std::wstring&, std::wstring_view) {}
struct ProofUiLifecycle {
    struct Impl : FUObjectDeleteListener {
        explicit Impl(std::chrono::milliseconds delay) : retry_interval(delay) {}
        FWeakObjectPtr proof_ui;
        bool added{};
        std::chrono::milliseconds retry_interval;
        std::chrono::steady_clock::time_point next_proof_ui_create_attempt_at{};
        std::atomic<uintptr_t> watched_widget{};
        std::atomic<bool> widget_deleted{}, widget_removed{};
        bool viewport_check_pending{};
        std::wstring proof_ui_create_failure;
        uint64_t widgets_created{};
        bool set_battle_level_map_visible(bool) { return true; }
#include "proof_ui_state_under_test.h"
    };
    explicit ProofUiLifecycle(std::chrono::milliseconds interval) : impl_(std::make_unique<Impl>(interval)) {}
    std::unique_ptr<Impl> impl_;
    bool install_lifecycle_events() { return true; }
    UObject* widget() { return impl_->proof_ui.Get(); }
#include "proof_ui_facade_under_test.h"
};
class RuntimePlayerContextService {
public:
    AActor* pawn{};int resolves{};
    UObject* trusted_current_playable_cached_sora_pawn(){++resolves;return pawn;}
};
struct Bootstrap {
    explicit Bootstrap(RuntimePlayerContextService& player):config{player,{}}{}
    RuntimeWorldBootstrapConfig config;
    FWeakObjectPtr world,ready_pawn,ready_world,ready_controller;
    ProofUiLifecycle proof_ui{config.asset_load_retry_interval};
    std::array<int,runtime_required_world_actors.size()> actors{};
    RuntimeWorldBootstrapProgress progress;
    RuntimeWorldBootstrapWork work;
    uint64_t attempts{};
    std::chrono::steady_clock::time_point next_class_scan_at{};
    std::wstring loading_failure,shop_failure;
    static constexpr auto remove_widget_path=L"remove";
    struct Inbox {int pumps{},resets{},ready_events{};void process_events(){++pumps;}void reset(){++resets;}void world_ready(){++ready_events;}};
    Inbox vbonus_tracking,event_tracking;
    bool set_battle_level_map_visible(bool){return true;}
    void ensure_actor(size_t,UObject*,std::chrono::steady_clock::time_point){}
    UClass* prewarm_proof_ui_class(UObject*,std::chrono::steady_clock::time_point){
        ++class_queries;static UClass type;return asset_ready?&type:nullptr;
    }
    void report_failure(std::wstring_view,std::wstring&,std::wstring_view){}
#include "world_bootstrap_work_under_test.h"
};
}
int main(){
    using namespace kh3ap;
    using namespace std::chrono_literals;
    auto now=std::chrono::steady_clock::time_point{}+1s;
    UObject level,other_level,unrelated;
    AActor pawn;pawn.level=&level;
    RuntimePlayerContextService player;
    Bootstrap service(player);
    service.tick(now); // Initial readiness unavailable: pending work only.
    const auto first=service.attempts;
    service.tick(now+100ms);assert(service.attempts==first);
    player.pawn=&pawn;loading=false;
    service.world_ready();assert(service.event_tracking.ready_events==0); // Construction is not possession.
    UObject controller;controller.owned_pawn=&pawn;pawn.controller=&controller;
    service.world_ready();assert(service.event_tracking.ready_events==1);
    for(int i=0;i<1000;++i)service.world_ready();
    assert(service.event_tracking.ready_events==1); // Name callback fallback does not redo early work.
    ++controller.generation;service.world_ready();assert(service.event_tracking.ready_events==2);
    now+=250ms;service.tick(now);assert(shop_calls==0);
    loading=true;now+=250ms;service.tick(now);assert(shop_calls==1&&create_calls==0);
    asset_ready=false;now+=250ms;service.tick(now);assert(create_calls==0&&service.work.pending());
    asset_ready=true;service.work.request();now+=1ms;
    during_create=[&]{service.work.request();}; // A lifecycle event within setup survives completion.
    service.tick(now);during_create={};
    assert(service.progress.complete()&&service.work.pending()&&create_calls==1);
    now+=1ms;service.tick(now);assert(!service.work.pending());
    auto idle_attempts=service.attempts;
    auto idle_resolves=player.resolves;
    auto idle_queries=class_queries;
    for(int i=0;i<10000;++i){now+=16ms;service.tick(now);}
    assert(service.attempts==idle_attempts&&player.resolves==idle_resolves&&class_queries==idle_queries);
    assert(service.event_tracking.pumps>=10000&&service.vbonus_tracking.pumps>=10000);
    service.proof_ui.impl_->NotifyUObjectDeleted(&unrelated,0);service.tick(now);
    assert(service.attempts==idle_attempts); // Unrelated GC never wakes bootstrap.
    auto* old=service.proof_ui.widget();old->in_viewport=false;service.proof_ui.impl_->widget_removed=true;
    now+=1ms;service.tick(now);
    assert(create_calls==2&&service.progress.complete()&&service.proof_ui.widget()!=old);
    assert(shop_calls==1); // Rebuilding UI does not repeat shop initialization.
    // A failed/reversed removal leaves the existing visible widget alone.
    service.proof_ui.impl_->widget_removed=true;now+=1ms;service.tick(now);assert(create_calls==2);
    auto* current=service.proof_ui.widget();current->live=false;
    service.proof_ui.impl_->NotifyUObjectDeleted(current,0);now+=1ms;service.tick(now);
    assert(create_calls==3&&service.progress.complete());
    // A stale GC notification cannot discard the replacement's valid weak identity.
    service.proof_ui.impl_->widget_deleted=true;idle_attempts=service.attempts;
    now+=1ms;service.tick(now);assert(service.attempts==idle_attempts&&create_calls==3);
    service.reset();assert(!service.progress.complete()&&service.work.pending());
    assert(!service.proof_ui.widget()&&!widgets.back()->in_viewport);
    pawn.level=&other_level;
    now+=1ms;service.tick(now);assert(shop_calls==2);
    now+=250ms;service.tick(now);assert(service.progress.complete()&&create_calls==4);
    // A generation change at the same world address must start a new lifecycle.
    ++other_level.generation;service.work.request();now+=1ms;service.tick(now);
    assert(shop_calls==3&&!service.progress.proof_ui_added);
    create_success=false;now+=250ms;service.tick(now);assert(service.work.pending());
    create_success=true;now+=2s;service.tick(now);assert(service.progress.complete());
    idle_attempts=service.attempts;
    now+=2s;service.tick(now);assert(service.attempts==idle_attempts);
    service.reset();service.config.enabled=false;now+=1s;service.tick(now);
    assert(service.attempts==idle_attempts);
}
