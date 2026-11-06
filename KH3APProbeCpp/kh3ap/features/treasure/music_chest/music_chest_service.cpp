#include <memory>
#include <utility>
#include "kh3ap/features/treasure/chest_commands.h"
#include "kh3ap/features/treasure/music_chest/music_chest_internal.h"
#include "kh3ap/features/treasure/music_chest/music_chest_pause.h"
#include "kh3ap/features/treasure/chest_activation_profile.h"
#include "kh3ap/app/config.h"
#include "kh3ap/bridge/rewards/reconciliation.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/core/conditional_profile_scope.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include <Unreal/AActor.hpp>
#include <Unreal/GameplayStatics.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#include <cstring>
#include <stdexcept>

namespace kh3ap
{
    namespace
    {
        constexpr auto chest_open_handler_vtable_offset = 0x7d0;
        constexpr auto music_chest_notice_load_uuid = 0x4b484720;

        constexpr auto music_chest_paint_load_uuid = 0x4b484721;
    } // namespace

    using namespace RC::Unreal;
    using namespace music_chest;
    using settings::s_enable_runtime_hot_path_profiling;

    RuntimeMusicChestService::Impl::Impl(RuntimeMusicChestConfig c) : config(std::move(c))
    {
        instance = this;
        set_handoff_audio(&audio);
    }

    RuntimeMusicChestService::Impl::~Impl()
    {
        if (activation_hook)
        {
            activation_hook->unHook();
        }
        if (delete_listener_installed)
        {
            UObjectArray::RemoveUObjectDeleteListener(this);
        }
        if (instance == this)
        {
            set_handoff_audio(nullptr);
            instance = nullptr;
        }
    }

    void RuntimeMusicChestService::Impl::NotifyUObjectDeleted(const UObjectBase* object, int32)
    {
        // GC callbacks may run off-thread. No UObject reads or weak allocation.
        const std::lock_guard lock(watched_mutex);
        if (watched_objects.contains(object))
        {
            deleted_objects.store(true, std::memory_order_release);
        }
    }

    void RuntimeMusicChestService::Impl::OnUObjectArrayShutdown()
    {
        if (delete_listener_installed)
        {
            UObjectArray::RemoveUObjectDeleteListener(this);
        }
        delete_listener_installed = false;
    }

    void RuntimeMusicChestService::Impl::watch_gates()
    {
        std::set<const UObjectBase*> objects;
        for (auto& [key, gate] : gates)
        {
            if (auto* chest = gate.chest.Get())
            {
                objects.insert(chest);
            }
            if (auto* reactor = gate.reactor.Get())
            {
                objects.insert(reactor);
            }
        }
        const std::lock_guard lock(watched_mutex);
        watched_objects.swap(objects);
    }

    void RuntimeMusicChestService::Impl::activated(UObject* chest, bool active)
    {
        auto& self = *instance;
        reinterpret_cast<void (*)(UObject*, bool)>(self.activation_original)(chest, active);
        // Never allocate UE weak serials on an async loader thread. Such an
        // arrival requests a single game-thread catch-up scan instead.
        if (GetCurrentThreadId() != self.game_thread)
        {
            self.off_thread_activation.store(true);
            return;
        }
        try
        {
            if (active)
            {
                ++self.activation_events;
            }
            auto weak = make_uobject_weak(chest);
            if (weak.Get() == chest)
            {
                self.discovery.notify(weak);
                self.maintenance_work.request();
                // Inactive unopened chests also have map icons.
                if (self.config.chest_changed)
                {
                    self.config.chest_changed(weak);
                }
            }
            else
            {
                self.off_thread_activation.store(true);
            }
        }
        catch (...)
        {
            self.off_thread_activation.store(true);
        }
    }

