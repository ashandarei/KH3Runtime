#include "kh3ap/core/settings/pak_seed_settings.h"
#include "kh3ap/core/settings/seed_slot_options.h"
#include "kh3ap/features/story/quality_of_life_service.h"
#include "kh3ap/features/story/quality_of_life.h"
#include "kh3ap/features/story/progression/vbonus_tracking_policy.h"
#include <array>
#include <cassert>
#include <cstring>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

#define STR(text) L##text
namespace RC {
enum class LogLevel { Warning, Error };
struct Output { template<LogLevel,class... Args> static void send(Args&&...) {} };
namespace Unreal {
enum { FNAME_Add };
struct FName {
    uint64_t raw;
    FName(const wchar_t* value,int) : raw(std::hash<std::wstring_view>{}(value)) {}
};
}
}
namespace kh3ap {
constexpr bool s_enable_runtime_hot_path_profiling=true;
static int dispatches{},settings_reads{};
static bool settings_available=true,hook_failure=false;
static JsonObject seed_options{R"({"dark_baymax_phase_1_skip":true,"max_level_ship":true})"};
static const JsonObject missing_seed_options;
void publish_seed(std::string_view record) { seed_options=JsonObject(record); }
static std::set<VBonusFlagEvents*> boolean_listeners,integer_listeners;
struct RuntimeHotPathProfileScope { explicit RuntimeHotPathProfileScope(const wchar_t*) {} };
template<bool> struct RuntimeConditionalProfileScope {
    explicit RuntimeConditionalProfileScope(const wchar_t*) { ++dispatches; }
};
void subscribe_game_flags(VBonusFlagEvents& inbox,bool integer=false) {
    if(hook_failure)throw std::runtime_error("test hook failure");
    (integer?integer_listeners:boolean_listeners).insert(&inbox);
}
void unsubscribe_game_flags(VBonusFlagEvents& inbox) {
    integer_listeners.erase(&inbox);boolean_listeners.erase(&inbox);
}
uint64_t key(std::wstring_view name) { return RC::Unreal::FName(std::wstring{name}.c_str(),0).raw; }
void written(std::wstring_view name,bool integer,bool value=true) {
    for(auto* inbox:integer?integer_listeners:boolean_listeners)inbox->write(key(name),value);
}
class RuntimeState {
public:
    int bx=710;
    bool ship=false;
    std::array<int,2> reads{},writes{};
    std::array<bool,2> read_failure{},write_failure{};
    void native_bx(int value) { bx=value;written(L"gameflow_BX",true); }
    void native_ship(bool value) { ship=value;written(L"gameflow_CA_specialship_powerup",false,value); }
};
const JsonObject& installed_seed_slot_data() {
    ++settings_reads;
    return settings_available?seed_options:missing_seed_options;
}
bool should_run_periodic_update(std::chrono::steady_clock::time_point& last,
        std::chrono::steady_clock::time_point now,std::chrono::steady_clock::duration interval) {
    if(last!=std::chrono::steady_clock::time_point{}&&now-last<interval)return false;
    last=now;return true;
}
template<class... Args> bool try_call_get_game_flag(std::wstring_view,int32_t& value,RuntimeState& state,Args&&...) {
    ++state.reads[0];value=state.bx;return !state.read_failure[0];
}
template<class... Args> bool try_call_get_game_flag_boolean(std::wstring_view,bool& value,RuntimeState& state,Args&&...) {
    ++state.reads[1];value=state.ship;return !state.read_failure[1];
}
template<class... Args> bool try_call_set_game_flag(std::wstring_view,int32_t value,RuntimeState& state,Args&&...) {
    ++state.writes[0];if(state.write_failure[0])return false;state.native_bx(value);return true;
}
template<class... Args> bool try_call_set_game_flag_boolean(std::wstring_view,bool value,RuntimeState& state,Args&&...) {
    ++state.writes[1];if(state.write_failure[1])return false;state.native_ship(value);return true;
}
}
#include "quality_of_life_events_under_test.h"

