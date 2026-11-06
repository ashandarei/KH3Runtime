#include "kh3ap/features/treasure/chest_discovery_queue.h"
#include "kh3ap/features/treasure/chest_activation_profile.h"
#include <cassert>
#include <cstdint>

int main() {
    kh3ap::ChestDiscoveryQueue<uint64_t, 3> queue;
    // Initial catch-up includes actors whose activation preceded installation.
    queue.notify(1);
    assert(queue.take().scan);
    for(int i=0;i<10000;++i)assert(!queue.pending());
    // Preserve/deduplicate arrivals while loading delays the consumer.
    for(int i=0;i<10000;++i)queue.notify(11);
    queue.notify(12);
    auto batch=queue.take();
    assert(!batch.scan && (batch.actors==std::vector<uint64_t>{11,12}));
    // Re-activation and a new object generation are both new work after drain.
    queue.notify(11);queue.notify((1ull<<32)|11);
    batch=queue.take();
    assert(!batch.scan && batch.actors.size()==2);
    assert(!queue.pending());
    // An overflow is bounded and requests exactly one catch-up, then goes idle.
    for(uint64_t i=1;i<10000;++i)queue.notify(i);
    batch=queue.take();
    assert(batch.scan && batch.actors.empty());
    assert(!queue.pending() && !queue.take().scan);
    queue.notify(22);queue.request_scan();queue.request_scan();
    assert(queue.take().scan && !queue.pending());
    // Map and remapped-chest consumers have independent drains. A hidden map
    // keeps streamed arrivals even if interaction handling has already run.
    kh3ap::ChestDiscoveryQueue<uint64_t,3> map,interaction;
    map.take(); interaction.take();
    for(auto actor:{11ull,12ull}) {map.notify(actor);interaction.notify(actor);}
    assert(interaction.take().actors.size()==2);
    assert(!interaction.pending() && map.pending());
    map.notify(11); // repeated activation while hidden coalesces
    map.notify((1ull<<32)|11); // streamed replacement is a distinct weak generation
    batch=map.take();
    assert(!batch.scan && batch.actors.size()==3 && !map.pending());
    // Neither time passing nor view reopening can request world discovery.
    for(int i=0;i<10000;++i)assert(!map.take().scan);
    using namespace kh3ap::chest_activation;
    assert(!select(0,0).rva);
    for(auto profile:{select(0x60547d11,0xaebf000),select(0x66743f34,0xa969000)}) {
        const auto bytes=signature(profile);
        uint32_t relative{};
        for(size_t i=0;i<4;++i)relative|=uint32_t(bytes[29+i])<<(8*i);
        assert(profile.rva+33+relative==profile.base_setter);
    }
}