    void RuntimeMusicChestService::Impl::install_activation_hook()
    {
        if (!delete_listener_installed)
        {
            UObjectArray::AddUObjectDeleteListener(this);
            delete_listener_installed = true;
        }
        if (activation_attempted)
        {
            return;
        }
        activation_attempted = true;
        try
        {
            const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
            const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
            if (!base || dos->e_magic != IMAGE_DOS_SIGNATURE)
            {
                throw std::runtime_error("chest_activation_image");
            }
            const auto* pe = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
            const auto profile = chest_activation::select(pe->FileHeader.TimeDateStamp,
                                                          pe->OptionalHeader.SizeOfImage);
            if (pe->Signature != IMAGE_NT_SIGNATURE || !profile.rva)
            {
                throw std::runtime_error("chest_activation_unsupported_build");
            }
            const auto bytes = chest_activation::signature(profile);
            if (std::memcmp(reinterpret_cast<const void*>(base + profile.rva), bytes.data(),
                            bytes.size()))
            {
                throw std::runtime_error("chest_activation_signature");
            }
            auto* cdo = loaded(L"/Script/TresGame.Default__TresTreasureBox");
            if (!is_uobject_valid_fast(cdo) ||
                (*reinterpret_cast<uintptr_t**>(
                    cdo))[chest_open_handler_vtable_offset / sizeof(void*)] != base + profile.rva)
            {
                throw std::runtime_error("chest_activation_vtable");
            }
            game_thread = GetCurrentThreadId();
            activation_hook = std::make_unique<PLH::x64Detour>(
                base + profile.rva, reinterpret_cast<uint64_t>(&activated), &activation_original);
            activation_hook->setIsFollowCallOnFnAddress(false);
            activation_hook->setDetourScheme(PLH::x64Detour::VALLOC2);
            if (!activation_hook->hook() || !activation_original)
            {
                activation_hook.reset();
                throw std::runtime_error("chest_activation_hook_failed");
            }
            RC::Output::send<RC::LogLevel::Normal>(
                STR("[KH3APProbeCpp] Chest discovery activation hook installed rva=0x{:X}\n"),
                profile.rva);
        }
        catch (const std::exception& e)
        {
            activation_error = e.what();
            RC::Output::send<RC::LogLevel::Warning>(
                STR("[KH3APProbeCpp] Chest discovery hook unavailable: {}\n"),
                std::wstring(activation_error.begin(), activation_error.end()));
        }
    }

    void RuntimeMusicChestService::Impl::reset()
    {
        {
            const std::lock_guard lock(watched_mutex);
            watched_objects.clear();
        }
        opening_work.clear();
        for (auto& [key, gate] : gates)
        {
            retire(gate);
        }
        gates.clear();
        keyblade_cache.clear();
        status.reset();
        placed_initialization = {};
        placed_error.clear();
        discovery.request_scan();
        maintenance_work.request();
        publication_work.request();
    }

    void RuntimeMusicChestService::Impl::refresh_assignments()
    {
        const auto dir = config.bridge_dir();
        if (dir != directory)
        {
            reset();
            assignments.clear();
            has_vanilla_assignments = false;
            last_snapshot.reset();
            directory = dir;
        }
        if (!directory)
        {
            return;
        }
        auto next = reconciliation::view()->chests;
        if (next && (next->bridge_dir != *directory || next->process_id != GetCurrentProcessId() ||
                     next->seed.empty() || next->slot < 1))
        {
            next.reset();
        }
        if (next == last_snapshot)
        {
            return;
        }
        if (!next)
        {
            reset();
            assignments.clear();
            has_vanilla_assignments = false;
            seed.clear();
            slot = 0;
            last_snapshot.reset();
            return;
        }
        if (!last_snapshot || !next->same_layout(*last_snapshot))
        {
            reset();
        }
        assignments = next->assignments;
        seed = next->seed;
        slot = next->slot;
        has_vanilla_assignments = next->has_vanilla_assignments;
        last_snapshot = std::move(next);
        maintenance_work.request();
        publication_work.request();
        // A cleared manifest revision wakes placed-chest animation work once.
        // Ordinary Keyblade gates never enter the per-frame opening path.
        opening_work.clear();
        for (const auto& [key, gate] : gates)
        {
            const auto assignment = assignments.find(gate.treasure);
            if (gate.spawned && !gate.opened && gate.save_error.empty() &&
                assignment != assignments.end() && assignment->second.cleared)
            {
                opening_work.request(key);
            }
        }
    }

