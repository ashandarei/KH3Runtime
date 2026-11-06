#include "kh3ap/core/events/event_work.h"
#include "kh3ap/features/moogle_shop/workshop_events.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>
using namespace std::chrono_literals;
namespace kh3ap {
struct TestClock { static inline std::chrono::steady_clock::time_point time{};static auto now(){return time;} };
struct RuntimeHotPathProfileScope { explicit RuntimeHotPathProfileScope(const wchar_t*){} };
namespace pipe {inline uint64_t incoming{};uint64_t input_generation(){return incoming;}}
struct Revision {uint64_t value{};uint64_t revision(){return value;}};
struct NativeWorkshopSnapshot {std::array<uint32_t,8> synthesis{},photos{},enhancements{};};
inline NativeWorkshopSnapshot flags;
inline bool native_ready=true;
inline int native_reads{};
bool try_query_native_workshop(NativeWorkshopSnapshot& result,void*){++native_reads;result=flags;return native_ready;}
bool bit(const std::array<uint32_t,8>& words,int n){return (words[n/32]&(1u<<(n%32)))!=0;}
struct Workshop {
    struct Config {std::function<bool()> gameplay_ready=[] {return true;};std::function<bool(std::wstring_view)> owns_item=[](auto){return true;};} config;
    struct Photos {bool ready=true;int calls{};bool update(bool){++calls;return ready;}}photo_requests;
    struct Check {std::wstring type,key,keyblade_token;int flag{};};
    std::vector<Check> checks{{L"synthesis",L"craft",L"",0},{L"photo_mission",L"photo",L"",1},{L"keyblade_upgrade",L"forge",L"key",2}};
    std::set<std::wstring> completed;
    RuntimeEventWork work;Revision mutations;
    uint64_t observed_revision{},input_generation{},object_revision{};bool max_levels{},dirty{},write_ok=true;
    int writes{},scopes{};
    void install_hooks(){}
    bool scope(){++scopes;return !checks.empty();}
    bool persist(){++writes;return write_ok;}
#include "workshop_remaining_under_test.h"
};
struct Party {
    RuntimeEventWork maintenance_;
    std::unique_ptr<Revision> navigation_hooks_=std::make_unique<Revision>();
    uint64_t navigation_revision_{};int nav_reads{},placement_reads{};bool placed=true;
    bool maintain_party_navigation(){++nav_reads;return true;}
    bool maintain_party_placement(){++placement_reads;return placed;}
#include "party_remaining_under_test.h"
};
struct UObject {UObject* outer{};bool live=true;std::array<float,3> location{};std::wstring path;std::wstring GetPathName(){return path;}UObject* GetOuterPrivate(){return outer;}};
bool is_uobject_valid_fast(UObject* o){return o&&o->live;}
UObject* try_read_reflected_object_property_value(UObject* o,const wchar_t*){return o->outer;}
inline std::vector<UObject*> level_actors;
inline int actor_reads{};inline bool actors_ready=true;
auto authored_level_actors(UObject*,UObject*) -> std::optional<std::vector<UObject*>> {++actor_reads;if(!actors_ready)return {};return level_actors;}
auto authored_location(UObject* o){return std::optional{o->location};}
namespace color_objects {
struct Snapshot {std::vector<UObject*> pawns;uint64_t generation{};bool unfinished{};};
inline Snapshot current;inline int snapshots{};
uint64_t generation(){return current.generation;}
Snapshot snapshot(){++snapshots;return current;}
}
inline bool weapons_ready=true;inline int weapon_updates{};
bool runtime_color_apply_equipped_weapons(int,int,int){++weapon_updates;return weapons_ready;}
struct Colors {
    uint64_t pawn_generation_{~uint64_t{0}};bool pawn_discovery_pending_{true},weapons_dirty_{true};
    std::chrono::steady_clock::time_point pawn_retry_at_{},weapon_retry_at_{};
    std::vector<UObject*> queued;int state{};
    static UObject* resolve_persistent_level(UObject* o){return o?o->outer:nullptr;}
    void maybe_queue(UObject* o,std::chrono::steady_clock::time_point){queued.push_back(o);}
    int seed_identity(){return 0;}int harmony_mode(){return 0;}
#include "color_discovery_remaining_under_test.h"
#include "color_weapons_remaining_under_test.h"
};
struct Coordinates {
    struct Actor {std::wstring path;};struct Source {std::wstring actors;};
    std::vector<Actor> ingredient_spot_actors;std::vector<Source> adversary_journal_sources;
    bool actors_indexed=true;
    std::map<std::wstring,std::set<std::wstring>> package_actors;
    std::map<std::wstring,std::array<float,3>> positions;std::set<std::wstring> cached_packages;
    static std::wstring package(std::wstring_view){return {};}
    static std::vector<std::wstring> split(std::wstring_view,wchar_t){return {};}
#include "coordinates_remaining_under_test.h"
};
}
int main(){using namespace kh3ap;
    Workshop w;w.tick();assert(native_reads==1&&w.completed.empty());
    for(int i=0;i<10000;++i){TestClock::time+=1s;w.tick();}
    assert(native_reads==1&&w.scopes==1&&w.photo_requests.calls==1);
    flags.synthesis[0]=1;flags.photos[0]=2;flags.enhancements[0]=4;++w.mutations.value;
    w.write_ok=false;w.tick();assert(w.completed.size()==3&&w.dirty);
    w.tick();assert(w.writes==1);TestClock::time+=250ms;w.write_ok=true;w.tick();assert(w.writes==2&&!w.dirty);
    const auto reads=native_reads;for(int i=0;i<1000;++i){TestClock::time+=1s;w.tick();}assert(native_reads==reads);
    ++pipe::incoming;w.tick();assert(native_reads==reads+1); // ownership/seed updates wake max-level checks
    const auto grant_reads=native_reads;
    // Receipt ingestion was already reconciled. A later successful native
    // setter/direct write must still wake us without a new pipe generation.
    notify_workshop_grant_applied(L"progressive_keyblade_level",L"KEYBLADE::Kingdom Key");
    w.tick();assert(native_reads==grant_reads+1);
    notify_workshop_grant_applied(L"bp_tres_get_item",L"WEP_KEYBLADE_SO_00");
    w.tick();assert(native_reads==grant_reads+2);
    notify_workshop_grant_applied(L"ultima_weapon_shard",L"KH3AP_ULTIMA_SHARD");
    w.tick();assert(native_reads==grant_reads+3);
    notify_workshop_grant_applied(L"bp_tres_get_item",L"ITEM_POTION");
    w.tick();assert(native_reads==grant_reads+3);
    w.photo_requests.ready=false;w.work.request();w.tick();auto calls=w.photo_requests.calls;
    w.tick();assert(w.photo_requests.calls==calls);TestClock::time+=250ms;w.photo_requests.ready=true;w.tick();
    Party p;p.tick_maintenance(TestClock::now());assert(p.nav_reads==1);
    for(int i=0;i<10000;++i){TestClock::time+=1s;p.tick_maintenance(TestClock::now());}assert(p.nav_reads==1);
    ++p.navigation_hooks_->value;p.placed=false;p.tick_maintenance(TestClock::now());assert(p.nav_reads==2);
    p.tick_maintenance(TestClock::now());assert(p.nav_reads==2);
    TestClock::time+=250ms;p.placed=true;p.tick_maintenance(TestClock::now());assert(p.nav_reads==3);
    for(int i=0;i<1000;++i){TestClock::time+=1s;p.tick_maintenance(TestClock::now());}assert(p.nav_reads==3);
    UObject level,other_level,sora{&level},friend_pawn{&level},other{&other_level};
    Colors colors;color_objects::current={{&friend_pawn,&other},1,true};colors.discover_pawns(&sora,TestClock::now());assert(colors.queued.size()==1);
    for(int i=0;i<1000;++i)colors.discover_pawns(&sora,TestClock::now());assert(color_objects::snapshots==1);
    color_objects::current.unfinished=false;TestClock::time+=100ms;colors.discover_pawns(&sora,TestClock::now());assert(color_objects::snapshots==2);
    for(int i=0;i<10000;++i){TestClock::time+=1s;colors.discover_pawns(&sora,TestClock::now());}assert(color_objects::snapshots==2);
    ++color_objects::current.generation;colors.discover_pawns(&sora,TestClock::now());assert(color_objects::snapshots==3);
    weapons_ready=false;colors.color_equipped_weapons(TestClock::now());colors.color_equipped_weapons(TestClock::now());assert(weapon_updates==1);
    TestClock::time+=100ms;weapons_ready=true;colors.color_equipped_weapons(TestClock::now());assert(weapon_updates==2);
    for(int i=0;i<10000;++i){TestClock::time+=1s;colors.color_equipped_weapons(TestClock::now());}assert(weapon_updates==2);
    UObject world{&level},actor{&level};actor.location={1,2,3};Coordinates coords;
    coords.package_actors[L"map"]={L"target",L"wrong_level",L"missing"};actor.path=L"target";other.path=L"wrong_level";level_actors={&actor,&other};
    assert(coords.cache_package(L"map",&world));assert(coords.positions.size()==1&&coords.positions[L"target"]==actor.location);
    assert(actor_reads==1&&coords.cached_packages.contains(L"map"));
    actors_ready=false;assert(!coords.cache_package(L"unloaded",&world) || !coords.package_actors.contains(L"unloaded"));
    coords.package_actors[L"pending"]={L"target"};assert(!coords.cache_package(L"pending",&world)&&!coords.cached_packages.contains(L"pending"));
}
