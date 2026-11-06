#include "kh3ap/core/ui/ui_layout.h"
#include <cassert>
#include <cstring>
#include <cstdint>

namespace
{
    template <class T> T native_field(const kh3ap::native_ui::InputEvent& event, size_t offset)
    {
        T result{};
        std::memcpy(&result, event.data() + offset, sizeof(result));
        return result;
    }
} // namespace

int main()
{
    using namespace kh3ap::native_ui;
    static_assert(sizeof(InputEvent) == 0x30);
    static_assert(alignof(InputEvent) == 16);
    InputEvent event;
    for (size_t offset = 0; offset < sizeof(event); ++offset)
    {
        assert(event.data()[offset] == std::byte{});
    }
    const uintptr_t sender_identity = 0x123456789abcdef0ULL;
    event.set_sender(reinterpret_cast<void*>(sender_identity));
    event.set_kind(decide_event);
    event.set_anchor(101);
    event.set_parameter(5);
    event.set_selection(2);
    // These offsets are the external retail ABI, independent of the adapter's implementation.
    assert(native_field<uintptr_t>(event, 0x08) == sender_identity);
    assert(native_field<int32_t>(event, 0x10) == 7);
    assert(native_field<int32_t>(event, 0x14) == 101);
    assert(native_field<int32_t>(event, 0x18) == 5);
    assert(native_field<int32_t>(event, 0x28) == 2);
    assert(!event.handled());
    const uint32_t unrelated_flag = 8;
    std::memcpy(event.data() + 0x24, &unrelated_flag, sizeof(unrelated_flag));
    assert(!event.handled());
    const uint32_t native_handled_flags = unrelated_flag | 2;
    std::memcpy(event.data() + 0x24, &native_handled_flags, sizeof(native_handled_flags));
    assert(event.handled());
    auto copy = event;
    copy.set_kind(cancel_event);
    assert(native_field<int32_t>(event, 0x10) == 7);
    assert(native_field<int32_t>(copy, 0x10) == 6);
}