    void RuntimeMusicChestService::Impl::discover_chest(UObject* chest, UObject* pawn)
    {
        ++discovery_actors;
        if (!active_chest(chest) || owning_world(chest) != world(pawn) ||
            is_uobject_valid_fast(try_read_reflected_object_property_value(chest, L"m_pOpenner")))
        {
            return;
        }
        auto* p = chest->GetPropertyByNameInChain(L"m_TreasureID");
        if (!p || p->GetElementSize() != sizeof(FName) ||
            p->GetClass().GetName() != L"NameProperty")
        {
            return;
        }
        auto treasure = p->ContainerPtrToValuePtr<FName>(chest)->ToString();
        if (!assignments.contains(treasure) || assignments.at(treasure).placed)
        {
            return;
        }
        auto key = chest->GetPathName();
        if (auto found = gates.find(key);
            found != gates.end() && found->second.chest.Get() != chest)
        {
            // Streaming can recreate the same actor path with a new serial.
            retire(found->second);
            gates.erase(found);
        }
        if (!gates.contains(key))
        {
            auto* r = try_read_reflected_object_property_value(chest, L"MyReactor");
            if (!is_uobject_valid_fast(r) || reactor_command(r) != chest_command::open)
            {
                return;
            }
            Gate gate;
            gate.chest = make_uobject_weak(chest);
            gate.reactor = make_uobject_weak(r);
            gate.treasure = treasure;
            if (gate.chest.Get() != chest || gate.reactor.Get() != r)
            {
                throw std::runtime_error("chest_discovery_weak_handle");
            }
            // Close the gate while callback/material assets finish loading.
            const uint8_t command = chest_command::examine;
            reactor_command(r, &command);
            gates.emplace(key, std::move(gate));
            ++discovery_added;
            watch_gates();
            if constexpr (s_enable_runtime_hot_path_profiling)
            {
                RC::Output::send<RC::LogLevel::Normal>(
                    STR("[KH3APProbeCpp] ChestDiscovery added treasure={} actor={}\n"), treasure,
                    key);
            }
        }
    }

    void RuntimeMusicChestService::Impl::discover_vanilla(UObject* pawn)
    {
        RuntimeConditionalProfileScope<s_enable_runtime_hot_path_profiling> profile{
            L"music_chests:vanilla_discovery"};
        auto* klass = UObjectGlobals::StaticFindObject<UClass*>(
            nullptr, nullptr, L"/Script/TresGame.TresTreasureBox");
        if (!is_uobject_valid_fast(klass))
        {
            return;
        }
        std::vector<AActor*> actors;
        UGameplayStatics::GetAllActorsOfClass(pawn, klass, actors);
        ++discovery_scans;
        for (auto* chest : actors)
        {
            discover_chest(chest, pawn);
        }
    }

