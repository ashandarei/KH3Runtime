#include "kh3ap/features/treasure/music_chest/music_chest_opening_work.h"
#include <cassert>
#include <stdexcept>
#include <vector>

int main() {
    kh3ap::MusicChestOpeningWork work;
    int visits=0;
    for(int frame=0;frame<10000;++frame)work.process([&](const auto&){++visits;return true;});
    assert(!work.pending()&&visits==0); // Idle frames never visit gates.
    work.request(L"placed:a");work.request(L"placed:a");work.request(L"placed:b");
    std::vector<std::wstring> seen;
    work.process([&](const auto& key){seen.push_back(key);return true;});
    assert((seen==std::vector<std::wstring>{L"placed:a",L"placed:b"})); // Coalesced; pause retains both.
    work.process([](const auto& key){return key==L"placed:b";}); // A completed; B is still animating.
    assert(work.pending());
    try {work.process([](const auto&)->bool{throw std::runtime_error("pose_unavailable");});assert(false);}
    catch(const std::runtime_error&){}
    assert(work.pending()); // A transient engine failure cannot lose the remaining animation.
    seen.clear();
    work.process([&](const auto& key){seen.push_back(key);return false;}); // B completed/unloaded/terminal error.
    assert((seen==std::vector<std::wstring>{L"placed:b"})&&!work.pending());
    work.request(L"placed:a"); // A new actor generation can use the same path after streaming.
    work.clear(); // World/layout reset discards old work before gates are retired.
    work.process([&](const auto&){++visits;return true;});
    assert(!work.pending()&&visits==0);
}
