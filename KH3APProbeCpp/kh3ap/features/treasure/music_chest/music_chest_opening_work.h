#pragma once
#include <set>
#include <string>

namespace kh3ap {
// Only cleared placed chests enter this work set. Keys carry no UObject pointers;
// each visit resolves the current gate and weak actor identity again.
class MusicChestOpeningWork {
public:
    void request(const std::wstring& key) { keys_.insert(key); }
    void clear() { keys_.clear(); }
    bool pending() const { return !keys_.empty(); }
    template<class Visit> void process(Visit&& visit) {
        for(auto it=keys_.begin();it!=keys_.end();) {
            if(visit(*it))++it; // Loading, pause or an unfinished animation retains work.
            else it=keys_.erase(it);
        }
    }
private:
    std::set<std::wstring> keys_;
};
}
