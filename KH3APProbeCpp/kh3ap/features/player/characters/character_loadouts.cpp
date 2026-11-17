#include "character_loadouts.h"
#include "character_loadout_state.h"
#include "character_profile.h"
#include "kh3ap/core/platform/steam_parity_profile.h"
#include "kh3ap/hooks/native_detour_lifetime.h"
#include "kh3ap/bridge/rewards/reconciliation.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include <polyhook2/Detour/x64Detour.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#include <utility>
#include <atomic>
#define NOMINMAX
#include <Windows.h>

namespace kh3ap::character_loadouts
{
    using namespace RC::Unreal;
    namespace
    {
        constexpr size_t active_save_offset = 0x870;
        uintptr_t base{};
        uintptr_t accessor_rva{};
        std::atomic<bool> installed{};
        uint64_t original_pack{}, original_unpack{}, original_reset{};
        std::unique_ptr<PLH::x64Detour> pack_hook, unpack_hook, reset_hook;
        FWeakObjectPtr pending_owner;
        int64_t pending_epoch{};
        std::shared_ptr<const reconciliation::View> pending;

        auto save_owner() -> UObject*
        {
            int32_t displacement{};
            std::memcpy(&displacement, reinterpret_cast<const void*>(base + accessor_rva + 9), 4);
            auto* engine = *reinterpret_cast<UObject**>(base + accessor_rva + 13 + displacement);
            if (!is_uobject_valid_fast(engine)) { return nullptr; }
            auto* singleton = *reinterpret_cast<UObject**>(reinterpret_cast<std::byte*>(engine) + 0x230);
            if (!is_uobject_valid_fast(singleton)) { return nullptr; }
            auto* owner = *reinterpret_cast<UObject**>(reinterpret_cast<std::byte*>(singleton) + 0x50);
            return is_uobject_valid_fast(owner) && owner->GetClassPrivate()->GetName() == L"TresGameInstance"
                ? owner : nullptr;
        }

        void failed(const wchar_t* operation, const std::exception& error)
        {
            RC::Output::send<RC::LogLevel::Error>(
                STR("[KH3APProbeCpp] Character loadout {} failed: {}\n"), operation,
                std::wstring(error.what(), error.what() + std::strlen(error.what())));
        }

        bool pack(void* first, void* second)
        {
            if (!installed.load(std::memory_order_acquire))
            {
                return reinterpret_cast<bool (*)(void*, void*)>(original_pack)(first, second);
            }
            // This is the native payload construction boundary shared by manual
            // saves and autosaves. Never sample equipment later on an I/O thread.
            SnapshotPtr snapshot;
            try
            {
                if (auto* owner = save_owner())
                {
                    snapshot = capture(reinterpret_cast<const std::byte*>(owner) + active_save_offset);
                }
            }
            catch (const std::exception& error) { failed(L"capture", error); }
            reconciliation::capture_character_loadouts(std::move(snapshot));
            return reinterpret_cast<bool (*)(void*, void*)>(original_pack)(first, second);
        }

        bool unpack(void* payload)
        {
            if (!installed.load(std::memory_order_acquire))
            {
                return reinterpret_cast<bool (*)(void*)>(original_unpack)(payload);
            }
            pending.reset();
            pending_owner = {};
            const bool result = reinterpret_cast<bool (*)(void*)>(original_unpack)(payload);
            if (result)
            {
                // The observed slot read and this query share the journal's
                // FIFO. A new process must finish binding/loading that journal
                // before the native default reset can consume this snapshot.
                pending_epoch = reconciliation::epoch();
                pending = reconciliation::character_loadouts_for_load(pending_epoch);
                pending_owner = make_uobject_weak(save_owner());
                if (!pending)
                {
                    RC::Output::send<RC::LogLevel::Error>(
                        STR("[KH3APProbeCpp] Character loadout journal unavailable at native load\n"));
                }
            }
            return result;
        }

        void reset(void* save_data)
        {
            reinterpret_cast<void (*)(void*)>(original_reset)(save_data);
            if (!installed.load(std::memory_order_acquire)) { return; }
            auto* owner = pending_owner.Get();
            if (!pending || !owner || pending_epoch != reconciliation::epoch() ||
                save_data != reinterpret_cast<std::byte*>(owner) + active_save_offset)
            {
                pending.reset();
                pending_owner = {};
                return;
            }
            const auto loaded = std::exchange(pending, {});
            pending_owner = {};
            try
            {
                if (loaded->saved_character_loadouts)
                {
                    restore(static_cast<std::byte*>(save_data), *loaded->saved_character_loadouts);
                    RC::Output::send<RC::LogLevel::Normal>(
                        STR("[KH3APProbeCpp] Character loadouts restored save_slot={} records={}\n"),
                        loaded->loaded_save_slot, identities.size());
                }
            }
            catch (const std::exception& error) { failed(L"restore", error); }
        }
    }

    void install()
    {
        using character_native::Site;
        if (installed.load(std::memory_order_acquire)) { return; }
        // A previous partial install may still own a live trampoline. Do not
        // replace it, or publish persistence until all three hooks are ready.
        if (pack_hook || unpack_hook || reset_hook)
        {
            uninstall();
            if (pack_hook || unpack_hook || reset_hook) { return; }
        }
        if (!character_native::verify_sites({Site::loadout_pack, Site::loadout_unpack, Site::loadout_reset})) { return; }
        base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        const auto& profile = character_native::build() == character_native::Build::Steam
            ? steam_parity_profiles::steam : steam_parity_profiles::egs;
        const auto& accessor = profile.save_accessor;
        if (std::memcmp(reinterpret_cast<const void*>(base + accessor.rva), accessor.bytes.data(), accessor.bytes.size()))
        {
            RC::Output::send<RC::LogLevel::Error>(STR("[KH3APProbeCpp] Character loadout save accessor rejected\n"));
            return;
        }
        accessor_rva = accessor.rva;
        pack_hook = std::make_unique<PLH::x64Detour>(base + character_native::rva(Site::loadout_pack), reinterpret_cast<uint64_t>(&pack), &original_pack);
        unpack_hook = std::make_unique<PLH::x64Detour>(base + character_native::rva(Site::loadout_unpack), reinterpret_cast<uint64_t>(&unpack), &original_unpack);
        reset_hook = std::make_unique<PLH::x64Detour>(base + character_native::rva(Site::loadout_reset), reinterpret_cast<uint64_t>(&reset), &original_reset);
        if (!pack_hook->hook() || !original_pack || !unpack_hook->hook() || !original_unpack || !reset_hook->hook() || !original_reset)
        {
            uninstall();
            RC::Output::send<RC::LogLevel::Error>(STR("[KH3APProbeCpp] Character loadout hooks failed\n"));
            return;
        }
        installed.store(true, std::memory_order_release);
        RC::Output::send<RC::LogLevel::Normal>(STR("[KH3APProbeCpp] Character loadout save persistence installed\n"));
    }

    void uninstall()
    {
        installed.store(false, std::memory_order_release);
        for (auto* hook : {&pack_hook, &unpack_hook, &reset_hook})
        {
            if (!release_native_detour(*hook))
            {
                RC::Output::send<RC::LogLevel::Error>(
                    STR("[KH3APProbeCpp] Character loadout unhook failed; retaining trampoline\n"));
            }
        }
        pending.reset();
        pending_owner = {};
    }
}
