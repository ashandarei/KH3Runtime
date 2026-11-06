#include "kh3ap/core/events/event_work.h"
#include "kh3ap/features/story/minigames/beat_of_lead_completion.h"
#include "kh3ap/features/story/minigames/union_cross_completion.h"
#include "kh3ap/features/story/cutscenes/guardians_of_light_skip.h"
#include <cassert>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>
#define STR(value) L##value
namespace RC {
enum class LogLevel {Normal,Warning,Error};
struct Output {template<LogLevel,class... T>static void send(T&&...) {}};
}
namespace kh3ap {
using RC::Output;using RC::LogLevel;
struct TestClock {static inline std::chrono::steady_clock::time_point time{};static auto now(){return time;}};
using EObjectFlags=int;
constexpr int RF_NeedLoad=1,RF_NeedPostLoad=2,RF_NeedPostLoadSubobjects=4;
constexpr int RF_ClassDefaultObject=8,RF_ArchetypeObject=16,unfinished_component_flags=31;
struct UObject {
    bool live=true,repeatable{},started{},recorded{},available=true;
    int score{},flags{},id=1;UObject* type{};
    bool HasAnyFlags(int value){return (flags&value)!=0;}
    UObject* GetClassPrivate(){return type;}
};
struct Weak {
    UObject* obj{};Weak()=default;Weak(std::nullptr_t){};Weak(UObject* value):obj(value){}
    UObject* Get()const{return obj&&obj->live?obj:nullptr;}
};
struct BoolProperty {int reads{};bool GetPropertyValueInContainer(UObject* o){++reads;return o->started;}};
struct MinigameProperty {int reads{};bool GetPropertyValueInContainer(UObject* o){++reads;return o->repeatable;}};
struct Hook {
    std::function<void()> wake;bool supported=true;
    bool install(const wchar_t*,int,std::function<void()> callback){wake=std::move(callback);return supported;}
    void fire(){if(wake)wake();}
};
std::wstring object_name_without_scan(UObject*){return L"object";}
std::wstring format_object_debug_name(UObject*){return L"object";}
std::wstring truncate_for_log(std::wstring value,int){return value;}
bool is_uobject_valid_fast(UObject* o){return o&&o->live;}
uint64_t manager_identity(UObject* o){return reinterpret_cast<uintptr_t>(o);}
uint64_t actor_identity(UObject* o){return reinterpret_cast<uintptr_t>(o);}
int score_reads{};
std::optional<int32_t> try_read_score(UObject* o,int*){++score_reads;return o->available?std::optional{o->score}:std::nullopt;}
std::optional<bool> try_read_started(UObject* o,BoolProperty* p){return o->available?std::optional{p->GetPropertyValueInContainer(o)}:std::nullopt;}
enum class BeatOfLeadCompletionDispatchResult {Dispatched,PreparationFailed,DispatchThrew};
enum class UnionCrossCompletionDispatchResult {Dispatched,PreparationFailed,DispatchThrew};
struct Beat {
    struct Config {bool enabled=true;}config;
    bool hooks_attempted{},hooks_ready{},reflection_failure_reported{},prepare=true;
    Hook score_hook,total_hook;RuntimeEventWork work;
    Weak active_manager;
    MinigameProperty* minigame_property{};int* score_property{};
    RuntimeBeatOfLeadCompletionTracker completion_tracker;
    int dispatches{};
    auto dispatch_story_completion(UObject*){
        ++dispatches;return prepare?BeatOfLeadCompletionDispatchResult::Dispatched:BeatOfLeadCompletionDispatchResult::PreparationFailed;
    }
#include "beat_events_under_test.h"
};
struct Union {
    struct Config {bool enabled=true;}config;
    bool hook_attempted{},hook_ready{},reflection_failure_reported{},prepare=true;
    Hook start_hook;RuntimeEventWork work;
    Weak active_actor,active_actor_class;BoolProperty* started_property{};
    RuntimeUnionCrossStartTracker start_tracker;
    int dispatches{};
    auto dispatch_final_completion(UObject*){
        ++dispatches;return prepare?UnionCrossCompletionDispatchResult::Dispatched:UnionCrossCompletionDispatchResult::PreparationFailed;
    }
#include "union_events_under_test.h"
};
namespace reconciliation {
struct View {
    bool ready = true;
    std::wstring seed = L"seed";
    int64_t slot = 1;
};
inline int64_t generation{};
inline std::shared_ptr<const View> snapshot = std::make_shared<const View>();
int64_t epoch(){return generation;}
auto view(){return snapshot;}
}
inline uint64_t adversary_generation{};
uint64_t read_adversary_revision(){return adversary_generation;}
template<bool>struct RuntimeConditionalProfileScope {explicit RuntimeConditionalProfileScope(const wchar_t*){}};
struct Journal {
    uint64_t incoming{},collected_revision{},adversary_revision{};int64_t observed_epoch{-1};
    struct Config {std::function<uint64_t()> collected_revision;}config{[this]{return incoming;}};
    RuntimeEventWork preload_work,page_work;
    std::shared_ptr<const reconciliation::View> gate_view;
    bool app_gates{};
    bool movie_complete{},preload_ready{},page_ready=true;
    int preloads{},updates{};
    void preload(std::chrono::steady_clock::time_point){++preloads;movie_complete=preload_ready;}
    bool update_page(){++updates;return page_ready;}
#include "journal_events_under_test.h"
};
inline std::atomic<uint64_t> photo_generation{};
namespace lucky_marks {
inline uint64_t reward_generation{};
uint64_t revision(){return reward_generation;}
}
struct LuckyEmblemPersistence {
    bool native_ready = true;
    bool fail_reconcile{};
    bool fail_observer{};
    int observer_installs{};
    int reconciles{};
    std::filesystem::path last_bridge;
    std::wstring last_seed;
    int64_t last_slot{};
    UObject* last_pawn{};

