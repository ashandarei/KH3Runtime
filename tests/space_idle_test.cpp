#include "kh3ap/features/world/space/space_refresh_state.h"
#include "kh3ap/features/world/space/space_ship_policy.h"
#include <cassert>
#include <filesystem>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace kh3ap {
namespace space {
bool is_space(const std::wstring& world) { return world==L"space"; }
}
struct RuntimeHotPathProfileScope { explicit RuntimeHotPathProfileScope(const wchar_t*) {} };
static int writes{}, moves{};
static bool write_ok=true, move_ok=true;
static std::string temporary, visible;
unsigned GetCurrentProcessId() { return 42; }
std::string json_escape(const std::wstring& value) { return {value.begin(),value.end()}; }
std::string now_iso8601_utc() { return "2026-10-17T22:00:00Z"; }
bool write_text_file(const std::filesystem::path&,const std::string& text) {
    ++writes;
    if(write_ok)temporary=text;
    return write_ok;
}
constexpr int MOVEFILE_REPLACE_EXISTING=1;
bool MoveFileExW(const wchar_t*,const wchar_t*,int) {
    ++moves;
    if(move_ok)visible=temporary;
    return move_ok;
}
struct RuntimeSpaceService {
    struct Impl {
        inline static Impl* active{};
        struct Access {
            std::filesystem::path bridge{L"bridge"};
            bool access_ready=true, include_space=false;
            std::wstring seed{L"seed"};
            std::set<std::wstring> allowed{L"kg"};
        } access;
        struct Ships {
            bool inventory_verified=true;
            unsigned unlocked_zones(const Access&) const { return 7; }
            void write_status(std::ostream& out,const Access& a) const {
                out<<"{\"required\":"<<(a.include_space?"true":"false")<<'}';
            }
        } ships;
        struct Rewards {
            unsigned recoveries{};
            bool active() const { return false; }
            void request_recovery() { ++recoveries; }
        } rewards;
        struct Notifications {
            unsigned count{};
            unsigned shown() const { return count; }
            unsigned pending() const { return 0; }
        } notifications;
        struct Landing {
            void write_status(std::ostream& out) const { out<<"{}"; }
        } landing_ui;
        std::wstring current_world{L"land"};
        bool presentation_ready{}, loading_closed=true, controller_ready{}, status_pending=true;
        bool access_refresh_pending{}, ships_refresh_pending{};
        void* delivery_controller{};
        space::RefreshRequest context_refresh, flight_refresh, overview_refresh;
        std::chrono::steady_clock::time_point context_deadline{};
        std::string published_status;
        uint64_t context_samples{}, presentation_attempts{}, status_publications{};
        uint64_t travel_points_enabled{},world_fast_travel_enabled{},overview_refreshes{};
        uint64_t flight_visibility_updates{},flight_discoveries{},redirected_saves{},redirected_jumps{};
        uint64_t denied_landings{},denied_ship_travel{},mission_events{};
        std::vector<int> flight_symbols;
        static void write_flight_symbols(std::ostream& out,const std::vector<int>&) { out<<"[]"; }
        void publish_status();
    };
    static void notify_context_changed();
    static void notify_map_ready();
};
}
#include "space_idle_under_test.h"

int main() {
    using kh3ap::RuntimeSpaceService;
    using namespace kh3ap;
    RuntimeSpaceService::Impl service;
    RuntimeSpaceService::Impl::active=&service;
    service.publish_status();
    assert(writes==1&&moves==1&&service.status_publications==1&&!service.status_pending);
    const auto original=visible;
    // Repeated observations must not become repeated physical publications.
    for(int i=0;i<1000;++i) {
        ++service.context_samples; ++service.presentation_attempts;
        service.status_pending=true;
        service.publish_status();
    }
    assert(writes==1&&moves==1&&visible==original&&!service.status_pending);
    service.presentation_ready=true;
    service.publish_status();
    assert(writes==2&&service.status_publications==2);
    assert(visible.find("\"context_samples\":1000")!=visible.npos);
    assert(visible.find("\"inventory_delivery_ready\":true")!=visible.npos);
    service.notifications.count=1;
    service.publish_status();
    assert(writes==3&&service.status_publications==3);

    // Neither failed temp writes nor failed atomic replacement commit state.
    const auto last=service.published_status;
    service.current_world=L"space";
    write_ok=false;
    service.publish_status();
    assert(service.status_pending&&service.published_status==last&&service.status_publications==3);
    write_ok=true;move_ok=false;
    service.publish_status();
    assert(service.status_pending&&service.published_status==last&&service.status_publications==3);
    service.current_world=L"new_land"; // A newer event while publication retries.
    move_ok=true;
    service.publish_status();
    assert(!service.status_pending&&service.status_publications==4);
    assert(visible.find("new_land")!=visible.npos);

    // Production map notification must not arm impossible vanilla ship work.
    service.ships_refresh_pending=true;
    RuntimeSpaceService::notify_map_ready();
    assert(!service.ships_refresh_pending&&!service.ships.inventory_verified);
    assert(service.rewards.recoveries==1&&service.status_pending);
    auto now=std::chrono::steady_clock::now();
    assert(service.context_refresh.take(now));
    assert(service.flight_refresh.take(now)&&service.overview_refresh.take(now));
    // Enabled seeds and startup before access delivery still wake correctly.
    service.access.include_space=true;
    RuntimeSpaceService::notify_map_ready();
    assert(service.ships_refresh_pending&&service.context_refresh.take(now));
    service.access.bridge.clear();
    RuntimeSpaceService::notify_context_changed();
    assert(service.access_refresh_pending&&service.context_refresh.take(now));
    RuntimeSpaceService::Impl::active=nullptr;
    RuntimeSpaceService::notify_map_ready();
}
