#include "kh3ap/features/colors/color_object_registry.h"
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

namespace kh3ap::color_objects
{
    namespace
    {
        constexpr auto max_color_class_ancestry_depth = 64;
    }

    using namespace RC;
    using namespace RC::Unreal;
    namespace
    {
        enum class Kind { Pawn, Ship, Editor, Table, World, Other };
        constexpr auto templates = static_cast<EObjectFlags>(RF_ClassDefaultObject | RF_ArchetypeObject);
        constexpr auto loading = static_cast<EObjectFlags>(RF_NeedLoad | RF_NeedPostLoad | RF_NeedPostLoadSubobjects);
        struct Registry final : FUObjectDeleteListener
        {
            struct Pending { UObject* object; FUObjectItem* slot; Kind kind; };
            struct Entry { FWeakObjectPtr weak; Kind kind; };
            std::array<FName, 5> classes{};
            std::mutex mutex;
            std::unordered_map<const UObjectBase*, Pending> pending;
            std::unordered_map<const UObjectBase*, Entry> tracked;
            std::atomic<bool> enabled{};
            uint64_t bootstrap_scans{}, generation{}, gummi_generation{};

            void changed(Kind kind) { ++(kind == Kind::Pawn ? generation : gummi_generation); }

            auto classify(UObject* object) const -> Kind
            {
                if (!object || object->HasAnyFlags(templates)) return Kind::Other;
                // UObjectArray create listener: classify from the assigned class;
                // instance properties may still await serialization/PostLoad.
                size_t depth = 0;
                for (auto* type = object->GetClassPrivate(); type && depth++ < max_color_class_ancestry_depth;
                     type = static_cast<UClass*>(type->GetSuperStruct()))
                    for (size_t i = 0; i < classes.size(); ++i)
                        if (type->GetNamePrivate().Equals(classes[i])) return static_cast<Kind>(i);
                return Kind::Other;
            }
            auto enqueue(UObject* object) -> void
            {
                if (!enabled.load(std::memory_order_acquire)) return;
                const auto kind = classify(object);
                if (kind == Kind::Other) return;
                const std::lock_guard lock{mutex};
                if (tracked.contains(object) || pending.contains(object)) return;
                if (auto* slot = object->GetObjectItem())
                {
                    pending.emplace(object, Pending{object, slot, kind});
                    changed(kind);
                }
            }
            auto NotifyUObjectDeleted(const UObjectBase* object, int32) -> void override
            {
                // No reads of a dying UObject and no weak allocation on GC threads.
                const std::lock_guard lock{mutex};
                if (auto it=pending.find(object);it!=pending.end()) { changed(it->second.kind);pending.erase(it); }
                if (auto it=tracked.find(object);it!=tracked.end()) { changed(it->second.kind);tracked.erase(it); }
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

    auto initialize() -> void
    {
        auto& state = registry();
        if (state.enabled.load(std::memory_order_acquire)) return;
        // Classification visits the most specific class before its parents,
        // keeping Gummi ships separate from character discovery.
        state.classes = {FName{L"Pawn", FNAME_Add}, FName{L"TresCreateGummiShipActor", FNAME_Add},
            FName{L"TresGummiEditorManager", FNAME_Add}, FName{L"DataTable", FNAME_Add}, FName{L"World", FNAME_Add}};
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
    auto gummi_generation() -> uint64_t { auto& state=registry(); const std::lock_guard lock{state.mutex}; return state.gummi_generation; }

    auto snapshot(bool gummi) -> Snapshot
    {
        auto& state = registry();
        const std::lock_guard lock{state.mutex};
        Snapshot result;
        const auto selected=[&](Kind kind) { return (kind!=Kind::Pawn)==gummi; };
        std::erase_if(state.pending, [&](const auto& pair) {
            const auto& [key, entry] = pair;
            if (!selected(entry.kind)) return false;
            // A deletion callback removes pending entries before slot reuse.
            if (entry.slot->GetUObject() != entry.object || !is_uobject_valid_fast(entry.object)) return true;
            auto weak = make_uobject_weak(entry.object);
            if (weak.Get() != entry.object) { result.unfinished = true; return false; }
            state.tracked.emplace(key, Registry::Entry{weak, entry.kind});
            return true;
        });
        std::array<std::vector<UObject*>*, 5> lists{&result.pawns,&result.ships,&result.editors,&result.tables,&result.worlds};
        std::erase_if(state.tracked, [&](const auto& pair) {
            const auto& entry = pair.second;
            if (!selected(entry.kind)) return false;
            auto* object = entry.weak.Get();
            if (!is_uobject_valid_fast(object)) return true;
            if (object->HasAnyFlags(loading)) { result.unfinished = true; return false; }
            if (!object->HasAnyFlags(templates)) lists[static_cast<size_t>(entry.kind)]->push_back(object);
            return false;
        });
        for (auto* list : lists)
            std::ranges::sort(*list, {}, [](UObject* object) { return object->GetInternalIndex(); });
        result.bootstrap_scans = state.bootstrap_scans;
        result.generation = gummi ? state.gummi_generation : state.generation;
        return result;
    }
}
