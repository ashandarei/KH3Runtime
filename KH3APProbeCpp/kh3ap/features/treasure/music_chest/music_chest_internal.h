#pragma once

// PolyHook/AsmJit must be parsed before Unreal's platform/assertion macros.
#include <polyhook2/Detour/x64Detour.hpp>
#include "kh3ap/features/treasure/music_chest/music_chest_service.h"
#include "kh3ap/features/treasure/music_chest/music_chest.h"
#include "kh3ap/features/treasure/music_chest/music_chest_status.h"
#include "kh3ap/features/treasure/music_chest/music_chest_initialization.h"
#include "kh3ap/features/treasure/music_chest/music_chest_opening_work.h"
#include "kh3ap/features/treasure/chest_discovery_queue.h"
#include "kh3ap/features/treasure/chest_maintenance_work.h"
#include "kh3ap/features/treasure/music_chest/mom_audio.h"
#include "kh3ap/bridge/checks/chest_snapshot.h"
#include "kh3ap/core/events/keyblade_inventory_events.h"
#include "kh3ap/features/grants/messages/ap_message_param_storage.h"
#include <Unreal/UObjectArray.hpp>
#include <Unreal/FWeakObjectPtr.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#include <atomic>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <string>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace kh3ap
{
    // Shared only by the focused implementation files. Scheduling stays in the
    // service; helpers never add independent polling or retain unrooted UObjects.
    struct RuntimeMusicChestService::Impl : RC::Unreal::FUObjectDeleteListener
    {
        using Clock = music_chest::Clock;
        using UObject = RC::Unreal::UObject;
        using UObjectBase = RC::Unreal::UObjectBase;
        using FWeakObjectPtr = RC::Unreal::FWeakObjectPtr;
        using FFrame = RC::Unreal::FFrame;
        using Assignment = chest_manifest::Assignment;
        struct Gate
        {
            FWeakObjectPtr chest, notice, reactor;
            bool bound{}, painted{}, spawned{}, opened{}, opening{}, animation_finished{};
            Clock::time_point opening_start{};
            std::string save_error;
            uint8_t original_command{3};
            std::wstring treasure;
            bool keyblade_owned{};
            Clock::time_point notice_next{};
            std::unique_ptr<RuntimeApMessageParamStorage> notice_storage;
        };
        RuntimeMusicChestConfig config;
        RuntimeMoMAudio audio{
            [](const wchar_t* message)
            {
                RC::Output::send<RC::LogLevel::Normal>(STR("[KH3APProbeCpp] {}\n"), message);
            }};
        std::map<std::wstring, Assignment> assignments;
        std::map<std::wstring, Gate> gates;
        MusicChestOpeningWork opening_work;
        ChestMaintenanceWork maintenance_work, publication_work, assignment_work;
        uint64_t maintenance_passes{};
        std::mutex watched_mutex;
        std::set<const UObjectBase*> watched_objects;
        std::atomic_bool deleted_objects{};
        bool delete_listener_installed{};
        std::wstring seed;
        int64_t slot{}, sequence{};
        std::optional<std::filesystem::path> directory;
        std::string error;
        std::shared_ptr<const chest_manifest::Snapshot> last_snapshot;
        std::shared_ptr<const chest_manifest::Snapshot> observed_snapshot;
        RuntimeKeybladeOwnershipCache keyblade_cache;
        FWeakObjectPtr inventory_pawn;
        int64_t inventory_epoch{-1};
        uint64_t applied_inventory_revision{};
        RuntimeMusicChestStatus status;
        RuntimeMusicChestInitialization placed_initialization;
        std::string placed_error;
        bool has_vanilla_assignments{};
        FWeakObjectPtr current_world;
        struct SameWeakIdentity
        {
            bool operator()(const FWeakObjectPtr& a, const FWeakObjectPtr& b) const
            {
                static_assert(sizeof(FWeakObjectPtr) == 2 * sizeof(int32_t));
                return std::memcmp(&a, &b, sizeof(a)) == 0; // index and serial, even after unload
            }
        };
        ChestDiscoveryQueue<FWeakObjectPtr, 1024, SameWeakIdentity> discovery;
        std::unique_ptr<PLH::x64Detour> activation_hook;
        uint64_t activation_original{};
        DWORD game_thread{};
        bool activation_attempted{};
        std::string activation_error;
        std::atomic_bool off_thread_activation{};
        uint64_t activation_events{}, discovery_scans{}, discovery_actors{}, discovery_added{};
        // Each asynchronous load can finish on a different tick. Keep the assets
        // resident independently so GC cannot unload one while the other loads.
        // These process-lifetime roots are intentionally left to engine shutdown.
        UObject* retained_notice{};
        UObject* retained_paint{};
        std::map<std::wstring, UObject*> retained_spawn_assets;
        std::wstring notice_load_state, paint_load_state;
        std::wstring spawn_load_state;
        std::string last_asset_status;
        std::string last_discovery_status;
        bool shared_assets_requested{}, spawn_assets_requested{};
        Clock::time_point next_asset_request{}, next_shared_asset_request{};
        uint32_t load_sequence{};
        inline static Impl* instance{};

        void retain_ready_assets();
        explicit Impl(RuntimeMusicChestConfig c);
        ~Impl();
        void NotifyUObjectDeleted(const UObjectBase* object, int32_t) override;
        void OnUObjectArrayShutdown() override;
        void watch_gates();
        static void activated(UObject* chest, bool active);
        void install_activation_hook();
        void retire(Gate& gate);
        void reset();
        void notify_player_changed();
        bool keyblade_owned(UObject* pawn, const Assignment& a);
        uint8_t desired_command(UObject* pawn, Gate& gate, const Assignment& a);
        void refresh_keyblade_gates(UObject* pawn);
        void publish_status();
        bool write_status();
        void refresh_assignments();
        static void checked(UObject* context, FFrame& stack, void*);
        void paint(Gate& gate);
        void bind(Gate& gate);
        UObject* spawned_component(Gate& gate, const wchar_t* name);
        void pose(Gate& gate, float fraction);
        bool prepare_spawn_assets(UObject* pawn);
        void spawn_placed(UObject* pawn, const std::wstring& id, const Assignment& a);
        void advance_spawned(UObject* pawn);
        void discover_chest(UObject* chest, UObject* pawn);
        void discover_vanilla(UObject* pawn);
        void tick();
        bool maintain(UObject* pawn);
    };
} // namespace kh3ap