    void RuntimeMusicChestService::Impl::tick()
    {
        install_activation_hook();
        if (off_thread_activation.exchange(false))
        {
            discovery.request_scan();
            maintenance_work.request();
            if (config.chest_rescan)
            {
                config.chest_rescan();
            }
        }
        if (deleted_objects.exchange(false, std::memory_order_acq_rel))
        {
            maintenance_work.request();
        }
        const auto incoming = reconciliation::view()->chests;
        if (incoming != observed_snapshot)
        {
            observed_snapshot = incoming;
            assignment_work.request();
        }
        if (assignment_work.take(Clock::now()))
        {
            bool complete = false;
            try
            {
                refresh_assignments();
                complete = true;
            }
            catch (const std::exception& e)
            {
                error = e.what();
                publish_status();
            }
            assignment_work.finish(Clock::now(), complete);
        }
        const auto revision = read_runtime_keyblade_inventory_revision();
        const bool inventory_changed = revision != applied_inventory_revision && !gates.empty();
        // Loaded gates alone need no per-frame pawn lookup. Events remain
        // pending until playable context returns; animations/handoffs stay live.
        UObject* pawn{};
        if (pause_active() || opening_work.pending() || inventory_changed ||
            (!assignments.empty() && discovery.pending()))
        {
            RuntimeConditionalProfileScope<s_enable_runtime_hot_path_profiling> profile{
                L"music_chests:frame_pawn"};
            pawn = config.playable_sora();
        }
        update_pause(pawn, audio);
        try
        {
            if (opening_work.pending() && is_uobject_valid_fast(pawn))
            {
                RuntimeConditionalProfileScope<s_enable_runtime_hot_path_profiling> profile{
                    L"music_chests:opening_tick"};
                advance_spawned(pawn);
            }
        }
        catch (const std::exception& e)
        {
            error = e.what();
        }
        // Preference changes and grants refresh loaded Keyblade chests.
        // Defer during menus, retaining the incoming revision.
        if (inventory_changed && is_uobject_valid_fast(pawn) && config.gameplay_ready() &&
            world(pawn) == current_world.Get())
        {
            refresh_keyblade_gates(pawn);
            applied_inventory_revision = revision;
            maintenance_work.request();
        }
        if (discovery.pending() && !assignments.empty() && is_uobject_valid_fast(pawn) &&
            config.gameplay_ready())
        {
            try
            {
                if (current_world.Get() != world(pawn))
                {
                    reset();
                    current_world = make_uobject_weak(world(pawn));
                }
                if (discovery.pending())
                {
                    auto batch = discovery.take();
                    if (has_vanilla_assignments)
                    {
                        if (batch.scan)
                        {
                            discover_vanilla(pawn);
                        }
                        else
                        {
                            for (auto& weak : batch.actors)
                            {
                                if (auto* chest = weak.Get())
                                {
                                    discover_chest(chest, pawn);
                                }
                            }
                        }
                    }
                    maintenance_work.request(); // Bind newly arrived chests on this update.
                }
            }
            catch (const std::exception& e)
            {
                error = e.what();
                maintenance_work.request();
                publish_status();
            }
        }
        const auto now = Clock::now();
        if (maintenance_work.take(now))
        {
            RuntimeConditionalProfileScope<s_enable_runtime_hot_path_profiling> profile{
                L"music_chests:maintenance_work"};
            ++maintenance_passes;
            bool complete = false;
            try
            {
                complete = maintain(pawn);
            }
            catch (const std::exception& e)
            {
                error = e.what();
            }
            maintenance_work.finish(now, complete);
            watch_gates();
            publish_status();
        }
        if (publication_work.take(now))
        {
            bool complete = false;
            try
            {
                complete = write_status();
            }
            catch (const std::exception& e)
            {
                error = e.what();
            }
            publication_work.finish(now, complete);
        }
    }

