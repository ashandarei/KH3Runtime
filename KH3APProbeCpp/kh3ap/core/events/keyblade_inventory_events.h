#pragma once

#include <atomic>
#include <cstdint>
#include <string_view>
#include <string>
#include <map>
#include <mutex>

namespace kh3ap
{
    // Invalidation only: a dispatched grant is not proof of native ownership.
    // Consumers re-read inventory on the game thread. Production Chestsanity
    // and the diagnostic experiment share revisions across grant paths.
    inline std::atomic<uint64_t> runtime_keyblade_inventory_revision{0};
    inline std::mutex runtime_keyblade_inventory_mutex;
    inline std::map<std::wstring,uint64_t,std::less<>> runtime_keyblade_inventory_revisions;
    inline void notify_runtime_keyblade_inventory_changed(std::wstring_view token)
    {
        if(token.starts_with(L"WEP_KEYBLADE_"))
        {
            const std::lock_guard lock(runtime_keyblade_inventory_mutex);
            ++runtime_keyblade_inventory_revisions[std::wstring(token)];
            runtime_keyblade_inventory_revision.fetch_add(1,std::memory_order_release);
        }
    }
    inline uint64_t read_runtime_keyblade_inventory_revision()
    {
        return runtime_keyblade_inventory_revision.load(std::memory_order_acquire);
    }
    inline uint64_t read_runtime_keyblade_inventory_revision(std::wstring_view token)
    {
        const std::lock_guard lock(runtime_keyblade_inventory_mutex);
        const auto it=runtime_keyblade_inventory_revisions.find(token);
        return it==runtime_keyblade_inventory_revisions.end()?0:it->second;
    }

    // Game-thread cache shared by every loaded chest using the same keyblade.
    // A grant only invalidates; ownership always comes from the native query.
    class RuntimeKeybladeOwnershipCache
    {
    public:
        struct Entry { bool valid{},owned{}; uint64_t revision{},seen{}; size_t queries{}; };
        template<class Query>
        const Entry& get(const std::wstring& token,Query query)
        {
            auto& entry=entries[token];
            const auto revision=read_runtime_keyblade_inventory_revision();
            if(!entry.valid||entry.seen!=revision)
            {
                const auto keyed=read_runtime_keyblade_inventory_revision(token);
                if(!entry.valid||entry.revision!=keyed)
                {
                    const bool owned=query(); // Do not mark a failed query current.
                    entry.owned=owned;entry.valid=true;entry.revision=keyed;++entry.queries;
                }
                entry.seen=revision;
            }
            return entry;
        }
        void invalidate(const std::wstring& token) { entries[token].valid=false; }
        void clear() { entries.clear(); }
    private:
        std::map<std::wstring,Entry> entries;
    };
}
