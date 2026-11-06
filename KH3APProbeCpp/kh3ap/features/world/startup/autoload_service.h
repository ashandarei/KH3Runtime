#pragma once

#include <cstdint>

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>

namespace RC::Unreal { class UObject; }

namespace kh3ap
{
    struct RuntimeAutoloadConfig
    {
        std::function<std::optional<std::filesystem::path>()> bridge_dir;
        std::function<RC::Unreal::UObject*()> playable_sora;
        std::function<std::wstring_view()> map_ready_context;
        std::function<bool()> blocking_ui_active;
        std::function<bool()> sora_idle;
        std::function<bool()> space_blocking_ui_active;
    };

    // Explicit, expiring startup requests only. Runs on the existing game-thread
    // driver before the title-screen fast path; never dispatches input events.
    class RuntimeAutoloadService
    {
    public:
        explicit RuntimeAutoloadService(RuntimeAutoloadConfig config);
        ~RuntimeAutoloadService();
        void tick(bool unreal_initialized, bool title_active, bool ignored_map);
        void flush_native_load_failure();
        void manual_save_completed(int32_t slot);
    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
