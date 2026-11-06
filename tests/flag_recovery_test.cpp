#include <cassert>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace pipe {
uint64_t generation{};
auto input_generation() { return generation; }
auto input_revision(const std::filesystem::path&) -> uint64_t { return 0; }
}
namespace RC {
enum class LogLevel { Warning, Error };
struct Output { template<LogLevel, class... T> static void send(T&&...){} };
}
constexpr auto config_file=L"slot_data.json";
auto read_text_file(const std::filesystem::path&) -> std::optional<std::string> { return {}; }
struct Inbox {
    std::set<uint64_t> names;
    auto pending() -> bool { return !names.empty(); }
    auto take() { auto copy=names;names.clear();return copy; }
};
struct FakeBoundaries {
    uint64_t generation{},config_revision{},recovery_scans{},reads{},writes{};
    bool initialized{true},failed{},dirty{},recover{},recovering{},throw_read{};
    std::wstring recovery_after;
    std::filesystem::path bridge{"test"};
    Inbox inbox,integer_inbox;
    std::map<std::wstring,int> flags;
    std::map<uint64_t,std::vector<std::wstring>> watched;
    std::set<std::wstring> completed;
    auto resolver() -> std::optional<std::filesystem::path> { return bridge; }
    void configure(const std::filesystem::path&,const std::string&) { assert(false); }
    void record(const std::wstring& id,const wchar_t*) {
        if(completed.contains(id))return;
        ++reads;
        if(throw_read)throw std::runtime_error("flag_unavailable");
        if(flags.at(id)) { completed.insert(id);dirty=true; }
    }
    void persist() { ++writes;dirty=false; }
};
struct VBonus : FakeBoundaries {
#include "vbonus_recovery_under_test.h"
};
struct Event : FakeBoundaries {
#include "event_recovery_under_test.h"
};
template<class Service> void check() {
    Service service;
    for(int i=0;i<101;++i)service.flags[L"check_"+std::to_wstring(i)]=0;
    service.flags[L"zz_last"]=1;
    service.recover=true;
    service.process();
    assert(service.recovery_scans==1&&service.reads==16&&service.recovering);
    // A native completion at the end of the catalog is immediate, even while
    // the bounded initial recovery is still near its beginning.
    service.watched[99]={L"zz_last"};service.inbox.names.insert(99);
    auto before=service.reads;service.process();
    assert(service.reads<=before+17&&service.completed.contains(L"zz_last"));
    while(service.recovering) {
        before=service.reads;service.process();assert(service.reads<=before+16);
    }
    assert(service.recovery_scans==1&&service.reads==102);
    before=service.reads;auto writes=service.writes;
    for(int i=0;i<10000;++i)service.process();
    assert(service.reads==before&&service.writes==writes); // No recurring recovery scan.
    service.recover=true;service.process();assert(service.recovery_scans==2&&service.recovering);
    service.throw_read=true;service.process();
    assert(service.failed&&!service.recovering&&!service.recover);
    before=service.reads;for(int i=0;i<100;++i)service.process();assert(service.reads==before);
    // Explicit readiness can recover a failed service; the queue restarts and
    // still finishes rather than retaining an invalid iterator or stale cursor.
    service.failed=false;service.throw_read=false;service.recover=true;
    service.process();assert(service.recovery_scans==3&&service.recovering);
    while(service.recovering)service.process();
    assert(!service.failed&&service.completed.size()==1);
}
int main() {
    check<VBonus>();check<Event>();
    Event event;
    event.flags[L"integer_condition"]=1;
    event.watched[1]={L"integer_condition"};event.integer_inbox.names.insert(1);
    event.process();assert(event.completed.contains(L"integer_condition"));
}
