#include "kh3ap/features/treasure/chest_maintenance_work.h"
#include "kh3ap/features/treasure/chest_discovery_queue.h"
#include "kh3ap/features/treasure/music_chest/music_chest_initialization.h"
#include "kh3ap/features/treasure/music_chest/music_chest_opening_work.h"
#include "kh3ap/features/treasure/music_chest/music_pause_policy.h"
#include <atomic>
#include <cassert>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>

namespace kh3ap {
struct Clock {
    using time_point=std::chrono::steady_clock::time_point;
    static inline time_point current{};
    static time_point now(){return current;}
};
struct UObjectBase {};
struct UObject : UObjectBase {
    bool live=true,active=true;
    unsigned generation=1;
    uint8_t command=3;
    std::wstring GetPathName(){return L"world";}
};
struct Weak {
    UObject* value{};unsigned generation{};
    Weak()=default;
    Weak(UObject* object):value(object),generation(object?object->generation:0){}
    UObject* Get() const {return value&&value->live&&value->generation==generation?value:nullptr;}
};
using int32=int32_t;
struct Snapshot {};
namespace reconciliation {
struct View {std::shared_ptr<const Snapshot> chests;};
inline View state;
View* view(){return &state;}
}
inline uint64_t revision{};
uint64_t read_runtime_keyblade_inventory_revision(){return revision;}
bool is_uobject_valid_fast(UObject* object){return object&&object->live;}
Weak make_uobject_weak(UObject* object){return object;}
inline UObject level,pawn;
UObject* world(UObject*){return &level;}
bool active_chest(UObject* object){return is_uobject_valid_fast(object)&&object->active;}
uint8_t reactor_command(UObject* object,const uint8_t* command=nullptr){
    if(!object)throw std::runtime_error("invalid reactor");
    if(command)object->command=*command;
    return object->command;
}
inline RuntimeMusicPausePolicy pause_policy;
inline Weak paused_world;
bool foreground(){return true;}
bool pause_game(UObject*,bool){return true;}
constexpr bool s_enable_runtime_hot_path_profiling=true;
template<bool>struct RuntimeConditionalProfileScope {explicit RuntimeConditionalProfileScope(const wchar_t*){}};
constexpr auto notice_path=L"notice",paint_path=L"paint";
bool request_runtime_asset_class_load(UObject*,const wchar_t*,std::wstring*,int){return true;}
bool request_runtime_asset_load(UObject*,const wchar_t*,std::wstring*,int){return true;}

struct DeleteListener {virtual void NotifyUObjectDeleted(const UObjectBase*,int32)=0;};
struct Service : DeleteListener {
    struct Assignment {bool placed{},cleared{};std::wstring map=L"world",keyblade;};
    struct Gate {Weak chest,reactor;std::wstring treasure;bool spawned{},bound{},painted{};};
    int pawn_reads{},asset_reads{},assign_reads{},discoveries{},gate_reads{},writes{},retired{},openings{};
    bool ready=true,context_ready=true,callback_ready=true,paint_ready=true,write_ok=true,bind_ok=true,assignment_ok=true;
    std::function<void()> during_bind;
    struct Config {
        Service* self;
        UObject* playable_sora(){++self->pawn_reads;return self->context_ready?&pawn:nullptr;}
        bool gameplay_ready(){return self->ready;}
        std::function<void()> chest_rescan;
    } config{this,{}};
    struct Audio {void set_muted(bool){}} audio;
    std::map<std::wstring,Assignment> assignments;
    std::map<std::wstring,Gate> gates;
    ChestDiscoveryQueue<Weak,1024,std::equal_to<Weak>> discovery;
    ChestMaintenanceWork maintenance_work,publication_work,assignment_work;
    MusicChestOpeningWork opening_work;
    RuntimeMusicChestInitialization placed_initialization;
    Weak current_world{&level};
    std::shared_ptr<const Snapshot> observed_snapshot;
    bool has_vanilla_assignments=true,shared_assets_requested{};
    UObject* retained_notice{};
    UObject* retained_paint{};
    Clock::time_point next_shared_asset_request{};
    std::wstring notice_load_state,paint_load_state;
    std::string placed_error,error;
    uint64_t applied_inventory_revision{},maintenance_passes{};
    std::atomic_bool off_thread_activation{},deleted_objects{};
    std::mutex watched_mutex;
    std::set<const UObjectBase*> watched_objects;
    void install_activation_hook(){}
    void refresh_assignments(){++assign_reads;if(!assignment_ok)throw std::runtime_error("manifest failure");}
    void retain_ready_assets(){++asset_reads;if(callback_ready)retained_notice=&pawn;if(paint_ready)retained_paint=&pawn;}
    void publish_status(){publication_work.request();}
    bool write_status(){++writes;return write_ok;}
    void reset(){gates.clear();placed_initialization={};discovery.request_scan();maintenance_work.request();}
    void retire(Gate&){++retired;}
    void advance_spawned(UObject*){++openings;opening_work.clear();publish_status();}
    void refresh_keyblade_gates(UObject*){++gate_reads;}
    void discover_vanilla(UObject*){++discoveries;}
    void discover_chest(UObject*,UObject*){++discoveries;}
    bool prepare_spawn_assets(UObject*){return true;}
    void spawn_placed(UObject*,const std::wstring&,const Assignment&){}
    uint8_t desired_command(UObject*,Gate&,const Assignment& a){++gate_reads;return a.cleared?3:4;}
    void bind(Gate& gate){if(!bind_ok)throw std::runtime_error("bind failure");gate.bound=true;if(during_bind)during_bind();}
    void paint(Gate& gate){gate.painted=true;}
#include "chest_maintenance_under_test.h"
};
}
int main(){
    using namespace kh3ap;using namespace std::chrono_literals;
    Clock::current=std::chrono::steady_clock::time_point{}+1s;
    Service service;
    UObject chest,reactor,unrelated;
    service.assignments[L"test"]={};
    service.gates[L"chest"]={&chest,&reactor,L"test"};
    service.callback_ready=service.paint_ready=false;
    service.tick();assert(service.maintenance_work.pending()&&!service.gates.at(L"chest").bound);
    auto passes=service.maintenance_passes;
    Clock::current+=100ms;service.tick();assert(service.maintenance_passes==passes);
    service.callback_ready=true;Clock::current+=1s;service.tick();
    assert(service.gates.at(L"chest").bound&&!service.gates.at(L"chest").painted);
    service.paint_ready=true;Clock::current+=1s;service.tick();
    assert(service.gates.at(L"chest").painted&&!service.maintenance_work.pending());
    auto pawns=service.pawn_reads,assets=service.asset_reads,reads=service.gate_reads,writes=service.writes;
    passes=service.maintenance_passes;
    for(int i=0;i<10000;++i){Clock::current+=16ms;service.tick();}
    assert(service.maintenance_passes==passes&&service.pawn_reads==pawns&&service.asset_reads==assets);
    assert(service.gate_reads==reads&&service.writes==writes);
    service.NotifyUObjectDeleted(&unrelated,0);service.tick();assert(service.maintenance_passes==passes);
    service.ready=false;++revision;service.tick();assert(service.applied_inventory_revision!=revision);
    service.ready=true;service.tick();assert(service.applied_inventory_revision==revision);
    service.assignments[L"test"].cleared=true;
    reconciliation::state.chests=std::make_shared<Snapshot>();service.maintenance_work.request();service.tick();
    assert(reactor.command==3);
    service.gates.at(L"chest").bound=false;
    service.during_bind=[&]{service.maintenance_work.request();};service.maintenance_work.request();service.tick();
    assert(service.maintenance_work.pending());service.during_bind={};service.tick();assert(!service.maintenance_work.pending());
    // Failed status publication retries IO only, without revisiting settled gates.
    service.write_ok=false;service.publish_status();service.tick();passes=service.maintenance_passes;
    service.write_ok=true;Clock::current+=1s;service.tick();
    assert(!service.publication_work.pending()&&service.maintenance_passes==passes);
    // Failed binding remains pending; a new activation bypasses its retry delay.
    service.gates.at(L"chest").bound=false;service.bind_ok=false;service.maintenance_work.request();service.tick();
    assert(service.maintenance_work.pending());service.bind_ok=true;service.maintenance_work.request();service.tick();
    assert(!service.maintenance_work.pending());
    chest.live=false;service.NotifyUObjectDeleted(&chest,0);service.tick();assert(service.gates.empty()&&service.retired==1);
    service.off_thread_activation=true;service.tick();assert(service.discoveries>=2);
    // Invalid context and map transitions keep unfinished setup pending.
    service.context_ready=false;service.maintenance_work.request();service.tick();assert(service.maintenance_work.pending());
    service.context_ready=true;service.current_world={};Clock::current+=1s;service.tick();assert(service.maintenance_work.pending());
    service.tick();assert(!service.maintenance_work.pending());
    // Manifest failures retry after a deadline instead of on every frame.
    service.assignment_ok=false;reconciliation::state.chests=std::make_shared<Snapshot>();service.tick();
    auto assignment_reads=service.assign_reads;Clock::current+=100ms;service.tick();assert(service.assign_reads==assignment_reads);
    service.assignment_ok=true;Clock::current+=1s;service.tick();assert(service.assign_reads==assignment_reads+1);
    service.opening_work.request(L"placed");service.tick();assert(service.openings==1);
}