    bool RuntimeMusicChestService::Impl::maintain(UObject* pawn)
    {
        try
        {
            if (assignments.empty())
            {
                return true;
            }
            if (!pawn)
            {
                pawn = config.playable_sora();
            }
            if (!is_uobject_valid_fast(pawn) || !config.gameplay_ready())
            {
                return false;
            }
            if (current_world.Get() != world(pawn))
            {
                reset();
                current_world = make_uobject_weak(world(pawn));
                return false;
            }
            for (auto it = gates.begin(); it != gates.end();)
            {
                if (!is_uobject_valid_fast(it->second.chest.Get()) ||
                    !is_uobject_valid_fast(it->second.reactor.Get()))
                {
                    retire(it->second);
                    it = gates.erase(it);
                }
                else
                {
                    ++it;
                }
            }
            // All loaded, settled gates sleep. Only incomplete asset setup or
            // failed operations return false and retain a retry deadline.
            shared_assets_requested = true;
            retain_ready_assets();
            bool assets = retained_notice && retained_paint;
            error.clear();
            if (!assets && (has_vanilla_assignments || !placed_initialization.stopped()) &&
                Clock::now() >= next_shared_asset_request)
            {
                next_shared_asset_request = Clock::now() + music_chest_asset_retry_interval;
                if (!retained_notice)
                {
                    request_runtime_asset_class_load(pawn, notice_path, &notice_load_state,
                                                     music_chest_notice_load_uuid);
                }
                if (!retained_paint)
                {
                    request_runtime_asset_load(pawn, paint_path, &paint_load_state,
                                               music_chest_paint_load_uuid);
                }
            }
            bool wanted = false;
            for (auto& [id, a] : assignments)
            {
                if (a.placed && a.map == current_world.Get()->GetPathName())
                {
                    wanted = true;
                }
            }
            bool placement_assets = !wanted;
            if (wanted && !placed_initialization.stopped())
            {
                // Request placed assets alongside shared assets, not after them.
                const bool spawn_ready = prepare_spawn_assets(pawn);
                placement_assets = spawn_ready && assets;
                placed_error = placement_assets ? "" : "Waiting for placed music chest assets";
            }
            placed_initialization.attempt(
                placement_assets,
                [&]
                {
                    try
                    {
                        if (!wanted)
                        {
                            return true;
                        }
                        for (auto& [id, a] : assignments)
                        {
                            if (a.placed && a.map == current_world.Get()->GetPathName() &&
                                !gates.contains(L"placed:" + id))
                            {
                                spawn_placed(pawn, id, a);
                            }
                        }
                        placed_error.clear();
                        return true;
                    }
                    catch (const std::exception& e)
                    {
                        placed_error = e.what();
                        return false;
                    }
                });
            if (!placed_error.empty())
            {
                error = placed_initialization.stopped()
                            ? "Placed music chest initialization stopped after 30 attempts: " +
                                  placed_error
                            : placed_error;
            }
            bool gates_complete = true;
            for (auto& [key, gate] : gates)
            {
                if (gate.spawned)
                {
                    continue;
                }
                if (!active_chest(gate.chest.Get()))
                {
                    continue;
                }
                const auto& a = assignments.at(gate.treasure);
                // Gate maintenance also applies grant-invalidated ownership;
                // Examine can refresh it immediately between these passes.
                const uint8_t command = desired_command(pawn, gate, a);
                if (reactor_command(gate.reactor.Get()) != command)
                {
                    reactor_command(gate.reactor.Get(), &command);
                }
                // A delayed cosmetic material must not disable Examine/song launch.
                try
                {
                    if (retained_notice && !gate.bound)
                    {
                        bind(gate);
                    }
                    if (gate.bound && retained_paint && !gate.painted)
                    {
                        paint(gate);
                    }
                }
                catch (const std::exception& e)
                {
                    error = e.what();
                    gates_complete = false;
                }
            }
            if (!assets)
            {
                error = std::string("Waiting for music chest assets: ") +
                        (retained_notice ? "" : "callback ") + (retained_paint ? "" : "material") +
                        (error.empty() ? "" : "; " + error);
            }
            return gates_complete && assets && placed_initialization.stopped();
        }
        catch (const std::exception& e)
        {
            error = e.what();
            return false;
        }
    }

    RuntimeMusicChestService::RuntimeMusicChestService(RuntimeMusicChestConfig c)
        : impl_(std::make_unique<Impl>(std::move(c)))
    {
    }
    RuntimeMusicChestService::~RuntimeMusicChestService() = default;
    void RuntimeMusicChestService::tick()
    {
        impl_->tick();
    }
    void RuntimeMusicChestService::reset_map_lifecycle()
    {
        impl_->discovery.request_scan();
        impl_->maintenance_work.request();
        impl_->assignment_work.request();
    }
} // namespace kh3ap
