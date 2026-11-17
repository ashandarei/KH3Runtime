#include "character_loadouts.h"
#include "character_loadout_state.h"
#include "character_profile.h"
#include "kh3ap/bridge/rewards/reconciliation.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include <polyhook2/Detour/x64Detour.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#include <utility>
#define NOMINMAX
#include <Windows.h>

namespace kh3ap::character_loadouts
{
    using namespace RC::Unreal;
    namespace
    {
        constexpr size_t active_save_offset = 0x870;
        constexpr uintptr_t pack_rva = 0x1830100;
        constexpr uintptr_t unpack_rva = 0x1831410;
        constexpr uintptr_t reset_rva = 0x110ef50;
        constexpr uintptr_t accessor_rva = 0x1877cb0;
        constexpr std::array<uint8_t, 16> pack_signature{
            0x48,0x8b,0xc4,0x55,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8d,0x68,0xa1};
        constexpr std::array<uint8_t, 16> unpack_signature{
            0x40,0x53,0x55,0x56,0x57,0x41,0x55,0x41,0x57,0x48,0x83,0xec,0x28,0x48,0x8b,0x1d};
        constexpr std::array<uint8_t, 16> reset_signature{
            0x40,0x53,0x48,0x83,0xec,0x50,0x48,0x89,0x6c,0x24,0x60,0x48,0x8b,0xd9,0x48,0x89};
        constexpr std::array<uint8_t, 9> accessor_signature{
            0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0x1d};
        uintptr_t base{};
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
        if (pack_hook || character_native::build() != character_native::Build::Egs) { return; }
        base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        const auto matches = [](uintptr_t rva, const auto& signature)
        {
            return std::memcmp(reinterpret_cast<const void*>(base + rva), signature.data(), signature.size()) == 0;
        };
        if (!matches(pack_rva, pack_signature) || !matches(unpack_rva, unpack_signature) ||
            !matches(reset_rva, reset_signature) || !matches(accessor_rva, accessor_signature))
        {
            RC::Output::send<RC::LogLevel::Error>(STR("[KH3APProbeCpp] Character loadout native signatures rejected\n"));
            return;
        }
        pack_hook = std::make_unique<PLH::x64Detour>(base + pack_rva, reinterpret_cast<uint64_t>(&pack), &original_pack);
        unpack_hook = std::make_unique<PLH::x64Detour>(base + unpack_rva, reinterpret_cast<uint64_t>(&unpack), &original_unpack);
        reset_hook = std::make_unique<PLH::x64Detour>(base + reset_rva, reinterpret_cast<uint64_t>(&reset), &original_reset);
        if (!pack_hook->hook() || !unpack_hook->hook() || !reset_hook->hook())
        {
            uninstall();
            RC::Output::send<RC::LogLevel::Error>(STR("[KH3APProbeCpp] Character loadout hooks failed\n"));
            return;
        }
        RC::Output::send<RC::LogLevel::Normal>(STR("[KH3APProbeCpp] Character loadout save persistence installed\n"));
    }

    void uninstall()
    {
        for (auto* hook : {&pack_hook, &unpack_hook, &reset_hook})
        {
            if (*hook) { (*hook)->unHook(); hook->reset(); }
        }
        pending.reset();
        pending_owner = {};
    }
}
