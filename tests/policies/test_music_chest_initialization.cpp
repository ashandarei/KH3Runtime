#include "kh3ap/features/treasure/music_chest/music_chest_initialization.h"
#include <cassert>
#include <stdexcept>

int main() {
    kh3ap::RuntimeMusicChestInitialization state;
    int calls{};
    for(int tick=0;tick<10000;++tick)state.attempt(true,[&]{return ++calls==3;});
    assert(calls==3 && state.complete); // No checks after success.
    state={};calls=0;
    for(int tick=0;tick<10000;++tick)state.attempt(true,[&]{++calls;return false;});
    assert(calls==30 && state.stopped() && !state.complete);
    state={};calls=0;
    for(int tick=0;tick<10000;++tick) {
        try {state.attempt(true,[&]() -> bool {++calls;throw std::runtime_error("spawn failed");});}
        catch(const std::runtime_error&){}
    }
    assert(calls==30 && state.stopped()); // Exceptions cannot extend retries.
    state={};state.attempt(true,[]{return true;});
    assert(state.complete && state.attempts==1); // A new world gets a fresh budget.
    state={};calls=0;
    for(int tick=0;tick<120;++tick)state.attempt(false,[&]{++calls;return true;});
    assert(calls==0 && state.attempts==0 && !state.stopped());
    state.attempt(true,[&]{++calls;return true;});
    assert(calls==1 && state.complete); // Cold streaming cannot exhaust spawn retries.
    state={};calls=0;
    state.attempt(true,[&]{++calls;return false;});
    for(int tick=0;tick<120;++tick)state.attempt(false,[&]{++calls;return false;});
    assert(calls==1 && state.attempts==1);
    state.attempt(true,[&]{++calls;return true;});
    assert(calls==2 && state.complete); // A dependency loss doesn't spend attempts either.
}
