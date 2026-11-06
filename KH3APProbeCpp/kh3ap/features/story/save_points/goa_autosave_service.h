#pragma once

#include <cstdint>
#include <memory>
#include <string_view>

namespace kh3ap
{
    class RuntimeState;

    struct RuntimeGoaAutosaveConfig
    {
        RuntimeState& runtime_state;
    };

    class RuntimeGoaAutosaveService
    {
    public:
        explicit RuntimeGoaAutosaveService(RuntimeGoaAutosaveConfig config);
        ~RuntimeGoaAutosaveService();

        RuntimeGoaAutosaveService(const RuntimeGoaAutosaveService&) = delete;
        auto operator=(const RuntimeGoaAutosaveService&)
                -> RuntimeGoaAutosaveService& = delete;
        RuntimeGoaAutosaveService(RuntimeGoaAutosaveService&&) noexcept;
        auto operator=(RuntimeGoaAutosaveService&&) noexcept
                -> RuntimeGoaAutosaveService&;

        [[nodiscard]] auto is_goa_01_context_active() const -> bool;
        auto queue_after_manual_save(
                std::wstring_view save_file_path,
                int32_t slot_number) -> void;
        auto process_pending() -> void;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
