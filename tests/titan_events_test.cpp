#include "titan_events_under_test.h"
#include <cassert>
#include <iostream>
using namespace kh3ap;
using namespace RC::Unreal;
int main(){
    UObject entity[2],pawn[2],obstacle[3],outer;
    UClass replacement[2];
    for(int i=0;i<2;++i){
        entity[i].path=entities[i];replacement[i].path=L"/Game/Replacement"+std::to_wstring(i);
        pawn[i].type=&replacement[i];pawn[i].outer=&outer;entity[i].entity_actor=&pawn[i];
        track_titan_arena_entity(&entity[i],sources[i],replacement[i].path);
    }
    for(int i=0;i<3;++i)obstacle[i].path=titan_arena_obstacles[i];
    fake_titan::objects.objects={&entity[0],&entity[1],&obstacle[0],&obstacle[1]};
    process_titan_arena_replacement(true);
    assert(obstacle[0].collision&&originals.empty()); // no partial geometry change
    auto snapshots=fake_titan::snapshots;
    for(int i=0;i<10000;++i)process_titan_arena_replacement(true);
    assert(fake_titan::snapshots==snapshots);
    fake_titan::objects.objects.push_back(&obstacle[2]);++fake_titan::revision;
    entity[0].outer=entity[1].outer=&outer;
    fake_titan::objects.pawns={&pawn[0],&pawn[1]};
    entity[1].entity_actor=nullptr;
    process_titan_arena_replacement(true);
    assert(obstacle[0].collision&&work.pending());
    // Native FinishSpawning assigns ownership without a repair/AI-state event.
    entity[1].entity_actor=&pawn[1];
    fake_titan::Clock::time+=std::chrono::milliseconds(100);
    process_titan_arena_replacement(true);
    for(auto& o:obstacle)assert(!o.collision);
    assert(!obstacle[0].hidden&&!obstacle[1].hidden&&obstacle[2].hidden&&originals.size()==3);
    auto calls=fake_titan::calls;snapshots=fake_titan::snapshots;
    for(int i=0;i<10000;++i){fake_titan::Clock::time+=std::chrono::seconds(1);process_titan_arena_replacement(true);}
    assert(fake_titan::calls==calls&&fake_titan::snapshots==snapshots);
    // Retry temporarily clears ownership: keep the removed walls, await binding.
    entity[0].entity_actor=nullptr;notify_titan_arena_binding(entity[0].path);
    process_titan_arena_replacement(true);assert(!obstacle[0].collision&&originals.size()==3);
    entity[0].entity_actor=&pawn[0];notify_titan_arena_binding(entity[0].path);
    process_titan_arena_replacement(true);
    // A streamed replacement must capture its own original values.
    UObject new_wall;new_wall.path=obstacle[0].path;new_wall.collision=false;new_wall.hidden=true;
    obstacle[0].alive=false;++obstacle[0].serial;
    fake_titan::objects.objects[2]=&new_wall;++fake_titan::revision;
    process_titan_arena_replacement(true);assert(!new_wall.collision&&new_wall.hidden);
    process_titan_arena_replacement(false);
    assert(originals.empty()&&!new_wall.collision&&new_wall.hidden);
    assert(obstacle[1].collision&&obstacle[2].collision&&!obstacle[2].hidden);
    // Native vanilla binding restores geometry rather than applying a replacement.
    process_titan_arena_replacement(true);assert(!obstacle[1].collision);
    replacement[0].path=sources[0];notify_titan_arena_binding(entity[0].path);
    process_titan_arena_replacement(true);assert(obstacle[1].collision&&originals.empty());
    // A queued PostLoad retry stops immediately once ready.
    replacement[0].path=L"/Game/Replacement0";
    fake_titan::objects.unfinished=true;++fake_titan::revision;
    process_titan_arena_replacement(true);snapshots=fake_titan::snapshots;
    fake_titan::Clock::time+=std::chrono::milliseconds(99);process_titan_arena_replacement(true);
    assert(fake_titan::snapshots==snapshots);
    fake_titan::objects.unfinished=false;fake_titan::Clock::time+=std::chrono::milliseconds(1);
    process_titan_arena_replacement(true);assert(fake_titan::snapshots==snapshots+1);
    snapshots=fake_titan::snapshots;
    fake_titan::Clock::time+=std::chrono::seconds(10);process_titan_arena_replacement(true);
    assert(fake_titan::snapshots==snapshots);
    process_titan_arena_replacement(false);
    std::cout<<"Titan lifecycle, late streaming, Retry gap, replacement originals, toggle/vanilla restore, PostLoad and 20000 quiet ticks passed\n";
}