    void install_observer()
    {
        ++observer_installs;
        if (fail_observer)
        {
            throw std::runtime_error("test_photo_observer_failed");
        }
    }

    static uint64_t photo_revision(){return photo_generation.load();}

    bool reconcile(const std::filesystem::path& bridge, std::wstring_view seed,
                   int64_t slot, UObject* pawn)
    {
        ++reconciles;
        last_bridge = bridge;
        last_seed = seed;
        last_slot = slot;
        last_pawn = pawn;
        if (fail_reconcile)
        {
            throw std::runtime_error("test_native_emblem_state_failed");
        }
        return native_ready && !seed.empty() && slot > 0 && is_uobject_valid_fast(pawn);
    }
};
int emblem_reads{};
std::optional<bool> query_lucky_emblem_recorded(UObject* o,std::wstring_view){++emblem_reads;return o->available?std::optional{o->recorded}:std::nullopt;}
struct Emblem {
    struct Tracked {Weak component;int mark_id{};std::optional<bool> last_recorded;bool identity_failure_logged{},query_failure_logged{};};
    using TrackedComponent = Tracked;
    std::vector<Tracked> components;
    std::set<int32_t> captured_ids;std::optional<int32_t> latest_capture_id;
    std::optional<std::filesystem::path> active_bridge_dir;
    UObject native_pawn;
    struct Config {
        bool enabled = true;
        std::chrono::steady_clock::duration retry_interval = std::chrono::milliseconds(500);
        std::wstring recorded_function_name;
        std::function<UObject*()> playable_pawn_resolver;
    } config{true, std::chrono::milliseconds(500), {}, [this]{return &native_pawn;}};
    LuckyEmblemPersistence persistence;
    RuntimeEventWork work;bool photo_events_attempted{},persistence_failed{},photo_pending{},dirty{},write_ok=true;
    int64_t observed_epoch{-1};uint64_t observed_photo_generation{},observed_reward_revision{},sequence{};int writes{};
    std::optional<std::filesystem::path> resolve_bridge_dir(){return std::filesystem::path{"bridge"};}
    bool write_state(const std::filesystem::path&){++writes;return write_ok;}
    bool resolve_mark_id(Tracked& t,UObject* o){if(!o->id)return false;t.mark_id=o->id;return o->id>0;}
#include "emblem_events_under_test.h"
};
constexpr bool s_enable_guardians_of_light_slow_mickey_skip=true,s_debug_logging=false;
constexpr auto s_set_game_flag_path=L"set",s_get_game_flag_path=L"get",s_tres_game_blueprint_library_default_object_path=L"library";
int guardian_writes{};bool guardian_ok=true;
template<class... Args>bool try_call_set_game_flag(std::wstring_view,int32_t,Args&&...){++guardian_writes;return guardian_ok;}
template<class... Args>bool try_call_get_game_flag(std::wstring_view,int32_t& value,Args&&...){value=21400;return true;}
struct Guardians {
    struct Context {std::wstring world,outer,object;};
    std::optional<Context> context;
    int frame_reads{};
    struct Config {int runtime_state{};std::function<std::optional<Context>()> frame_context_resolver;std::function<bool()> guardians_and_mickey_walk_skip_enabled_resolver;}config{0,[this]{++frame_reads;return context;},[]{return true;}};
    RuntimeEventWork guardians_work;
    RuntimeGuardiansOfLightSkipEntryState guardians_entry_state;
    bool guardians_write_failure_logged{};
    std::wstring runtime_context(){return L"context";}
#include "guardians_events_under_test.h"
};
}
int main(){
    using namespace kh3ap;using namespace std::chrono_literals;
    TestClock::time=std::chrono::steady_clock::time_point{}+1s;
    RuntimeEventWork work;assert(work.take(TestClock::now()));work.finish(TestClock::now(),false);
    assert(!work.take(TestClock::now()+100ms));work.request();assert(work.take(TestClock::now()+1ms));
    work.request();work.finish(TestClock::now(),true);assert(work.take(TestClock::now()));
    auto wake=work.notifier();std::thread producer([&]{for(int i=0;i<1000;++i)wake();});producer.join();
    assert(work.take(TestClock::now()));assert(!work.take(TestClock::now()));
    work.request();assert(work.take(TestClock::now()));work.request();work.finish(TestClock::now(),false);
    assert(work.take(TestClock::now()+1ms));
    UObject manager,klass;manager.type=&klass;MinigameProperty minigame;int property{};
    Beat beat;beat.active_manager=&manager;beat.minigame_property=&minigame;beat.score_property=&property;
    beat.tick();auto before=score_reads;
    for(int i=0;i<10000;++i)beat.tick();assert(score_reads==before&&beat.dispatches==0);
    manager.score=1;manager.repeatable=true;beat.score_hook.fire();beat.tick();assert(beat.dispatches==0);
    manager.repeatable=false;beat.prepare=false;beat.score_hook.fire();beat.tick();assert(beat.dispatches==1);
    beat.tick();assert(beat.dispatches==1);beat.prepare=true;TestClock::time+=250ms;beat.tick();assert(beat.dispatches==2);
    beat.score_hook.fire();beat.tick();assert(beat.dispatches==2);
    Beat unsupported;unsupported.score_hook.supported=false;unsupported.tick();before=score_reads;
    for(int i=0;i<10000;++i)unsupported.tick();assert(score_reads==before);
    manager.started=false;BoolProperty started;Union ride;ride.active_actor=&manager;ride.active_actor_class=&klass;ride.started_property=&started;
    ride.tick();before=started.reads;for(int i=0;i<10000;++i)ride.tick();assert(started.reads==before);
    manager.started=true;ride.start_hook.fire();ride.tick();assert(ride.dispatches==1);
    ride.start_hook.fire();ride.tick();assert(ride.dispatches==1);
    ride.reset();ride.active_actor=&manager;ride.active_actor_class=&klass;ride.started_property=&started;
    manager.live=false;ride.tick();assert(ride.dispatches==1);manager.live=true;
    Journal journal;journal.tick();assert(!journal.movie_complete);before=journal.preloads;
    journal.tick();assert(journal.preloads==before);journal.preload_ready=true;TestClock::time+=100ms;journal.tick();
    before=journal.updates;for(int i=0;i<10000;++i)journal.tick();assert(journal.updates==before);
    ++journal.incoming;journal.tick();assert(journal.updates==before+1);
    ++adversary_generation;journal.tick();assert(journal.updates==before+2);
    journal.page_ready=false;journal.page_work.request();journal.tick();auto attempts=journal.updates;journal.tick();assert(journal.updates==attempts);
    journal.page_ready=true;journal.page_work.request();journal.tick();assert(journal.updates==attempts+1);
    journal.app_gates = true;
    before = journal.updates;
    reconciliation::snapshot = std::make_shared<const reconciliation::View>();
    journal.tick();
    assert(journal.updates == before + 1);
    before = journal.updates;
    for (int i = 0; i < 10000; ++i)
    {
        journal.tick();
    }
    assert(journal.updates == before);
    journal.app_gates = false;
    reconciliation::snapshot = std::make_shared<const reconciliation::View>();
    journal.tick();
    assert(journal.updates == before);
    Emblem emblems;UObject detector;emblems.components.push_back({&detector});emblems.tick(TestClock::now());
    before=emblem_reads;for(int i=0;i<10000;++i)emblems.tick(TestClock::now()+i*16ms);assert(emblem_reads==before);
    detector.recorded=true;++photo_generation;emblems.tick(TestClock::now());assert(emblems.captured_ids.contains(1)&&emblems.latest_capture_id==1);
    emblems.write_ok=false;emblems.dirty=true;emblems.work.request();emblems.tick(TestClock::now());before=emblems.writes;
    emblems.tick(TestClock::now()+100ms);assert(emblems.writes==before);emblems.write_ok=true;emblems.tick(TestClock::now()+500ms);assert(!emblems.dirty);
    detector.live=false;++photo_generation;emblems.tick(TestClock::now()+1s);assert(emblems.components.empty());
    Emblem templates;UObject cdo,archetype,loading;
    cdo.flags=RF_ClassDefaultObject;archetype.flags=RF_ArchetypeObject;loading.flags=RF_NeedPostLoad;
    templates.components={{&cdo},{&archetype},{&loading}};templates.tick(TestClock::now());
    assert(templates.components.size()==1&&templates.work.pending());
    loading.flags=0;templates.tick(TestClock::now()+500ms);assert(!templates.work.pending());
    before=emblem_reads;
    for(int i=0;i<10000;++i)templates.tick(TestClock::now()+1s+i*16ms);
    assert(emblem_reads==before);
    Emblem unsupported_emblem;UObject extra;extra.id=-1;
    unsupported_emblem.components={{&extra}};unsupported_emblem.tick(TestClock::now());
    assert(unsupported_emblem.components.empty()&&!unsupported_emblem.work.pending());

    // Persistence may wait for a playable pawn without turning into an idle
    // per-frame read. Once ready, the existing retry must stop completely.
    Emblem waiting;
    waiting.config.playable_pawn_resolver = []{return nullptr;};
    auto now = TestClock::now();
    waiting.tick(now);
    assert(waiting.persistence.reconciles == 1 && waiting.work.pending());
    waiting.tick(now + 100ms);
    assert(waiting.persistence.reconciles == 1);
    waiting.config.playable_pawn_resolver = [&]{return &waiting.native_pawn;};
    waiting.tick(now + 500ms);
    assert(waiting.persistence.reconciles == 2 && !waiting.work.pending());
    assert(waiting.persistence.last_bridge == std::filesystem::path{"bridge"});
    assert(waiting.persistence.last_seed == L"seed" && waiting.persistence.last_slot == 1);
    assert(waiting.persistence.last_pawn == &waiting.native_pawn);
    for (int i = 0; i < 10000; ++i)
    {
        waiting.tick(now + 1s + i * 16ms);
    }
    assert(waiting.persistence.reconciles == 2);

    // Unready AP state must defer reconciliation rather than pass an empty
    // identity into native persistence, then recover on the bounded retry.
    Emblem unbound;
    auto unready = std::make_shared<reconciliation::View>();
    unready->ready = false;
    reconciliation::snapshot = unready;
    unbound.tick(now);
    assert(unbound.persistence.reconciles == 0 && unbound.work.pending());
    reconciliation::snapshot = std::make_shared<const reconciliation::View>();
    unbound.tick(now + 500ms);
    assert(unbound.persistence.reconciles == 1 && !unbound.work.pending());

    // A persistence exception is latched to avoid repeated failing native
    // reads. A reward revision or delivery epoch explicitly re-arms it.
    Emblem failed;
    failed.persistence.fail_reconcile = true;
    failed.tick(now);
    assert(failed.persistence_failed && failed.persistence.reconciles == 1);
    assert(!failed.work.pending());
    for (int i = 0; i < 10000; ++i)
    {
        failed.tick(now + 1s + i * 16ms);
    }
    assert(failed.persistence.reconciles == 1);
    failed.persistence.fail_reconcile = false;
    ++lucky_marks::reward_generation;
    failed.tick(now + 200s);
    assert(!failed.persistence_failed && failed.persistence.reconciles == 2);
    assert(!failed.work.pending());
    failed.persistence.fail_reconcile = true;
    ++lucky_marks::reward_generation;
    failed.tick(now + 201s);
    assert(failed.persistence_failed && failed.persistence.reconciles == 3);
    failed.persistence.fail_reconcile = false;
    ++reconciliation::generation;
    failed.tick(now + 202s);
    assert(!failed.persistence_failed && failed.persistence.reconciles == 4);

    // A photo notification survives a temporarily unfinished detector and
    // reports the transition once that same detector can be read.
    Emblem pending_photo;
    UObject delayed_detector;
    pending_photo.components.push_back({&delayed_detector});
    pending_photo.tick(now);
    delayed_detector.flags = RF_NeedLoad;
    delayed_detector.recorded = true;
    ++photo_generation;
    pending_photo.tick(now);
    assert(pending_photo.photo_pending && pending_photo.work.pending());
    assert(!pending_photo.latest_capture_id);
    delayed_detector.flags = 0;
    pending_photo.tick(now + 500ms);
    assert(!pending_photo.photo_pending && !pending_photo.work.pending());
    assert(pending_photo.latest_capture_id == delayed_detector.id);
    pending_photo.reset_map_lifecycle();
    assert(pending_photo.components.empty() && pending_photo.work.pending());

    Guardians guardians;guardians.context=Guardians::Context{L"/Game/Levels/kg/kg_01/",{}, {}};guardians.process_guardians_of_light_skip(TestClock::now());
    before=guardians.frame_reads;
    assert(guardian_writes==0);for(int i=0;i<10000;++i)guardians.process_guardians_of_light_skip(TestClock::now()+i*16ms);assert(guardian_writes==0&&guardians.frame_reads==before);
    guardians.context->world=L"/Game/Levels/bt_DLC/bt_07/";guardians.guardians_work.request();guardian_ok=false;guardians.process_guardians_of_light_skip(TestClock::now());assert(guardian_writes==1);
    guardians.process_guardians_of_light_skip(TestClock::now()+500ms);assert(guardian_writes==1);
    guardian_ok=true;guardians.process_guardians_of_light_skip(TestClock::now()+1s);assert(guardian_writes==2);
    guardians.guardians_work.request();guardians.process_guardians_of_light_skip(TestClock::now()+2s);assert(guardian_writes==2);
    guardians.context->world=L"normal";guardians.guardians_work.request();guardians.process_guardians_of_light_skip(TestClock::now()+3s);
    guardians.context->world=L"/Game/Levels/bt_DLC/bt_07/";guardians.guardians_work.request();guardians.process_guardians_of_light_skip(TestClock::now()+4s);assert(guardian_writes==3);
}
