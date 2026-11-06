#pragma once

#include <memory>
#include <string_view>

namespace kh3ap
{
    class RuntimeState;

    struct RuntimeKingdomOfCoronaQolConfig
    {
        RuntimeState& runtime_state;
        std::wstring_view set_game_flag_path{};
        std::wstring_view set_game_flag_boolean_path{};
        std::wstring_view set_flag_label_path{};
        std::wstring_view greater_equal_flag_label_path{};
        std::wstring_view get_game_flag_path{};
        std::wstring_view get_game_flag_boolean_path{};
        std::wstring_view map_ready_event_path{};
        std::wstring_view blueprint_library_default_object_path{};
        bool enabled{true};
        bool debug_logging{false};
    };

    class RuntimeKingdomOfCoronaQolService
    {
    public:
        explicit RuntimeKingdomOfCoronaQolService(
                RuntimeKingdomOfCoronaQolConfig config);
        ~RuntimeKingdomOfCoronaQolService();

        RuntimeKingdomOfCoronaQolService(
                const RuntimeKingdomOfCoronaQolService&) = delete;
        auto operator=(const RuntimeKingdomOfCoronaQolService&)
                -> RuntimeKingdomOfCoronaQolService& = delete;
        RuntimeKingdomOfCoronaQolService(
                RuntimeKingdomOfCoronaQolService&&) noexcept;
        auto operator=(RuntimeKingdomOfCoronaQolService&&) noexcept
                -> RuntimeKingdomOfCoronaQolService&;

        auto install_once() -> void;
        auto uninstall() -> void;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
