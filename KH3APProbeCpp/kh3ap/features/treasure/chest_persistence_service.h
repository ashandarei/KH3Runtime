#pragma once

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>

namespace RC::Unreal { class UObject; }

namespace kh3ap
{
    class RuntimeState;

    // Reads the native packed bool; fake boxes have no treasure journal entry.
    bool is_fake_chest(RC::Unreal::UObject* chest);

    // Returns the previous completion bit; repair sets it only when absent.
    // Decoy chests have no journal entry and return false without repair.
    // Throws on unsupported native signatures or an uninitialized real chest.
    bool reconcile_chest_record(RC::Unreal::UObject* chest, bool repair = false);

    // Restore presentation only for an already recorded chest. An active
    // opener retains ownership of its normal opening animation.
    bool restore_recorded_chest_appearance(RC::Unreal::UObject* chest);

    bool native_chest_was_opened(std::wstring_view id);
    void record_native_chest(std::wstring_view id);

    // The callback runs after the native journal setter. Defer any gameplay or
    // persistence work until that call's owning animation/script has returned.
    using NativeChestOpenObserver = void (*)(RC::Unreal::UObject*) noexcept;
    void set_native_chest_open_observer(NativeChestOpenObserver observer) noexcept;

    class RuntimeChestPersistenceService
    {
    public:
        explicit RuntimeChestPersistenceService(std::function<void()> on_completion = {});
        ~RuntimeChestPersistenceService();
        auto notify_treasure_changed() -> void;
        auto process(RuntimeState& state, const std::optional<std::filesystem::path>& bridge_dir,
                     std::chrono::steady_clock::time_point now) -> void;
    private:
        friend bool native_chest_was_opened(std::wstring_view);
        friend void record_native_chest(std::wstring_view);
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