int main() {
    using namespace kh3ap;
    using namespace std::chrono_literals;
    auto now=std::chrono::steady_clock::time_point{}+1s;
    auto config=[](RuntimeState& state) {
        RuntimeQualityOfLifeConfig value{state};
        value.bridge_dir_resolver=[] {return std::optional{std::filesystem::path{L"bridge"}};};
        return value;
    };
    RuntimeState state;
    {
        auto disabled=config(state);disabled.enabled=false;
        RuntimeQualityOfLifeService service(disabled);service.tick(now);
        assert(settings_reads==0&&dispatches==0);
    }
    settings_available=false;
    {
        RuntimeQualityOfLifeService service(config(state));
        service.tick(now);
        for(int i=0;i<100;++i)service.tick(now+1ms*i);
        assert(settings_reads==1&&dispatches==0);
        settings_available=true;now+=1s;service.tick(now);
        assert(state.bx==811&&state.ship&&state.writes[0]==1&&state.writes[1]==1);
        assert(boolean_listeners.size()==1&&integer_listeners.size()==1);
        service.tick(now+1ms); // Observe the service's own writes without recursion.
        const auto idle_reads=state.reads;const auto idle_dispatches=dispatches;
        for(int i=0;i<10000;++i)service.tick(now+16ms*i);
        assert(state.reads==idle_reads&&dispatches==idle_dispatches);
        written(L"unrelated_flag",true);written(L"unrelated_flag",false,false);
        service.tick(now+200s);assert(state.reads==idle_reads);
        // Zero/other integer values still notify, but only 710 maps to 811.
        state.native_bx(0);service.tick(now+201s);
        assert(state.bx==0&&state.reads[1]==idle_reads[1]);
        for(int i=0;i<10;++i)state.native_bx(710);
        service.tick(now+202s);assert(state.bx==811&&state.writes[0]==2);
        service.tick(now+202s+1ms);
        const auto before_bool=state.reads[0];
        state.native_ship(false);service.tick(now+203s);
        assert(state.ship&&state.writes[1]==2&&state.reads[0]==before_bool);
        service.tick(now+203s+1ms);
        // Save restoration can replace values without individual setter calls.
        state.bx=710;state.ship=false;
        service.request_refresh();service.request_refresh();service.tick(now+204s);
        assert(state.bx==811&&state.ship);
        service.tick(now+204s+1ms);
        // Retry only failed work; a later native event bypasses its backoff.
        state.read_failure[0]=true;state.native_bx(710);service.tick(now+205s);
        auto failed_reads=state.reads;
        service.tick(now+205s+500ms);assert(state.reads==failed_reads);
        state.read_failure[0]=false;state.native_bx(710);service.tick(now+205s+600ms);
        assert(state.bx==811&&state.reads[1]==failed_reads[1]);
        service.tick(now+205s+601ms);
        state.write_failure[1]=true;state.native_ship(false);service.tick(now+206s);
        assert(!state.ship);failed_reads=state.reads;
        service.tick(now+206s+500ms);assert(state.reads==failed_reads);
        state.write_failure[1]=false;service.tick(now+207s);assert(state.ship);
        service.tick(now+207s+1ms);
        // A service binds immutable installed-seed options once. Refreshing
        // game flags must not silently retarget it to a later provider value.
        const auto bound_settings_reads=settings_reads;
        publish_seed(R"({"dark_baymax_phase_1_skip":false,"max_level_ship":false})");
        state.bx=710;state.ship=false;service.request_refresh();service.tick(now+208s);
        assert(state.bx==811&&state.ship&&settings_reads==bound_settings_reads);
    }
    assert(boolean_listeners.empty()&&integer_listeners.empty());
    publish_seed(R"({"dark_baymax_phase_1_skip":false,"max_level_ship":false})");
    {
        RuntimeState disabled_state;RuntimeQualityOfLifeService service(config(disabled_state));
        service.tick(now);service.request_refresh();service.tick(now+1s);
        assert((disabled_state.reads==std::array<int,2>{}));
        assert(boolean_listeners.empty()&&integer_listeners.empty());
    }
    // Newly bound seeds use the actual slot-option conversion: generated
    // numeric toggles and JSON booleans select only their corresponding rule.
    publish_seed(R"({"dark_baymax_phase_1_skip":1,"max_level_ship":0})");
    {
        RuntimeState mixed_state;RuntimeQualityOfLifeService service(config(mixed_state));
        service.tick(now);
        assert(mixed_state.bx==811&&!mixed_state.ship);
        assert(mixed_state.reads[0]==1&&mixed_state.reads[1]==0);
        assert(integer_listeners.size()==1&&boolean_listeners.empty());
    }
    publish_seed(R"({"dark_baymax_phase_1_skip":false,"max_level_ship":true})");
    {
        RuntimeState mixed_state;RuntimeQualityOfLifeService service(config(mixed_state));
        service.tick(now);
        assert(mixed_state.bx==710&&mixed_state.ship);
        assert(mixed_state.reads[0]==0&&mixed_state.reads[1]==1);
        assert(integer_listeners.empty()&&boolean_listeners.size()==1);
    }
    publish_seed(R"({"dark_baymax_phase_1_skip":2,"max_level_ship":"true"})");
    {
        RuntimeState invalid_options_state;RuntimeQualityOfLifeService service(config(invalid_options_state));
        service.tick(now);
        assert((invalid_options_state.reads==std::array<int,2>{}));
        assert(integer_listeners.empty()&&boolean_listeners.empty());
    }
    publish_seed(R"({"dark_baymax_phase_1_skip":true,"max_level_ship":true})");hook_failure=true;
    {
        RuntimeState failed_state;RuntimeQualityOfLifeService service(config(failed_state));
        service.tick(now);service.tick(now+10s);
        assert((failed_state.reads==std::array<int,2>{}));
        assert(boolean_listeners.empty()&&integer_listeners.empty());
    }
    // Existing completion consumers must continue ignoring false writes.
    VBonusFlagEvents completions;completions.configure({1});completions.write(1,false);
    assert(!completions.pending());completions.write(1,true);assert(completions.pending());
}
