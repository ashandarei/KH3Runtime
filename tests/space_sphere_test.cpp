#include "kh3ap/features/world/space/space_sphere_policy.h"
#include <cassert>
#include <set>
using kh3ap::space::sphere_opened;
int main() {
    for(unsigned id=0;id<256;++id) {
        assert(!kh3ap::space::sphere_completion_notification(id,false));
        assert(kh3ap::space::sphere_completion_notification(id,true)==(id<9));
    }
    // Captured EGS missed reward: bytes 00 10, Misty Stream sphere 1 (id 3).
    for(unsigned id=0;id<256;++id)assert(sphere_opened(0x1000,id)==(id==3));
    for(unsigned bit=0;bit<16;++bit)
        for(unsigned id=0;id<256;++id)
            assert(sphere_opened(static_cast<uint16_t>(0x8000u>>bit),id)==(bit<9&&id==bit));
    std::set<unsigned> completed;
    auto recover=[&](uint16_t flags){
        for(unsigned id=0;id<9;++id)if(sphere_opened(flags,id))completed.insert(id);
    };
    recover(0);assert(completed.empty());
    recover(0x007f);assert(completed.empty()); // GM04/reserved bits.
    recover(0x1000);recover(0x1000);assert(completed==std::set<unsigned>{3});
    recover(0xffff);assert(completed.size()==9);
}
