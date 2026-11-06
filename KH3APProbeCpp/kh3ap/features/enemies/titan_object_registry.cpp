#include "kh3ap/features/enemies/titan_object_registry.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UObjectArray.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/NameTypes.hpp>
#include <UnrealDef.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <mutex>
#include <unordered_map>

namespace kh3ap::titan_objects
{
    using namespace RC;
    using namespace RC::Unreal;
    namespace
    {
        constexpr auto templates = static_cast<EObjectFlags>(RF_ClassDefaultObject | RF_ArchetypeObject);
        constexpr auto loading = static_cast<EObjectFlags>(RF_NeedLoad | RF_NeedPostLoad | RF_NeedPostLoadSubobjects);
        struct Registry final : FUObjectDeleteListener
        {
            struct Pending { UObject* object; FUObjectItem* slot; bool pawn; };
            struct Entry { FWeakObjectPtr weak; bool pawn; };
            std::array<FName, 5> names{};
            std::array<FName, 2> pawn_classes{};
            std::array<bool, 2> pawn_class_set{};
            std::mutex mutex;
            std::unordered_map<const UObjectBase*, Pending> pending;
            std::unordered_map<const UObjectBase*, Entry> tracked;
            std::atomic<bool> enabled{};
            uint64_t bootstrap_scans{}, generation{};

            auto named_actor(UObject* object) const -> bool
            {
                if (!object || object->HasAnyFlags(templates)) return false;
                // Exact cooked instance names are available at construction.
                // Full paths and serialized properties are checked on the game thread.
                for (const auto& name : names)
                    if (object->GetNamePrivate().Equals(name)) return true;
                return false;
            }
            auto enqueue(UObject* object) -> void
            {
                if (!enabled.load(std::memory_order_acquire)) return;
                if (!object || object->HasAnyFlags(templates)) return;
                const bool named = named_actor(object);
                bool pawn = false;
                if (!named && object->GetClassPrivate())
                    for (size_t i=0;i<pawn_classes.size();++i)
                        if (pawn_class_set[i] && object->GetClassPrivate()->GetNamePrivate().Equals(pawn_classes[i])) { pawn=true;break; }
                if (!named && !pawn) return;
                const std::lock_guard lock{mutex};
                if (tracked.contains(object) || pending.contains(object)) return;
                if (auto* slot = object->GetObjectItem())
                {
                    pending.emplace(object, Pending{object, slot, pawn});
                    ++generation;
                }
            }
            auto NotifyUObjectDeleted(const UObjectBase* object, int32) -> void override
            {
                // No reads of a dying UObject and no weak allocation on GC threads.
                const std::lock_guard lock{mutex};
                const auto removed = pending.erase(object) + tracked.erase(object);
                if (removed) { ++generation; }
            }
            auto OnUObjectArrayShutdown() -> void override
            {
                enabled.store(false, std::memory_order_release);
                UObjectArray::RemoveUObjectDeleteListener(this);
                const std::lock_guard lock{mutex};
                pending.clear(); tracked.clear();
            }
        };
        // The shared process-lifetime create listener never targets a
        // destructible service. The engine removes its listener at shutdown.
        auto registry() -> Registry& { static auto* value = new Registry; return *value; }
    }

    auto watch_pawn_class(size_t index, std::wstring_view path) -> void
    {
        auto& state=registry();
        if (index>=state.pawn_classes.size() || state.enabled.load(std::memory_order_acquire)) return;
        const auto dot=path.rfind(L'.');
        if (dot==std::wstring_view::npos) return;
        const std::wstring name{path.substr(dot+1)};
        state.pawn_classes[index]=FName{name.c_str(),FNAME_Add};
        state.pawn_class_set[index]=true;
    }

    auto initialize() -> void
    {
        auto& state = registry();
        if (state.enabled.load(std::memory_order_acquire)) return;
        state.names = {FName{L"e_he902_Pawn1317", FNAME_Add}, FName{L"e_he904_Pawn1045", FNAME_Add},
            FName{L"he_05_invisiblewall_01", FNAME_Add}, FName{L"he_05_invisiblewall_02", FNAME_Add},
            FName{L"SM_he_Ter_ZeusRockBattleA", FNAME_Add}};
        UObjectArray::AddUObjectDeleteListener(&state);
        state.enabled.store(true, std::memory_order_release);
        ++state.bootstrap_scans;
        UObjectGlobals::ForEachUObject([&](UObject* object, int32_t, int32_t) {
            state.enqueue(object);
            return LoopAction::Continue;
        });
    }

    auto constructed(UObject* object) -> void { registry().enqueue(object); }

    auto generation() -> uint64_t { auto& state=registry(); const std::lock_guard lock{state.mutex}; return state.generation; }

    auto snapshot() -> Snapshot
    {
        auto& state = registry();
        const std::lock_guard lock{state.mutex};
        Snapshot result;
        std::erase_if(state.pending, [&](const auto& pair) {
            const auto& [key, entry] = pair;
            // A deletion callback removes pending entries before slot reuse.
            if (entry.slot->GetUObject() != entry.object || !is_uobject_valid_fast(entry.object)) return true;
            auto weak = make_uobject_weak(entry.object);
            if (weak.Get() != entry.object) { result.unfinished = true; return false; }
            state.tracked.emplace(key, Registry::Entry{weak, entry.pawn});
            return true;
        });
        std::erase_if(state.tracked, [&](const auto& pair) {
            auto* object = pair.second.weak.Get();
            if (!is_uobject_valid_fast(object)) return true;
            if (object->HasAnyFlags(loading)) { result.unfinished = true; return false; }
            if (!object->HasAnyFlags(templates))
                (pair.second.pawn ? result.pawns : result.objects).push_back(object);
            return false;
        });
        std::ranges::sort(result.objects, {}, [](UObject* object) { return object->GetInternalIndex(); });
        result.bootstrap_scans = state.bootstrap_scans;
        result.generation = state.generation;
        return result;
    }
}
