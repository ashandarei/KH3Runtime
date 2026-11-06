#include "kh3ap/features/grants/delivery/item_delivery_wake.h"
#include "kh3ap/features/player/keyblades/keyblade_style_schedule.h"
#include <cassert>
#include <chrono>
#ifdef _WIN32
#include "kh3ap/bridge/transport/request_file_watcher.h"
#include <fstream>
#include <thread>
#endif

int main() {
    using namespace std::chrono;
    kh3ap::item_delivery::DeliveryWakeSchedule wake;
    const auto now=steady_clock::now();
    kh3ap::RuntimeKeybladeStyleSchedule style;
    assert(!style.has_pending_settle());
    assert(style.take(now,true));
    assert(style.has_pending_settle());
    assert(!style.take(now+milliseconds(499),false));
    assert(style.take(now+milliseconds(500),false));
    assert(!style.has_pending_settle());
    assert(!wake.should_process(now,false));
    assert(!wake.should_process(now+hours(1),false));
    assert(wake.should_process(now,true));
    wake.set_pending(true);
    assert(!wake.should_process(now+milliseconds(999),false));
    assert(wake.should_process(now+seconds(1),false));
    wake.set_pending(false);
    assert(!wake.should_process(now+seconds(30),false));
    wake.request();
    assert(wake.should_process(now+seconds(30),false));
#ifdef _WIN32
    const auto root=std::filesystem::temp_directory_path()/
        ("KH3AP-wake-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));
    std::filesystem::create_directories(root);
    {
        kh3ap::RequestFileWatcher watcher{L"command.json"};
        assert(!watcher.update(root));
        std::ofstream(root/L"status.json")<<"heartbeat";
        std::this_thread::sleep_for(milliseconds(20));
        assert(!watcher.update(root));
        std::ofstream(root/L"command.json")<<"request";
        bool delivered=false;
        for(int i=0;i<50&&!delivered;++i) {
            std::this_thread::sleep_for(milliseconds(10));
            delivered=watcher.update(root);
        }
        assert(delivered);
        assert(!watcher.update(root));
        assert(watcher.update(root,true));
        std::filesystem::remove(root/L"command.json");
        watcher.update(root,true);
        assert(!watcher.update(root,true));
    }
    std::filesystem::remove(root/L"status.json");
    std::filesystem::remove(root);
#endif
}
