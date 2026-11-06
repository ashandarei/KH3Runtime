#include "kh3ap/core/scene_attachment_math.h"
#include <cassert>
#include <limits>
using namespace kh3ap;
int main() {
    const auto near=[](float a,float b){return std::abs(a-b)<.001;};
    auto p=scene_parent_point({1,2,3},{10,20,30},{0,0,0},{2,3,4});
    assert(p && (*p==ScenePoint{12,26,42}));
    p=scene_parent_point({1,0,0},{0,0,0},{0,90,0},{1,1,1});
    assert(p && near((*p)[0],0) && near((*p)[1],1));
    p=scene_parent_point({1,0,0},{0,0,0},{90,0,0},{1,1,1});
    assert(p && near((*p)[0],0) && near((*p)[2],1));
    p=scene_parent_point({0,1,0},{0,0,0},{0,0,90},{1,1,1});
    assert(p && near((*p)[1],0) && near((*p)[2],-1));
    p=scene_parent_point({1,0,0},{10,0,0},{0,90,0},{2,1,1});
    p=scene_parent_point(*p,{0,20,0},{0,90,0},{1,1,1});
    assert(p && near((*p)[0],-2) && near((*p)[1],30));
    assert(!scene_parent_point({std::numeric_limits<float>::quiet_NaN(),0,0},{},{},{1,1,1}));
    assert(!scene_parent_point({10000000,0,0},{},{},{100,1,1}));
}
