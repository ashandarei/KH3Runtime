#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace kh3ap::native_ui
{
    // Shipping KH3 GFx input event and shared UIP base layout.
    inline constexpr auto input_event_alignment = 16;
    inline constexpr auto input_event_bytes = 0x30;
    inline constexpr auto event_sender_offset = 8;
    inline constexpr auto event_kind_offset = 0x10;
    inline constexpr auto event_anchor_offset = 0x14;
    inline constexpr auto event_parameter_offset = 0x18;
    inline constexpr auto event_flags_offset = 0x24;
    inline constexpr auto event_selection_offset = 0x28;
    inline constexpr auto event_handled_mask = 2;
    inline constexpr int32_t focus_event = 3;
    inline constexpr int32_t page_advance_event = 5;
    inline constexpr int32_t cancel_event = 6;
    inline constexpr int32_t decide_event = 7;
    inline constexpr auto input_handler_vtable_index = 89;
    inline constexpr auto panel_open_offset = 0xa1;
    inline constexpr auto panel_ready_offset = 0xa2;
    inline constexpr auto message_id_offset = 0xa8;
    inline constexpr auto message_page_count_offset = 0x270;
    inline constexpr auto message_current_page_offset = 0x278;
    inline constexpr auto message_type_offset = 0x280;
    inline constexpr auto message_text_offset = 0x2a0;
    // Own the verified native byte layout in one place. Callers express the
    // event's meaning; only this adapter encodes fields for the retail ABI.
    class alignas(input_event_alignment) InputEvent
    {
    public:
        explicit InputEvent(int32_t kind = 0) noexcept
        {
            set_kind(kind);
        }

        void set_sender(const void* sender) noexcept
        {
            write<event_sender_offset>(sender);
        }
        void set_kind(int32_t kind) noexcept
        {
            write<event_kind_offset>(kind);
        }
        void set_anchor(int32_t anchor) noexcept
        {
            write<event_anchor_offset>(anchor);
        }
        void set_parameter(int32_t parameter) noexcept
        {
            write<event_parameter_offset>(parameter);
        }
        void set_selection(int32_t selection) noexcept
        {
            write<event_selection_offset>(selection);
        }
        [[nodiscard]] bool handled() const noexcept
        {
            uint32_t flags{};
            std::memcpy(&flags, bytes_.data() + event_flags_offset, sizeof(flags));
            return (flags & event_handled_mask) != 0;
        }
        [[nodiscard]] std::byte* data() noexcept
        {
            return bytes_.data();
        }
        [[nodiscard]] const std::byte* data() const noexcept
        {
            return bytes_.data();
        }

    private:
        template <size_t Offset, class T> void write(const T& value) noexcept
        {
            static_assert(std::is_trivially_copyable_v<T>);
            static_assert(Offset + sizeof(T) <= input_event_bytes);
            std::memcpy(bytes_.data() + Offset, &value, sizeof(value));
        }
        std::array<std::byte, input_event_bytes> bytes_{};
    };
    static_assert(sizeof(InputEvent) == input_event_bytes);
    static_assert(alignof(InputEvent) == input_event_alignment);
    static_assert(std::is_trivially_copyable_v<InputEvent>);
} // namespace kh3ap::native_ui
